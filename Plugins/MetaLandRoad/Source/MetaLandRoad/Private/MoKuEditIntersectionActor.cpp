#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplinesComponent.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadEditorMode.h"
#include "Algo/Reverse.h"
#include "CompGeom/PolygonTriangulation.h"
#include "CurveOps/TriangulateCurvesOp.h"
#include "DynamicMesh/MeshNormals.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "MoKuEditSplineActor.h"
#include "Components/DynamicMeshComponent.h"
#include "MoKuEditorUtils.h"
#include "CurveOp.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "WorldEditorSettings.h"
#include "EditSplineRoadInfo.h"
#include "RoadBrushCacheContainer.h"




#define LOCTEXT_NAMESPACE "MoKuEditIntersectionActor"


using namespace UE::Geometry;


/** Append or reuse vertex by XY; existing Z wins. Always refresh UV (strip asphalt vs
 *  hub world-XY must not stick on welded verts — that smeared Fig1 cover UVs). */
static int32 FindOrAppendVertexWeldXY(
	FDynamicMesh3& Mesh,
	const FVector3d& Local,
	const FVector2f& UV,
	double WeldDistSq = 4.0)
{
	for (const int32 ExistingVid : Mesh.VertexIndicesItr())
	{
		if (!Mesh.IsVertex(ExistingVid))
		{
			continue;
		}
		const FVector3d Existing = Mesh.GetVertex(ExistingVid);
		if (FVector::DistSquared2D(Existing, Local) <= WeldDistSq)
		{
			Mesh.SetVertexUV(ExistingVid, UV);
			return ExistingVid;
		}
	}
	const int32 Vid = Mesh.AppendVertex(Local);
	Mesh.SetVertexUV(Vid, UV);
	return Vid;
}

/** Cluster verts by XY and unify Z so strip/fill/mouth share one height. */
static void UnifyNearbyVertexHeights(FDynamicMesh3& Mesh, double WeldDistCm)
{
	const double DistSq = FMath::Square(WeldDistCm);
	TArray<int32> Vids;
	for (const int32 Vid : Mesh.VertexIndicesItr())
	{
		if (Mesh.IsVertex(Vid))
		{
			Vids.Add(Vid);
		}
	}
	if (Vids.Num() < 2)
	{
		return;
	}

	TArray<int32> Parent;
	Parent.SetNum(Vids.Num());
	for (int32 i = 0; i < Vids.Num(); ++i)
	{
		Parent[i] = i;
	}
	auto FindRoot = [&Parent](int32 Index) -> int32
	{
		while (Parent[Index] != Index)
		{
			Parent[Index] = Parent[Parent[Index]];
			Index = Parent[Index];
		}
		return Index;
	};
	auto Union = [&Parent, &FindRoot](int32 A, int32 B)
	{
		A = FindRoot(A);
		B = FindRoot(B);
		if (A != B)
		{
			Parent[B] = A;
		}
	};

	for (int32 i = 0; i < Vids.Num(); ++i)
	{
		const FVector3d Pi = Mesh.GetVertex(Vids[i]);
		for (int32 j = i + 1; j < Vids.Num(); ++j)
		{
			if (FVector::DistSquared2D(Pi, Mesh.GetVertex(Vids[j])) <= DistSq)
			{
				Union(i, j);
			}
		}
	}

	TMap<int32, TPair<double, int32>> Acc;
	for (int32 i = 0; i < Vids.Num(); ++i)
	{
		const int32 Root = FindRoot(i);
		TPair<double, int32>& Entry = Acc.FindOrAdd(Root);
		Entry.Key += Mesh.GetVertex(Vids[i]).Z;
		Entry.Value += 1;
	}
	int32 WeldClusters = 0;
	for (const TPair<int32, TPair<double, int32>>& Pair : Acc)
	{
		if (Pair.Value.Value > 1)
		{
			++WeldClusters;
		}
	}
	for (int32 i = 0; i < Vids.Num(); ++i)
	{
		const int32 Root = FindRoot(i);
		const TPair<double, int32>* Entry = Acc.Find(Root);
		if (!Entry || Entry->Value <= 0)
		{
			continue;
		}
		FVector3d P = Mesh.GetVertex(Vids[i]);
		P.Z = Entry->Key / static_cast<double>(Entry->Value);
		Mesh.SetVertex(Vids[i], P);
	}
	if (WeldClusters > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[Fig1/HeightWeld] xyClusters=%d verts=%d"), WeldClusters, Vids.Num());
	}
}

/** Build-time winding; final 倒叙 is EnsureJunctionCoverFacesWorldUp → ReverseOrientation. */
static void AppendWorldUpTriangle(
	FDynamicMesh3& Mesh,
	int32 A,
	int32 B,
	int32 C,
	const FTransform& ActorToWorld)
{
	const FVector WA = ActorToWorld.TransformPosition(FVector(Mesh.GetVertex(A)));
	const FVector WB = ActorToWorld.TransformPosition(FVector(Mesh.GetVertex(B)));
	const FVector WC = ActorToWorld.TransformPosition(FVector(Mesh.GetVertex(C)));
	if (FVector::CrossProduct(WB - WA, WC - WA).Z >= 0.0f)
	{
		Mesh.AppendTriangle(FIndex3i(A, B, C));
	}
	else
	{
		Mesh.AppendTriangle(FIndex3i(A, C, B));
	}
}

/**
 * Aesthetic multi-way / T corner fillet from distance + port ring angle (图三环形度数).
 * - RingDeg: polar gap between consecutive arms (clamped)
 * - Depth/Sagitta: from chord, DistToHub, road half-width, sin(ring/2)
 * - Segments: max(arc-length / TargetCm, ringAngle / MaxDegPerSeg)
 */
struct FAestheticCornerFillet
{
	double RadiusCm = 80.0;
	double SagittaCm = 20.0;
	double StripDepthCm = 30.0;
	double RingAngleDeg = 60.0;
	int32 NumSegments = 16;
};

static double WrapPositiveAngleRad(double AngleRad)
{
	constexpr double TwoPi = 2.0 * PI;
	while (AngleRad < 0.0)
	{
		AngleRad += TwoPi;
	}
	while (AngleRad >= TwoPi)
	{
		AngleRad -= TwoPi;
	}
	return AngleRad;
}

static FAestheticCornerFillet ComputeAestheticCornerFillet(
	const FVector& OuterRight,
	const FVector& OuterLeft,
	const FVector& Anchor,
	double RoadHalfWidthCm,
	double PortAngleSpanRad,
	double LinkEdgeDepthCm,
	bool bShallowMultiWay)
{
	FAestheticCornerFillet Out;
	const double Chord = FVector::Dist2D(OuterRight, OuterLeft);
	const FVector Mid = (OuterRight + OuterLeft) * 0.5;
	const double DistHub = FVector::Dist2D(Mid, Anchor);
	const double HalfChord = Chord * 0.5;
	const double HalfW = FMath::Max(RoadHalfWidthCm, 50.0);

	double RingRad = WrapPositiveAngleRad(PortAngleSpanRad);
	if (RingRad > PI)
	{
		RingRad = 2.0 * PI - RingRad;
	}
	RingRad = FMath::Clamp(RingRad, FMath::DegreesToRadians(28.0), FMath::DegreesToRadians(140.0));
	Out.RingAngleDeg = FMath::RadiansToDegrees(RingRad);

	const double SinHalf = FMath::Max(FMath::Sin(RingRad * 0.5), 0.18);
	const double IdealR = HalfChord / SinHalf;
	const double IdealSag = IdealR - FMath::Sqrt(FMath::Max(0.0, IdealR * IdealR - HalfChord * HalfChord));

	// Cross/N: keep fillets shallow so 4+ curb strips do not dive into hub and tangle.
	const double MaxSagByHub = DistHub * (bShallowMultiWay ? 0.22 : 0.38);
	const double MaxSagByChord = HalfChord * (bShallowMultiWay ? 0.28 : 0.48);
	const double MinSag = FMath::Clamp(FMath::Min(HalfChord * 0.10, HalfW * 0.14), 10.0, 32.0);
	const double WidthSag = HalfW * (bShallowMultiWay ? 0.12 : 0.18)
		* (0.85 + 0.15 * FMath::Sin(RingRad * 0.5));
	Out.SagittaCm = FMath::Clamp(
		FMath::Max(IdealSag * (bShallowMultiWay ? 0.55 : 1.0), WidthSag),
		MinSag,
		FMath::Min3(MaxSagByHub, MaxSagByChord, FMath::Max(HalfW * 0.55, 60.0)));
	Out.RadiusCm = (HalfChord * HalfChord + Out.SagittaCm * Out.SagittaCm)
		/ FMath::Max(2.0 * Out.SagittaCm, KINDA_SMALL_NUMBER);

	// Constant strip width = mouth edge-lane depth at link (not a second independent arc).
	Out.StripDepthCm = FMath::Clamp(
		LinkEdgeDepthCm > KINDA_SMALL_NUMBER ? LinkEdgeDepthCm : HalfW * 0.16,
		14.0,
		FMath::Min(HalfW * 0.35, 70.0));

	constexpr double TargetSegCm = 24.0;
	constexpr double MaxDegPerSeg = 5.5;
	constexpr int32 MinSegs = 6;
	constexpr int32 MaxSegs = 36;
	const double CentralAngle = 2.0 * FMath::Asin(
		FMath::Clamp(HalfChord / FMath::Max(Out.RadiusCm, HalfChord + 1.0), 0.0, 1.0));
	const double ArcLenCm = FMath::Max(Chord, Out.RadiusCm * CentralAngle);
	const int32 SegByLen = FMath::CeilToInt(ArcLenCm / TargetSegCm);
	const int32 SegByAng = FMath::CeilToInt(Out.RingAngleDeg / MaxDegPerSeg);
	Out.NumSegments = FMath::Clamp(FMath::Max(SegByLen, SegByAng), MinSegs, MaxSegs);
	return Out;
}

/** Offset polyline by constant width along path normal (toward Hub) — no radial stretch. */
static void BuildConstantWidthCurbInner(
	const TArray<FVector3d>& OuterArc,
	const FVector& Hub,
	double StripDepthCm,
	const FVector& WeldInnerStart,
	const FVector& WeldInnerEnd,
	TArray<FVector3d>& OutInnerArc)
{
	OutInnerArc.Reset();
	if (OuterArc.Num() < 2)
	{
		return;
	}

	const double Depth = FMath::Max(StripDepthCm, 1.0);
	OutInnerArc.Reserve(OuterArc.Num());
	for (int32 i = 0; i < OuterArc.Num(); ++i)
	{
		const FVector Outer = OuterArc[i];
		FVector Tangent = FVector::ZeroVector;
		if (i + 1 < OuterArc.Num())
		{
			Tangent += (OuterArc[i + 1] - Outer).GetSafeNormal2D();
		}
		if (i > 0)
		{
			Tangent += (Outer - OuterArc[i - 1]).GetSafeNormal2D();
		}
		if (!Tangent.Normalize())
		{
			Tangent = FVector::ForwardVector;
		}
		// Left normal of directed path; flip so it points toward hub (内挤).
		FVector Normal(-Tangent.Y, Tangent.X, 0.0);
		const FVector TowardHub = (Hub - Outer).GetSafeNormal2D();
		if (!TowardHub.IsNearlyZero() && FVector::DotProduct(Normal, TowardHub) < 0.0)
		{
			Normal = -Normal;
		}
		FVector Inner = Outer + Normal * Depth;
		Inner.Z = Outer.Z;
		OutInnerArc.Add(Inner);
	}

	OutInnerArc[0] = WeldInnerStart;
	OutInnerArc.Last() = WeldInnerEnd;
	if (OutInnerArc.Num() >= 4)
	{
		OutInnerArc[1] = FMath::Lerp(OutInnerArc[1], FMath::Lerp(WeldInnerStart, OutInnerArc[1], 0.7), 0.45);
		const int32 Last = OutInnerArc.Num() - 1;
		OutInnerArc[Last - 1] = FMath::Lerp(
			OutInnerArc[Last - 1],
			FMath::Lerp(WeldInnerEnd, OutInnerArc[Last - 1], 0.7),
			0.45);
	}
}

/**
 * Mouth cut CS (L→R facing hub): inset by Depth along the shared toward-hub direction.
 * Per-point path normals can flip into the road body → 重面 on asphalt.
 */
static void OffsetMouthCutTowardHub(
	const TArray<FVector>& OuterPoly,
	const FVector& Hub,
	double DepthCm,
	TArray<FVector>& OutInnerPoly)
{
	OutInnerPoly.Reset();
	if (OuterPoly.Num() < 2)
	{
		return;
	}
	const double Depth = FMath::Max(DepthCm, 1.0);
	const FVector MouthMid = (OuterPoly[0] + OuterPoly.Last()) * 0.5;
	FVector Across = (OuterPoly.Last() - OuterPoly[0]).GetSafeNormal2D();
	FVector TowardHub = FVector::CrossProduct(Across, FVector::UpVector).GetSafeNormal2D();
	const FVector ToAnchor = (Hub - MouthMid).GetSafeNormal2D();
	if (!ToAnchor.IsNearlyZero() && FVector::DotProduct(TowardHub, ToAnchor) < 0.0)
	{
		TowardHub = -TowardHub;
	}
	if (TowardHub.IsNearlyZero())
	{
		TowardHub = ToAnchor;
	}

	OutInnerPoly.Reserve(OuterPoly.Num());
	for (const FVector& Outer : OuterPoly)
	{
		FVector Inner = Outer + TowardHub * Depth;
		Inner.Z = Outer.Z;
		OutInnerPoly.Add(Inner);
	}

	// Sanity: inset mid must be closer to hub than mouth mid (else we'd paint onto the road).
	if (OutInnerPoly.Num() >= 2)
	{
		const FVector InnerMid = (OutInnerPoly[0] + OutInnerPoly.Last()) * 0.5;
		if (FVector::DistSquared2D(InnerMid, Hub) >= FVector::DistSquared2D(MouthMid, Hub) - 1.0)
		{
			TowardHub = -TowardHub;
			for (int32 i = 0; i < OutInnerPoly.Num(); ++i)
			{
				FVector Inner = OuterPoly[i] + TowardHub * Depth;
				Inner.Z = OuterPoly[i].Z;
				OutInnerPoly[i] = Inner;
			}
		}
	}
}

/** True when Start–End chord cuts through the hub (opposite mouths), not a curb corner. */
static bool IsThroughHubChord(const FVector& Start, const FVector& End, const FVector& Hub)
{
	const FVector Mid = (Start + End) * 0.5;
	const double MidDist = FVector::Dist2D(Mid, Hub);
	const double EndDist = FMath::Min(FVector::Dist2D(Start, Hub), FVector::Dist2D(End, Hub));
	// 0.45 was killing obtuse adjacent corners on irregular N-ways (all → straight sawtooth).
	return EndDist > 1.0 && MidDist < EndDist * 0.28;
}

/**
 * Inner rail = constant-width parallel of outer rail toward the edge-lane interior
 * (绿箭头两点方向), NOT toward hub and NOT a second independent chord-arc (those cross/pinch).
 */
static void BuildParallelLaneInnerRail(
	const TArray<FVector3d>& OuterArc,
	const FVector& InnerStart,
	const FVector& InnerEnd,
	TArray<FVector3d>& OutInnerArc)
{
	OutInnerArc.Reset();
	if (OuterArc.Num() < 2)
	{
		return;
	}

	const double DepthStart = FVector::Dist2D(OuterArc[0], InnerStart);
	const double DepthEnd = FVector::Dist2D(OuterArc.Last(), InnerEnd);
	const FVector PreferStart = (InnerStart - OuterArc[0]).GetSafeNormal2D();
	const FVector PreferEnd = (InnerEnd - OuterArc.Last()).GetSafeNormal2D();

	OutInnerArc.Reserve(OuterArc.Num());
	for (int32 i = 0; i < OuterArc.Num(); ++i)
	{
		const double T = (OuterArc.Num() <= 1)
			? 0.0
			: static_cast<double>(i) / static_cast<double>(OuterArc.Num() - 1);
		FVector Tangent = FVector::ZeroVector;
		if (i + 1 < OuterArc.Num())
		{
			Tangent += (OuterArc[i + 1] - OuterArc[i]).GetSafeNormal2D();
		}
		if (i > 0)
		{
			Tangent += (OuterArc[i] - OuterArc[i - 1]).GetSafeNormal2D();
		}
		if (!Tangent.Normalize())
		{
			Tangent = FVector::ForwardVector;
		}
		FVector Normal(-Tangent.Y, Tangent.X, 0.0);
		const FVector Prefer = FMath::Lerp(PreferStart, PreferEnd, T).GetSafeNormal2D();
		if (!Prefer.IsNearlyZero() && FVector::DotProduct(Normal, Prefer) < 0.0)
		{
			Normal = -Normal;
		}
		const double Depth = FMath::Lerp(DepthStart, DepthEnd, T);
		FVector Inner = OuterArc[i] + Normal * FMath::Max(Depth, 1.0);
		Inner.Z = OuterArc[i].Z;
		OutInnerArc.Add(Inner);
	}
	OutInnerArc[0] = InnerStart;
	OutInnerArc.Last() = InnerEnd;
}

/** Reject folded / self-crossing polylines (红线针刺、折线过多). */
static bool IsLaneLinkPolylineValid(const TArray<FVector3d>& Poly, double ChordLen)
{
	if (Poly.Num() < 2)
	{
		return false;
	}
	double PolyLen = 0.0;
	for (int32 i = 0; i + 1 < Poly.Num(); ++i)
	{
		PolyLen += FVector::Dist2D(Poly[i], Poly[i + 1]);
		if (i + 2 < Poly.Num())
		{
			const FVector A = (Poly[i + 1] - Poly[i]).GetSafeNormal2D();
			const FVector B = (Poly[i + 2] - Poly[i + 1]).GetSafeNormal2D();
			if (!A.IsNearlyZero() && !B.IsNearlyZero() && FVector::DotProduct(A, B) < -0.2)
			{
				return false;
			}
		}
	}
	return PolyLen <= ChordLen * 2.2;
}

/** Smooth inward Bezier (4–10 segs). Avoids circular long-way flips → jagged red polylines. */
static void BuildSmoothInwardLaneBezier(
	const FVector& Start,
	const FVector& End,
	const FVector& Hub,
	double SagittaCm,
	int32 NumSegments,
	TArray<FVector3d>& OutArc)
{
	OutArc.Reset();
	const FVector Mid = (Start + End) * 0.5;
	FVector TowardHub = (Hub - Mid).GetSafeNormal2D();
	if (TowardHub.IsNearlyZero())
	{
		OutArc.Add(Start);
		OutArc.Add(End);
		return;
	}
	const double Chord = FVector::Dist2D(Start, End);
	// Quadratic mid P(0.5)=Mid+0.5*(Apex-Mid); Apex at 2*Sag so curve reaches TargetSag.
	const double Sag = FMath::Clamp(SagittaCm, 30.0, FMath::Min(Chord * 0.42, 220.0));
	const FVector Apex = Mid + TowardHub * (2.0 * Sag);
	const int32 Segs = FMath::Clamp(NumSegments, 8, 20);
	OutArc.Reserve(Segs + 1);
	const float Z0 = static_cast<float>(Start.Z);
	const float Z1 = static_cast<float>(End.Z);
	for (int32 i = 0; i <= Segs; ++i)
	{
		const double T = static_cast<double>(i) / static_cast<double>(Segs);
		const double U = 1.0 - T;
		FVector P = Start * (U * U) + Apex * (2.0 * U * T) + End * (T * T);
		P.Z = FMath::Lerp(Z0, Z1, static_cast<float>(T));
		OutArc.Add(P);
	}
	OutArc[0] = Start;
	OutArc.Last() = End;
}

static bool PointInPolygon2D(const FVector2d& P, const TArray<FVector2d>& Poly)
{
	const int32 N = Poly.Num();
	if (N < 3)
	{
		return false;
	}
	bool bInside = false;
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FVector2d& A = Poly[i];
		const FVector2d& B = Poly[j];
		if (((A.Y > P.Y) != (B.Y > P.Y))
			&& (P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X))
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

UMetaLandRoadJunctionComponent::UMetaLandRoadJunctionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMetaLandRoadJunctionComponent::ApplyNode(const FMetaLandRoadJunctionNode& InNode)
{
	JunctionNode = InNode;
}

void UMetaLandRoadJunctionComponent::ClearLinks()
{
	// Snapshot roads first — must refresh meshes AFTER trim zones are cleared (旧面残留).
	TArray<AMoKuEditSplineActor*> RoadsToRefresh = LinkedRoadActors;

	for (AMoKuEditSplineActor* RoadActor : LinkedRoadActors)
	{
		if (!RoadActor)
		{
			continue;
		}

		AMoKuEditIntersectionActor* OwnerJunction = Cast<AMoKuEditIntersectionActor>(GetOwner());
		if (RoadActor->StartOfJunction.Get() == OwnerJunction)
		{
			RoadActor->StartOfJunction = nullptr;
			RoadActor->StartJunctionMeshTrimS = -1.0;
		}
		if (RoadActor->EndOfJunction.Get() == OwnerJunction)
		{
			RoadActor->EndOfJunction = nullptr;
			RoadActor->EndJunctionMeshTrimS = -1.0;
		}
		RoadActor->JunctionTrimZones.RemoveAll([OwnerJunction](const FMetaLandRoadJunctionTrimZone& Zone)
		{
			return Zone.Junction.Get() == OwnerJunction;
		});
	}

	LinkedRoadActors.Reset();

	AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh = true;
	for (AMoKuEditSplineActor* RoadActor : RoadsToRefresh)
	{
		if (RoadActor)
		{
			RoadActor->RefreshDynamicRoadMesh();
		}
	}
	AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh = false;
}

void UMetaLandRoadJunctionComponent::LinkRoadActor(AMoKuEditSplineActor* RoadActor, const FMetaLandRoadJunctionArmRef& Arm)
{
	if (!RoadActor)
	{
		return;
	}

	LinkedRoadActors.AddUnique(RoadActor);

	UMoKuEditSplinesComponent* Spline = RoadActor->GetSplineComponent();
	if (!Spline)
	{
		return;
	}

	AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(GetOwner());
	const double SplineLength = Spline->GetSplineLength();
	if (SplineLength <= KINDA_SMALL_NUMBER || !Junction)
	{
		return;
	}

	FMetaLandRoadJunctionTrimZone Zone;
	Zone.Junction = Junction;
	Zone.ConnectSide = Arm.ConnectSide;

	if (Arm.bThroughArm)
	{
		Zone.ApproachCutS = Arm.CutDistanceS;
		Zone.DepartCutS = Arm.DepartCutS;
		RoadActor->EndOfJunction = Junction;
		RoadActor->EndJunctionMeshTrimS = Arm.CutDistanceS;
		RoadActor->StartOfJunction = Junction;
		RoadActor->StartJunctionMeshTrimS = Arm.DepartCutS;
	}
	else if (Arm.IntersectDistanceS >= SplineLength * 0.5)
	{
		Zone.ApproachCutS = Arm.CutDistanceS;
		Zone.DepartCutS = -1.0;
		RoadActor->EndOfJunction = Junction;
		RoadActor->EndJunctionMeshTrimS = Arm.CutDistanceS;
	}
	else
	{
		Zone.ApproachCutS = -1.0;
		Zone.DepartCutS = Arm.DepartCutS;
		RoadActor->StartOfJunction = Junction;
		RoadActor->StartJunctionMeshTrimS = Arm.DepartCutS;
	}

	RoadActor->JunctionTrimZones.RemoveAll([Junction](const FMetaLandRoadJunctionTrimZone& ExistingZone)
	{
		return ExistingZone.Junction.Get() == Junction;
	});
	RoadActor->JunctionTrimZones.Add(Zone);
}

AMoKuEditIntersectionActor::AMoKuEditIntersectionActor(const FObjectInitializer& ObjectInitializer)
:Super(ObjectInitializer)
{
	DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));
	SplineComp = CreateDefaultSubobject<UMoKuEditSplinesComponent>(TEXT("SplineComponent"));
	JunctionData = CreateDefaultSubobject<UMetaLandRoadJunctionComponent>(TEXT("JunctionData"));
	RootComponent = SplineComp;
	if (SplineComp)
	{
		SplineComp->SetVisibility(false);
		SplineComp->bShowLaneVisualization = false;
	}
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->SetupAttachment(SplineComp);
	}
}

UMaterialInterface* AMoKuEditIntersectionActor::ResolveJunctionMaterial() const
{
	if (!JunctionData)
	{
		return AMoKuEditSplineActor::EnsureAsphaltTestMaterial();
	}

	for (AMoKuEditSplineActor* LinkedRoad : JunctionData->LinkedRoadActors)
	{
		if (!LinkedRoad)
		{
			continue;
		}
		// DynamicMesh slot 0 is the live asphalt the road actually draws.
		if (LinkedRoad->DynamicMeshComponent)
		{
			if (UMaterialInterface* RoadMat = LinkedRoad->DynamicMeshComponent->GetMaterial(0))
			{
				return RoadMat;
			}
		}
		if (LinkedRoad->RoadMaterial)
		{
			return LinkedRoad->RoadMaterial;
		}
	}
	return AMoKuEditSplineActor::EnsureAsphaltTestMaterial();
}

FVector2f AMoKuEditIntersectionActor::ComputeJunctionVertexUV(const FVector& WorldPos) const
{
	// Do NOT use MakeAsphaltUVAtDistance for hub points: Dot(Pos−Centerline, Right) blows up
	// across a wide junction and creates hairline UV stripes on single-sided asphalt.
	const float Tile = FMath::Max(MetaLandRoad::DefaultAsphaltUvTileCm, 1.0f);
	const FVector Anchor =
		(JunctionData && !JunctionData->JunctionNode.AnchorWorld.IsNearlyZero())
			? JunctionData->JunctionNode.AnchorWorld
			: GetActorLocation();
	return FVector2f(
		static_cast<float>((WorldPos.X - Anchor.X) / Tile),
		static_cast<float>((WorldPos.Y - Anchor.Y) / Tile));
}

double AMoKuEditIntersectionActor::GetCornerArcDepthForPoints(const FVector& PointA, const FVector& PointB) const
{
	constexpr double DefaultDepth = 200.0;
	if (!JunctionData)
	{
		return DefaultDepth;
	}

	double ArcDepth = DefaultDepth;
	auto ConsiderPoint = [this, &ArcDepth](const FVector& Point)
	{
		for (const FMetaLandRoadJunctionArmRef& Arm : JunctionData->JunctionNode.Arms)
		{
			const UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
			if (!Spline)
			{
				continue;
			}

			const double CutCandidates[] = { Arm.CutDistanceS, Arm.DepartCutS };
			for (double CutS : CutCandidates)
			{
				if (CutS <= 0.0)
				{
					continue;
				}

				FVector LeftWorld;
				FVector RightWorld;
				FVector CenterWorld;
				if (!Spline->GetRoadEdgePointsAtS(CutS, LeftWorld, RightWorld, CenterWorld, ESplineCoordinateSpace::World))
				{
					continue;
				}

				constexpr double MatchDistSq = 150.0 * 150.0;
				if (FVector::DistSquared2D(Point, RightWorld) < MatchDistSq
					|| FVector::DistSquared2D(Point, LeftWorld) < MatchDistSq
					|| FVector::DistSquared2D(Point, CenterWorld) < MatchDistSq * 4.0)
				{
					ArcDepth = FMath::Max(ArcDepth, Spline->GetRoadHalfWidthAtS(CutS));
				}
			}
		}
	};

	ConsiderPoint(PointA);
	ConsiderPoint(PointB);
	// Wide roads (halfW ~1024) must not be clamped to 600 — that starved fillet chord gates.
	return FMath::Clamp(ArcDepth, 50.0, 4000.0);
}

void AMoKuEditIntersectionActor::SyncJunctionFrameworkFromArms()
{
	if (!JunctionData)
	{
		return;
	}

	const FMetaLandRoadJunctionNode& Node = JunctionData->JunctionNode;
	CornerInfo.Empty();

	auto RegisterCutEdge = [this](
		UMetaLandRoadSplineComponent* Spline,
		AMoKuEditSplineActor* RoadActor,
		double CutS)
	{
		if (!Spline || !RoadActor || CutS <= 0.0)
		{
			return;
		}

		TArray<FVector> CrossSection;
		if (!Spline->GetRoadCrossSectionPointsAtS(CutS, CrossSection, ESplineCoordinateSpace::World))
		{
			FVector LeftWorld;
			FVector RightWorld;
			FVector CenterWorld;
			if (!Spline->GetRoadEdgePointsAtS(CutS, LeftWorld, RightWorld, CenterWorld, ESplineCoordinateSpace::World))
			{
				return;
			}
			CrossSection = { LeftWorld, CenterWorld, RightWorld };
		}

		if (CrossSection.Num() < 2)
		{
			return;
		}

		// Reference silhouette cut: only left-outer + right-outer (straight cap like orange outline).
		FVector LeftOuter = CrossSection[0];
		FVector RightOuter = CrossSection.Last();
		const FVector Hub = JunctionData->JunctionNode.AnchorWorld.IsNearlyZero()
			? GetActorLocation()
			: JunctionData->JunctionNode.AnchorWorld;
		const FVector CutMid = (LeftOuter + RightOuter) * 0.5;
		FVector TowardJunction = (Hub - CutMid).GetSafeNormal2D();
		if (TowardJunction.IsNearlyZero())
		{
			const FVector Tangent = Spline->GetTangentAtDistanceAlongSpline(CutS, ESplineCoordinateSpace::World).GetSafeNormal2D();
			const double SplineLength = Spline->GetSplineLength();
			TowardJunction = (CutS <= SplineLength * 0.5) ? Tangent : -Tangent;
		}
		const FVector RightFacing = FVector::CrossProduct(TowardJunction, FVector::UpVector).GetSafeNormal2D();
		if (FVector::DotProduct(LeftOuter - CutMid, RightFacing)
			> FVector::DotProduct(RightOuter - CutMid, RightFacing))
		{
			Swap(LeftOuter, RightOuter);
		}

		FCornerInfo CutCorner;
		CutCorner.Tag = ECornerTag::Side;
		CutCorner.CornerPoints.Add(LeftOuter);
		CutCorner.CornerPoints.Add(RightOuter);
		CutCorner.SampleDistanceS = CutS;
		CutCorner.SampleSpline = Spline;
		CornerInfo.Add(CutCorner);

		const double SplineLength = Spline->GetSplineLength();
		const bool bNearStart = CutS <= SplineLength * 0.5;
		const FString ConnectionKey = bNearStart ? TEXT("Start") : TEXT("End");
		FConnectionInfo& Connection = RoadActor->JunctionConnections.FindOrAdd(ConnectionKey);
		Connection.Empty();
		Connection.ConnectEdge.Add(RightOuter);
		Connection.ConnectEdge.Add(LeftOuter);

		const FVector Tangent = Spline->GetTangentAtDistanceAlongSpline(CutS, ESplineCoordinateSpace::World).GetSafeNormal();
		const FVector Direction = bNearStart ? -Tangent : Tangent;
		Connection.ConnectDirection.Add(Direction);
		Connection.ConnectDirection.Add(Direction);
	};

	for (const FMetaLandRoadJunctionArmRef& Arm : Node.Arms)
	{
		UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
		if (!Spline)
		{
			continue;
		}

		AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Spline->GetOwner());
		if (!RoadActor)
		{
			continue;
		}

		if (Arm.bThroughArm)
		{
			RegisterCutEdge(Spline, RoadActor, Arm.CutDistanceS);
			if (Arm.DepartCutS > Arm.CutDistanceS)
			{
				RegisterCutEdge(Spline, RoadActor, Arm.DepartCutS);
			}
		}
		else if (Arm.IntersectDistanceS >= Spline->GetSplineLength() * 0.5)
		{
			RegisterCutEdge(Spline, RoadActor, Arm.CutDistanceS);
		}
		else
		{
			RegisterCutEdge(Spline, RoadActor, Arm.DepartCutS);
		}
	}
}

void AMoKuEditIntersectionActor::ClearJunctionCoverMesh()
{
	CornerInfo.Reset();
	EdgePath.Reset();
	if (!DynamicMeshComponent)
	{
		return;
	}
	if (UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh())
	{
		DynamicMesh->Reset();
		DynamicMeshComponent->NotifyMeshUpdated();
		DynamicMeshComponent->UpdateCollision(false);
	}
}

void AMoKuEditIntersectionActor::SyncTerrainInfluenceFromLinkedRoads()
{
	// Terrain Height/Weight live on AMoKuEditBaseActor — seed Road/Dirty, then mirror linked road falloffs.
	EnsureDefaultTerrainInfluenceSettings();
	if (!JunctionData)
	{
		return;
	}

	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayer = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	const FName ShoulderLayer = Settings ? Settings->GetDefaultShoulderWeightmapLayerName() : FName(TEXT("Dirty"));

	for (AMoKuEditSplineActor* LinkedRoad : JunctionData->LinkedRoadActors)
	{
		if (!LinkedRoad || !LinkedRoad->bAffectsLandscape)
		{
			continue;
		}

		const FSplineRoadHeightmapSettings& SrcH = LinkedRoad->RoadHeightmapSettings;
		FRoadFalloffSettings& DstFalloff = RoadHeightmapSettings.FalloffSettings;
		// Only fill seed/empty junction values — never overwrite artist edits on the hub.
		if (DstFalloff.FalloffWidth <= KINDA_SMALL_NUMBER
			|| FMath::IsNearlyEqual(DstFalloff.FalloffWidth, 800.f, 1.f))
		{
			if (SrcH.FalloffSettings.FalloffWidth > KINDA_SMALL_NUMBER)
			{
				DstFalloff.FalloffWidth = SrcH.FalloffSettings.FalloffWidth;
			}
		}
		if (FMath::IsNearlyZero(DstFalloff.ZOffset) && !FMath::IsNearlyZero(SrcH.FalloffSettings.ZOffset))
		{
			DstFalloff.ZOffset = SrcH.FalloffSettings.ZOffset;
		}

		auto CopyWeightIfSeed = [this](FName LayerName, const AMoKuEditSplineActor* SrcRoad)
		{
			if (LayerName == NAME_None || !SrcRoad)
			{
				return;
			}
			const FRoadBodyWeightmapSettings* SrcW = SrcRoad->RoadBodyWeightmapSettings.Find(LayerName);
			FRoadBodyWeightmapSettings* DstW = RoadBodyWeightmapSettings.Find(LayerName);
			if (!SrcW || !DstW)
			{
				return;
			}
			if (DstW->FalloffWidth <= KINDA_SMALL_NUMBER
				|| FMath::IsNearlyEqual(DstW->FalloffWidth, 400.f, 1.f))
			{
				DstW->FalloffWidth = SrcW->FalloffWidth;
				DstW->EdgeOffset = SrcW->EdgeOffset;
				DstW->TextureInfluence = SrcW->TextureInfluence;
			}
		};
		CopyWeightIfSeed(RoadLayer, LinkedRoad);
		CopyWeightIfSeed(ShoulderLayer, LinkedRoad);

		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Junction %s synced Terrain from linked road %s (HeightFalloff=%.0f)"),
			*GetName(), *LinkedRoad->GetName(), RoadHeightmapSettings.FalloffSettings.FalloffWidth);
		break;
	}
}

void AMoKuEditIntersectionActor::RefreshJunctionCoverMesh()
{
	if (!DynamicMeshComponent || !JunctionData)
	{
		return;
	}

	// Always drop previous cover first — T→Cross / N expand must not keep old faces.
	ClearJunctionCoverMesh();

	if (JunctionData->JunctionNode.Arms.Num() < 2)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RefreshJunctionCoverMesh: arms=%d — cannot build cover on %s"),
			JunctionData->JunctionNode.Arms.Num(),
			*GetName());
		NotifyLandscapeBrushChanged(/*bShape=*/true, /*bWeight=*/true, /*bUser=*/false);
		return;
	}

	// Re-apply trim gaps on linked roads. Suppress junction recook + landscape notifies:
	// each RefreshDynamicRoadMesh used to RequestLandscapeUpdate (full 41-road pass).
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		TGuardValue<bool> SuppressLandscape(
			AMoKuEditBaseActor::bSuppressLandscapeBrushNotify, true);
		for (AMoKuEditSplineActor* LinkedRoad : JunctionData->LinkedRoadActors)
		{
			if (LinkedRoad)
			{
				LinkedRoad->RefreshDynamicRoadMesh();
			}
		}
	}

	for (const FMetaLandRoadJunctionArmRef& Arm : JunctionData->JunctionNode.Arms)
	{
		if (UMetaLandRoadSplineComponent* ArmSpline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get()))
		{
			ArmSpline->GetRoadLayoutMutable().UpdateLayout(ArmSpline);
			ArmSpline->UpdateRoadLayout();
		}
	}

	if (!BuildFig1JunctionCoverMesh())
	{
		ClearJunctionCoverMesh();
		UE_LOG(LogTemp, Error, TEXT("RefreshJunctionCoverMesh: Fig1 cover FAILED — cover cleared (no stale faces)"));
	}

	// Hub cover is the brush silhouette for WorldEditor height/weight (same base Terrain UPROPERTY).
	SyncTerrainInfluenceFromLinkedRoads();

	// Mark linked-road DF caches dirty (shape changed during suppress) then one landscape tick.
	InitSceneManagement();
	if (AWorldSceneManagement* Mgr = SceneManagement.Get())
	{
		for (AMoKuEditSplineActor* LinkedRoad : JunctionData->LinkedRoadActors)
		{
			if (!LinkedRoad)
			{
				continue;
			}
			if (URoadBodyBrushCacheContainer* CacheObj = Cast<URoadBodyBrushCacheContainer>(
					Mgr->GetActorCache(LinkedRoad, URoadBodyBrushCacheContainer::StaticClass())))
			{
				CacheObj->Cache.CacheIsValid = false;
			}
		}
	}
	NotifyLandscapeBrushChanged(/*bShape=*/true, /*bWeight=*/true, /*bUser=*/false);
}

static void EnsureJunctionDynamicMeshRegistered(UDynamicMeshComponent* DynamicMeshComponent, AActor* Owner)
{
	if (!DynamicMeshComponent || DynamicMeshComponent->IsRegistered())
	{
		return;
	}
	// Map load / RF_Archetype: GetWorld() is null — RegisterComponent ensure-crashes.
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World)
	{
		World = DynamicMeshComponent->GetWorld();
	}
	if (World)
	{
		DynamicMeshComponent->RegisterComponentWithWorld(World);
	}
}

void AMoKuEditIntersectionActor::PublishJunctionDynamicMesh(FDynamicMesh3& Mesh)
{
	if (!DynamicMeshComponent || Mesh.TriangleCount() == 0)
	{
		return;
	}

	EnsureJunctionCoverFacesWorldUp(Mesh);

	AMoKuEditSplineActor::FinalizeRoadMeshUVs(Mesh);
	AMoKuEditSplineActor::FinalizeRoadMeshNormalsAndOverlays(Mesh);

	if (UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh())
	{
		EnsureJunctionDynamicMeshRegistered(DynamicMeshComponent, this);
		DynamicMesh->Reset();
		DynamicMesh->SetMesh(MoveTemp(Mesh));
		DynamicMeshComponent->SetRelativeLocation(FVector::ZeroVector);
		DynamicMeshComponent->SetVisibility(true);
		DynamicMeshComponent->SetHiddenInGame(false);
		DynamicMeshComponent->SetCastShadow(false);
		DynamicMeshComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
		DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		if (UBodySetup* BodySetup = DynamicMeshComponent->GetBodySetup())
		{
			BodySetup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
		}
		DynamicMeshComponent->bEnableComplexCollision = true;
		DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
		if (DynamicMeshComponent->IsRegistered())
		{
			DynamicMeshComponent->UpdateCollision(false);
		}
		DynamicMeshComponent->NotifyMeshUpdated();
		// Prefer linked road asphalt; fall back to shared test asphalt (avoids landscape/paint mats).
		UMaterialInterface* JunctionMat = ResolveJunctionMaterial();
		if (!JunctionMat)
		{
			JunctionMat = AMoKuEditSplineActor::EnsureAsphaltTestMaterial();
		}
		if (JunctionMat)
		{
			DynamicMeshComponent->SetMaterial(0, JunctionMat);
			DynamicMeshComponent->MarkRenderStateDirty();
		}
		// Landscape notify once at end of RefreshJunctionCoverMesh (avoid double full rebuild).
	}
}

void AMoKuEditIntersectionActor::EnsureJunctionCoverFacesWorldUp(FDynamicMesh3& Mesh) const
{
	if (Mesh.TriangleCount() == 0)
	{
		return;
	}

	// 倒叙封面: bottom-up single-sided view seeing the fill means fronts pointed at the ground.
	// Flip every triangle index order once at publish (strips + fillets + center).
	Mesh.ReverseOrientation(true);

	const FTransform ActorToWorld = GetActorTransform();
	int32 FacingNegZ = 0;
	int32 FacingPosZ = 0;
	for (int32 TriangleId : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Verts = Mesh.GetTriangle(TriangleId);
		const FVector WA = ActorToWorld.TransformPosition(FVector(Mesh.GetVertex(Verts.A)));
		const FVector WB = ActorToWorld.TransformPosition(FVector(Mesh.GetVertex(Verts.B)));
		const FVector WC = ActorToWorld.TransformPosition(FVector(Mesh.GetVertex(Verts.C)));
		if (FVector::CrossProduct(WB - WA, WC - WA).Z < 0.0f)
		{
			++FacingNegZ;
		}
		else
		{
			++FacingPosZ;
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("EnsureJunctionCoverFacesWorldUp: forced 倒叙 ReverseOrientation — Cross+Z=%d Cross-Z=%d (top view should see cover; bottom-up should cull)"),
		FacingPosZ,
		FacingNegZ);
}

void AMoKuEditIntersectionActor::AppendCrossSectionStrip(
	FDynamicMesh3& Mesh,
	const TArray<FVector>& OuterWorld,
	const TArray<FVector>& InnerWorld,
	const UMetaLandRoadSplineComponent* OuterSpline,
	double OuterS,
	const UMetaLandRoadSplineComponent* InnerSpline,
	double InnerS,
	float MeshHeightOffset,
	const FTransform& ActorTransform) const
{
	const int32 Count = FMath::Min(OuterWorld.Num(), InnerWorld.Num());
	if (Count < 2)
	{
		return;
	}

	TArray<int32> OuterVids;
	TArray<int32> InnerVids;
	OuterVids.Reserve(Count);
	InnerVids.Reserve(Count);

	for (int32 PointIndex = 0; PointIndex < Count; ++PointIndex)
	{
		// UV from road-height positions (same as lane mesh); lift verts after.
		const FVector OuterUVPos = OuterWorld[PointIndex];
		const FVector InnerUVPos = InnerWorld[PointIndex];
		FVector OuterPos = OuterUVPos;
		FVector InnerPos = InnerUVPos;
		OuterPos.Z += MeshHeightOffset;
		InnerPos.Z += MeshHeightOffset;

		// Skill: mouth/curb UV = World XY chart (same as hub fill). MakeAsphaltUVAtDistance
		// on inset lip verts then weld→fill reused asphalt UVs → hub fan stretch (箭头3).
		const int32 OuterVid = FindOrAppendVertexWeldXY(
			Mesh,
			FVector3d(ActorTransform.InverseTransformPosition(OuterPos)),
			ComputeJunctionVertexUV(OuterUVPos));
		const int32 InnerVid = FindOrAppendVertexWeldXY(
			Mesh,
			FVector3d(ActorTransform.InverseTransformPosition(InnerPos)),
			ComputeJunctionVertexUV(InnerUVPos));
		(void)OuterSpline;
		(void)OuterS;
		(void)InnerSpline;
		(void)InnerS;
		OuterVids.Add(OuterVid);
		InnerVids.Add(InnerVid);
	}

	const FTransform ActorToWorld = ActorTransform;
	for (int32 PointIndex = 0; PointIndex + 1 < Count; ++PointIndex)
	{
		const int32 OL = OuterVids[PointIndex];
		const int32 OR = OuterVids[PointIndex + 1];
		const int32 IL = InnerVids[PointIndex];
		const int32 IR = InnerVids[PointIndex + 1];
		// Quad diagonal OL-IR; each tri picks A-B-C order that faces world +Z.
		AppendWorldUpTriangle(Mesh, OL, OR, IR, ActorToWorld);
		AppendWorldUpTriangle(Mesh, OL, IR, IL, ActorToWorld);
	}
}

void AMoKuEditIntersectionActor::AppendPolylineStrip(
	FDynamicMesh3& Mesh,
	const TArray<FVector3d>& OuterPoly,
	const TArray<FVector3d>& InnerPoly,
	const UMetaLandRoadSplineComponent* OuterSplineA,
	double OuterSA,
	const UMetaLandRoadSplineComponent* OuterSplineB,
	double OuterSB,
	const UMetaLandRoadSplineComponent* InnerSplineA,
	double InnerSA,
	const UMetaLandRoadSplineComponent* InnerSplineB,
	double InnerSB,
	float MeshHeightOffset,
	const FTransform& ActorTransform) const
{
	const int32 Count = FMath::Min(OuterPoly.Num(), InnerPoly.Num());
	if (Count < 2)
	{
		return;
	}

	// One world-XY chart for the whole cover (same as blank fill).
	// Lane-seed + per-row world-metric shears on unequal outer/inner arcs → multi-way smear.
	(void)OuterSplineA;
	(void)OuterSA;
	(void)OuterSplineB;
	(void)OuterSB;
	(void)InnerSplineA;
	(void)InnerSA;
	(void)InnerSplineB;
	(void)InnerSB;

	TArray<int32> OuterVids;
	TArray<int32> InnerVids;
	OuterVids.Reserve(Count);
	InnerVids.Reserve(Count);

	for (int32 PointIndex = 0; PointIndex < Count; ++PointIndex)
	{
		const FVector OuterUVPos = OuterPoly[PointIndex];
		const FVector InnerUVPos = InnerPoly[PointIndex];
		FVector OuterPos = OuterUVPos;
		FVector InnerPos = InnerUVPos;
		OuterPos.Z += MeshHeightOffset;
		InnerPos.Z += MeshHeightOffset;

		const int32 OuterVid = FindOrAppendVertexWeldXY(
			Mesh,
			FVector3d(ActorTransform.InverseTransformPosition(OuterPos)),
			ComputeJunctionVertexUV(OuterUVPos));
		const int32 InnerVid = FindOrAppendVertexWeldXY(
			Mesh,
			FVector3d(ActorTransform.InverseTransformPosition(InnerPos)),
			ComputeJunctionVertexUV(InnerUVPos));
		OuterVids.Add(OuterVid);
		InnerVids.Add(InnerVid);
	}

	const FTransform ActorToWorld = ActorTransform;
	for (int32 PointIndex = 0; PointIndex + 1 < Count; ++PointIndex)
	{
		const int32 OL = OuterVids[PointIndex];
		const int32 OR = OuterVids[PointIndex + 1];
		const int32 IL = InnerVids[PointIndex];
		const int32 IR = InnerVids[PointIndex + 1];
		AppendWorldUpTriangle(Mesh, OL, OR, IR, ActorToWorld);
		AppendWorldUpTriangle(Mesh, OL, IR, IL, ActorToWorld);
	}
}

void AMoKuEditIntersectionActor::AppendFig1CenterFill(
	FDynamicMesh3& Mesh,
	const TArray<FVector>& HoleWorldRing,
	const TArray<FVector2f>& HoleUVs,
	float MeshHeightOffset,
	const FTransform& ActorTransform)
{
	// Ear-clip preferring short diagonals — TriangulateSimplePolygon on sparse diamonds → sliver fans (红线内抽象拓扑).
	if (HoleWorldRing.Num() < 3 || HoleWorldRing.Num() != HoleUVs.Num())
	{
		return;
	}

	// Same Z policy as lane strips (caller pre-flattens world Z). Do NOT re-average —
	// strip vs fill Z mismatch opens 断层 along the shared inner rail.
	TArray<int32> RingVids;
	TArray<FVector3d> PosList;
	RingVids.Reserve(HoleWorldRing.Num());
	PosList.Reserve(HoleWorldRing.Num());
	for (int32 i = 0; i < HoleWorldRing.Num(); ++i)
	{
		FVector WorldPos = HoleWorldRing[i];
		WorldPos.Z += MeshHeightOffset;
		const FVector3d Local(ActorTransform.InverseTransformPosition(WorldPos));
		// XY weld only — existing strip Z wins (高度焊接).
		const int32 Vid = FindOrAppendVertexWeldXY(Mesh, Local, HoleUVs[i]);
		RingVids.Add(Vid);
		PosList.Add(Mesh.GetVertex(Vid));
	}

	double SignedAreaLocal = 0.0;
	for (int32 i = 0; i < PosList.Num(); ++i)
	{
		const FVector3d& A = PosList[i];
		const FVector3d& B = PosList[(i + 1) % PosList.Num()];
		SignedAreaLocal += A.X * B.Y - B.X * A.Y;
	}
	if (SignedAreaLocal < 0.0)
	{
		Algo::Reverse(RingVids);
		Algo::Reverse(PosList);
	}

	// Only drop exact consecutive duplicates. Collinear cull chords the Bezier rail
	// while lane strips keep midpoints → visible 断层 / gap along the shared edge.
	for (int32 i = PosList.Num() - 1; i >= 0 && PosList.Num() > 3; --i)
	{
		const int32 Prev = (i - 1 + PosList.Num()) % PosList.Num();
		const FVector3d& A = PosList[Prev];
		const FVector3d& B = PosList[i];
		if (FVector::DistSquared2D(A, B) < 1.0)
		{
			PosList.RemoveAt(i);
			RingVids.RemoveAt(i);
		}
	}

	int32 TrisAdded = 0;
	int32 Guard = 0;
	while (PosList.Num() > 3 && Guard++ < 8000)
	{
		int32 EarIndex = INDEX_NONE;
		double BestCost = TNumericLimits<double>::Max();
		for (int32 i = 0; i < PosList.Num(); ++i)
		{
			const int32 Prev = (i - 1 + PosList.Num()) % PosList.Num();
			const int32 Next = (i + 1) % PosList.Num();
			const FVector3d& A = PosList[Prev];
			const FVector3d& B = PosList[i];
			const FVector3d& C = PosList[Next];
			if (Cross2d(B - A, C - B) <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			bool bPointInside = false;
			for (int32 j = 0; j < PosList.Num(); ++j)
			{
				if (j == Prev || j == i || j == Next)
				{
					continue;
				}
				if (PointInTriangle(PosList[j], A, B, C))
				{
					bPointInside = true;
					break;
				}
			}
			if (bPointInside)
			{
				continue;
			}
			// Prefer compact ears (short longest edge) — avoids red-zone sliver fans.
			const double Cost = FMath::Max3(
				FVector::Dist2D(A, B),
				FVector::Dist2D(B, C),
				FVector::Dist2D(C, A));
			if (Cost < BestCost)
			{
				BestCost = Cost;
				EarIndex = i;
			}
		}
		if (EarIndex == INDEX_NONE)
		{
			break;
		}
		const int32 Prev = (EarIndex - 1 + PosList.Num()) % PosList.Num();
		const int32 Next = (EarIndex + 1) % PosList.Num();
		AppendWorldUpTriangle(Mesh, RingVids[Prev], RingVids[EarIndex], RingVids[Next], ActorTransform);
		++TrisAdded;
		PosList.RemoveAt(EarIndex);
		RingVids.RemoveAt(EarIndex);
	}

	if (PosList.Num() == 3 && RingVids.Num() == 3)
	{
		AppendWorldUpTriangle(Mesh, RingVids[0], RingVids[1], RingVids[2], ActorTransform);
		++TrisAdded;
	}
	else if (PosList.Num() > 3)
	{
		TArray<FVector2d> Poly2D;
		Poly2D.Reserve(PosList.Num());
		for (const FVector3d& P : PosList)
		{
			Poly2D.Add(FVector2d(P.X, P.Y));
		}
		TArray<FIndex3i> Tris;
		PolygonTriangulation::TriangulateSimplePolygon(Poly2D, Tris, /*bOrientAsHoleFill*/ false);
		for (const FIndex3i& Tri : Tris)
		{
			if (!RingVids.IsValidIndex(Tri.A) || !RingVids.IsValidIndex(Tri.B) || !RingVids.IsValidIndex(Tri.C))
			{
				continue;
			}
			const FVector2d Centroid(
				(Poly2D[Tri.A].X + Poly2D[Tri.B].X + Poly2D[Tri.C].X) / 3.0,
				(Poly2D[Tri.A].Y + Poly2D[Tri.B].Y + Poly2D[Tri.C].Y) / 3.0);
			if (!PointInPolygon2D(Centroid, Poly2D))
			{
				continue;
			}
			AppendWorldUpTriangle(Mesh, RingVids[Tri.A], RingVids[Tri.B], RingVids[Tri.C], ActorTransform);
			++TrisAdded;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Fig1/CenterFill] holePts=%d tris=%d (short-ear clip)"), HoleWorldRing.Num(), TrisAdded);
}

bool AMoKuEditIntersectionActor::BuildFig1JunctionCoverMesh()
{
	if (!JunctionData || !DynamicMeshComponent)
	{
		return false;
	}

	const FMetaLandRoadJunctionNode& Node = JunctionData->JunctionNode;
	if (Node.Arms.Num() < 2)
	{
		return false;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Fig1] BEGIN junction=%s arms=%d topology=%d"),
		*GetName(),
		Node.Arms.Num(),
		static_cast<int32>(Node.Topology));

	struct FFig1ArmSlice
	{
		UMetaLandRoadSplineComponent* Spline = nullptr;
		double OuterS = 0.0;
		double InnerS = 0.0;
		TArray<FVector> OuterCS;
		TArray<FVector> InnerCS;
		double PolarAngle = 0.0;
		EMetaLandRoadJunctionConnectSide ConnectSide = EMetaLandRoadJunctionConnectSide::Both;
	};

	const FVector Anchor = Node.AnchorWorld.IsNearlyZero() ? GetActorLocation() : Node.AnchorWorld;
	bool bTJunction = (Node.Topology == EMetaLandRoadJunctionTopology::T);
	if (!bTJunction)
	{
		// Recook/create may leave Topology stale; half ConnectSide implies T through.
		for (const FMetaLandRoadJunctionArmRef& Arm : Node.Arms)
		{
			if (Arm.bThroughArm && Arm.ConnectSide != EMetaLandRoadJunctionConnectSide::Both)
			{
				bTJunction = true;
				break;
			}
		}
	}
	TArray<FFig1ArmSlice> ArmSlices;

	// Aug2: half at CS.Num()/2 so T mouth stays ≥3 pts and ends on EdgeC (weld to T-open).
	auto KeepConnectSideHalf = [](TArray<FVector>& CS, EMetaLandRoadJunctionConnectSide Side)
	{
		if (Side == EMetaLandRoadJunctionConnectSide::Both || CS.Num() < 3)
		{
			return;
		}
		const int32 Mid = CS.Num() / 2;
		if (Side == EMetaLandRoadJunctionConnectSide::Left)
		{
			CS.SetNum(Mid + 1);
		}
		else if (Side == EMetaLandRoadJunctionConnectSide::Right)
		{
			TArray<FVector> Half;
			Half.Reserve(CS.Num() - Mid);
			for (int32 PointIndex = Mid; PointIndex < CS.Num(); ++PointIndex)
			{
				Half.Add(CS[PointIndex]);
			}
			CS = MoveTemp(Half);
		}
	};

	auto TryAddSlice = [&ArmSlices, &Anchor, &KeepConnectSideHalf](
		UMetaLandRoadSplineComponent* Spline,
		double CutS,
		double IntersectS,
		EMetaLandRoadJunctionConnectSide ConnectSide,
		const TArray<int32>& SubsetLanes,
		double StripDepthCm)
	{
		if (!Spline || CutS <= KINDA_SMALL_NUMBER || IntersectS <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		if (FMath::IsNearlyEqual(CutS, IntersectS, 1.0))
		{
			return;
		}

		const double SampleS = CutS;
		const bool bSubsetMouth = SubsetLanes.Num() > 0;

		FFig1ArmSlice Slice;
		Slice.Spline = Spline;
		Slice.OuterS = CutS;
		Slice.InnerS = CutS;
		Slice.ConnectSide = ConnectSide;

		bool bGotCS = false;
		if (bSubsetMouth)
		{
			// 2-lane (etc.) extend: mouth width = selected lanes only, not full road.
			bGotCS = Spline->GetRoadCrossSectionPointsForLaneIndicesAtS(
				SampleS, SubsetLanes, Slice.OuterCS, ESplineCoordinateSpace::World)
				&& Slice.OuterCS.Num() >= 2;
		}
		if (!bGotCS)
		{
			bGotCS = Spline->GetRoadCrossSectionPointsAtS(
				SampleS, Slice.OuterCS, ESplineCoordinateSpace::World)
				&& Slice.OuterCS.Num() >= 2;
		}
		if (!bGotCS)
		{
			FVector EdgeL, EdgeR, EdgeC;
			if (!Spline->GetRoadEdgePointsAtS(SampleS, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Fig1] TryAddSlice skip — no CS/edges at S=%.1f"), CutS);
				return;
			}
			Slice.OuterCS = { EdgeL, EdgeR };
		}

		if (!bSubsetMouth)
		{
			KeepConnectSideHalf(Slice.OuterCS, ConnectSide);
			if (Slice.OuterCS.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Fig1] TryAddSlice skip — half-CS empty side=%d"), static_cast<int32>(ConnectSide));
				return;
			}

			// Full-road snap: EdgeL/EdgeR/EdgeC (T-open shares EdgeC).
			FVector EdgeL, EdgeR, EdgeC;
			if (Spline->GetRoadEdgePointsAtS(SampleS, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
			{
				if (ConnectSide == EMetaLandRoadJunctionConnectSide::Left)
				{
					Slice.OuterCS[0] = EdgeL;
					Slice.OuterCS.Last() = EdgeC;
				}
				else if (ConnectSide == EMetaLandRoadJunctionConnectSide::Right)
				{
					Slice.OuterCS[0] = EdgeC;
					Slice.OuterCS.Last() = EdgeR;
				}
				else
				{
					Slice.OuterCS[0] = EdgeL;
					Slice.OuterCS.Last() = EdgeR;
				}
			}
		}
		else if (ConnectSide != EMetaLandRoadJunctionConnectSide::Both)
		{
			// T + 2-lane subset: weld center-facing end to EdgeC (same as half-CS) for T-open weld.
			FVector EdgeL, EdgeR, EdgeC;
			if (Spline->GetRoadEdgePointsAtS(SampleS, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
			{
				if (ConnectSide == EMetaLandRoadJunctionConnectSide::Left)
				{
					Slice.OuterCS[0] = EdgeL;
					Slice.OuterCS.Last() = EdgeC;
				}
				else
				{
					Slice.OuterCS[0] = EdgeC;
					Slice.OuterCS.Last() = EdgeR;
				}
			}
		}

		const FVector OuterMid = (Slice.OuterCS[0] + Slice.OuterCS.Last()) * 0.5;
		FVector TowardJunction = (Anchor - OuterMid).GetSafeNormal2D();
		if (TowardJunction.IsNearlyZero())
		{
			const double TowardS = IntersectS - CutS;
			const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(CutS, ESplineCoordinateSpace::World)
				.GetSafeNormal2D();
			TowardJunction = (TowardS >= 0.0) ? Tangent : -Tangent;
		}
		const FVector RightFacingJunction = FVector::CrossProduct(TowardJunction, FVector::UpVector).GetSafeNormal2D();
		{
			const double Score0 = FVector::DotProduct(Slice.OuterCS[0] - OuterMid, RightFacingJunction);
			const double Score1 = FVector::DotProduct(Slice.OuterCS.Last() - OuterMid, RightFacingJunction);
			if (Score0 > Score1)
			{
				Algo::Reverse(Slice.OuterCS);
			}
		}
		// After L→R facing-hub sort, re-pin endpoints to CutS road edges (same S as trim).
		{
			FVector EdgeL, EdgeR, EdgeC;
			if (Spline->GetRoadEdgePointsAtS(SampleS, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World)
				&& Slice.OuterCS.Num() >= 2)
			{
				auto ClosestEdge = [&](const FVector& P) -> FVector
				{
					const double DL = FVector::DistSquared2D(P, EdgeL);
					const double DR = FVector::DistSquared2D(P, EdgeR);
					const double DC = FVector::DistSquared2D(P, EdgeC);
					if (ConnectSide == EMetaLandRoadJunctionConnectSide::Left)
					{
						return DL <= DC ? EdgeL : EdgeC;
					}
					if (ConnectSide == EMetaLandRoadJunctionConnectSide::Right)
					{
						return DR <= DC ? EdgeR : EdgeC;
					}
					return DL <= DR ? EdgeL : EdgeR;
				};
				Slice.OuterCS[0] = ClosestEdge(Slice.OuterCS[0]);
				Slice.OuterCS.Last() = ClosestEdge(Slice.OuterCS.Last());
			}
		}

		Slice.InnerCS = Slice.OuterCS;
		const FVector MouthMid = (Slice.OuterCS[0] + Slice.OuterCS.Last()) * 0.5;
		Slice.PolarAngle = FMath::Atan2(MouthMid.Y - Anchor.Y, MouthMid.X - Anchor.X);
		ArmSlices.Add(Slice);
		FVector LogL, LogR, LogC;
		if (Spline->GetRoadEdgePointsAtS(SampleS, LogL, LogR, LogC, ESplineCoordinateSpace::World))
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[Fig1/MouthSample] S=%.1f hubS=%.1f edgeW=%.0f CSpts=%d subset=%d side=%d pinCutS=1"),
				SampleS,
				IntersectS,
				FVector::Dist2D(LogL, LogR),
				Slice.OuterCS.Num(),
				bSubsetMouth ? 1 : 0,
				static_cast<int32>(ConnectSide));
		}
		(void)StripDepthCm;
	};

	for (const FMetaLandRoadJunctionArmRef& Arm : Node.Arms)
	{
		UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
		if (!Spline)
		{
			continue;
		}

		const double StripDepthCm = 0.0;
		const bool bTrueTHalf =
			bTJunction
			&& Node.Arms.Num() == 2
			&& Arm.bThroughArm
			&& Arm.ConnectSide != EMetaLandRoadJunctionConnectSide::Both;
		const EMetaLandRoadJunctionConnectSide SliceSide = bTrueTHalf
			? Arm.ConnectSide
			: EMetaLandRoadJunctionConnectSide::Both;

		if (Arm.bThroughArm)
		{
			if (Arm.CutDistanceS > KINDA_SMALL_NUMBER)
			{
				TryAddSlice(
					Spline, Arm.CutDistanceS, Arm.IntersectDistanceS, SliceSide,
					Arm.ConnectedLaneIndices, StripDepthCm);
			}
			if (Arm.DepartCutS > Arm.CutDistanceS + KINDA_SMALL_NUMBER)
			{
				TryAddSlice(
					Spline, Arm.DepartCutS, Arm.IntersectDistanceS, SliceSide,
					Arm.ConnectedLaneIndices, StripDepthCm);
			}
		}
		else if (Arm.IntersectDistanceS >= Spline->GetSplineLength() * 0.5)
		{
			if (Arm.CutDistanceS > KINDA_SMALL_NUMBER)
			{
				TryAddSlice(
					Spline, Arm.CutDistanceS, Arm.IntersectDistanceS, SliceSide,
					Arm.ConnectedLaneIndices, StripDepthCm);
			}
		}
		else if (Arm.DepartCutS > KINDA_SMALL_NUMBER)
		{
			TryAddSlice(
				Spline, Arm.DepartCutS, Arm.IntersectDistanceS, SliceSide,
				Arm.ConnectedLaneIndices, StripDepthCm);
		}
	}

	if (ArmSlices.Num() < 2)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("BuildFig1JunctionCoverMesh: ArmSlices=%d arms=%d — abort (cuts collapsed or CS failed)"),
			ArmSlices.Num(),
			Node.Arms.Num());
		for (int32 ArmIndex = 0; ArmIndex < Node.Arms.Num(); ++ArmIndex)
		{
			const FMetaLandRoadJunctionArmRef& Arm = Node.Arms[ArmIndex];
			UE_LOG(
				LogTemp,
				Error,
				TEXT("  arm%d CutS=%.1f DepartS=%.1f IntersectS=%.1f through=%d side=%d spline=%s"),
				ArmIndex,
				Arm.CutDistanceS,
				Arm.DepartCutS,
				Arm.IntersectDistanceS,
				Arm.bThroughArm ? 1 : 0,
				static_cast<int32>(Arm.ConnectSide),
				*GetNameSafe(Arm.Spline.Get()));
		}
		return false;
	}

	ArmSlices.Sort([](const FFig1ArmSlice& A, const FFig1ArmSlice& B)
	{
		return A.PolarAngle < B.PolarAngle;
	});

	// MetaRoad Fig1 ring: LaneStrip mouths + CornerFillets + CenterFill (hub vacancy).
	CornerInfo.Reset();
	EdgePath.Reset();

	TArray<TArray<FVector3d>> CurbOuterArcs;
	TArray<TArray<FVector3d>> CurbInnerArcs;
	TArray<bool> bMeshFillet;
	CurbOuterArcs.SetNum(ArmSlices.Num());
	CurbInnerArcs.SetNum(ArmSlices.Num());
	bMeshFillet.Init(false, ArmSlices.Num());
	int32 MaxCornerSegs = 14;

	// Edge lane = 缁跨澶翠袱鐐? outermost CS sample + next sample inward (one lane), NOT whole-road outer only.
	auto ResolveTConnectSideForSlice = [&Node](const FFig1ArmSlice& Slice) -> EMetaLandRoadJunctionConnectSide
	{
		for (const FMetaLandRoadJunctionArmRef& Arm : Node.Arms)
		{
			if (Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get()) == Slice.Spline
				&& Arm.ConnectSide != EMetaLandRoadJunctionConnectSide::Both)
			{
				return Arm.ConnectSide;
			}
		}
		return Slice.ConnectSide;
	};

	auto WeldSideToFarCurb = [](FCornerInfo& SideEntry, const FVector& FarCurb)
	{
		if (SideEntry.CornerPoints.Num() == 0)
		{
			return;
		}
		if (FVector::Dist2D(SideEntry.CornerPoints.Last(), FarCurb) > 5.0)
		{
			SideEntry.CornerPoints.Add(FarCurb);
		}
	};

	auto GetEdgeLanePair = [](const TArray<FVector>& CS, bool bRight, FVector& OutOuter, FVector& OutInner) -> bool
	{
		if (CS.Num() < 2)
		{
			return false;
		}
		if (bRight)
		{
			OutOuter = CS.Last();
			OutInner = (CS.Num() >= 3) ? CS[CS.Num() - 2] : CS[0];
		}
		else
		{
			OutOuter = CS[0];
			OutInner = (CS.Num() >= 3) ? CS[1] : CS.Last();
		}
		return true;
	};

	for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
	{
		const FFig1ArmSlice& Port = ArmSlices[PortIndex];
		const FFig1ArmSlice& NextPort = ArmSlices[(PortIndex + 1) % ArmSlices.Num()];

		FCornerInfo Side;
		Side.Tag = ECornerTag::Side;
		Side.SampleSpline = Port.Spline;
		Side.SampleDistanceS = Port.OuterS;
		for (const FVector& Point : Port.OuterCS)
		{
			Side.CornerPoints.Add(Point);
		}
		CornerInfo.Add(Side);

		FVector OuterRight, InnerRight, OuterLeft, InnerLeft;
		if (!GetEdgeLanePair(Port.OuterCS, true, OuterRight, InnerRight)
			|| !GetEdgeLanePair(NextPort.OuterCS, false, OuterLeft, InnerLeft))
		{
			bMeshFillet[PortIndex] = false;
			CurbOuterArcs[PortIndex].Reset();
			CurbInnerArcs[PortIndex].Reset();
			FCornerInfo EmptyArc;
			EmptyArc.Tag = ECornerTag::Corner;
			CornerInfo.Add(EmptyArc);
			continue;
		}

		const double LaneW = FMath::Max(
			0.5 * (FVector::Dist2D(OuterRight, InnerRight) + FVector::Dist2D(OuterLeft, InnerLeft)),
			50.0);
		const double OuterChord = FVector::Dist2D(OuterRight, OuterLeft);
		const double InnerChord = FVector::Dist2D(InnerRight, InnerLeft);
		const FVector OuterMid = (OuterRight + OuterLeft) * 0.5;
		const double DistHub = FVector::Dist2D(OuterMid, Anchor);
		const double HalfOuterChord = OuterChord * 0.5;

		const bool bSameSplineCorner = (Port.Spline != nullptr && Port.Spline == NextPort.Spline);
		const bool bTOpenSide = bTJunction && bSameSplineCorner;
		const bool bRealCorner = !bSameSplineCorner && OuterChord > 10.0 && InnerChord > 10.0;
		bMeshFillet[PortIndex] = bRealCorner;

		FCornerInfo ArcInfo;
		ArcInfo.Tag = ECornerTag::Corner;
		if (bRealCorner)
		{
			// Fig2/3: Bezier on green-arrow outers (few segs) + parallel inner at laneW.
			// Circular long-way flip + 12鈥?8 segs 鈫?jagged/folded red polylines & deformation.
			const double TargetSag = FMath::Clamp(
				FMath::Min3(HalfOuterChord * 0.28, DistHub * 0.28, LaneW * 1.05),
				40.0,
				FMath::Min(LaneW * 1.35, 220.0));
			const int32 ArcSegs = FMath::Clamp(FMath::CeilToInt(OuterChord / 55.0), 10, 18);
			MaxCornerSegs = FMath::Max(MaxCornerSegs, ArcSegs);

			BuildSmoothInwardLaneBezier(
				OuterRight,
				OuterLeft,
				Anchor,
				TargetSag,
				ArcSegs,
				CurbOuterArcs[PortIndex]);

			BuildParallelLaneInnerRail(
				CurbOuterArcs[PortIndex],
				InnerRight,
				InnerLeft,
				CurbInnerArcs[PortIndex]);

			bool bRailsOk = CurbInnerArcs[PortIndex].Num() == CurbOuterArcs[PortIndex].Num()
				&& CurbInnerArcs[PortIndex].Num() >= 2
				&& IsLaneLinkPolylineValid(CurbOuterArcs[PortIndex], OuterChord)
				&& IsLaneLinkPolylineValid(CurbInnerArcs[PortIndex], InnerChord);
			if (bRailsOk && CurbOuterArcs[PortIndex].Num() >= 3)
			{
				const int32 Mid = CurbOuterArcs[PortIndex].Num() / 2;
				const double MidLaneW = FVector::Dist2D(
					CurbOuterArcs[PortIndex][Mid],
					CurbInnerArcs[PortIndex][Mid]);
				bRailsOk = MidLaneW >= LaneW * 0.25 && MidLaneW <= LaneW * 2.5;
			}
			if (!bRailsOk)
			{
				CurbOuterArcs[PortIndex] = { OuterRight, OuterLeft };
				CurbInnerArcs[PortIndex] = { InnerRight, InnerLeft };
				bMeshFillet[PortIndex] = false;
				UE_LOG(LogTemp, Warning, TEXT("[Fig1/LaneLink] port%d rails invalid — straight lane"), PortIndex);
			}
			else
			{
				// Force shared mouth/road endpoints (XY weld with strip + hole).
				CurbOuterArcs[PortIndex][0] = OuterRight;
				CurbOuterArcs[PortIndex].Last() = OuterLeft;
				CurbInnerArcs[PortIndex][0] = InnerRight;
				CurbInnerArcs[PortIndex].Last() = InnerLeft;
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[Fig1/LaneLink] port%d->%d bezierRail segs=%d chord=%.0f sag=%.0f laneW=%.0f"),
					PortIndex,
					(PortIndex + 1) % ArmSlices.Num(),
					ArcSegs,
					OuterChord,
					TargetSag,
					LaneW);
			}
			ArcInfo.CornerPoints = CurbOuterArcs[PortIndex];
			(void)InnerChord;
		}
		else if (bTOpenSide)
		{
			// T open flank (full road gap): close the FAR half of the through hole.
			// Stub-side mouths are half-CS; far outer鈫抩uter + inner at center fills the rest.
			bMeshFillet[PortIndex] = false;
			FVector PortL, PortR, PortC = FVector::ZeroVector, NextL, NextR, NextC = FVector::ZeroVector;
			const bool bGot = Port.Spline
				&& NextPort.Spline
				&& Port.Spline->GetRoadEdgePointsAtS(
					Port.OuterS, PortL, PortR, PortC, ESplineCoordinateSpace::World)
				&& NextPort.Spline->GetRoadEdgePointsAtS(
					NextPort.OuterS, NextL, NextR, NextC, ESplineCoordinateSpace::World);
			if (bGot)
			{
				const EMetaLandRoadJunctionConnectSide TSide = ResolveTConnectSideForSlice(Port);
				const bool bStubOnRight =
					(TSide == EMetaLandRoadJunctionConnectSide::Right);
				const FVector PortFar = bStubOnRight ? PortL : PortR;
				const FVector NextFar = bStubOnRight ? NextL : NextR;
				if (CornerInfo.Num() > 0 && CornerInfo.Last().Tag == ECornerTag::Side)
				{
					WeldSideToFarCurb(CornerInfo.Last(), PortFar);
				}
				CurbOuterArcs[PortIndex] = { PortFar, NextFar };
				CurbInnerArcs[PortIndex] = { PortC, NextC };
				bMeshFillet[PortIndex] = true; // mesh far-half strip into the gap
			}
			else
			{
				CurbOuterArcs[PortIndex] = { OuterRight, OuterLeft };
				CurbInnerArcs[PortIndex] = { InnerRight, InnerLeft };
			}
			ArcInfo.CornerPoints = CurbOuterArcs[PortIndex];
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[Fig1/LaneLink] T-open farHalf port%d side=%d"),
				PortIndex,
				static_cast<int32>(ResolveTConnectSideForSlice(Port)));
		}
		else
		{
			bMeshFillet[PortIndex] = false;
			CurbOuterArcs[PortIndex] = { OuterRight, OuterLeft };
			CurbInnerArcs[PortIndex] = { InnerRight, InnerLeft };
			ArcInfo.CornerPoints = CurbOuterArcs[PortIndex];
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[Fig1/LaneLink] straight port%d outChord=%.0f sameSpline=%d open=%d"),
				PortIndex,
				OuterChord,
				bSameSplineCorner ? 1 : 0,
				bTOpenSide ? 1 : 0);
		}
		CornerInfo.Add(ArcInfo);
	}

	// If stub sits between through mouths in polar order, T-open never fires — force farHalf.
	bool bHaveTOpen = false;
	for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
	{
		const FFig1ArmSlice& Port = ArmSlices[PortIndex];
		const FFig1ArmSlice& NextPort = ArmSlices[(PortIndex + 1) % ArmSlices.Num()];
		if (bTJunction && Port.Spline && Port.Spline == NextPort.Spline
			&& !FMath::IsNearlyEqual(Port.OuterS, NextPort.OuterS, 1.0)
			&& bMeshFillet[PortIndex])
		{
			bHaveTOpen = true;
			break;
		}
	}
	TArray<TArray<FVector3d>> ExtraFarOuter;
	TArray<TArray<FVector3d>> ExtraFarInner;
	if (bTJunction && !bHaveTOpen)
	{
		for (int32 A = 0; A < ArmSlices.Num(); ++A)
		{
			for (int32 B = A + 1; B < ArmSlices.Num(); ++B)
			{
				const FFig1ArmSlice& SA = ArmSlices[A];
				const FFig1ArmSlice& SB = ArmSlices[B];
				if (!SA.Spline || SA.Spline != SB.Spline
					|| FMath::IsNearlyEqual(SA.OuterS, SB.OuterS, 1.0))
				{
					continue;
				}
				FVector AL, AR, AC, BL, BR, BC;
				if (!SA.Spline->GetRoadEdgePointsAtS(SA.OuterS, AL, AR, AC, ESplineCoordinateSpace::World)
					|| !SB.Spline->GetRoadEdgePointsAtS(SB.OuterS, BL, BR, BC, ESplineCoordinateSpace::World))
				{
					continue;
				}
				const EMetaLandRoadJunctionConnectSide TSide =
					ResolveTConnectSideForSlice(SA) != EMetaLandRoadJunctionConnectSide::Both
						? ResolveTConnectSideForSlice(SA)
						: ResolveTConnectSideForSlice(SB);
				const bool bStubOnRight =
					(TSide == EMetaLandRoadJunctionConnectSide::Right);
				const FVector AFar = bStubOnRight ? AL : AR;
				const FVector BFar = bStubOnRight ? BL : BR;
				ExtraFarOuter.Add({ AFar, BFar });
				ExtraFarInner.Add({ AC, BC });
				UE_LOG(LogTemp, Log, TEXT("[Fig1/LaneLink] T-open farHalf FORCED %d<->%d"), A, B);
			}
		}
	}

	// Strip rails still use edge-lane inset; hub fill mouth = full OuterCS (接口宽度=路尖).
	for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
	{
		const TArray<FVector>& CS = ArmSlices[PortIndex].OuterCS;
		ArmSlices[PortIndex].InnerCS.Reset();
		if (CS.Num() >= 4)
		{
			ArmSlices[PortIndex].InnerCS.Add(CS[1]);
			ArmSlices[PortIndex].InnerCS.Add(CS[CS.Num() - 2]);
		}
		else if (CS.Num() >= 2)
		{
			ArmSlices[PortIndex].InnerCS = CS;
		}
	}

	// EdgePath = dual-rail outers for audit (lane outer rail + mouth outers).
	EdgePath.Reset();
	for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
	{
		const TArray<FVector>& CutCS = ArmSlices[PortIndex].OuterCS;
		if (CutCS.Num() >= 2)
		{
			if (EdgePath.Num() == 0 || FVector::DistSquared(EdgePath.Last(), CutCS[0]) >= 1.0)
			{
				EdgePath.Add(CutCS[0]);
			}
			if (FVector::DistSquared(EdgePath.Last(), CutCS.Last()) >= 1.0)
			{
				EdgePath.Add(CutCS.Last());
			}
		}
		for (int32 Pi = 1; Pi < CurbOuterArcs[PortIndex].Num(); ++Pi)
		{
			if (EdgePath.Num() == 0 || FVector::DistSquared(EdgePath.Last(), CurbOuterArcs[PortIndex][Pi]) >= 1.0)
			{
				EdgePath.Add(CurbOuterArcs[PortIndex][Pi]);
			}
		}
	}

	// Soft audit only — hard abort cleared T covers (丁字口空洞 / 无 junction) while roads stayed trimmed.
	if (!ValidateJunctionMeshPointIndices(TEXT("Fig1/BoundaryRing")))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("BuildFig1JunctionCoverMesh: ring audit warnings — still publishing (T/Cross need cover)"));
	}

	for (int32 ArmIndex = 0; ArmIndex < ArmSlices.Num(); ++ArmIndex)
	{
		const FFig1ArmSlice& Slice = ArmSlices[ArmIndex];
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Fig1/Port] port=%d OuterCS=%d flushCut=1 OuterS=%.1f theta=%.1f side=%d fillet=%d"),
			ArmIndex,
			Slice.OuterCS.Num(),
			Slice.OuterS,
			FMath::RadiansToDegrees(Slice.PolarAngle),
			static_cast<int32>(Slice.ConnectSide),
			bMeshFillet[ArmIndex] ? 1 : 0);
		if (Slice.OuterCS.Num() < 2)
		{
			UE_LOG(LogTemp, Error, TEXT("[Fig1/Port] port=%d bad CS 鈥?abort"), ArmIndex);
			return false;
		}
	}

	// Fig3 pipeline: edge-lane links first (arc+laneW inset), then fillmesh UV in the hole.
	// Ends of each link = road edge points (缁跨澶?. No exterior tabs (绾㈢澶村簾鍑犱綍).
	constexpr float MeshHeightOffset = 10.0f;
	const FTransform ActorTransform = GetActorTransform();
	// Keep mouth/rail Z from road CS samples (same +10 as asphalt). Global CoverFlatZ
	// average was pulling ports off the road cut -> height seam / light leak.

	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.EnableVertexUVs(FVector2f::Zero());

	int32 LaneLinkCount = 0;
	auto AppendLaneStrip = [&](const TArray<FVector3d>& OuterArc, const TArray<FVector3d>& InnerArc)
	{
		if (OuterArc.Num() < 2 || InnerArc.Num() < 2)
		{
			return;
		}
		AppendPolylineStrip(
			Mesh,
			OuterArc,
			InnerArc,
			nullptr,
			0.0,
			nullptr,
			0.0,
			nullptr,
			0.0,
			nullptr,
			0.0,
			MeshHeightOffset,
			ActorTransform);
		++LaneLinkCount;
	};
	for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
	{
		if (!bMeshFillet[PortIndex])
		{
			continue;
		}
		AppendLaneStrip(CurbOuterArcs[PortIndex], CurbInnerArcs[PortIndex]);
	}
	for (int32 ExtraIndex = 0; ExtraIndex < ExtraFarOuter.Num(); ++ExtraIndex)
	{
		AppendLaneStrip(ExtraFarOuter[ExtraIndex], ExtraFarInner[ExtraIndex]);
	}

	{
		TArray<FVector> HoleRing;
		TArray<FVector2f> HoleUVs;
		HoleRing.Reserve(ArmSlices.Num() * (4 + MaxCornerSegs));
		HoleUVs.Reserve(ArmSlices.Num() * (4 + MaxCornerSegs));

		auto AppendWorld = [this, &HoleRing, &HoleUVs](const FVector& WorldPos)
		{
			if (HoleRing.Num() > 0 && FVector::DistSquared2D(HoleRing.Last(), WorldPos) < 1.0)
			{
				return;
			}
			HoleRing.Add(WorldPos);
			HoleUVs.Add(ComputeJunctionVertexUV(WorldPos));
		};

		for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
		{
			const TArray<FVector>& MouthOuter = ArmSlices[PortIndex].OuterCS;
			if (MouthOuter.Num() < 2)
			{
				continue;
			}
			// T / half-CS mouths (<4): FULL OuterCS — mid-chord left a blue hole in 丁字口.
			// Cross with full CS: CS[1]..CS[N-2] + curb inner (edge lanes = strips).
			const bool bFullMouthHole = bTJunction || MouthOuter.Num() < 4;
			if (bFullMouthHole)
			{
				for (const FVector& MouthPt : MouthOuter)
				{
					AppendWorld(MouthPt);
				}
				const TArray<FVector3d>& CloseRail = CurbOuterArcs[PortIndex].Num() >= 2
					? CurbOuterArcs[PortIndex]
					: CurbInnerArcs[PortIndex];
				for (int32 Pi = 1; Pi < CloseRail.Num(); ++Pi)
				{
					AppendWorld(FVector(CloseRail[Pi]));
				}
			}
			else
			{
				for (int32 Mi = 1; Mi <= MouthOuter.Num() - 2; ++Mi)
				{
					AppendWorld(MouthOuter[Mi]);
				}
				const TArray<FVector3d>& CloseInner = CurbInnerArcs[PortIndex].Num() >= 2
					? CurbInnerArcs[PortIndex]
					: CurbOuterArcs[PortIndex];
				for (int32 Pi = 1; Pi < CloseInner.Num(); ++Pi)
				{
					AppendWorld(FVector(CloseInner[Pi]));
				}
			}
		}
		// Forced T far-half chords are meshed as ExtraFar strips but must also close the hub ring.
		for (int32 ExtraIndex = 0; ExtraIndex < ExtraFarOuter.Num(); ++ExtraIndex)
		{
			const TArray<FVector3d>& FarOuter = ExtraFarOuter[ExtraIndex];
			for (int32 Pi = 0; Pi < FarOuter.Num(); ++Pi)
			{
				AppendWorld(FVector(FarOuter[Pi]));
			}
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Fig1/Hub] laneLinks=%d extraFar=%d holePts=%d ports=%d t=%d (T=fullMouth+outer / Cross=inset+inner)"),
			LaneLinkCount,
			ExtraFarOuter.Num(),
			HoleRing.Num(),
			ArmSlices.Num(),
			bTJunction ? 1 : 0);

		if (HoleRing.Num() >= 3)
		{
			AppendFig1CenterFill(Mesh, HoleRing, HoleUVs, MeshHeightOffset, ActorTransform);
		}
	}

	// Last resort: closed silhouette (T = full mouth + outer; Cross = inset + inner).
	if (Mesh.TriangleCount() == 0)
	{
		TArray<FVector> Silhouette;
		TArray<FVector2f> SilhouetteUVs;
		auto AppendSil = [&](const FVector& P)
		{
			if (Silhouette.Num() > 0 && FVector::DistSquared2D(Silhouette.Last(), P) < 1.0)
			{
				return;
			}
			Silhouette.Add(P);
			SilhouetteUVs.Add(ComputeJunctionVertexUV(P));
		};
		for (int32 PortIndex = 0; PortIndex < ArmSlices.Num(); ++PortIndex)
		{
			const TArray<FVector>& CS = ArmSlices[PortIndex].OuterCS;
			if (CS.Num() < 2)
			{
				continue;
			}
			const bool bFullMouthHole = bTJunction || CS.Num() < 4;
			if (bFullMouthHole)
			{
				for (const FVector& P : CS)
				{
					AppendSil(P);
				}
				const TArray<FVector3d>& CloseRail = CurbOuterArcs[PortIndex].Num() >= 2
					? CurbOuterArcs[PortIndex]
					: CurbInnerArcs[PortIndex];
				for (int32 Pi = 1; Pi < CloseRail.Num(); ++Pi)
				{
					AppendSil(FVector(CloseRail[Pi]));
				}
			}
			else
			{
				for (int32 Mi = 1; Mi <= CS.Num() - 2; ++Mi)
				{
					AppendSil(CS[Mi]);
				}
				const TArray<FVector3d>& CloseInner = CurbInnerArcs[PortIndex].Num() >= 2
					? CurbInnerArcs[PortIndex]
					: CurbOuterArcs[PortIndex];
				for (int32 Pi = 1; Pi < CloseInner.Num(); ++Pi)
				{
					AppendSil(FVector(CloseInner[Pi]));
				}
			}
		}
		for (int32 ExtraIndex = 0; ExtraIndex < ExtraFarOuter.Num(); ++ExtraIndex)
		{
			for (const FVector3d& P : ExtraFarOuter[ExtraIndex])
			{
				AppendSil(FVector(P));
			}
		}
		if (Silhouette.Num() >= 3)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Fig1/Hub] primary fill empty — silhouette fallback pts=%d t=%d"), Silhouette.Num(), bTJunction ? 1 : 0);
			AppendFig1CenterFill(Mesh, Silhouette, SilhouetteUVs, MeshHeightOffset, ActorTransform);
		}
	}

	if (Mesh.TriangleCount() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildFig1JunctionCoverMesh: zero tris after fallback — abort"));
		return false;
	}

	// Final height weld: unify Z for XY-coincident verts (strip/fill seams).
	UnifyNearbyVertexHeights(Mesh, 2.5);
	PublishJunctionDynamicMesh(Mesh);
	const int32 PublishedTris = DynamicMeshComponent && DynamicMeshComponent->GetDynamicMesh()
		? DynamicMeshComponent->GetDynamicMesh()->GetTriangleCount()
		: 0;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Fig1] END junction=%s topology=%d ports=%d tris=%d laneLinks=%d (Fig3 edge+fill)"),
		*GetName(),
		static_cast<int32>(Node.Topology),
		ArmSlices.Num(),
		PublishedTris,
		LaneLinkCount);
	return true;
}

void AMoKuEditIntersectionActor::RecookFromLinkedRoads()
{
	if (!JunctionData || JunctionData->JunctionNode.Arms.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("RecookFromLinkedRoads: abort — no JunctionData or arms<2"));
		return;
	}

	// Cover path refreshes linked roads once — avoid double RefreshArmMesh (卡).
	UMetaLandRoadEditorMode::RecookJunctionActorStatic(this, /*bRefreshLinkedRoadMeshes=*/false);
}

#if WITH_EDITOR
void AMoKuEditIntersectionActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(AMoKuEditIntersectionActor, CutExpandScale)
		|| PropName == GET_MEMBER_NAME_CHECKED(FMetaLandRoadJunctionNode, CutExpandScale))
	{
		if (JunctionData)
		{
			JunctionData->Modify();
			JunctionData->JunctionNode.CutExpandScale = CutExpandScale;
		}
		RecookFromLinkedRoads();
	}
}
#endif

void AMoKuEditIntersectionActor::DebugRecookJunction()
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(TEXT("Debug Recook Junction")));
	Modify();
	const int32 ArmCount = JunctionData ? JunctionData->JunctionNode.Arms.Num() : 0;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[DebugRecook] BEGIN junction=%s arms=%d"),
		*GetName(),
		ArmCount);
	RecookFromLinkedRoads();
	const int32 TriCount = DynamicMeshComponent && DynamicMeshComponent->GetDynamicMesh()
		? DynamicMeshComponent->GetDynamicMesh()->GetTriangleCount()
		: 0;
	if (TriCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[DebugRecook] END junction=%s tris=%d"), *GetName(), TriCount);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugRecook] END junction=%s tris=0 (cook failed / cleared)"), *GetName());
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
#endif
}


void AMoKuEditIntersectionActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (SplineComp)
	{
		SplineComp->SetVisibility(false);
		SplineComp->bShowLaneVisualization = false;
	}
	RefreshJunctionCoverMesh();
}

void AMoKuEditIntersectionActor::SetMeshTransform(const FTransform& InTransform)
{
	//if (StaticMeshAsset)
	//{
	//	StaticMeshAsset->SetWorldLocation(InTransform.GetLocation());
	//	StaticMeshAsset->SetWorldRotation(InTransform.GetRotation());
	//	StaticMeshAsset->SetWorldScale3D(InTransform.GetScale3D());
	//}

}
void AMoKuEditIntersectionActor::UpdateSplineCornerInfo(bool bIsRecreate)
{
	if (!bIsRecreate)
	{
		return;
	}

	if (JunctionData && JunctionData->JunctionNode.Arms.Num() >= 2)
	{
		SyncJunctionFrameworkFromArms();
		return;
	}

	CornerInfo.Empty();

	auto GetEdgeCenter = [](TArray<FVector3d> EdgePosition)->FVector
		{
			FVector Sum(ForceInit);
			for (auto& Pos : EdgePosition)
			{
				Sum += Pos;
			}
			return Sum /= double(EdgePosition.Num());
		};
	FVector CurActorLocation = this->GetActorLocation();
	for (auto& ConnectActor : ConnectionActors)
	{
		TArray<FVector3d> StartRoadEdge = ConnectActor->JunctionConnections["Start"].ConnectEdge;
		FVector3d StartEdgeCenter = GetEdgeCenter(StartRoadEdge);
		TArray<FVector3d> EndRoadEdge = ConnectActor->JunctionConnections["End"].ConnectEdge;
		FVector3d EndEdgeCenter = GetEdgeCenter(EndRoadEdge);

		double D0 = FVector::Distance(StartEdgeCenter, CurActorLocation);
		double D1 = FVector::Distance(EndEdgeCenter, CurActorLocation);
		FCornerInfo LinkEdge;
		if (D0 < D1)
		{
			LinkEdge.CornerPoints = StartRoadEdge;
		}
		else
		{
			LinkEdge.CornerPoints = EndRoadEdge;
		}
		CornerInfo.Add(LinkEdge);
	}
}
void AMoKuEditIntersectionActor::FillMeshActor(bool bIsRecreate)
{
	UpdateSplineCornerInfo(bIsRecreate);
	RefreshJunctionCoverMesh();
}

void AMoKuEditIntersectionActor::BuildOutwardCircularArc(
	const FVector& ArcStart,
	const FVector& ArcEnd,
	const FVector& IntersectPoint,
	double RadiusCm,
	int32 NumSegments,
	TArray<FVector3d>& OutArcPoints,
	double PreferredSagittaCm,
	bool bBulgeTowardHub)
{
	OutArcPoints.Reset();

	const FVector Start2D(ArcStart.X, ArcStart.Y, 0.0);
	const FVector End2D(ArcEnd.X, ArcEnd.Y, 0.0);
	const FVector Hub2D(IntersectPoint.X, IntersectPoint.Y, 0.0);
	const FVector Chord = End2D - Start2D;
	const double ChordLen = Chord.Size2D();
	const int32 Segs = FMath::Max(NumSegments, 2);
	if (ChordLen < KINDA_SMALL_NUMBER)
	{
		OutArcPoints.Add(ArcStart);
		OutArcPoints.Add(ArcEnd);
		return;
	}

	const FVector Mid = (Start2D + End2D) * 0.5;
	const double HalfChord = ChordLen * 0.5;

	FVector TowardHub = Hub2D - Mid;
	TowardHub.Z = 0.0;
	const double DistToHub = TowardHub.Size();
	if (DistToHub < KINDA_SMALL_NUMBER)
	{
		OutArcPoints.Add(ArcStart);
		OutArcPoints.Add(ArcEnd);
		return;
	}
	TowardHub /= DistToHub;

	double R = FMath::Max(FMath::Max(RadiusCm, HalfChord * 1.02), HalfChord + 1.0);
	double Sagitta = R - FMath::Sqrt(FMath::Max(0.0, R * R - HalfChord * HalfChord));
	if (PreferredSagittaCm > KINDA_SMALL_NUMBER)
	{
		Sagitta = PreferredSagittaCm;
	}
	// Prefer caller PreferredSagitta when set — old DistToHub*0.45 crushed T fillets to flat diagonals.
	const double MaxSag = bBulgeTowardHub
		? ((PreferredSagittaCm > KINDA_SMALL_NUMBER)
			? FMath::Min(HalfChord * 0.55, FMath::Max(PreferredSagittaCm, FMath::Min(DistToHub * 0.65, HalfChord * 0.45)))
			: FMath::Min3(HalfChord * 0.34, DistToHub * 0.50, FMath::Max(HalfChord * 0.14, 50.0)))
		: ((PreferredSagittaCm > KINDA_SMALL_NUMBER)
			? FMath::Min(HalfChord * 0.48, FMath::Max(PreferredSagittaCm, HalfChord * 0.22))
			: FMath::Min(HalfChord * 0.42, FMath::Max(HalfChord * 0.18, 40.0)));
	const double MinSag = (PreferredSagittaCm > KINDA_SMALL_NUMBER)
		? FMath::Min(PreferredSagittaCm * 0.85, MaxSag)
		: FMath::Min(HalfChord * 0.12, 24.0);
	Sagitta = FMath::Clamp(Sagitta, MinSag, MaxSag);
	R = (HalfChord * HalfChord + Sagitta * Sagitta) / FMath::Max(2.0 * Sagitta, KINDA_SMALL_NUMBER);

	// Inward: center opposite hub. Exterior hub-ring: center toward hub so bulge fills corner wedges.
	const FVector Center2D = bBulgeTowardHub
		? (Mid - TowardHub * (R - Sagitta))
		: (Mid + TowardHub * (R - Sagitta));
	FVector FromStart = Start2D - Center2D;
	FVector FromEnd = End2D - Center2D;
	FromStart.Z = 0.0;
	FromEnd.Z = 0.0;
	double AngleStart = FMath::Atan2(FromStart.Y, FromStart.X);
	double AngleEnd = FMath::Atan2(FromEnd.Y, FromEnd.X);
	double Delta = AngleEnd - AngleStart;
	while (Delta <= -PI)
	{
		Delta += 2.0 * PI;
	}
	while (Delta > PI)
	{
		Delta -= 2.0 * PI;
	}

	{
		const double MidAngleProbe = AngleStart + Delta * 0.5;
		const FVector Probe = Center2D + FVector(FMath::Cos(MidAngleProbe), FMath::Sin(MidAngleProbe), 0.0) * R;
		const bool bProbeTowardHub =
			FVector::DistSquared2D(Probe, Hub2D) < FVector::DistSquared2D(Mid, Hub2D);
		const bool bNeedFlip = bBulgeTowardHub ? !bProbeTowardHub : bProbeTowardHub;
		if (bNeedFlip)
		{
			if (Delta > 0.0)
			{
				Delta -= 2.0 * PI;
			}
			else
			{
				Delta += 2.0 * PI;
			}
		}
	}

	const float Z0 = static_cast<float>(ArcStart.Z);
	const float Z1 = static_cast<float>(ArcEnd.Z);
	OutArcPoints.Reserve(Segs + 1);
	for (int32 Seg = 0; Seg <= Segs; ++Seg)
	{
		const double T = static_cast<double>(Seg) / static_cast<double>(Segs);
		const double Angle = AngleStart + Delta * T;
		FVector Point(
			Center2D.X + FMath::Cos(Angle) * R,
			Center2D.Y + FMath::Sin(Angle) * R,
			FMath::Lerp(Z0, Z1, static_cast<float>(T)));
		OutArcPoints.Add(Point);
	}
	OutArcPoints[0] = ArcStart;
	OutArcPoints.Last() = ArcEnd;

	const FVector ArcMid = OutArcPoints[OutArcPoints.Num() / 2];
	const bool bTowardHub = FVector::DistSquared2D(ArcMid, Hub2D) < FVector::DistSquared2D(Mid, Hub2D);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Arc] towardHub=%d wantIn=%d Chord=%.1f Sag=%.1f R=%.1f Segs=%d"),
		bTowardHub ? 1 : 0,
		bBulgeTowardHub ? 1 : 0,
		ChordLen,
		Sagitta,
		R,
		Segs);
}

bool AMoKuEditIntersectionActor::ValidateJunctionMeshPointIndices(const TCHAR* ContextTag) const
{
	bool bOk = true;
	if (EdgePath.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] EdgePath point count=%d (<3)"), ContextTag, EdgePath.Num());
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] === Junction point-index audit ==="), ContextTag);
	int32 GlobalIndex = 0;

	FVector Hub = GetActorLocation();
	if (JunctionData && !JunctionData->JunctionNode.AnchorWorld.IsNearlyZero())
	{
		Hub = JunctionData->JunctionNode.AnchorWorld;
	}
	else if (EdgePath.Num() > 0)
	{
		FVector Acc = FVector::ZeroVector;
		for (const FVector& P : EdgePath)
		{
			Acc += P;
		}
		Hub = Acc / static_cast<double>(EdgePath.Num());
	}

	for (int32 CornerIndex = 0; CornerIndex < CornerInfo.Num(); ++CornerIndex)
	{
		const FCornerInfo& Info = CornerInfo[CornerIndex];
		const bool bExpectSide = (CornerIndex % 2) == 0;
		const TCHAR* TagName = Info.Tag == ECornerTag::Corner ? TEXT("Corner") : TEXT("Side");
		if (bExpectSide && Info.Tag != ECornerTag::Side)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Ring pattern break at [%d]: expected Side, got Corner"), ContextTag, CornerIndex);
			bOk = false;
		}
		if (!bExpectSide && Info.Tag != ECornerTag::Corner)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Ring pattern break at [%d]: expected Corner/Arc, got Side"), ContextTag, CornerIndex);
			bOk = false;
		}

		for (int32 LocalIndex = 0; LocalIndex < Info.CornerPoints.Num(); ++LocalIndex)
		{
			const FVector& P = Info.CornerPoints[LocalIndex];
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[%s] CornerInfo[%d].%s local=%d  P=(%.1f, %.1f, %.1f)"),
				ContextTag,
				CornerIndex,
				TagName,
				LocalIndex,
				P.X, P.Y, P.Z);
			if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z))
			{
				UE_LOG(LogTemp, Error, TEXT("[%s] Non-finite point at CornerInfo[%d] local=%d"), ContextTag, CornerIndex, LocalIndex);
				bOk = false;
			}
		}

		if (Info.Tag == ECornerTag::Corner && Info.CornerPoints.Num() >= 3)
		{
			const FVector ArcStart = Info.CornerPoints[0];
			const FVector ArcEnd = Info.CornerPoints.Last();
			const FVector ArcMid = Info.CornerPoints[Info.CornerPoints.Num() / 2];
			const FVector Chord = ArcEnd - ArcStart;
			const FVector Mid = (ArcStart + ArcEnd) * 0.5;
			const double CrossInt =
				Chord.X * (Hub.Y - Mid.Y) - Chord.Y * (Hub.X - Mid.X);
			const double CrossArc =
				Chord.X * (ArcMid.Y - Mid.Y) - Chord.Y * (ArcMid.X - Mid.X);
			const FVector BulgeDir = (ArcMid - Mid).GetSafeNormal2D();
			const FVector MidToHub = (Hub - Mid).GetSafeNormal2D();
			// Prefer inward (toward hub). Soft-warn only — T far-half / polar order can trip this.
			if (FMath::Abs(CrossInt) > KINDA_SMALL_NUMBER && (CrossInt * CrossArc) < 0.0)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[%s] Arc[%d] exterior bulge (soft) — fillet should bend toward hub"),
					ContextTag,
					CornerIndex);
			}
			else if (!BulgeDir.IsNearlyZero() && !MidToHub.IsNearlyZero()
				&& FVector::DotProduct(BulgeDir, MidToHub) <= 0.0)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[%s] Arc[%d] bulge away from hub (soft Dot<=0)"),
					ContextTag,
					CornerIndex);
			}
		}

		if (CornerIndex + 1 < CornerInfo.Num())
		{
			const FCornerInfo& Next = CornerInfo[CornerIndex + 1];
			if (Info.CornerPoints.Num() > 0 && Next.CornerPoints.Num() > 0)
			{
				const double SeamDist = FVector::Dist(Info.CornerPoints.Last(), Next.CornerPoints[0]);
				if (SeamDist > 5.0)
				{
					UE_LOG(
						LogTemp,
						Error,
						TEXT("[%s] SEAM BREAK CornerInfo[%d].Last -> [%d].First dist=%.2f (point indices not welded)"),
						ContextTag,
						CornerIndex,
						CornerIndex + 1,
						SeamDist);
					bOk = false;
				}
			}
		}
		GlobalIndex += Info.CornerPoints.Num();
	}

	if (CornerInfo.Num() >= 2
		&& CornerInfo[0].CornerPoints.Num() > 0
		&& CornerInfo.Last().CornerPoints.Num() > 0)
	{
		const double WrapSeam = FVector::Dist(CornerInfo.Last().CornerPoints.Last(), CornerInfo[0].CornerPoints[0]);
		if (WrapSeam > 5.0)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[%s] SEAM BREAK wrap Last.Last -> [0].First dist=%.2f"),
				ContextTag,
				WrapSeam);
			bOk = false;
		}
	}

	for (int32 i = 0; i < EdgePath.Num(); ++i)
	{
		const FVector& P = EdgePath[i];
		UE_LOG(LogTemp, Verbose, TEXT("[%s] EdgePath[%d]=(%.1f,%.1f,%.1f)"), ContextTag, i, P.X, P.Y, P.Z);
		if (i > 0 && FVector::DistSquared(EdgePath[i - 1], P) < 0.01)
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] Duplicate consecutive EdgePath[%d]/[%d]"), ContextTag, i - 1, i);
		}
	}

	double SignedArea = 0.0;
	for (int32 i = 0; i < EdgePath.Num(); ++i)
	{
		const FVector& A = EdgePath[i];
		const FVector& B = EdgePath[(i + 1) % EdgePath.Num()];
		SignedArea += static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
	}
	if (SignedArea <= 0.0)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] EdgePath winding not CCW (signedArea=%.3f)"), ContextTag, SignedArea * 0.5);
		bOk = false;
	}

	const double CloseGap = FVector::Dist(EdgePath[0], EdgePath.Last());
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[%s] EdgePathNum=%d CornerInfoNum=%d closeGap=%.2f signedArea=%.1f ok=%d"),
		ContextTag,
		EdgePath.Num(),
		CornerInfo.Num(),
		CloseGap,
		SignedArea * 0.5,
		bOk ? 1 : 0);

	return bOk;
}

bool AMoKuEditIntersectionActor::BuildJunctionBoundaryRing()
{
	// Reference silhouette (Houdini→UE style):
	//   polar-sort cut caps → CCW walk each Left→Right around Hub
	//   → N inward corner arcs (toward Hub) → EdgePath = Side→Arc→Side→Arc…
	TArray<FCornerInfo> SideEdges;
	SideEdges.Reserve(CornerInfo.Num());
	for (const FCornerInfo& Info : CornerInfo)
	{
		if (Info.Tag == ECornerTag::Corner || Info.CornerPoints.Num() < 2)
		{
			continue;
		}
		FCornerInfo Side = Info;
		Side.Tag = ECornerTag::Side;
		// Keep only outer endpoints for the orange-outline silhouette.
		if (Side.CornerPoints.Num() > 2)
		{
			const FVector3d Left = Side.CornerPoints[0];
			const FVector3d Right = Side.CornerPoints.Last();
			Side.CornerPoints.Reset();
			Side.CornerPoints.Add(Left);
			Side.CornerPoints.Add(Right);
		}
		SideEdges.Add(Side);
	}

	if (SideEdges.Num() < 2)
	{
		return false;
	}

	FVector Anchor = GetActorLocation();
	if (JunctionData && !JunctionData->JunctionNode.AnchorWorld.IsNearlyZero())
	{
		Anchor = JunctionData->JunctionNode.AnchorWorld;
	}

	FVector Hub = FVector::ZeroVector;
	for (const FCornerInfo& Side : SideEdges)
	{
		Hub += (Side.CornerPoints[0] + Side.CornerPoints.Last()) * 0.5;
	}
	Hub /= static_cast<double>(SideEdges.Num());
	if (!Anchor.IsNearlyZero())
	{
		// Blend anchor if present — keeps hub near true intersect.
		Hub = (Hub + Anchor) * 0.5;
	}

	SideEdges.Sort([&Hub](const FCornerInfo& A, const FCornerInfo& B)
	{
		const FVector MidA = (A.CornerPoints[0] + A.CornerPoints.Last()) * 0.5;
		const FVector MidB = (B.CornerPoints[0] + B.CornerPoints.Last()) * 0.5;
		return FMath::Atan2(MidA.Y - Hub.Y, MidA.X - Hub.X)
			< FMath::Atan2(MidB.Y - Hub.Y, MidB.X - Hub.X);
	});

	auto HubCrossZ = [&Hub](const FVector& P0, const FVector& P1) -> double
	{
		return (P0.X - Hub.X) * (P1.Y - Hub.Y) - (P0.Y - Hub.Y) * (P1.X - Hub.X);
	};

	auto AssembleRing = [&](bool bReversePolar) -> double
	{
		TArray<FCornerInfo> Ordered = SideEdges;
		if (bReversePolar)
		{
			Algo::Reverse(Ordered);
		}

		// Each cut: walk so Hub is on the left (CCW around Hub).
		for (FCornerInfo& Side : Ordered)
		{
			if (HubCrossZ(Side.CornerPoints[0], Side.CornerPoints.Last()) < 0.0)
			{
				Algo::Reverse(Side.CornerPoints);
			}
		}

		TArray<FCornerInfo> Arcs;
		Arcs.Reserve(Ordered.Num());
		for (int32 i = 0; i < Ordered.Num(); ++i)
		{
			const FVector ArcStart = Ordered[i].CornerPoints.Last();
			const FVector ArcEnd = Ordered[(i + 1) % Ordered.Num()].CornerPoints[0];
			const double HalfW = GetCornerArcDepthForPoints(ArcStart, ArcEnd);
			const FVector StartRel = ArcStart - Hub;
			const FVector EndRel = ArcEnd - Hub;
			const double PortSpanRad = WrapPositiveAngleRad(
				FMath::Atan2(EndRel.Y, EndRel.X) - FMath::Atan2(StartRel.Y, StartRel.X));
			const FAestheticCornerFillet Aesthetic = ComputeAestheticCornerFillet(
				ArcStart,
				ArcEnd,
				Hub,
				HalfW,
				PortSpanRad,
				HalfW * 0.16,
				/*bShallowMultiWay*/ true);

			FCornerInfo Arc;
			Arc.Tag = ECornerTag::Corner;
			BuildOutwardCircularArc(
				ArcStart,
				ArcEnd,
				Hub,
				Aesthetic.RadiusCm,
				Aesthetic.NumSegments,
				Arc.CornerPoints,
				Aesthetic.SagittaCm);
			if (Arc.CornerPoints.Num() < 2)
			{
				Arc.CornerPoints = { ArcStart, ArcEnd };
			}
			// Weld exact endpoints to cut corners (vertex indices continuous).
			Arc.CornerPoints[0] = ArcStart;
			Arc.CornerPoints.Last() = ArcEnd;
			Arcs.Add(Arc);
		}

		CornerInfo.Reset();
		EdgePath.Reset();
		auto AppendPts = [this](const TArray<FVector3d>& Pts, bool bSkipFirst)
		{
			for (int32 Pi = bSkipFirst ? 1 : 0; Pi < Pts.Num(); ++Pi)
			{
				if (EdgePath.Num() > 0 && FVector::DistSquared(EdgePath.Last(), Pts[Pi]) < 1.0)
				{
					continue;
				}
				EdgePath.Add(Pts[Pi]);
			}
		};

		for (int32 i = 0; i < Ordered.Num(); ++i)
		{
			CornerInfo.Add(Ordered[i]);
			CornerInfo.Add(Arcs[i]);
			AppendPts(Ordered[i].CornerPoints, false);
			AppendPts(Arcs[i].CornerPoints, true);
		}
		if (EdgePath.Num() > 2 && FVector::DistSquared(EdgePath[0], EdgePath.Last()) < 25.0)
		{
			EdgePath.Pop();
		}

		double Area = 0.0;
		for (int32 i = 0; i < EdgePath.Num(); ++i)
		{
			const FVector& A = EdgePath[i];
			const FVector& B = EdgePath[(i + 1) % EdgePath.Num()];
			Area += static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
		}
		return Area;
	};

	UE_LOG(
		LogTemp,
		Log,
		TEXT("BuildJunctionBoundaryRing [RefSilhouette]: sides=%d Hub=(%.1f,%.1f)"),
		SideEdges.Num(),
		Hub.X,
		Hub.Y);

	double SignedArea = AssembleRing(false);
	if (SignedArea < 0.0)
	{
		SignedArea = AssembleRing(true);
		UE_LOG(LogTemp, Log, TEXT("BuildJunctionBoundaryRing: reversed polar for CCW area=%.1f"), SignedArea * 0.5);
	}

	// Log ring vertex write order for audit (Side L/R then Arc samples).
	for (int32 Ci = 0; Ci < CornerInfo.Num(); ++Ci)
	{
		const FCornerInfo& Info = CornerInfo[Ci];
		const TCHAR* Tag = Info.Tag == ECornerTag::Corner ? TEXT("Arc") : TEXT("Cut");
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RefRing] [%d].%s pts=%d first=(%.1f,%.1f) last=(%.1f,%.1f)"),
			Ci,
			Tag,
			Info.CornerPoints.Num(),
			Info.CornerPoints[0].X,
			Info.CornerPoints[0].Y,
			Info.CornerPoints.Last().X,
			Info.CornerPoints.Last().Y);
	}

	return EdgePath.Num() >= 3 && SignedArea > 0.0;
}

void AMoKuEditIntersectionActor::SortEdgePath()
{
	if (EdgePath.Num() < 3)
	{
		if (!BuildJunctionBoundaryRing())
		{
			return;
		}
	}
	TriangulateJunctionCoverMesh();
}

void AMoKuEditIntersectionActor::TriangulateJunctionCoverMesh()
{
	if (!DynamicMeshComponent || EdgePath.Num() < 3)
	{
		return;
	}

	if (!ValidateJunctionMeshPointIndices(TEXT("TriangulateJunctionCoverMesh")))
	{
		UE_LOG(LogTemp, Error, TEXT("TriangulateJunctionCoverMesh: abort — point-index audit failed"));
		return;
	}

	TArray<int32> VertexIDs;

	TUniquePtr<FDynamicMesh3> Mesh = MakeUnique<FDynamicMesh3>();
	// Topology first (no attributes). ReverseOrientation before UV/normal overlays.

	const float MeshHeightOffset = 10.0f;
	const FTransform ActorTransform = GetActorTransform();
	VertexIDs.SetNumUninitialized(EdgePath.Num());
	TArray<FVector2f> BoundaryUVs;
	BoundaryUVs.SetNum(EdgePath.Num());

	// Weld UV: Side points use SampleSpline@CutS; Corner/arc lerp from adjacent Side endpoints.
	TArray<FVector2f> CornerEndpointUV;
	CornerEndpointUV.SetNum(CornerInfo.Num());
	for (int32 CornerIndex = 0; CornerIndex < CornerInfo.Num(); ++CornerIndex)
	{
		const FCornerInfo& Info = CornerInfo[CornerIndex];
		if (Info.Tag == ECornerTag::Side && Info.SampleSpline.IsValid() && Info.SampleDistanceS >= 0.0
			&& Info.CornerPoints.Num() > 0)
		{
			CornerEndpointUV[CornerIndex] = AMoKuEditSplineActor::MakeAsphaltUVAtDistance(
				Info.SampleSpline.Get(),
				Info.SampleDistanceS,
				Info.CornerPoints[0]);
		}
		else if (Info.CornerPoints.Num() > 0)
		{
			CornerEndpointUV[CornerIndex] = ComputeJunctionVertexUV(Info.CornerPoints[0]);
		}
	}

	auto UVForEdgePathPoint = [&](int32 /*EdgeIndex*/, const FVector& WorldPos) -> FVector2f
	{
		// Map EdgePath point back to owning CornerInfo segment for UV.
		for (int32 CornerIndex = 0; CornerIndex < CornerInfo.Num(); ++CornerIndex)
		{
			const FCornerInfo& Info = CornerInfo[CornerIndex];
			const int32 Count = Info.CornerPoints.Num();
			if (Count == 0)
			{
				continue;
			}
			for (int32 Local = 0; Local < Count; ++Local)
			{
				if (FVector::DistSquared(Info.CornerPoints[Local], WorldPos) < 4.0)
				{
					if (Info.Tag == ECornerTag::Side && Info.SampleSpline.IsValid() && Info.SampleDistanceS >= 0.0)
					{
						return AMoKuEditSplineActor::MakeAsphaltUVAtDistance(
							Info.SampleSpline.Get(),
							Info.SampleDistanceS,
							WorldPos);
					}
					if (Info.Tag == ECornerTag::Corner && Count > 1)
					{
						const int32 PrevSide = (CornerIndex - 1 + CornerInfo.Num()) % CornerInfo.Num();
						const int32 NextSide = (CornerIndex + 1) % CornerInfo.Num();
						FVector2f UV0 = CornerEndpointUV[PrevSide];
						FVector2f UV1 = CornerEndpointUV[NextSide];
						if (CornerInfo[PrevSide].Tag == ECornerTag::Side
							&& CornerInfo[PrevSide].SampleSpline.IsValid()
							&& CornerInfo[PrevSide].CornerPoints.Num() > 0)
						{
							UV0 = AMoKuEditSplineActor::MakeAsphaltUVAtDistance(
								CornerInfo[PrevSide].SampleSpline.Get(),
								CornerInfo[PrevSide].SampleDistanceS,
								CornerInfo[PrevSide].CornerPoints.Last());
						}
						if (CornerInfo[NextSide].Tag == ECornerTag::Side
							&& CornerInfo[NextSide].SampleSpline.IsValid()
							&& CornerInfo[NextSide].CornerPoints.Num() > 0)
						{
							UV1 = AMoKuEditSplineActor::MakeAsphaltUVAtDistance(
								CornerInfo[NextSide].SampleSpline.Get(),
								CornerInfo[NextSide].SampleDistanceS,
								CornerInfo[NextSide].CornerPoints[0]);
						}
						const float T = static_cast<float>(Local) / static_cast<float>(Count - 1);
						return FMath::Lerp(UV0, UV1, T);
					}
					return ComputeJunctionVertexUV(WorldPos);
				}
			}
		}
		return ComputeJunctionVertexUV(WorldPos);
	};

	for (int32 i = 0; i < EdgePath.Num(); i++)
	{
		FVector WorldPos = EdgePath[i];
		WorldPos.Z += MeshHeightOffset;
		const FVector LocalPos = ActorTransform.InverseTransformPosition(WorldPos);
		VertexIDs[i] = Mesh->AppendVertex(FVector3d(LocalPos));
		BoundaryUVs[i] = UVForEdgePathPoint(i, EdgePath[i]);
	}

	// Ear-clip the CCW boundary — NO hub fan (slivers + broken UV on terrain).
	TArray<FVector3d> PosList;
	TArray<int32> RingVids = VertexIDs;
	PosList.Reserve(EdgePath.Num());
	for (int32 i = 0; i < EdgePath.Num(); ++i)
	{
		PosList.Add(Mesh->GetVertex(VertexIDs[i]));
	}

	TArray<FIndex3i> Triangles;
	int32 Guard = 0;
	while (PosList.Num() > 3 && Guard++ < 5000)
	{
		int32 EarIndex = -1;
		double BestRadius = -1.0;
		for (int32 i = 0; i < PosList.Num(); ++i)
		{
			const int32 PrevIndex = (i - 1 + PosList.Num()) % PosList.Num();
			const int32 NextIndex = (i + 1) % PosList.Num();
			const FVector3d& PrevPos = PosList[PrevIndex];
			const FVector3d& CurrPos = PosList[i];
			const FVector3d& NextPos = PosList[NextIndex];

			// CCW ring: convex ear turns left (CrossZ > 0).
			const double TurnZ = Cross2d(CurrPos - PrevPos, NextPos - CurrPos);
			if (TurnZ <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double Radius = GetCircumRadiusValue(PrevPos, CurrPos, NextPos);
			if (Radius < 0.0)
			{
				continue;
			}

			bool bPointInside = false;
			for (int32 j = 0; j < PosList.Num(); ++j)
			{
				if (j == PrevIndex || j == i || j == NextIndex)
				{
					continue;
				}
				if (PointInTriangle(PosList[j], PrevPos, CurrPos, NextPos))
				{
					bPointInside = true;
					break;
				}
			}
			if (bPointInside)
			{
				continue;
			}
			if (Radius > BestRadius)
			{
				BestRadius = Radius;
				EarIndex = i;
			}
		}

		if (EarIndex < 0)
		{
			break;
		}

		const int32 Prev = (EarIndex - 1 + PosList.Num()) % PosList.Num();
		const int32 Next = (EarIndex + 1) % PosList.Num();
		Triangles.Add(FIndex3i(RingVids[Prev], RingVids[EarIndex], RingVids[Next]));
		PosList.RemoveAt(EarIndex);
		RingVids.RemoveAt(EarIndex);
	}

	if (PosList.Num() == 3 && RingVids.Num() == 3)
	{
		Triangles.Add(FIndex3i(RingVids[0], RingVids[1], RingVids[2]));
	}

	for (const FIndex3i& Tri : Triangles)
	{
		Mesh->AppendTriangle(Tri);
	}

	EnsureJunctionCoverFacesWorldUp(*Mesh);

	Mesh->EnableAttributes();
	Mesh->EnableVertexUVs(FVector2f::Zero());
	for (int32 i = 0; i < EdgePath.Num(); ++i)
	{
		Mesh->SetVertexUV(VertexIDs[i], BoundaryUVs[i]);
	}

	AMoKuEditSplineActor::FinalizeRoadMeshUVs(*Mesh);
	AMoKuEditSplineActor::FinalizeRoadMeshNormalsAndOverlays(*Mesh);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("TriangulateJunctionCoverMesh [EarClip]: boundary=%d tris=%d (no hub fan)"),
		EdgePath.Num(),
		Mesh->TriangleCount());

	if (Mesh->TriangleCount() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TriangulationFailed: EdgePath=%d"), EdgePath.Num());
		return;
	}

	if (UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh())
	{
		EnsureJunctionDynamicMeshRegistered(DynamicMeshComponent, this);
		DynamicMesh->Reset();
		DynamicMesh->SetMesh(MoveTemp(*Mesh));
		DynamicMeshComponent->SetRelativeLocation(FVector::ZeroVector);
		DynamicMeshComponent->SetVisibility(true);
		DynamicMeshComponent->SetHiddenInGame(false);
		DynamicMeshComponent->SetCastShadow(false);
		DynamicMeshComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
		DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		if (UBodySetup* BodySetup = DynamicMeshComponent->GetBodySetup())
		{
			BodySetup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
		}
		DynamicMeshComponent->bEnableComplexCollision = true;
		DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
		if (DynamicMeshComponent->IsRegistered())
		{
			DynamicMeshComponent->UpdateCollision(false);
		}
		DynamicMeshComponent->NotifyMeshUpdated();
		if (UMaterialInterface* JunctionMat = ResolveJunctionMaterial())
		{
			DynamicMeshComponent->SetMaterial(0, JunctionMat);
		}
	}
}
bool AMoKuEditIntersectionActor::FindClosedLotsByConnector(TArray<TArray<FVector>>&OutConnectionPoints)
{
	for (const FCornerInfo& item : CornerInfo)
	{	
		TArray<FVector> Connections;
		//if (item.Tag == ECornerTag::Corner)
		{
			FVector StartConnection = item.CornerPoints[0];
			FVector EndConnection = item.CornerPoints.Last();
			          
			for (const AMoKuEditSplineActor* ConnectActor : ConnectionActors)
			{
				if (!ConnectActor->StartOfJunction.Get() || !ConnectActor->EndOfJunction.Get())continue;
				TMap<FString, TArray<FVector>> ConnectionList;
				if (ConnectActor->LeftCurve)
				{
					FVector StartLocation = ConnectActor->LeftCurve->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
					int Count = ConnectActor->LeftCurve->GetNumberOfSplinePoints();
					FVector EndLocation = ConnectActor->LeftCurve->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World);
					TArray<FVector> Collection({ StartLocation, EndLocation });
					ConnectionList.Add({ "0",Collection });
				}
				if (ConnectActor->RightCurve)
				{
					FVector StartLocation = ConnectActor->RightCurve->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
					int Count = ConnectActor->RightCurve->GetNumberOfSplinePoints();
					FVector EndLocation = ConnectActor->RightCurve->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World);
					TArray<FVector> Collection({ StartLocation, EndLocation });
					ConnectionList.Add({ "1",Collection });
				}
				TArray<FVector> CurvePoints;
				for (const auto& Pair : ConnectionList)
				{
					if (Pair.Key == "0")
					{
						for (auto& Ref : Pair.Value)
						{
							float DistA = FVector::Dist(Ref, StartConnection);
							//float DistB = FVector::Dist(Ref, EndConnection);
							if (DistA < 50.0f)
							{
								ConnectActor->LeftCurve->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 1.0f, CurvePoints);
								if (CurvePoints.Num() > 0)
								{
									for (const FVector& CurvePoint : item.CornerPoints)
									{
										CurvePoints.Add(CurvePoint);
									}
									OutConnectionPoints.Add(CurvePoints);
								}
							}
						}
					}

					if (Pair.Key == "1")
					{
						for (auto& Ref : Pair.Value)
						{
							float DistA = FVector::Dist(Ref, StartConnection);
							float DistB = FVector::Dist(Ref, EndConnection);
							if (DistA < 50.0f || DistB < 50.0f)
							{
								ConnectActor->RightCurve->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 1.0f, CurvePoints);
								if (CurvePoints.Num() > 0)
								{
									for (const FVector& CurvePoint : item.CornerPoints)
									{
										CurvePoints.Add(CurvePoint);
									}
									OutConnectionPoints.Add(CurvePoints);
								}
							}
						}
					}
					OutConnectionPoints.Add(Connections);
				}
			}
		}
	}

	if (OutConnectionPoints.Num() > 0)return true;

	return false;


}
void AMoKuEditIntersectionActor::MeshBuilder()
{
	FTriangulateCurvesOp TriangulateCurvesOp;
	TriangulateCurvesOp.Thickness = 0.0f;
	TriangulateCurvesOp.FlattenMethod = EFlattenCurveMethod::DoNotFlatten;
	TriangulateCurvesOp.CurveOffset = 0.0f;
	TriangulateCurvesOp.OffsetOpenMethod = EOffsetOpenCurvesMethod::TreatAsClosed;
	TriangulateCurvesOp.OffsetJoinMethod = EOffsetJoinMethod::Miter;
	TriangulateCurvesOp.OpenEndShape = EOpenCurveEndShapes::Square;
	TriangulateCurvesOp.MiterLimit =1.0;
	TriangulateCurvesOp.bFlipResult = false;
	TriangulateCurvesOp.OffsetClosedMethod = EOffsetClosedCurvesMethod::DoNotOffset;
	FTransform TransformInfo  = this->GetActorTransform();

	TriangulateCurvesOp.AddWorldCurve(EdgePath,true,TransformInfo);
	TriangulateCurvesOp.CalculateResult(nullptr);
	TUniquePtr<FDynamicMesh3>ResultMesh = TriangulateCurvesOp.ExtractResult();
	if (!ResultMesh || ResultMesh->TriangleCount() == 0)
	{
		UE_LOG(LogTemp,Warning,TEXT("TriangulationFailed"));
	}
	FDynamicMesh3* DynamicMeshPtr = ResultMesh.Release();
	UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh();


	int32 TriangleCount = DynamicMeshPtr->GetTrianglesRefCounts().GetCount();
	//int32 TriangleCount = UGeometryScriptLibrary_MeshQueryFunctions::GetNumTriangleIDs(DynamicMesh);

	for (int32 TriangleID= 0; TriangleID <TriangleCount; TriangleID++)
	{
		if (!DynamicMeshPtr->IsTriangle(TriangleID))continue;
		FVector3d Normal = DynamicMeshPtr->GetTriNormal(TriangleID);
		if (Normal.Z < 0)
		{
			FIndex3i Verts = DynamicMeshPtr->GetTriangle(TriangleID);
			DynamicMeshPtr->SetTriangle(TriangleID, FIndex3i(Verts.A, Verts.C, Verts.B));
		}
	}
	DynamicMesh->SetMesh(MoveTemp(*DynamicMeshPtr));
	DynamicMeshComponent->SetWorldTransform(TransformInfo);
	DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DynamicMeshComponent->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	DynamicMeshComponent->bEnableComplexCollision = true;
	DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
	DynamicMeshComponent->UpdateCollision(false);
	DynamicMeshComponent->NotifyMeshUpdated();
	if (UMaterialInterface* AsphaltMat = AMoKuEditSplineActor::EnsureAsphaltTestMaterial())
	{
		DynamicMeshComponent->SetMaterial(0, AsphaltMat);
	}
}


//void AMoKuEditIntersectionActor::SPawnGizmoInfo(const FVector& InRefVector)
//{
//	if (StaticMeshAsset)
//	{
//		GizmoTransformInfos.Empty();
//		TArray<FComponentSocketDescription> SocketDescriptions;
//		StaticMeshAsset->QuerySupportedSockets(SocketDescriptions);
//		FSocketGizmoTransformInfo GizmoTransformInfo;
//
//		for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
//		{
//			FName SocketName = SocketDesc.Name;
//			FTransform SocketTransform = StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
//			GizmoTransformInfo.Transform = SocketTransform;
//			FTransform LocalTransform = StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_Component);
//			GizmoTransformInfo.Tangent = LocalTransform.TransformVector(InRefVector);
//			GizmoTransformInfos.Add(GizmoTransformInfo);
//		}
//	}
//}


TArray<FSocketGizmoTransformInfo> AMoKuEditIntersectionActor::GetGizmoInfo()
{
	/*GizmoTransformInfos.Empty();
	if (StaticMeshAsset)
	{
		TArray<FComponentSocketDescription> SocketDescriptions;
		StaticMeshAsset->QuerySupportedSockets(SocketDescriptions);
		FSocketGizmoTransformInfo GizmoTransformInfo;
		for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
		{
			FName SocketName = SocketDesc.Name;
			FVector Tangent(1, 0, 0);
			FTransform SocketTransform = StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
			FVector SocketPosition = SocketTransform.GetLocation();
			FVector Direction = SocketTransform.TransformVector(Tangent);
			GizmoTransformInfo.Tangent = Direction;
			GizmoTransformInfo.Transform = SocketTransform;
			GizmoTransformInfos.Add(GizmoTransformInfo);
		}
		
	}*/
	return GizmoTransformInfos;
}
FEditSplineIntersectInfo AMoKuEditIntersectionActor::CheckIntersectionState(AMoKuEditBaseActor* InEditActor)
{
	FEditSplineIntersectInfo Result;
	if (!InEditActor)
	{
		return FEditSplineIntersectInfo();
	}
	if (AMoKuEditSplineActor* EditActor = Cast<AMoKuEditSplineActor>(InEditActor))
	{
		TArray<FVector> LeftIntersectionPoints;
		TArray<FVector> RightIntersectionPoints;
		int LeftCount = 0;
		int RightCount = 0;
		for (auto& Info : CornerInfo)
		{
			TObjectPtr<USplineComponent>NewSpline = NewObject<USplineComponent>();
			NewSpline->SetSplinePoints(Info.CornerPoints, ESplineCoordinateSpace::World);
			FVector LeftIntersectedPoint;
			bool IntersectedLeft = MoKuSplineEditorUtils::DoSplineIntersect(NewSpline, EditActor->LeftCurve, LeftIntersectedPoint);
			FVector RightIntersectedPoint;
			bool IntersectedRight = MoKuSplineEditorUtils::DoSplineIntersect(NewSpline, EditActor->RightCurve, RightIntersectedPoint);

			if (IntersectedLeft)
			{
				LeftIntersectionPoints.Add(LeftIntersectedPoint);
				LeftCount++;
			}
			if (IntersectedRight)
			{
				LeftIntersectionPoints.Add(RightIntersectedPoint);
				RightCount++;
			}
		}

		if (RightCount > 0 && LeftCount > 0)Result.State = EIntersectionState::Both;
		if (RightCount + LeftCount == 0)Result.State = EIntersectionState::None;
		Result.LeftIntersectPositions = LeftIntersectionPoints;
		Result.RightIntersectPositions = RightIntersectionPoints;
		return Result;
	}
	return Result;
}

void AMoKuEditIntersectionActor::UpdateJunctionInfo(class FCurvesRoadOp* InRoadOp)
{
	//InRoadOp
	//for (const auto& ConnectActor : ConnectionActors)
	//{



	//}

}

double AMoKuEditIntersectionActor::Cross2d(const FVector3d& A, const FVector3d& B)
{

	return A.X * B.Y - A.Y * B.X;
}

bool AMoKuEditIntersectionActor::PointInTriangle(const FVector& P,const FVector& A, const FVector& B, const FVector& C)
{

	double Threshold = 0.00001f;
	double R1 = Cross2d(B - A, P - A);
	double R2 = Cross2d(C - B, P - B);
	double R3 = Cross2d(A - C, P - C);

	return ((R1 >= -Threshold && R2 >= -Threshold && R3 >= -Threshold) || (R1 <= -Threshold && R2 <= -Threshold && R3 <= -Threshold));
}

double AMoKuEditIntersectionActor::GetCircumRadiusValue(const FVector3d& P1, const FVector3d& P2, const FVector3d& P3)
{
	double A = Distance(P1, P2);
	double B=  Distance(P3, P2);
	double C = Distance(P1, P3);

	double AreaValue = FMath::Abs(Cross2d(P2 - P1, P3 - P1));
	if (AreaValue < 0.000001f) return-1;
	double Radius = (A * B * C) / (2 * AreaValue + 0.000001f);

	return 1.0 / (Radius + 0.000001f);


}

bool AMoKuEditIntersectionActor::IsConvex(const FVector3d& A, const FVector3d& B, const FVector3d& C)
{
	FVector A1 = Normalized(B - A);
	FVector A2 = Normalized(C - A);
	FVector Value = Cross(A1, A2);
	return Dot(Value, FVector3d(0, 0, 1)) > 0;
}




#undef LOCTEXT_NAMESPACE