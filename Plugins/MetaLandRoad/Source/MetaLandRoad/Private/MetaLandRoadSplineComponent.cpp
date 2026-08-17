#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadLaneEditPick.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadModule.h"
#include "MoKuEditSplineActor.h"
#include "Components/LineBatchComponent.h"
#include "SceneManagement.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

static float SumFaceWidths(const TArray<float>& Widths)
{
	float Sum = 0.0f;
	for (const float W : Widths)
	{
		Sum += W;
	}
	return Sum;
}

static void CollectUniqueFaceWidths(
	const UMetaLandRoadSplineComponent* Spline,
	TArray<float>& OutLeftWidths,
	TArray<float>& OutRightWidths)
{
	OutLeftWidths.Reset();
	OutRightWidths.Reset();
	if (!Spline)
	{
		return;
	}

	TSet<int32> Seen;
	for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
		for (int32 i = 0; i < Section.Left.Num(); ++i)
		{
			const int32 LaneIndex = Section.Left[i].GetLaneIndex();
			const int32 Key = (LaneIndex != 0) ? LaneIndex : -(1000 * (SectionIndex + 1) + i + 1);
			if (Seen.Contains(Key))
			{
				continue;
			}
			Seen.Add(Key);
			OutLeftWidths.Add(FMath::Max(Section.Left[i].Width.Eval(0.f), 1.0f));
		}
		for (int32 i = 0; i < Section.Right.Num(); ++i)
		{
			const int32 LaneIndex = Section.Right[i].GetLaneIndex();
			const int32 Key = (LaneIndex != 0) ? LaneIndex : (1000 * (SectionIndex + 1) + i + 1);
			if (Seen.Contains(Key))
			{
				continue;
			}
			Seen.Add(Key);
			OutRightWidths.Add(FMath::Max(Section.Right[i].Width.Eval(0.f), 1.0f));
		}
	}
}

static FMetaLandRoadLane MakeBlendWidthLane(float StartW, float EndW, float LengthCm)
{
	FMetaLandRoadLane Lane;
	FMetaLandRoadLaneDriving DrivingLane;
	DrivingLane.DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;
	Lane.LaneInstance.InitializeAs<FMetaLandRoadLaneDriving>(DrivingLane);
	Lane.Width.Reset();
	const FKeyHandle H0 = Lane.Width.AddKey(0.0f, FMath::Max(StartW, 8.0f));
	Lane.Width.SetKeyInterpMode(H0, ERichCurveInterpMode::RCIM_Linear);
	const FKeyHandle H1 = Lane.Width.AddKey(FMath::Max(LengthCm, 1.0f), FMath::Max(EndW, 8.0f));
	Lane.Width.SetKeyInterpMode(H1, ERichCurveInterpMode::RCIM_Linear);
	return Lane;
}

// =============================================================================
// [KEY-ALGO] ApplyMinLaneLeftRightWidthBlend
// 2合1 / N合1：按最小车道数=1 写成 Left[0]+Right[0]，只混 width。
// ROffset≡0，粉中线 = mesh 几何中心，头尾不再被 EvalLaneROffset 推偏。
// =============================================================================
static void ApplyMinLaneLeftRightWidthBlend(
	UMetaLandRoadSplineComponent* Spline,
	float StartLeftW,
	float StartRightW,
	float EndLeftW,
	float EndRightW)
{
	if (!Spline)
	{
		return;
	}

	const float Len = static_cast<float>(FMath::Max(Spline->GetSplineLength(), 1.0));
	TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSectionsMutable();
	if (Sections.Num() == 0)
	{
		Sections.AddDefaulted();
	}

	FMetaLandRoadLaneSection& Host = Sections[0];
	Host.Side = EMetaLandRoadLaneSectionSide::Both;
	Host.SOffset = 0.0;
	Host.Left.Reset();
	Host.Right.Reset();
	Host.Left.Add(MakeBlendWidthLane(StartLeftW, EndLeftW, Len));
	Host.Right.Add(MakeBlendWidthLane(StartRightW, EndRightW, Len));
	if (Sections.Num() > 1)
	{
		Sections.SetNum(1);
	}

	FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
	Layout.ROffset.Reset();
	const FKeyHandle R0 = Layout.ROffset.AddKey(0.0f, 0.0f);
	Layout.ROffset.SetKeyInterpMode(R0, ERichCurveInterpMode::RCIM_Linear);
	const FKeyHandle R1 = Layout.ROffset.AddKey(Len, 0.0f);
	Layout.ROffset.SetKeyInterpMode(R1, ERichCurveInterpMode::RCIM_Linear);
	Spline->UpdateLaneSectionBounds();
	Spline->UpdateRoadLayout();
}

#if WITH_EDITOR
static void RedrawNativeEditViewport(UMetaLandRoadSplineComponent* Spline)
{
	if (Spline && GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}
#endif

using namespace UE::Geometry;

namespace MetaLandRoadLaneMeshPrivate
{
	constexpr int32 NumPointPerSegment = MetaLandRoad::DefaultReparamStepsPerSegment;
	constexpr int32 MinNumStepsPerSection = MetaLandRoad::DefaultMinNumStepsPerSection;

	static int32 ComputeScaledMinStepsForSpan(
		double S0,
		double S1,
		double TotalSplineLength,
		int32 GlobalMinSteps = MinNumStepsPerSection)
	{
		if (TotalSplineLength <= KINDA_SMALL_NUMBER)
		{
			return FMath::Max(2, GlobalMinSteps);
		}

		const double SpanLength = FMath::Max(S1 - S0, 0.0);
		const double Ratio = SpanLength / TotalSplineLength;
		return FMath::Clamp(
			FMath::RoundToInt(static_cast<float>(GlobalMinSteps * Ratio)),
			4,
			FMath::Max(GlobalMinSteps, 4));
	}

	/**
	 * Inner rail must share Outer ring S keys. Index-lerp of a separately adaptive
	 * approximation puts Inner[i] at a different S than Outer[i] → fan/sliver quads
	 * at section boundaries (orange-box dense band).
	 */
	static void BuildInnerRingMatchingOuterS(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 InnerLaneIndex,
		double InnerAlpha,
		double S0,
		double S1,
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& OuterPoints,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& OutInner)
	{
		OutInner.Reset();
		if (!Component || OuterPoints.Num() < 2)
		{
			return;
		}

		OutInner.SetNum(OuterPoints.Num());
		for (int32 i = 0; i < OuterPoints.Num(); ++i)
		{
			double S = Component->GetDistanceAlongSplineAtSplineInputKey(OuterPoints[i].SplineParam);
			if (i == 0)
			{
				S = S0;
			}
			else if (i + 1 == OuterPoints.Num())
			{
				S = S1;
			}
			OutInner[i].Position = Component->EvalLanePosition(
				SectionIndex,
				InnerLaneIndex,
				S,
				InnerAlpha,
				ESplineCoordinateSpace::Local);
			OutInner[i].SplineParam = Component->GetInputKeyValueAtDistanceAlongSpline(S);
		}
	}

	void SnapLaneApproximationEndpoint(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 LaneIndex,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& Points,
		double S,
		bool bStart)
	{
		if (!Component || Points.Num() == 0)
		{
			return;
		}
		const int32 Idx = bStart ? 0 : Points.Num() - 1;
		Points[Idx].Position = Component->EvalLanePosition(
			SectionIndex, LaneIndex, S, 1.0, ESplineCoordinateSpace::Local);
		Points[Idx].SplineParam = Component->GetInputKeyValueAtDistanceAlongSpline(S);
	}

	bool BuildLaneRoadMesh(
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& InnerSide,
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& OuterSide,
		const UMetaLandRoadSplineComponent* Component,
		int32 LaneIndex,
		const FMetaLandRoadLaneSection& LaneSection,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		if (InnerSide.Num() != OuterSide.Num() || InnerSide.Num() < 2)
		{
			return false;
		}

		const int32 N = InnerSide.Num();
		const int32 BaseVertexID = OutMesh.VertexCount();
		const FVector HeightOffset(0.0, 0.0, RoadHeightOffset);
		const float UvTileCm = FMath::Max(
			Component ? Component->AsphaltUvTileCm : MetaLandRoad::DefaultAsphaltUvTileCm,
			50.0f);

		// Asphalt UVs: U = across, V = along spline (world cm / tile). Larger tile = fewer side-edge stripes.
		auto MakeAsphaltUV = [Component, UvTileCm](const FVector& LocalPos, float SplineParam) -> FVector2f
		{
			const double S = Component->GetDistanceAlongSplineAtSplineInputKey(SplineParam);
			const FVector Center = Component->GetLocationAtDistanceAlongSpline(
				S, ESplineCoordinateSpace::Local);
			const FVector Right = Component->GetRightVectorAtDistanceAlongSpline(
				S, ESplineCoordinateSpace::Local);
			const float LateralCm = static_cast<float>(FVector::DotProduct(LocalPos - Center, Right));
			return FVector2f(LateralCm / UvTileCm, static_cast<float>(S) / UvTileCm);
		};

		// Inner strip first, outer strip second (MetaRoad BuildRoadMesh order).
		for (int32 i = 0; i < N; ++i)
		{
			FVertexInfo InnerVertex;
			InnerVertex.Position = InnerSide[i].Position + HeightOffset;
			const FVector2f InnerUV = bGenerateUVs
				? MakeAsphaltUV(InnerSide[i].Position, InnerSide[i].SplineParam)
				: FVector2f::Zero();
			if (bGenerateUVs)
			{
				InnerVertex.UV = InnerUV;
				InnerVertex.bHaveUV = true;
			}
			const int32 InnerVid = OutMesh.AppendVertex(InnerVertex);
			if (bGenerateUVs && OutMesh.HasVertexUVs())
			{
				OutMesh.SetVertexUV(InnerVid, InnerUV);
			}
		}

		for (int32 i = 0; i < N; ++i)
		{
			FVertexInfo OuterVertex;
			OuterVertex.Position = OuterSide[i].Position + HeightOffset;
			const FVector2f OuterUV = bGenerateUVs
				? MakeAsphaltUV(OuterSide[i].Position, OuterSide[i].SplineParam)
				: FVector2f::Zero();
			if (bGenerateUVs)
			{
				OuterVertex.UV = OuterUV;
				OuterVertex.bHaveUV = true;
			}
			const int32 OuterVid = OutMesh.AppendVertex(OuterVertex);
			if (bGenerateUVs && OutMesh.HasVertexUVs())
			{
				OutMesh.SetVertexUV(OuterVid, OuterUV);
			}
		}

		// DynamicMesh is single-sided. Right-hand lanes use one winding; left-hand lanes need the opposite
		// so both sides face up in the preview mesh (MetaRoad SceneProxy uses a different facing).
		for (int32 i = 0; i < N - 1; ++i)
		{
			// Drop needle/sliver quads: either ring collapsed → radiating crack tris.
			constexpr double MinWidthSq = 1.0; // 1cm
			const double InnerWidthSq = FVector::DistSquared(InnerSide[i].Position, OuterSide[i].Position);
			const double NextInnerWidthSq = FVector::DistSquared(InnerSide[i + 1].Position, OuterSide[i + 1].Position);
			if (InnerWidthSq < MinWidthSq || NextInnerWidthSq < MinWidthSq)
			{
				continue;
			}

			if (LaneIndex < 0)
			{
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i, BaseVertexID + i + 1, BaseVertexID + i + N));
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i + N, BaseVertexID + i + 1, BaseVertexID + i + 1 + N));
			}
			else
			{
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i, BaseVertexID + i + N, BaseVertexID + i + 1));
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i + N, BaseVertexID + i + 1 + N, BaseVertexID + i + 1));
			}
		}

		return true;
	}

	bool AppendLaneMeshForSection(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 OuterLaneIndex,
		int32 InnerLaneIndex,
		double InnerAlpha,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SectionIndex);
		if ((S1 - S0) < 10.0) // <10cm: MinSteps packs rings into a fan band
		{
			return false;
		}

		TArray<FMetaLandRoadSplinePositionLinearApproximation> OuterPoints;
		Component->BuildLinearApproximation(
			OuterPoints,
			[&Section, OuterLaneIndex, Component](double S)
			{
				return Section.EvalLaneROffset(OuterLaneIndex, S, 1.0) + Component->EvalROffset(S);
			},
			S0,
			S1,
			NumPointPerSegment,
			MinNumSteps,
			ESplineCoordinateSpace::Local);

		if (OuterPoints.Num() < 2)
		{
			return false;
		}

		SnapLaneApproximationEndpoint(Component, SectionIndex, OuterLaneIndex, OuterPoints, S0, true);
		SnapLaneApproximationEndpoint(Component, SectionIndex, OuterLaneIndex, OuterPoints, S1, false);

		TArray<FMetaLandRoadSplinePositionLinearApproximation> InnerPoints;
		BuildInnerRingMatchingOuterS(
			Component,
			SectionIndex,
			InnerLaneIndex,
			InnerAlpha,
			S0,
			S1,
			OuterPoints,
			InnerPoints);
		if (InnerPoints.Num() != OuterPoints.Num())
		{
			return false;
		}

		return BuildLaneRoadMesh(
			InnerPoints,
			OuterPoints,
			Component,
			OuterLaneIndex,
			Section,
			RoadHeightOffset,
			bGenerateUVs,
			OutMesh);
	}

	void AppendLanesForSide(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 PreSectionIndex,
		bool bHasSide,
		bool bUseCurrentSectionLanes,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& /*PrevPoints*/,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh,
		int32 LaneSign)
	{
		const int32 LaneSectionIndex = bUseCurrentSectionLanes ? SectionIndex : PreSectionIndex;
		if (LaneSectionIndex < 0)
		{
			return;
		}

		const FMetaLandRoadLaneSection& LaneSection = Component->GetLaneSection(LaneSectionIndex);
		const TArray<FMetaLandRoadLane>& Lanes = LaneSign > 0 ? LaneSection.Right : LaneSection.Left;
		if (!bHasSide || Lanes.IsEmpty())
		{
			return;
		}

		int32 InnerLaneIndex = LANE_INDEX_NONE;
		double InnerAlpha = 0.0;
		for (int32 LaneArrayIndex = 0; LaneArrayIndex < Lanes.Num(); ++LaneArrayIndex)
		{
			const int32 OuterLaneIndex = LaneSign > 0 ? (LaneArrayIndex + 1) : -(LaneArrayIndex + 1);
			AppendLaneMeshForSection(
				Component,
				LaneSectionIndex,
				OuterLaneIndex,
				InnerLaneIndex,
				InnerAlpha,
				S0,
				S1,
				MinNumSteps,
				RoadHeightOffset,
				bGenerateUVs,
				OutMesh);
			InnerLaneIndex = OuterLaneIndex;
			InnerAlpha = 1.0;
		}
	}

}

namespace MetaLandRoadSplinePointPrivate
{
	static double CalcTangentMultiplier(const float InRadius, const float InRotInc)
	{
		static constexpr double A = 0.5;
		static constexpr double A2 = A * A;
		static constexpr double A3 = A2 * A;

		const FVector T0 = FVector::ForwardVector;
		const FVector T1 = T0.RotateAngleAxis(InRotInc, FVector::UpVector);
		const FVector P0 = FVector::RightVector * InRadius;
		const FVector P1 = P0.RotateAngleAxis(InRotInc, FVector::UpVector);

		const FVector ActualVal = FMath::CubicInterp(P0, T0, P1, T1, A);
		const FVector ExpectedVal = P0.RotateAngleAxis(InRotInc * A, FVector::UpVector);
		const double Diff = (ActualVal.X - ExpectedVal.X);

		static constexpr double C1 = (A3 - (2 * A2) + A);
		static constexpr double C2 = (A3 - A2);
		const double PartialInterp = -1.0 * ((C1 * T0.X) + (C2 * T1.X));

		return FMath::IsNearlyZero(PartialInterp) ? 1.0 : ((Diff / PartialInterp) + 1.0);
	}
}

UMetaLandRoadSplineComponent::UMetaLandRoadSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHiddenInGame = true;
	bDrawDebug = true;

	PredecessorConnection = CreateDefaultSubobject<UMetaLandRoadConnection>(TEXT("PredecessorConnection"));
	SuccessorConnection = CreateDefaultSubobject<UMetaLandRoadConnection>(TEXT("SuccessorConnection"));

	if (SplineCurves.Position.Points.Num() >= 2)
	{
		SplineCurves.Position.Points[1].OutVal = FVector(1000.0, 0.0, 0.0);
	}
	UpdateSpline();
}

void UMetaLandRoadSplineComponent::FixUpSegments()
{
	if (GetNumberOfSplinePoints() != PointTypes.Num())
	{
		PointTypes.Reset();
		PointTypes.SetNum(GetNumberOfSplinePoints());
	}
}

int32 UMetaLandRoadSplineComponent::GetNextPoint(int32 PointIndex) const
{
	if (IsClosedLoop())
	{
		return (PointIndex + 1) % GetNumberOfSplinePoints();
	}
	if (PointIndex < (GetNumberOfSplinePoints() - 2))
	{
		return PointIndex + 1;
	}
	return INDEX_NONE;
}

int32 UMetaLandRoadSplineComponent::GetPrevPoint(int32 PointIndex) const
{
	if (IsClosedLoop())
	{
		return (PointIndex == 0) ? GetNumberOfSplinePoints() - 1 : PointIndex - 1;
	}
	if (PointIndex > 0)
	{
		return PointIndex - 1;
	}
	return INDEX_NONE;
}

EMetaLandRoadSplinePointType UMetaLandRoadSplineComponent::GetRoadSplinePointType(int32 PointIndex) const
{
	if (PointIndex >= 0 && PointIndex < SplineCurves.Position.Points.Num())
	{
		if (PointTypes.IsValidIndex(PointIndex) && PointTypes[PointIndex] == EMetaLandRoadSplinePointTypeOverride::Arc)
		{
			return EMetaLandRoadSplinePointType::Arc;
		}
		return static_cast<EMetaLandRoadSplinePointType>(GetSplinePointType(PointIndex));
	}

	return EMetaLandRoadSplinePointType::Constant;
}

void UMetaLandRoadSplineComponent::SetRoadSplinePointType(int32 PointIndex, EMetaLandRoadSplinePointType Mode, bool bUpdateSpline)
{
	FixUpSegments();

	if (PointIndex >= 0 && PointIndex < SplineCurves.Position.Points.Num())
	{
		if (Mode == EMetaLandRoadSplinePointType::Arc)
		{
			PointTypes[PointIndex] = EMetaLandRoadSplinePointTypeOverride::Arc;
			SplineCurves.Position.Points[PointIndex].InterpMode = CIM_CurveUser;
			const int32 NextPoint = GetNextPoint(PointIndex);
			if (NextPoint >= 0)
			{
				SplineCurves.Position.Points[NextPoint].InterpMode = CIM_CurveUser;
			}
		}
		else
		{
			PointTypes[PointIndex] = EMetaLandRoadSplinePointTypeOverride::Inherited;
			SplineCurves.Position.Points[PointIndex].InterpMode = ConvertSplinePointTypeToInterpCurveMode(
				static_cast<ESplinePointType::Type>(Mode));
		}

		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}

bool UMetaLandRoadSplineComponent::AdjustArcSegment(int32 PointIndex, EMetaLandRoadComputeArcMode Mode)
{
	check(PointIndex >= 0 && PointIndex < GetNumberOfSplineSegments());

	bool bResult = true;

	const int32 StartPointIndex = PointIndex;
	const int32 EndPointIndex = (PointIndex + 1) % GetNumberOfSplinePoints();

	FInterpCurvePointVector& StartPoint = SplineCurves.Position.Points[StartPointIndex];
	FInterpCurvePointVector& EndPoint = SplineCurves.Position.Points[EndPointIndex];

	FVector StartToEnd = EndPoint.OutVal - StartPoint.OutVal;
	FVector StartToEndDir = StartToEnd.GetSafeNormal();
	FVector StartDir;
	FVector EndDir;

	if (Mode == EMetaLandRoadComputeArcMode::AdjStartTangent || Mode == EMetaLandRoadComputeArcMode::AdjEndTangent)
	{
		StartToEnd = EndPoint.OutVal - StartPoint.OutVal;
		StartToEndDir = StartToEnd.GetSafeNormal();
		if (Mode == EMetaLandRoadComputeArcMode::AdjStartTangent)
		{
			EndDir = -GetDirectionAtSplinePoint(EndPointIndex, ESplineCoordinateSpace::Local);
			StartDir = FMath::GetReflectionVector(EndDir, StartToEndDir);
		}
		else
		{
			StartDir = GetDirectionAtSplinePoint(StartPointIndex, ESplineCoordinateSpace::Local);
			EndDir = FMath::GetReflectionVector(StartDir, -StartToEndDir);
		}
	}
	else
	{
		StartDir = GetDirectionAtSplinePoint(StartPointIndex, ESplineCoordinateSpace::Local);
		EndDir = -GetDirectionAtSplinePoint(EndPointIndex, ESplineCoordinateSpace::Local);
		FVector Intersection;
		if (FMath::SegmentIntersection2D(
			StartPoint.OutVal,
			StartPoint.OutVal + StartDir * 1.0e7f,
			EndPoint.OutVal,
			EndPoint.OutVal + EndDir * 1.0e7f,
			Intersection))
		{
			const double A = (Intersection - StartPoint.OutVal).Size();
			const double B = (Intersection - EndPoint.OutVal).Size();
			if (Mode == EMetaLandRoadComputeArcMode::AdjStartPos)
			{
				StartPoint.OutVal += StartDir * (A - B);
			}
			else
			{
				EndPoint.OutVal += EndDir * (B - A);
			}
			StartToEnd = EndPoint.OutVal - StartPoint.OutVal;
			StartToEndDir = StartToEnd.GetSafeNormal();
		}
		else
		{
			bResult = false;
		}
	}

	const FVector StartRightVector(FVector2D(StartDir.X, StartDir.Y).GetRotated(90.0), StartDir.Z);
	const double CosA = StartRightVector.CosineAngle2D(StartToEndDir);

	if (StartDir.CosineAngle2D(StartToEndDir) < 0.0)
	{
		bResult = false;
	}

	const double Radius = (StartToEnd.Size2D() / 2.0) / CosA;
	const double Ang = (180.0 - FMath::Acos(CosA) * 2.0 / PI * 180.0);
	const double TangentMult = MetaLandRoadSplinePointPrivate::CalcTangentMultiplier(static_cast<float>(Radius), static_cast<float>(-Ang));

	StartPoint.LeaveTangent = StartDir * TangentMult;
	StartPoint.ArriveTangent = StartDir * StartPoint.ArriveTangent.Size() * (TangentMult >= 0.0 ? 1.0 : -1.0);

	EndPoint.LeaveTangent = -EndDir * EndPoint.LeaveTangent.Size() * (TangentMult >= 0.0 ? 1.0 : -1.0);
	EndPoint.ArriveTangent = -EndDir * TangentMult;

	return bResult;
}

void UMetaLandRoadSplineComponent::UpdateAutoTangents(int32 EditingPointIndex)
{
	if (GetNumberOfSplineSegments() <= 0)
	{
		return;
	}

	FixUpSegments();

	for (int32 PointIndex = 0; PointIndex < GetNumberOfSplineSegments(); ++PointIndex)
	{
		if (PointTypes.IsValidIndex(PointIndex) && PointTypes[PointIndex] == EMetaLandRoadSplinePointTypeOverride::Arc)
		{
			SplineCurves.Position.Points[PointIndex].InterpMode = CIM_CurveUser;
			const int32 NextPointIndex = GetNextPoint(PointIndex);
			if (NextPointIndex >= 0)
			{
				SplineCurves.Position.Points[NextPointIndex].InterpMode = CIM_CurveUser;
			}
		}
	}

	if (EditingPointIndex == INDEX_NONE)
	{
		for (int32 PointIndex = 0; PointIndex < GetNumberOfSplineSegments(); ++PointIndex)
		{
			if (GetRoadSplinePointType(PointIndex) == EMetaLandRoadSplinePointType::Arc)
			{
				const int32 NextPointIndex = GetNextPoint(PointIndex);
				const int32 PrePointIndex = GetPrevPoint(PointIndex);

				const bool bPinStartTangent = PrePointIndex >= 0
					&& GetRoadSplinePointType(PrePointIndex) == EMetaLandRoadSplinePointType::Linear;
				const bool bPinEndTangent = NextPointIndex >= 0
					&& GetRoadSplinePointType(NextPointIndex) == EMetaLandRoadSplinePointType::Linear;

				if (bPinStartTangent && bPinEndTangent)
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
				else if (bPinStartTangent)
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
				else if (bPinEndTangent)
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjStartTangent);
				}
				else
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
			}
		}
	}
	else
	{
		int32 NumSegments = IsClosedLoop() ? GetNumberOfSplineSegments() - 1 : GetNumberOfSplineSegments() - EditingPointIndex;
		for (int32 i = 0; i < NumSegments; ++i)
		{
			const int32 SegmentIndex = (i + EditingPointIndex) % GetNumberOfSplineSegments();
			if (GetRoadSplinePointType(SegmentIndex) == EMetaLandRoadSplinePointType::Arc)
			{
				const int32 NextSegment = GetNextPoint(SegmentIndex);
				const int32 PreSegment = GetPrevPoint(SegmentIndex);

				const bool bPinStartTangent = PreSegment >= 0
					&& GetRoadSplinePointType(PreSegment) == EMetaLandRoadSplinePointType::Linear;
				const bool bPinEndTangent = NextSegment >= 0
					&& GetRoadSplinePointType(NextSegment) == EMetaLandRoadSplinePointType::Linear;

				if (bPinStartTangent && bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndPos);
					break;
				}
				else if (bPinStartTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
				else if (bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndPos);
					break;
				}
				else
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
			}
			else
			{
				break;
			}
		}

		NumSegments = IsClosedLoop() ? GetNumberOfSplineSegments() - 1 : EditingPointIndex;
		for (int32 i = 0; i < NumSegments; ++i)
		{
			int32 SegmentIndex = EditingPointIndex - 1 - i;
			if (SegmentIndex < 0)
			{
				SegmentIndex += GetNumberOfSplineSegments();
			}

			if (GetRoadSplinePointType(SegmentIndex) == EMetaLandRoadSplinePointType::Arc)
			{
				const int32 NextSegment = GetNextPoint(SegmentIndex);
				const int32 PreSegment = GetPrevPoint(SegmentIndex);

				const bool bPinStartTangent = PreSegment >= 0
					&& GetRoadSplinePointType(PreSegment) == EMetaLandRoadSplinePointType::Linear;
				const bool bPinEndTangent = NextSegment >= 0
					&& GetRoadSplinePointType(NextSegment) == EMetaLandRoadSplinePointType::Linear;

				if (bPinStartTangent && bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartPos);
					break;
				}
				else if (bPinStartTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartPos);
					break;
				}
				else if (bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartTangent);
				}
				else
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartTangent);
				}
			}
			else
			{
				break;
			}
		}
	}
}

void UMetaLandRoadSplineComponent::OnRegister()
{
	Super::OnRegister();
	if (GetLaneSectionsNum() > 0)
	{
		UpdateRoadLayout();
	}
	else
	{
		UpdateLaneSectionBounds();
	}
	RefreshLaneVisualization();
}

void UMetaLandRoadSplineComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatch = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		{
			LineBatch->ClearBatch(static_cast<uint32>(GetUniqueID()));
		}
	}

	Super::OnUnregister();
}

void UMetaLandRoadSplineComponent::PostLoad()
{
	Super::PostLoad();
	FixUpSegments();
	if (GetLaneSectionsNum() > 0)
	{
		UpdateRoadLayout();
	}
	else
	{
		UpdateLaneSectionBounds();
	}
	RefreshLaneVisualization();
}

void UMetaLandRoadSplineComponent::UpdateSpline()
{
	FixUpSegments();
	UpdateAutoTangents();
	Super::UpdateSpline();
	UpdateLaneSectionBounds();
	RefreshLaneVisualization();
#if WITH_EDITOR
	// Spline changed while editing — redraw ComponentVisualizer PDI overlay.
	RedrawNativeEditViewport(this);
#endif
}

float UMetaLandRoadSplineComponent::GetSplinePointInputKey(int32 PointIndex) const
{
	if (!SplineCurves.Position.Points.IsValidIndex(PointIndex))
	{
		return 0.0f;
	}
	return SplineCurves.Position.Points[PointIndex].InVal;
}

void UMetaLandRoadSplineComponent::BuildCenterLineWorldPolyline(
	TArray<FVector>& OutPoints,
	int32 StepsPerSegment) const
{
	OutPoints.Reset();
	const int32 NumPts = GetNumberOfSplinePoints();
	const int32 NumSeg = GetNumberOfSplineSegments();
	if (NumPts < 2 || NumSeg < 1)
	{
		return;
	}

	const int32 Steps = FMath::Clamp(StepsPerSegment, 4, 64);
	for (int32 Seg = 0; Seg < NumSeg; ++Seg)
	{
		const int32 I0 = Seg;
		const int32 I1 = IsClosedLoop() ? ((Seg + 1) % NumPts) : (Seg + 1);
		if (!SplineCurves.Position.Points.IsValidIndex(I0)
			|| !SplineCurves.Position.Points.IsValidIndex(I1))
		{
			continue;
		}

		const float Key0 = GetSplinePointInputKey(I0);
		const float Key1 = IsClosedLoop() && I1 == 0
			? GetSplinePointInputKey(I0) + 1.0f
			: GetSplinePointInputKey(I1);
		const int32 StartStep = (Seg == 0) ? 0 : 1;
		for (int32 Step = StartStep; Step <= Steps; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
			const float Key = FMath::Lerp(Key0, Key1, Alpha);
			if (Step == 0)
			{
				OutPoints.Add(GetLocationAtSplinePoint(I0, ESplineCoordinateSpace::World));
			}
			else if (Step == Steps)
			{
				OutPoints.Add(GetLocationAtSplinePoint(I1, ESplineCoordinateSpace::World));
			}
			else
			{
				OutPoints.Add(GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World));
			}
		}
	}
}

float UMetaLandRoadSplineComponent::GetMainCenterLineHeightOffset() const
{
	return LaneVisualizationHeightOffset + 8.0f;
}

void UMetaLandRoadSplineComponent::DrawMainCenterLinePDI(
	FPrimitiveDrawInterface* PDI,
	const FLinearColor& Color,
	float Thickness) const
{
	if (!PDI || GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	TArray<FVector> Points;
	BuildCenterLineWorldPolyline(Points, 32);
	const FVector Raise(0.0f, 0.0f, GetMainCenterLineHeightOffset());
	for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
	{
		PDI->DrawLine(
			Points[PointIndex - 1] + Raise,
			Points[PointIndex] + Raise,
			Color,
			SDPG_Foreground,
			Thickness,
			0.0f,
			true);
	}
}

void UMetaLandRoadSplineComponent::UpdateSpline(int32 EditingPointIndex)
{
	FixUpSegments();
	UpdateAutoTangents(EditingPointIndex);
	Super::UpdateSpline();
	UpdateLaneSectionBounds();
	RefreshLaneVisualization();
#if WITH_EDITOR
	RedrawNativeEditViewport(this);
#endif
}

void UMetaLandRoadSplineComponent::UpdateRoadLayout()
{
	RoadLayout.UpdateLayout(this);
	UpdateLaneSectionBounds();
	RefreshLaneVisualization();
}

void UMetaLandRoadSplineComponent::UpdateLaneSectionBounds()
{
	RoadLayout.UpdateBounds(GetSplineLength());
}

void UMetaLandRoadSplineComponent::TrimLaneSections(double Tolerance)
{
	RoadLayout.TrimSections(GetSplineLength(), Tolerance, this);
	RoadLayout.UpdateLayout(this);
}

void UMetaLandRoadSplineComponent::RefreshLaneVisualization()
{
	MetaLandRoadLaneVisualization::RefreshLaneVisualization(this);
}

#if WITH_EDITOR
void UMetaLandRoadSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, RoadLayout)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, bShowLaneVisualization)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, LaneVisualizationSampleStep)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, LaneVisualizationHeightOffset))
	{
		UpdateRoadLayout();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, AsphaltUvTileCm))
	{
		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(GetOwner()))
		{
			RoadActor->RefreshDynamicRoadMesh();
		}
	}
}
#endif

const FMetaLandRoadLane* UMetaLandRoadSplineComponent::GetRoadLane(int SectionIndex, int LaneIndex) const
{
	if (SectionIndex >= 0 && SectionIndex < GetLaneSectionsNum())
	{
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		if (LaneIndex > 0 && LaneIndex <= Section.Right.Num())
		{
			return &Section.Right[LaneIndex - 1];
		}
		if (LaneIndex < 0 && -LaneIndex <= Section.Left.Num())
		{
			return &Section.Left[-LaneIndex - 1];
		}
	}
	return nullptr;
}

FMetaLandRoadLane* UMetaLandRoadSplineComponent::GetRoadLane(int SectionIndex, int LaneIndex)
{
	return const_cast<FMetaLandRoadLane*>(static_cast<const UMetaLandRoadSplineComponent*>(this)->GetRoadLane(SectionIndex, LaneIndex));
}

bool UMetaLandRoadSplineComponent::GetRoadEdgePointsAtS(
	double S,
	FVector& OutLeft,
	FVector& OutRight,
	FVector& OutCenter,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	if (GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const int32 SectionIndex = RoadLayout.FindSectionAtSOffset(S);
	if (SectionIndex == INDEX_NONE)
	{
		return false;
	}

	OutCenter = EvalLanePosition(SectionIndex, LANE_INDEX_NONE, S, 0.0, CoordinateSpace);
	const float Key = GetInputKeyValueAtDistanceAlongSpline(S);
	const FVector RightVector = GetRightVectorAtSplineInputKey(Key, CoordinateSpace);
	const double FallbackHalfWidth = MetaLandRoad::DefaultRoadLaneWidth * 2.0;

	// Always resolve Left AND Right faces via FindSideSection. Dual-lane L|R markers
	// share the same SOffset; FindSectionAtSOffset may land on only-Left or only-Right —
	// collapsing the missing face to Center was poisoning junction mouths (EdgeR==Center).
	OutLeft = OutCenter - RightVector * FallbackHalfWidth;
	OutRight = OutCenter + RightVector * FallbackHalfWidth;

	const int32 LeftSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
	const int32 RightSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);

	bool bHaveLeft = false;
	if (LeftSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& LeftSection = GetLaneSection(LeftSectionIndex);
		if (!LeftSection.Left.IsEmpty())
		{
			OutLeft = EvalLanePosition(
				LeftSectionIndex, -LeftSection.Left.Num(), S, 1.0, CoordinateSpace);
			bHaveLeft = true;
		}
	}

	bool bHaveRight = false;
	if (RightSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& RightSection = GetLaneSection(RightSectionIndex);
		if (!RightSection.Right.IsEmpty())
		{
			OutRight = EvalLanePosition(
				RightSectionIndex, RightSection.Right.Num(), S, 1.0, CoordinateSpace);
			bHaveRight = true;
		}
	}

	// One-sided bundle (e.g. 2×Left centered via ROffset): mesh spans outermost α=1
	// to innermost α=0 — NOT Center. Collapsing the missing face to Center halved mouth
	// width and laterally misaligned Fig1 cover vs road body.
	if (!bHaveLeft && bHaveRight)
	{
		OutLeft = EvalLanePosition(RightSectionIndex, 1, S, 0.0, CoordinateSpace);
	}
	if (!bHaveRight && bHaveLeft)
	{
		OutRight = EvalLanePosition(LeftSectionIndex, -1, S, 0.0, CoordinateSpace);
	}

	return bHaveLeft || bHaveRight;
}

bool UMetaLandRoadSplineComponent::GetRoadCrossSectionPointsAtS(
	double S,
	TArray<FVector>& OutPoints,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	OutPoints.Reset();
	if (GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const int32 HostSectionIndex = RoadLayout.FindSectionAtSOffset(S);
	if (HostSectionIndex == INDEX_NONE)
	{
		return false;
	}

	auto AppendUnique = [&OutPoints](const FVector& Point)
	{
		if (OutPoints.Num() > 0 && FVector::DistSquared(OutPoints.Last(), Point) < 1.0)
		{
			return;
		}
		OutPoints.Add(Point);
	};

	auto AppendSideLanes = [this, S, CoordinateSpace, &AppendUnique](int32 SectionIndex, bool bLeftSide)
	{
		if (SectionIndex == INDEX_NONE)
		{
			return;
		}
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		if (bLeftSide)
		{
			for (int32 LaneCount = Section.Left.Num(); LaneCount >= 1; --LaneCount)
			{
				AppendUnique(EvalLanePosition(SectionIndex, -LaneCount, S, 1.0, CoordinateSpace));
			}
		}
		else
		{
			for (int32 LaneCount = 1; LaneCount <= Section.Right.Num(); ++LaneCount)
			{
				AppendUnique(EvalLanePosition(SectionIndex, LaneCount, S, 1.0, CoordinateSpace));
			}
		}
	};

	const FMetaLandRoadLaneSection& HostSection = GetLaneSection(HostSectionIndex);
	const int32 LeftSectionIndex = RoadLayout.FindSideSection(HostSectionIndex, EMetaLandRoadLaneSectionSide::Left);
	const int32 RightSectionIndex = RoadLayout.FindSideSection(HostSectionIndex, EMetaLandRoadLaneSectionSide::Right);
	const bool bHaveLeftLanes = LeftSectionIndex != INDEX_NONE
		&& !GetLaneSection(LeftSectionIndex).Left.IsEmpty();
	const bool bHaveRightLanes = RightSectionIndex != INDEX_NONE
		&& !GetLaneSection(RightSectionIndex).Right.IsEmpty();

	// Mouth CS = section Left/Right lane edges only (α=1 outer → α=0 inner).
	// Never insert Center between faces — Center as mid made GetEdgeLanePair treat
	// EdgeC as lane-inner → red-V / fan strips / stripW=half-road (section L/R mix-up).
	if (bHaveLeftLanes)
	{
		AppendSideLanes(LeftSectionIndex, true);
		AppendUnique(EvalLanePosition(LeftSectionIndex, -1, S, 0.0, CoordinateSpace));
	}
	if (bHaveRightLanes)
	{
		AppendUnique(EvalLanePosition(RightSectionIndex, 1, S, 0.0, CoordinateSpace));
		AppendSideLanes(RightSectionIndex, false);
	}
	(void)HostSection;

	if (OutPoints.Num() < 2)
	{
		FVector Left;
		FVector Right;
		FVector Center;
		if (!GetRoadEdgePointsAtS(S, Left, Right, Center, CoordinateSpace))
		{
			return false;
		}
		OutPoints.Reset();
		OutPoints.Add(Left);
		// Do not insert Center between L/R — one-sided mouths already use α=0 as the
		// opposite edge; L-C-R here reintroduced the red-V notch on junction covers.
		OutPoints.Add(Right);
		(void)Center;
	}

	return OutPoints.Num() >= 2;
}

bool UMetaLandRoadSplineComponent::GetRoadCrossSectionPointsForLaneIndicesAtS(
	double S,
	const TArray<int32>& LaneIndices,
	TArray<FVector>& OutPoints,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	OutPoints.Reset();
	if (LaneIndices.Num() == 0)
	{
		return GetRoadCrossSectionPointsAtS(S, OutPoints, CoordinateSpace);
	}

	TArray<int32> SortedLanes;
	SortedLanes.Reserve(LaneIndices.Num());
	for (const int32 LaneIndex : LaneIndices)
	{
		if (LaneIndex != LANE_INDEX_NONE)
		{
			SortedLanes.AddUnique(LaneIndex);
		}
	}
	if (SortedLanes.Num() == 0)
	{
		return GetRoadCrossSectionPointsAtS(S, OutPoints, CoordinateSpace);
	}
	SortedLanes.Sort();

	const int32 HostSectionIndex = RoadLayout.FindSectionAtSOffset(S);
	if (HostSectionIndex == INDEX_NONE)
	{
		return false;
	}

	auto AppendUnique = [&OutPoints](const FVector& Point)
	{
		if (OutPoints.Num() > 0 && FVector::DistSquared(OutPoints.Last(), Point) < 1.0)
		{
			return;
		}
		OutPoints.Add(Point);
	};

	auto ResolveSectionForLane = [this, HostSectionIndex](int32 LaneIndex) -> int32
	{
		const int32 Resolved = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			this, HostSectionIndex, LaneIndex);
		return Resolved;
	};

	TArray<int32> LeftLanes;
	TArray<int32> RightLanes;
	for (const int32 LaneIndex : SortedLanes)
	{
		if (LaneIndex < 0)
		{
			LeftLanes.Add(LaneIndex);
		}
		else
		{
			RightLanes.Add(LaneIndex);
		}
	}

	// Left outer → inner (most-negative first), matching full-CS order.
	for (int32 Index = 0; Index < LeftLanes.Num(); ++Index)
	{
		const int32 LaneIndex = LeftLanes[Index];
		const int32 SectionIndex = ResolveSectionForLane(LaneIndex);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}
		AppendUnique(EvalLanePosition(SectionIndex, LaneIndex, S, 1.0, CoordinateSpace));
	}

	if (LeftLanes.Num() > 0 && RightLanes.Num() > 0)
	{
		// Face inners (alpha0), not Center — same section Left/Right rule as full CS.
		const int32 LeftInnerLane = LeftLanes.Last();
		const int32 LeftInnerSec = ResolveSectionForLane(LeftInnerLane);
		if (LeftInnerSec != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(LeftInnerSec, LeftInnerLane, S, 0.0, CoordinateSpace));
		}
		const int32 RightInnerLane = RightLanes[0];
		const int32 RightInnerSec = ResolveSectionForLane(RightInnerLane);
		if (RightInnerSec != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(RightInnerSec, RightInnerLane, S, 0.0, CoordinateSpace));
		}
	}
	else if (LeftLanes.Num() > 0)
	{
		const int32 InnerLane = LeftLanes.Last();
		const int32 SectionIndex = ResolveSectionForLane(InnerLane);
		if (SectionIndex != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(SectionIndex, InnerLane, S, 0.0, CoordinateSpace));
		}
	}
	else if (RightLanes.Num() > 0)
	{
		const int32 InnerLane = RightLanes[0];
		const int32 SectionIndex = ResolveSectionForLane(InnerLane);
		if (SectionIndex != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(SectionIndex, InnerLane, S, 0.0, CoordinateSpace));
		}
	}

	for (const int32 LaneIndex : RightLanes)
	{
		const int32 SectionIndex = ResolveSectionForLane(LaneIndex);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}
		AppendUnique(EvalLanePosition(SectionIndex, LaneIndex, S, 1.0, CoordinateSpace));
	}

	if (OutPoints.Num() < 2)
	{
		return GetRoadCrossSectionPointsAtS(S, OutPoints, CoordinateSpace);
	}
	return true;
}

double UMetaLandRoadSplineComponent::GetRoadHalfWidthForLaneIndicesAtS(
	double S,
	const TArray<int32>& LaneIndices) const
{
	if (LaneIndices.Num() == 0)
	{
		return GetRoadHalfWidthAtS(S);
	}

	TArray<FVector> MouthCS;
	if (!GetRoadCrossSectionPointsForLaneIndicesAtS(S, LaneIndices, MouthCS, ESplineCoordinateSpace::World)
		|| MouthCS.Num() < 2)
	{
		return GetRoadHalfWidthAtS(S);
	}

	// Mouth half-width = half of subset edge span. Dist-to-centerline overstates off-center
	// bundles and understates centered ones used for junction indent / custom lane mouths.
	return FMath::Max(FVector::Dist2D(MouthCS[0], MouthCS.Last()) * 0.5, 1.0);
}

double UMetaLandRoadSplineComponent::GetRoadHalfWidthAtS(double S) const
{
	FVector Left;
	FVector Right;
	FVector Center;
	if (!GetRoadEdgePointsAtS(S, Left, Right, Center, ESplineCoordinateSpace::World))
	{
		return MetaLandRoad::DefaultRoadLaneWidth;
	}

	return FMath::Max(FVector::Dist2D(Left, Center), FVector::Dist2D(Right, Center));
}

bool UMetaLandRoadSplineComponent::GenerateCombinedLaneDynamicMesh(
	FDynamicMesh3& OutMesh,
	float RoadHeightOffset,
	bool bGenerateUVs)
{
	using namespace MetaLandRoadLaneMeshPrivate;

	if (GetLaneSectionsNum() == 0)
	{
		return false;
	}

	// Existing actors may still have Linear keys — convert before sampling mesh.
	EnsureAllCurvePointTypesForSmoothCorners();

	if (bGenerateUVs)
	{
		OutMesh.EnableVertexUVs(FVector2f::Zero());
		OutMesh.EnableAttributes();
	}

	const double SplineLength = GetSplineLength();
	if (SplineLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TArray<TPair<double, double>> MeshSpans;
	if (const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(GetOwner()))
	{
		RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
	}
	if (MeshSpans.Num() == 0)
	{
		MeshSpans.Emplace(0.0, SplineLength);
	}

	/**
	 * Aug2 baseline (ugit-stash/8fcbde9 Junction测试第三):
	 * Independent face timelines — Left breaks only at Left/Both, Right at Right/Both.
	 */
	auto CollectSideBreaks = [this](bool bLeftFace, double MeshSMin, double MeshSMax) -> TArray<double>
	{
		TArray<double> Breaks;
		Breaks.Add(MeshSMin);
		for (int32 SectionIndex = 0; SectionIndex < GetLaneSectionsNum(); ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
			if (Section.SOffset <= MeshSMin + KINDA_SMALL_NUMBER
				|| Section.SOffset >= MeshSMax - KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const bool bBreaksThisFace = bLeftFace
				? MetaLandRoad::HasLeftSide(Section.Side)
				: MetaLandRoad::HasRightSide(Section.Side);
			if (bBreaksThisFace)
			{
				Breaks.Add(Section.SOffset);
			}
		}
		Breaks.Add(MeshSMax);
		Breaks.Sort();

		TArray<double> Unique;
		Unique.Reserve(Breaks.Num());
		for (double S : Breaks)
		{
			if (Unique.Num() == 0 || !FMath::IsNearlyEqual(Unique.Last(), S, KINDA_SMALL_NUMBER))
			{
				Unique.Add(S);
			}
		}
		return Unique;
	};

	auto ResolveLaneSectionForFace = [this](double SMid, bool bLeftFace) -> int32
	{
		int32 HostSection = RoadLayout.FindSectionAtSOffset(SMid);
		if (HostSection == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		return RoadLayout.FindSideSection(
			HostSection,
			bLeftFace ? EMetaLandRoadLaneSectionSide::Left : EMetaLandRoadLaneSectionSide::Right);
	};

	for (const TPair<double, double>& Span : MeshSpans)
	{
		const double MeshSMin = Span.Key;
		const double MeshSMax = Span.Value;
		if ((MeshSMax - MeshSMin) < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		auto BuildFaceSpans = [&](bool bLeftFace, int32 LaneSign)
		{
			const TArray<double> Breaks = CollectSideBreaks(bLeftFace, MeshSMin, MeshSMax);

			for (int32 BreakIdx = 0; BreakIdx + 1 < Breaks.Num(); ++BreakIdx)
			{
				const double S0 = Breaks[BreakIdx];
				const double S1 = Breaks[BreakIdx + 1];
				// Tiny face spans + MinSteps → dense fan. Still keep the terminal span to
				// MeshSMax so Left/Right asphalt tips both reach CutS (L/R section stagger).
				const bool bTerminalToTip = FMath::IsNearlyEqual(S1, MeshSMax, 1.0);
				if ((S1 - S0) < 10.0 && !bTerminalToTip)
				{
					continue;
				}

				const int32 LaneSectionIndex = ResolveLaneSectionForFace(0.5 * (S0 + S1), bLeftFace);
				if (LaneSectionIndex == INDEX_NONE)
				{
					continue;
				}

				const int32 SectionMinSteps = ComputeScaledMinStepsForSpan(S0, S1, SplineLength, 4);
				TArray<FMetaLandRoadSplinePositionLinearApproximation> UnusedPrev;
				AppendLanesForSide(
					this,
					LaneSectionIndex,
					INDEX_NONE,
					true,
					true,
					UnusedPrev,
					S0,
					S1,
					SectionMinSteps,
					RoadHeightOffset,
					bGenerateUVs,
					OutMesh,
					LaneSign);
			}
		};

		BuildFaceSpans(/*bLeftFace=*/false, /*LaneSign=*/+1);
		BuildFaceSpans(/*bLeftFace=*/true, /*LaneSign=*/-1);
	}

	return OutMesh.TriangleCount() > 0;
}


namespace MetaLandRoadSplinePrivate
{
	static void ShiftWidthCurve(FRichCurve& Width, double SDelta)
	{
		for (FRichCurveKey& Key : Width.Keys)
		{
			Key.Time += SDelta;
		}
	}

	static void ShiftLaneAttribute(FMetaLandRoadLaneAttribute& Attribute, double SDelta)
	{
		for (FMetaLandRoadLaneAttributeKey& Key : Attribute.Keys)
		{
			Key.SOffset += SDelta;
		}
	}

	static void ShiftLane(FMetaLandRoadLane& OldLane, FMetaLandRoadLane& NewLane, double SDelta)
	{
		ShiftWidthCurve(NewLane.Width, SDelta);
		for (TPair<FName, FMetaLandRoadLaneAttribute>& AttributePair : NewLane.Attributes)
		{
			ShiftLaneAttribute(AttributePair.Value, SDelta);
		}

		if (OldLane.IsForwardLane())
		{
			NewLane.SuccessorConnection = OldLane.SuccessorConnection;
			OldLane.SuccessorConnection = nullptr;
			NewLane.PredecessorConnection = nullptr;
		}
		else
		{
			NewLane.PredecessorConnection = OldLane.PredecessorConnection;
			OldLane.PredecessorConnection = nullptr;
			NewLane.SuccessorConnection = nullptr;
		}
	}
}

int32 UMetaLandRoadSplineComponent::FindRoadSectionOnSplineKey(float SplineKey) const
{
	for (int32 SectionIndex = 0; SectionIndex < GetLaneSectionsNum(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		const float StartKey = GetInputKeyValueAtDistanceAlongSpline(Section.SOffset);
		const float EndKey = GetInputKeyValueAtDistanceAlongSpline(Section.SOffsetEnd_Cached);
		if (SplineKey >= StartKey && SplineKey <= EndKey)
		{
			return SectionIndex;
		}
	}

	return INDEX_NONE;
}

int32 UMetaLandRoadSplineComponent::SplitSectionAtDistanceAlongSpline(double DistanceAlongSpline, EMetaLandRoadLaneSectionSide Side)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	return SplitSection(SplineKey, Side);
}

int32 UMetaLandRoadSplineComponent::SplitSectionAtSplinePoint(int32 PointIndex, EMetaLandRoadLaneSectionSide Side)
{
	if (!GetSplinePointsPosition().Points.IsValidIndex(PointIndex))
	{
		return INDEX_NONE;
	}

	return SplitSection(static_cast<float>(PointIndex), Side);
}

int32 UMetaLandRoadSplineComponent::EditorAddLeftSection(float SplineKey)
{
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddLeftSection ENTER  SplineKey=%f  Sections=%d  SplineLength=%f"),
		SplineKey,
		GetLaneSectionsNum(),
		GetSplineLength());
	const int32 Result = SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Left);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddLeftSection EXIT   Result=%d"), Result);
	return Result;
}

int32 UMetaLandRoadSplineComponent::EditorAddRightSection(float SplineKey)
{
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddRightSection ENTER SplineKey=%f  Sections=%d  SplineLength=%f"),
		SplineKey,
		GetLaneSectionsNum(),
		GetSplineLength());
	const int32 Result = SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Right);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddRightSection EXIT  Result=%d"), Result);
	return Result;
}

int32 UMetaLandRoadSplineComponent::EditorAddLeftSectionAtDistanceAlongSpline(double DistanceAlongSpline)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddLeftSectionAtDistanceAlongSpline  Distance=%f  SplineKey=%f"),
		DistanceAlongSpline,
		SplineKey);
	return EditorAddLeftSection(SplineKey);
}

int32 UMetaLandRoadSplineComponent::EditorAddRightSectionAtDistanceAlongSpline(double DistanceAlongSpline)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddRightSectionAtDistanceAlongSpline Distance=%f  SplineKey=%f"),
		DistanceAlongSpline,
		SplineKey);
	return EditorAddRightSection(SplineKey);
}

// =============================================================================
// [KEY-ALGO] SplitSection
// Alt+E: Add Section / Add Left Section / Add Right Section 的唯一写入入口。
// Side=Left|Right 只切那一面；对面幅继续走，两端应对齐同一条样条 S。
// 依赖 EnsureBothFacesOnRoad：对面幅为空时这里只能插入空 section。
// =============================================================================
int32 UMetaLandRoadSplineComponent::SplitSection(float SplineKey, EMetaLandRoadLaneSectionSide Side)
{
	MetaLandRoadProfile::EnsureBothFacesOnRoad(this);

	const int32 SectionIndex = FindRoadSectionOnSplineKey(SplineKey);
	if (SectionIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("SplitSection: no section contains spline key %f"), SplineKey);
		return INDEX_NONE;
	}

	const double SOffset = GetDistanceAlongSplineAtSplineInputKey(SplineKey);
	const double SplineLength = GetSplineLength();
	if (SOffset <= KINDA_SMALL_NUMBER || SOffset >= SplineLength - KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("SplitSection: invalid split distance %f on spline length %f"), SOffset, SplineLength);
		return INDEX_NONE;
	}

	// Same S is OK for opposite faces (Left then Right). Reject only when the new Side
	// overlaps an existing face at that S (Both overlaps everything; Left↔Left, Right↔Right).
	auto SideOverlapsFace = [](EMetaLandRoadLaneSectionSide Existing, EMetaLandRoadLaneSectionSide Incoming)
	{
		if (Existing == EMetaLandRoadLaneSectionSide::Both
			|| Incoming == EMetaLandRoadLaneSectionSide::Both)
		{
			return true;
		}
		return Existing == Incoming;
	};
	for (const FMetaLandRoadLaneSection& ExistingSection : RoadLayout.Sections)
	{
		if (FMath::IsNearlyEqual(ExistingSection.SOffset, SOffset, KINDA_SMALL_NUMBER)
			&& SideOverlapsFace(ExistingSection.Side, Side))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SplitSection: overlapping %d face already exists at distance %f"),
				static_cast<int32>(Side),
				SOffset);
			return INDEX_NONE;
		}
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(NSLOCTEXT("MetaLandRoad", "SplitSection", "Split Lane Section"), !GIsTransacting);
	Modify();
#endif

	bAutoMaintainMidpointSectionSplit = false;

	FMetaLandRoadLaneSection& SourceSection = RoadLayout.Sections[SectionIndex];

	FMetaLandRoadLaneSection SectionToAdd;
	SectionToAdd.SOffset = SOffset;
	SectionToAdd.Side = Side;
	SectionToAdd.Attributes = SourceSection.Attributes;

	int32 LeftSectionIndex = INDEX_NONE;
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Left)
	{
		LeftSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
		if (LeftSectionIndex != INDEX_NONE)
		{
			SectionToAdd.Left = RoadLayout.Sections[LeftSectionIndex].Left;
		}
	}

	int32 RightSectionIndex = INDEX_NONE;
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Right)
	{
		RightSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);
		if (RightSectionIndex != INDEX_NONE)
		{
			SectionToAdd.Right = RoadLayout.Sections[RightSectionIndex].Right;
		}
	}

	RoadLayout.Sections.Insert(SectionToAdd, SectionIndex + 1);
	UpdateLaneSectionBounds();
	UpdateRoadLayout();

	FMetaLandRoadLaneSection& NewSection = RoadLayout.Sections[SectionIndex + 1];

	for (int32 LaneIndex = 0; LaneIndex < NewSection.Left.Num(); ++LaneIndex)
	{
		if (LeftSectionIndex == INDEX_NONE)
		{
			break;
		}

		FMetaLandRoadLaneSection& OldSection = RoadLayout.Sections[LeftSectionIndex];
		MetaLandRoadSplinePrivate::ShiftLane(
			OldSection.Left[LaneIndex],
			NewSection.Left[LaneIndex],
			OldSection.SOffset - SOffset);
	}

	for (int32 LaneIndex = 0; LaneIndex < NewSection.Right.Num(); ++LaneIndex)
	{
		if (RightSectionIndex == INDEX_NONE)
		{
			break;
		}

		FMetaLandRoadLaneSection& OldSection = RoadLayout.Sections[RightSectionIndex];
		MetaLandRoadSplinePrivate::ShiftLane(
			OldSection.Right[LaneIndex],
			NewSection.Right[LaneIndex],
			OldSection.SOffset - SOffset);
	}

	const int32 CenterSectionIndex = FMath::Max(LeftSectionIndex, RightSectionIndex);
	if (CenterSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& OldSection = RoadLayout.Sections[CenterSectionIndex];
		for (TPair<FName, FMetaLandRoadLaneAttribute>& AttributePair : NewSection.Attributes)
		{
			MetaLandRoadSplinePrivate::ShiftLaneAttribute(AttributePair.Value, OldSection.SOffset - SOffset);
		}
	}

	// Only trim the side(s) that were actually split. Full Section::Trim also rewrote the
	// continued side's Width (e.g. Right when adding Left-only) and opened mesh V-gaps.
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Left)
	{
		if (LeftSectionIndex != INDEX_NONE)
		{
			for (FMetaLandRoadLane& Lane : RoadLayout.Sections[LeftSectionIndex].Left)
			{
				Lane.Trim(true);
			}
		}
		for (FMetaLandRoadLane& Lane : NewSection.Left)
		{
			Lane.Trim(true);
		}
	}
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Right)
	{
		if (RightSectionIndex != INDEX_NONE)
		{
			for (FMetaLandRoadLane& Lane : RoadLayout.Sections[RightSectionIndex].Right)
			{
				Lane.Trim(true);
			}
		}
		for (FMetaLandRoadLane& Lane : NewSection.Right)
		{
			Lane.Trim(true);
		}
	}
	UpdateRoadLayout();
	MarkRenderStateDirty();

	return SectionIndex + 1;
}

UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange UMetaLandRoadSplineComponent::GetLaneRange(int SectionIndex, int LaneIndex) const
{
	if (LaneIndex == LANE_INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		FMetaLandRoadLaneRange Range;
		Range.StartS = Section.SOffset;
		Range.EndS = Section.SOffsetEnd_Cached;
		return Range;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		this, SectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE || !RoadLayout.Sections.IsValidIndex(LaneSectionIndex))
	{
		return FMetaLandRoadLaneRange();
	}

	const FMetaLandRoadLaneSection& LaneSection = GetLaneSection(LaneSectionIndex);
	if (!LaneSection.CheckLaneIndex(LaneIndex))
	{
		return FMetaLandRoadLaneRange();
	}

	const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
	FMetaLandRoadLaneRange Range;
	Range.StartS = Lane.GetStartOffset();
	Range.EndS = Lane.GetEndOffset();
	return Range;
}

bool UMetaLandRoadSplineComponent::BuildDrawAnchorForLaneMouth(
	int32 SpanSectionIndex,
	int32 LaneIndex,
	double AnchorS,
	EMetaLandRoadMouthSide MouthSide,
	FMetaLandRoadDrawAnchor& OutAnchor) const
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(this, SpanSectionIndex, LaneIndex))
	{
		return false;
	}

	OutAnchor.SourceSpline = const_cast<UMetaLandRoadSplineComponent*>(this);
	OutAnchor.SectionIndex = SpanSectionIndex;
	OutAnchor.LaneIndex = LaneIndex;
	OutAnchor.AnchorS = AnchorS;
	OutAnchor.MouthSide = MouthSide;
	OutAnchor.bFromSuccessor = MouthSide == EMetaLandRoadMouthSide::End;
	OutAnchor.BundledLaneIndices.Reset();
	return OutAnchor.IsValid();
}

bool UMetaLandRoadSplineComponent::EvalMouthTransform(
	const FMetaLandRoadDrawAnchor& Anchor,
	FTransform& OutTransform) const
{
	if (!Anchor.IsValid() || Anchor.SourceSpline.Get() != this)
	{
		return false;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		this, Anchor.SectionIndex, Anchor.LaneIndex);
	if (LaneSectionIndex == INDEX_NONE || !GetLaneSections().IsValidIndex(LaneSectionIndex))
	{
		return false;
	}

	if (!GetLaneSection(LaneSectionIndex).CheckLaneIndex(Anchor.LaneIndex))
	{
		return false;
	}

	FMetaLandRoadDrawAnchor VisibleAnchor = Anchor;
	VisibleAnchor.AnchorS = MetaLandRoadLaneEditPick::ClampMouthAnchorSToVisibleMesh(
		this, Anchor.AnchorS, Anchor.MouthSide);

	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector OutwardTangent;
	if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		this, VisibleAnchor, TipLeft, TipRight, TipCenter, OutwardTangent))
	{
		return false;
	}

	const FVector Tangent = OutwardTangent.IsNearlyZero()
		? FVector::ForwardVector
		: OutwardTangent.GetSafeNormal();
	const FVector UpVector = GetUpVectorAtDistanceAlongSpline(
		VisibleAnchor.AnchorS, ESplineCoordinateSpace::World).GetSafeNormal();
	OutTransform = FTransform(Tangent.ToOrientationQuat(), TipCenter);
	if (!UpVector.IsNearlyZero())
	{
		OutTransform.SetRotation(FRotationMatrix::MakeFromXZ(Tangent, UpVector).ToQuat());
	}

	return true;
}

void UMetaLandRoadSplineComponent::FitSplineToAnchor(const FMetaLandRoadDrawAnchor& Anchor, bool bAtSplineStart)
{
	UMetaLandRoadSplineComponent* SourceSpline = Anchor.SourceSpline.Get();
	if (!IsValid(SourceSpline) || !Anchor.IsValid() || GetNumberOfSplinePoints() < 1)
	{
		return;
	}

	FVector MouthLoc = FVector::ZeroVector;
	FVector MouthOutward = FVector::ForwardVector;
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector OutwardTangent;
	if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		SourceSpline, Anchor, TipLeft, TipRight, TipCenter, OutwardTangent))
	{
		MouthLoc = TipCenter;
		MouthOutward = OutwardTangent.GetSafeNormal2D();
	}
	else
	{
		FTransform MouthTransform = FTransform::Identity;
		if (!SourceSpline->EvalMouthTransform(Anchor, MouthTransform))
		{
			return;
		}
		MouthLoc = MouthTransform.GetLocation();
		MouthOutward = MouthTransform.GetRotation().GetForwardVector().GetSafeNormal2D();
	}
	if (MouthOutward.IsNearlyZero())
	{
		MouthOutward = FVector::ForwardVector;
	}

	Modify();
	const int32 NumPts = GetNumberOfSplinePoints();
	const int32 TipIndex = bAtSplineStart ? 0 : (NumPts - 1);

	// Start-mouth freeze during continue draw only. EndConnection must always pin the tip.
	if (bAtSplineStart && NumPts > 2)
	{
		return;
	}

	const double TipS = bAtSplineStart ? 0.0 : GetSplineLength();
	// EndConnection: size tangents from TARGET mouth width (subset bundle), not the still-narrow tip.
	TArray<int32> MouthLaneIndices;
	MouthLaneIndices.Add(Anchor.LaneIndex);
	for (const int32 BundledLaneIndex : Anchor.BundledLaneIndices)
	{
		MouthLaneIndices.AddUnique(BundledLaneIndex);
	}
	const double TargetMouthHalf = SourceSpline->GetRoadHalfWidthForLaneIndicesAtS(
		Anchor.AnchorS, MouthLaneIndices);
	const double HalfWidthCm = FMath::Max3(
		GetRoadHalfWidthAtS(TipS),
		TargetMouthHalf,
		MetaLandRoad::DefaultRoadLaneWidth * 0.5);
	// Wider mouths need longer approach chord or the tip looks short / angled off the rails.
	const double MinAlongCm = FMath::Clamp(HalfWidthCm * 0.55, 80.0, 450.0);
	const FVector MouthPinned = MouthLoc;
	SetLocationAtSplinePoint(TipIndex, MouthPinned, ESplineCoordinateSpace::World, false);

	// Begin leave = MouthOutward; End arrive into mouth = -MouthOutward (MetaRoad FitSplineToEnd).
	const FVector TipDir = bAtSplineStart ? MouthOutward : (-MouthOutward);

	if (NumPts >= 2)
	{
		const int32 NeighborIndex = bAtSplineStart ? 1 : (TipIndex - 1);
		FVector NeighborLoc = GetLocationAtSplinePoint(NeighborIndex, ESplineCoordinateSpace::World);

		// Half-space nudge so tip leave/arrive is not a kink against the mouth tangent.
		// Start: neighbor must sit along +MouthOutward. End: along +MouthOutward (approach from outside).
		if (NumPts <= 2 || !bAtSplineStart)
		{
			const double DistToNeighbor = FVector::Dist2D(MouthPinned, NeighborLoc);
			const double Along = FVector::DotProduct(NeighborLoc - MouthPinned, MouthOutward);
			// Far opposite mouth of a 2-point connector — do not pull it onto this outward.
			if (DistToNeighbor <= MinAlongCm * 2.0 && Along < MinAlongCm * 0.35)
			{
				const FVector Lateral = NeighborLoc - MouthPinned - MouthOutward * Along;
				NeighborLoc = MouthPinned + MouthOutward * MinAlongCm + Lateral;
				NeighborLoc.Z = GetLocationAtSplinePoint(NeighborIndex, ESplineCoordinateSpace::World).Z;
				SetLocationAtSplinePoint(NeighborIndex, NeighborLoc, ESplineCoordinateSpace::World, false);
			}
		}

		const double ChordToNeighbor = FVector::Dist2D(MouthPinned, NeighborLoc);
		const double TangentSize = FMath::Max(ChordToNeighbor, MinAlongCm);
		SetSplinePointType(TipIndex, ESplinePointType::CurveCustomTangent, false);
		if (bAtSplineStart)
		{
			const FVector MouthArrive = GetArriveTangentAtSplinePoint(TipIndex, ESplineCoordinateSpace::World);
			SetTangentsAtSplinePoint(
				TipIndex,
				MouthArrive,
				TipDir * TangentSize,
				ESplineCoordinateSpace::World,
				false);
		}
		else
		{
			// MetaRoad FitSplineToEnd: Arrive + Leave both along connection into the mouth.
			SetTangentsAtSplinePoint(
				TipIndex,
				TipDir * TangentSize,
				TipDir * TangentSize,
				ESplineCoordinateSpace::World,
				false);
			// Long 2-point mouth-to-mouth: keep the opposite Bezier leave. Only
			// rewrite the neighbor on a short approach stub.
			if (ChordToNeighbor <= MinAlongCm * 2.0)
			{
				SetSplinePointType(NeighborIndex, ESplinePointType::CurveCustomTangent, false);
				const FVector ToTip = (MouthPinned - NeighborLoc).GetSafeNormal2D();
				if (!ToTip.IsNearlyZero())
				{
					const FVector NeighborArrive = GetArriveTangentAtSplinePoint(
						NeighborIndex, ESplineCoordinateSpace::World);
					SetTangentsAtSplinePoint(
						NeighborIndex,
						NeighborArrive.IsNearlyZero() ? (-ToTip * TangentSize) : NeighborArrive,
						ToTip * TangentSize,
						ESplineCoordinateSpace::World,
						false);
				}
			}
		}
	}
	else
	{
		SetSplinePointType(TipIndex, ESplinePointType::CurveCustomTangent, false);
		SetTangentAtSplinePoint(
			TipIndex,
			TipDir * 200.0,
			ESplineCoordinateSpace::World,
			false);
	}

	UpdateSpline();
}

// =============================================================================
// [KEY-ALGO] BlendEndWidthToTargetMouth
// 2合1：MinLane=1 → Left[0]+Right[0] 只混 width，ROffset≡0。
// 粉中线 = mesh 几何中心，头尾口对齐，不再用 EvalLaneROffset 的融合偏移。
// =============================================================================
bool UMetaLandRoadSplineComponent::BlendEndWidthToTargetMouth(
	const FMetaLandRoadDrawAnchor& TargetAnchor,
	EMetaLandRoadEndWidthBlendSpan SpanMode,
	double BlendLengthCm)
{
	UMetaLandRoadSplineComponent* TargetSpline = TargetAnchor.SourceSpline.Get();
	if (!IsValid(TargetSpline) || !TargetAnchor.IsValid() || GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	TArray<int32> TargetLaneIndices;
	TArray<float> TargetLaneWidths;
	if (!MetaLandRoadProfile::GetDrawAnchorBundleLaneWidths(
		TargetSpline, TargetAnchor, TargetLaneIndices, TargetLaneWidths))
	{
		return false;
	}

	TArray<float> TargetLeftWidths;
	TArray<float> TargetRightWidths;
	for (int32 Index = 0; Index < TargetLaneIndices.Num(); ++Index)
	{
		const float W = FMath::Max(TargetLaneWidths.IsValidIndex(Index) ? TargetLaneWidths[Index] : 1.0f, 1.0f);
		if (TargetLaneIndices[Index] > 0)
		{
			TargetRightWidths.Add(W);
		}
		else if (TargetLaneIndices[Index] < 0)
		{
			TargetLeftWidths.Add(W);
		}
	}

	TArray<float> SourceLeftWidths;
	TArray<float> SourceRightWidths;
	CollectUniqueFaceWidths(this, SourceLeftWidths, SourceRightWidths);

	const int32 SourceLaneCount = SourceLeftWidths.Num() + SourceRightWidths.Num();
	const int32 TargetLaneCount = TargetLeftWidths.Num() + TargetRightWidths.Num();

	const float SourceMouthSum = FMath::Max(
		SumFaceWidths(SourceLeftWidths) + SumFaceWidths(SourceRightWidths), 1.0f);
	const float TargetMouthWidthSum = FMath::Max(
		SumFaceWidths(TargetLeftWidths) + SumFaceWidths(TargetRightWidths), 1.0f);

	auto LogicalLaneCount = [](int32 FaceCount, float TotalW) -> int32
	{
		if (FaceCount <= 1)
		{
			return 1;
		}
		// 1-lane that EnsureBothFaces split into Left+Right halves still counts as 1.
		if (FaceCount == 2
			&& TotalW <= static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth) * 1.35f)
		{
			return 1;
		}
		return FaceCount;
	};
	const int32 MinLaneCount = FMath::Max(
		1,
		FMath::Min(
			LogicalLaneCount(SourceLaneCount, SourceMouthSum),
			LogicalLaneCount(TargetLaneCount, TargetMouthWidthSum)));

	(void)SpanMode;
	(void)BlendLengthCm;

	if (MinLaneCount <= 1)
	{
		// 2合1 / N合1：按最小车道=1 的中线切开，左右各 1 条，只混 width。
		// 左右半宽相同 → EvalLaneROffset 的 0 就是 mesh 几何中心，不再靠 ROffset 融合。
		const float StartHalf = SourceMouthSum * 0.5f;
		const float EndHalf = TargetMouthWidthSum * 0.5f;
		ApplyMinLaneLeftRightWidthBlend(this, StartHalf, StartHalf, EndHalf, EndHalf);
	}
	else if (SourceLeftWidths.Num() > 0 && SourceRightWidths.Num() > 0)
	{
		FMetaLandRoadLayout& Layout = GetRoadLayoutMutable();
		Layout.ROffset.Reset();
		Layout.ROffset.AddKey(0.0f, 0.0f);
		UpdateLaneSectionBounds();
		UpdateRoadLayout();
	}

	const double LenFinal = GetSplineLength();
	const float StartR = 0.0f;
	const float EndR = 0.0f;

	UpdateLaneSectionBounds();
	UpdateSpline();

	FVector EndLeft = FVector::ZeroVector;
	FVector EndRight = FVector::ZeroVector;
	FVector EndCenter = FVector::ZeroVector;
	FVector EndOut = FVector::ZeroVector;
	const bool bHaveEndFrame = MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		TargetSpline, TargetAnchor, EndLeft, EndRight, EndCenter, EndOut);
	const FVector TipWorld = GetLocationAtSplinePoint(
		GetNumberOfSplinePoints() - 1, ESplineCoordinateSpace::World);
	const double TipHalf = GetRoadHalfWidthAtS(LenFinal);
	const double TargetHalf = FVector::Dist2D(EndLeft, EndRight) * 0.5;
	const double CenterErr = bHaveEndFrame ? FVector::Dist2D(TipWorld, EndCenter) : -1.0;
	const double WidthErr = bHaveEndFrame ? FMath::Abs(TipHalf - TargetHalf) : -1.0;
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[MetaLandRoad] BlendEndWidth srcSum=%.1f tgtSum=%.1f lanes=%d startR=%.1f endR=%.1f tipHalf=%.1f tgtHalf=%.1f centerErr=%.1f widthErr=%.1f"),
		SourceMouthSum,
		TargetMouthWidthSum,
		MinLaneCount,
		StartR,
		EndR,
		TipHalf,
		TargetHalf,
		CenterErr,
		WidthErr);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991141,
			6.0f,
			(CenterErr > 40.0 || WidthErr > 40.0) ? FColor::Orange : FColor::Cyan,
			FString::Printf(
				TEXT("[MetaLandRoad] Blend %d→%d  centerErr=%.0fcm widthErr=%.0fcm (src=%.0f tgt=%.0f)"),
				SourceLaneCount,
				TargetLaneCount,
				CenterErr,
				WidthErr,
				SourceMouthSum,
				TargetMouthWidthSum));
	}
	if (CenterErr > 40.0 || WidthErr > 40.0)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[MetaLandRoad] BlendEndWidth mismatch: tip=%s targetCenter=%s tipHalf=%.1f tgtHalf=%.1f"),
			*TipWorld.ToCompactString(),
			*EndCenter.ToCompactString(),
			TipHalf,
			TargetHalf);
	}
	return true;
}

bool UMetaLandRoadSplineComponent::ConnectRoadToDrawAnchor(
	const FMetaLandRoadDrawAnchor& SourceAnchor,
	bool bAttachAtSplineStart)
{
	UMetaLandRoadSplineComponent* SourceSpline = SourceAnchor.SourceSpline.Get();
	if (!IsValid(SourceSpline) || !SourceAnchor.IsValid() || SourceSpline == this)
	{
		return false;
	}

	// Ensure lane Connection objects exist on the source.
	SourceSpline->GetRoadLayoutMutable().UpdateLayout(SourceSpline);
	GetRoadLayoutMutable().UpdateLayout(this);

	TArray<int32> SourceLaneIndices;
	SourceLaneIndices.Add(SourceAnchor.LaneIndex);
	for (int32 BundledLaneIndex : SourceAnchor.BundledLaneIndices)
	{
		SourceLaneIndices.AddUnique(BundledLaneIndex);
	}
	SourceLaneIndices.Sort([](int32 A, int32 B)
	{
		return FMath::Abs(A) < FMath::Abs(B);
	});

	UMetaLandRoadConnection* PrimaryRoadConnection = bAttachAtSplineStart
		? GetPredecessorConnection()
		: GetSuccessorConnection();
	if (!IsValid(PrimaryRoadConnection))
	{
		return false;
	}

	auto ResolveSourceLaneConnection = [&SourceAnchor, SourceSpline](int32 LaneIndex) -> UMetaLandRoadLaneConnection*
	{
		const FMetaLandRoadLane* SourceLane = SourceSpline->GetRoadLane(
			SourceAnchor.SectionIndex, LaneIndex);
		if (!SourceLane)
		{
			return nullptr;
		}
		if (SourceAnchor.MouthSide == EMetaLandRoadMouthSide::End || SourceAnchor.bFromSuccessor)
		{
			return SourceLane->SuccessorConnection;
		}
		return SourceLane->PredecessorConnection;
	};

	auto TryConnectLane = [](UMetaLandRoadConnection* RoadConnection, UMetaLandRoadLaneConnection* SourceLaneConnection) -> bool
	{
		if (!IsValid(RoadConnection) || !IsValid(SourceLaneConnection) || !SourceLaneConnection->IsConnectionValid())
		{
			return false;
		}
		if (!RoadConnection->CanConnectTo(SourceLaneConnection))
		{
			if (RoadConnection->IsConnected()
				&& RoadConnection->OuterLaneConnection.Get() == SourceLaneConnection)
			{
				return true;
			}
			return false;
		}
		RoadConnection->ConnectTo(SourceLaneConnection);
		return RoadConnection->IsConnected();
	};

	bool bAnyConnected = false;
	bool bUsedPrimaryConnection = false;
	for (int32 SourceLaneIndex : SourceLaneIndices)
	{
		UMetaLandRoadLaneConnection* SourceLaneConnection = ResolveSourceLaneConnection(SourceLaneIndex);
		if (!SourceLaneConnection)
		{
			continue;
		}

		UMetaLandRoadConnection* RoadConnection = nullptr;
		if (!bUsedPrimaryConnection)
		{
			RoadConnection = PrimaryRoadConnection;
			bUsedPrimaryConnection = true;
		}
		else
		{
			RoadConnection = NewObject<UMetaLandRoadConnection>(this, NAME_None, RF_Transactional);
		}

		if (TryConnectLane(RoadConnection, SourceLaneConnection))
		{
			bAnyConnected = true;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[LaneAnchor] ConnectRoadToDrawAnchor Road=%s AttachStart=%d → SourceLane=%d Mouth=%s Connected=%d"),
				*GetName(),
				bAttachAtSplineStart ? 1 : 0,
				SourceLaneIndex,
				SourceAnchor.MouthSide == EMetaLandRoadMouthSide::End ? TEXT("End") : TEXT("Start"),
				1);
		}
	}

	return bAnyConnected;
}

FVector UMetaLandRoadSplineComponent::EvalLanePosition(int SectionIndex, int LaneIndex, double S, double Alpha, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float SParam = GetInputKeyValueAtDistanceAlongSpline(S);
	const FVector Loc = GetLocationAtSplineInputKey(SParam, CoordinateSpace);

	double ROffset = EvalROffset(S);
	if (LaneIndex != LANE_INDEX_NONE)
	{
		ROffset += RoadLayout.Sections[SectionIndex].EvalLaneROffset(LaneIndex, S, Alpha);
	}

	const FMetaLandRoadLaneSection& Section = RoadLayout.Sections[SectionIndex];
	(void)Section;
	// Always evaluate RightVector at sample S — freezing at Section.SOffset skews lane/
	// section guides on curves (junction mouths looked axis-aligned / 切线不对).
	const FVector RVector = GetRightVectorAtDistanceAlongSpline(S, CoordinateSpace);
	return Loc + RVector * ROffset;
}

float UMetaLandRoadSplineComponent::ClosestKeyToSegment(float Key1, float Key2, const FVector& A1, const FVector& A2) const
{
	if (Key2 <= Key1)
	{
		return Key1;
	}

	FVector SubsegmentStart = GetLocationAtSplineInputKey(Key1, ESplineCoordinateSpace::World);
	double ClosestDistance = TNumericLimits<double>::Max();
	FVector OutBestLocation = SubsegmentStart;

	const int32 NumSubdivisionsPerSegm = 16;
	int32 NumSubdivisions = NumSubdivisionsPerSegm * (Key2 - Key1) + .5f;
	if (NumSubdivisions <= 1)
	{
		NumSubdivisions = 2;
	}
	const float StepLen = (Key2 - Key1) / NumSubdivisions;

	for (int32 Step = 1; Step <= NumSubdivisions; Step++)
	{

		const float Key = Key1 + Step * StepLen;
		const FVector SubsegmentEnd = GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);

		FVector SplineClosest;
		FVector RayClosest;
		FMath::SegmentDistToSegmentSafe(SubsegmentStart, SubsegmentEnd, A1, A2, SplineClosest, RayClosest);

		const double Distance = FVector::DistSquared(SplineClosest, RayClosest);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			OutBestLocation = SplineClosest;
		}
		SubsegmentStart = SubsegmentEnd;
	}

	return FindInputKeyClosestToWorldLocation(OutBestLocation);
}

float UMetaLandRoadSplineComponent::ClosestKeyToSegment2(float S1, float S2, const FVector& A1, const FVector& A2) const
{
	const float Key1 = GetInputKeyValueAtDistanceAlongSpline(S1);
	const float Key2 = GetInputKeyValueAtDistanceAlongSpline(S2);
	return ClosestKeyToSegment(Key1, Key2, A1, A2);
}
