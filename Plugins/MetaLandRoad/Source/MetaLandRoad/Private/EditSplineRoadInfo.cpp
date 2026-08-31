#include "EditSplineRoadInfo.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadSplineComponent.h"
#include "RenderingThread.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "WaterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "WorldEditorSettings.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#endif
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHI.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "LandscapeDataAccess.h"
#include "PixelShaderUtils.h"
#include "Algo/Reverse.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditSplineRoadInfo)
#define LOCTEXT_NAMESPACE "EditPolyLine"

static int32 FindLaneSectionIndexAtWorldS(const UMetaLandRoadSplineComponent* Spline, double WorldS)
{
	int32 BestIndex = 0;
	const TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSections();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		if (WorldS + KINDA_SMALL_NUMBER >= Sections[SectionIndex].SOffset)
		{
			BestIndex = SectionIndex;
		}
	}
	return BestIndex;
}

static bool EvalRoadLandscapeAtWorldS(
	const UMetaLandRoadSplineComponent* Spline,
	double WorldS,
	FMetaLandRoadLandscapeAttributeValue& OutValue)
{
	if (!Spline || Spline->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const int32 SectionIndex = FindLaneSectionIndexAtWorldS(Spline, WorldS);
	return Spline->GetLaneSection(SectionIndex).EvalLandscapeAt(
		WorldS - Spline->GetLaneSection(SectionIndex).SOffset,
		OutValue);
}

static bool SampleRoadLandscapeAttribute(AMoKuEditBaseActor* Actor, FMetaLandRoadLandscapeAttributeValue& OutValue)
{
	AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Actor);
	if (!Road)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent();
	if (!Spline)
	{
		return false;
	}

	return EvalRoadLandscapeAtWorldS(Spline, Spline->GetSplineLength() * 0.5, OutValue);
}

struct FRoadProfileSampleGPU
{
	FVector4f PosS = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f RightLR = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f Falloff = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f Extra = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
};

class FRoadLandscapeProfilePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRoadLandscapeProfilePS);
	SHADER_USE_PARAMETER_STRUCT(FRoadLandscapeProfilePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, T_Combined)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRoadProfileSample>, Samples)
		SHADER_PARAMETER(uint32, NumSamples)
		SHADER_PARAMETER(FIntPoint, RTSize)
		SHADER_PARAMETER(FIntPoint, StampSize)
		SHADER_PARAMETER(FMatrix44f, RenderAreaToWorld)
		SHADER_PARAMETER(float, LandscapeZScale)
		SHADER_PARAMETER(float, LandscapeCmPerQuad)
		SHADER_PARAMETER(FVector2f, RoadBoundsMin)
		SHADER_PARAMETER(FVector2f, RoadBoundsMax)
		SHADER_PARAMETER(uint32, WeightMode)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRoadLandscapeProfilePS, "/Plugin/MetaLandRoad/Private/RoadLandscapeProfile.usf", "RoadLandscapeProfilePS", SF_Pixel);

class FRoadWeightBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRoadWeightBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FRoadWeightBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, T_Weight)
		SHADER_PARAMETER(FIntPoint, RTSize)
		SHADER_PARAMETER(FIntPoint, BlurAxis)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRoadWeightBlurPS, "/Plugin/MetaLandRoad/Private/RoadLandscapeProfile.usf", "RoadWeightBlurPS", SF_Pixel);

static bool EnqueueRoadWeightBlur(AWorldSceneManagement* Owner, UTextureRenderTarget2D*& InOutWriteRT);

static constexpr float GRoadProfileSampleStepCm = 100.0f;
static constexpr int32 GRoadProfileMaxSamples = 4096;

static FMatrix44f MakeVertexAlignedRenderAreaToWorld(
	const FTransform& RenderAreaWorldTransform,
	const FTransform& LandscapeTransform)
{
	const FVector Origin = RenderAreaWorldTransform.TransformPosition(FVector::ZeroVector);
	const FVector AxisX = LandscapeTransform.TransformVector(FVector::ForwardVector);
	const FVector AxisY = LandscapeTransform.TransformVector(FVector::RightVector);
	FMatrix WorldFromTexel = FMatrix::Identity;
	WorldFromTexel.SetAxis(0, AxisX);
	WorldFromTexel.SetAxis(1, AxisY);
	WorldFromTexel.SetAxis(2, FVector::UpVector);
	WorldFromTexel.SetOrigin(Origin);
	return FMatrix44f(WorldFromTexel);
}

static float ProfileRibbonHalfForPad(const FRoadProfileSampleGPU& Sample)
{
	const float HalfL = FMath::Max(Sample.RightLR.Z, 0.0f);
	const float HalfR = FMath::Max(Sample.RightLR.W, 0.0f);
	const float Thin = FMath::Min(HalfL, HalfR);
	const float Fat = FMath::Max(HalfL, HalfR);
	// Edge-ring hub fill uses one huge inward half. Pad only the thin outward band.
	if (Fat > Thin * 3.0f)
	{
		return Thin;
	}
	return Fat;
}

static FIntRect ComputeWorldBoxPixelRect(
	const FVector2f& WorldMin,
	const FVector2f& WorldMax,
	const FMatrix44f& RenderAreaToWorld,
	const FIntPoint& ClampSize)
{
	if (ClampSize.X <= 0 || ClampSize.Y <= 0)
	{
		return FIntRect();
	}

	const FMatrix44f WorldToTexel = RenderAreaToWorld.Inverse();
	const FVector3f Corners[4] = {
		FVector3f(WorldMin.X, WorldMin.Y, 0.0f),
		FVector3f(WorldMax.X, WorldMin.Y, 0.0f),
		FVector3f(WorldMax.X, WorldMax.Y, 0.0f),
		FVector3f(WorldMin.X, WorldMax.Y, 0.0f)
	};

	float MinX = TNumericLimits<float>::Max();
	float MinY = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	float MaxY = TNumericLimits<float>::Lowest();
	for (const FVector3f& Corner : Corners)
	{
		const FVector3f Texel = WorldToTexel.TransformPosition(Corner);
		MinX = FMath::Min(MinX, Texel.X);
		MinY = FMath::Min(MinY, Texel.Y);
		MaxX = FMath::Max(MaxX, Texel.X);
		MaxY = FMath::Max(MaxY, Texel.Y);
	}

	const int32 X0 = FMath::Clamp(FMath::FloorToInt(MinX), 0, ClampSize.X);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(MinY), 0, ClampSize.Y);
	const int32 X1 = FMath::Clamp(FMath::CeilToInt(MaxX) + 1, 0, ClampSize.X);
	const int32 Y1 = FMath::Clamp(FMath::CeilToInt(MaxY) + 1, 0, ClampSize.Y);
	return FIntRect(X0, Y0, X1, Y1);
}

static bool BuildRoadProfileSamples(
	AMoKuEditSplineActor* Road,
	const FTransform& LandscapeTransform,
	TArray<FRoadProfileSampleGPU>& OutSamples)
{
	OutSamples.Reset();
	if (!Road)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent();
	if (!Spline)
	{
		return false;
	}

	const float ZScale = FMath::Max(FMath::Abs(static_cast<float>(LandscapeTransform.GetScale3D().Z)), 0.0001f);

	TArray<TPair<double, double>> Spans;
	Road->GetJunctionMeshGenerationSpans(Spans);
	if (Spans.Num() == 0)
	{
		const double Length = Spline->GetSplineLength();
		if (Length <= 1.0)
		{
			return false;
		}
		Spans.Emplace(0.0, Length);
	}

	int32 SpanId = 0;
	for (const TPair<double, double>& Span : Spans)
	{
		const double SpanLength = Span.Value - Span.Key;
		if (SpanLength <= 1.0)
		{
			++SpanId;
			continue;
		}

		const int32 Num = FMath::Clamp(FMath::CeilToInt(SpanLength / GRoadProfileSampleStepCm) + 1, 2, GRoadProfileMaxSamples);
		const double Step = SpanLength / static_cast<double>(Num - 1);
		for (int32 SampleIndex = 0; SampleIndex < Num && OutSamples.Num() < GRoadProfileMaxSamples; ++SampleIndex)
		{
			const double S = Span.Key + Step * static_cast<double>(SampleIndex);
			FVector Left;
			FVector Right;
			FVector Center;
			if (!Spline->GetRoadEdgePointsAtS(S, Left, Right, Center, ESplineCoordinateSpace::World))
			{
				continue;
			}

			FVector2D Right2D(Right.X - Center.X, Right.Y - Center.Y);
			if (!Right2D.Normalize())
			{
				const float InputKey = Spline->GetInputKeyValueAtDistanceAlongSpline(static_cast<float>(S));
				const FVector SplineRight = Spline->GetRightVectorAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
				Right2D = FVector2D(SplineRight.X, SplineRight.Y);
				Right2D.Normalize();
			}

			float SideOffsetCm = 0.0f;
			float SideFalloffCm = 2000.0f;
			float HeightOffsetCm = -5.0f;
			float LayerFactor = 1.0f;
			float WeightFalloffCm = 0.0f;
			FMetaLandRoadLandscapeAttributeValue LandscapeValue;
			if (EvalRoadLandscapeAtWorldS(Spline, S, LandscapeValue))
			{
				SideOffsetCm = FMath::Max(LandscapeValue.SideOffset, 0.0f);
				SideFalloffCm = FMath::Max(LandscapeValue.SideFalloff, 0.0f);
				HeightOffsetCm = FMath::Clamp(LandscapeValue.HeightOffset, -2000.0f, 2000.0f);
				LayerFactor = FMath::Clamp(LandscapeValue.LayerFactor, 0.0f, 1.0f);
				if (LandscapeValue.RoadWeightFalloffWidth >= 1.0f)
				{
					WeightFalloffCm = FMath::Clamp(LandscapeValue.RoadWeightFalloffWidth, 1.0f, 2500.0f);
				}
			}
			const float LocalZ = static_cast<float>(LandscapeTransform.InverseTransformPosition(Center).Z);
			const float PackedHeight = static_cast<float>(LandscapeDataAccess::GetTexHeight(LocalZ));
			const float PackedHeightOffset = (HeightOffsetCm / ZScale) * LANDSCAPE_INV_ZSCALE;

			FRoadProfileSampleGPU Sample;
			Sample.PosS = FVector4f(
				static_cast<float>(Center.X),
				static_cast<float>(Center.Y),
				PackedHeight,
				static_cast<float>(S));
			Sample.RightLR = FVector4f(
				static_cast<float>(Right2D.X),
				static_cast<float>(Right2D.Y),
				static_cast<float>(FMath::Max(FVector::Dist2D(Left, Center), 1.0)),
				static_cast<float>(FMath::Max(FVector::Dist2D(Right, Center), 1.0)));
			Sample.Falloff = FVector4f(
				SideOffsetCm,
				SideFalloffCm,
				PackedHeightOffset,
				WeightFalloffCm);
			Sample.Extra = FVector4f(
				0.0f,
				static_cast<float>(SpanId),
				LayerFactor,
				static_cast<float>(Center.Z));
			OutSamples.Add(Sample);
		}
		++SpanId;
		if (OutSamples.Num() >= GRoadProfileMaxSamples)
		{
			break;
		}
	}

	return OutSamples.Num() >= 2;
}

static void ApplyRoadWeightFalloffToProfileSamples(
	TArray<FRoadProfileSampleGPU>& Samples,
	float ActorFallbackCm)
{
	const float Fallback = FMath::Max(ActorFallbackCm, 1.0f);
	for (FRoadProfileSampleGPU& Sample : Samples)
	{
		const float KeyWidth = Sample.Falloff.W;
		Sample.Falloff.X = 0.0f;
		Sample.Falloff.Y = KeyWidth >= 1.0f ? KeyWidth : Fallback;
	}
}

static uint32 GetProfileWeightMode(FName LayerName)
{
	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayer = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	return (LayerName == RoadLayer) ? 1u : 2u;
}

static void AppendUniqueRingPoint(TArray<FVector>& Ring, const FVector& Point)
{
	if (Ring.Num() == 0 || FVector::DistSquared(Ring.Last(), Point) >= 1.0)
	{
		Ring.Add(Point);
	}
}

static double SignedRingAreaXY(const TArray<FVector>& Ring)
{
	double Area = 0.0;
	for (int32 Index = 0; Index < Ring.Num(); ++Index)
	{
		const FVector& A = Ring[Index];
		const FVector& B = Ring[(Index + 1) % Ring.Num()];
		Area += static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
	}
	return Area;
}

static bool CollectMeshBoundaryRingsWorldXY(
	UDynamicMeshComponent* MeshComp,
	TArray<TArray<FVector>>& OutRings)
{
	OutRings.Reset();
	if (!MeshComp)
	{
		return false;
	}

	const UDynamicMesh* DynamicMesh = MeshComp->GetDynamicMesh();
	const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!Mesh || Mesh->EdgeCount() < 3)
	{
		return false;
	}

	TMap<int32, TArray<int32>> Adj;
	for (const int32 Eid : Mesh->EdgeIndicesItr())
	{
		if (!Mesh->IsBoundaryEdge(Eid))
		{
			continue;
		}
		const FIndex2i Verts = Mesh->GetEdgeV(Eid);
		Adj.FindOrAdd(Verts.A).AddUnique(Verts.B);
		Adj.FindOrAdd(Verts.B).AddUnique(Verts.A);
	}
	if (Adj.Num() < 3)
	{
		return false;
	}

	const FTransform Xform = MeshComp->GetComponentTransform();
	auto WorldOf = [&](int32 Vid) -> FVector
	{
		return Xform.TransformPosition(FVector(Mesh->GetVertex(Vid)));
	};
	auto EdgeKey = [](int32 A, int32 B) -> uint64
	{
		const uint32 Lo = static_cast<uint32>(FMath::Min(A, B));
		const uint32 Hi = static_cast<uint32>(FMath::Max(A, B));
		return (static_cast<uint64>(Lo) << 32) | static_cast<uint64>(Hi);
	};

	TSet<uint64> Used;
	for (const TPair<int32, TArray<int32>>& Pair : Adj)
	{
		if (Pair.Value.Num() == 0)
		{
			continue;
		}
		const int32 Start = Pair.Key;
		const int32 FirstNext = Pair.Value[0];
		if (Used.Contains(EdgeKey(Start, FirstNext)))
		{
			continue;
		}

		TArray<FVector> Loop;
		int32 Curr = Start;
		int32 Next = FirstNext;
		int32 Guard = 0;
		const int32 GuardMax = Adj.Num() + 4;
		bool bClosed = false;
		while (Guard++ < GuardMax)
		{
			Loop.Add(WorldOf(Curr));
			Used.Add(EdgeKey(Curr, Next));
			int32 Found = INDEX_NONE;
			if (const TArray<int32>* Neighbors = Adj.Find(Next))
			{
				for (const int32 Nb : *Neighbors)
				{
					if (Nb != Curr && !Used.Contains(EdgeKey(Next, Nb)))
					{
						Found = Nb;
						break;
					}
				}
				if (Found == INDEX_NONE)
				{
					for (const int32 Nb : *Neighbors)
					{
						if (Nb == Start)
						{
							Found = Start;
							bClosed = true;
							break;
						}
					}
				}
			}
			if (Found == INDEX_NONE)
			{
				Loop.Reset();
				break;
			}
			Curr = Next;
			Next = Found;
			if (Curr == Start)
			{
				bClosed = true;
				break;
			}
		}
		if (!bClosed || Loop.Num() < 3)
		{
			continue;
		}
		if (FVector::DistSquared(Loop[0], Loop.Last()) < 25.0)
		{
			Loop.Pop();
		}
		if (Loop.Num() < 3 || FMath::Abs(SignedRingAreaXY(Loop)) < 100.0)
		{
			continue;
		}
		if (SignedRingAreaXY(Loop) < 0.0)
		{
			Algo::Reverse(Loop);
		}
		OutRings.Add(MoveTemp(Loop));
	}
	return OutRings.Num() > 0;
}

static bool CollectCornerInfoRing(const AMoKuEditIntersectionActor* Junction, TArray<FVector>& OutRing)
{
	OutRing.Reset();
	if (!Junction)
	{
		return false;
	}
	for (const FCornerInfo& Info : Junction->CornerInfo)
	{
		for (const FVector3d& Point : Info.CornerPoints)
		{
			AppendUniqueRingPoint(OutRing, FVector(Point));
		}
	}
	if (OutRing.Num() > 2 && FVector::DistSquared(OutRing[0], OutRing.Last()) < 25.0)
	{
		OutRing.Pop();
	}
	return OutRing.Num() >= 3;
}

static void AppendProfileSampleFromWorld(
	TArray<FRoadProfileSampleGPU>& OutSamples,
	const FTransform& LandscapeTransform,
	const FVector& Center,
	const FVector2D& Right2D,
	float HalfL,
	float HalfR,
	float SideOffsetCm,
	float SideFalloffCm,
	float HeightOffsetCm,
	float LayerFactor,
	float S,
	float SpanId,
	float SoftArc = 0.0f);

static bool AppendCoverRingSamples(
	TArray<FRoadProfileSampleGPU>& OutSamples,
	const FTransform& LandscapeTransform,
	const TArray<FVector>& Ring,
	const FVector& HubPos,
	float SideOffsetCm,
	float SideFalloffCm,
	float HeightOffsetCm,
	int32 SpanId)
{
	const int32 RingCount = Ring.Num();
	if (RingCount < 3)
	{
		return false;
	}

	const int32 StartNum = OutSamples.Num();
	for (int32 Index = 0; Index <= RingCount; ++Index)
	{
		if (OutSamples.Num() >= GRoadProfileMaxSamples)
		{
			break;
		}
		const FVector& P = Ring[Index % RingCount];
		const FVector Center(P.X, P.Y, HubPos.Z);
		AppendProfileSampleFromWorld(
			OutSamples, LandscapeTransform, Center, FVector2D(1.0f, 0.0f), 1.0f, 1.0f,
			SideOffsetCm, SideFalloffCm, HeightOffsetCm, 1.0f, 0.0f, static_cast<float>(SpanId), 2.0f);
	}
	return OutSamples.Num() - StartNum >= 2;
}

static void AppendProfileSampleFromWorld(
	TArray<FRoadProfileSampleGPU>& OutSamples,
	const FTransform& LandscapeTransform,
	const FVector& Center,
	const FVector2D& Right2D,
	float HalfL,
	float HalfR,
	float SideOffsetCm,
	float SideFalloffCm,
	float HeightOffsetCm,
	float LayerFactor,
	float S,
	float SpanId,
	float SoftArc)
{
	const float ZScale = FMath::Max(FMath::Abs(static_cast<float>(LandscapeTransform.GetScale3D().Z)), 0.0001f);
	const float LocalZ = static_cast<float>(LandscapeTransform.InverseTransformPosition(Center).Z);
	const float PackedHeight = static_cast<float>(LandscapeDataAccess::GetTexHeight(LocalZ));
	const float PackedHeightOffset = (HeightOffsetCm / ZScale) * LANDSCAPE_INV_ZSCALE;

	FRoadProfileSampleGPU Sample;
	Sample.PosS = FVector4f(
		static_cast<float>(Center.X),
		static_cast<float>(Center.Y),
		PackedHeight,
		S);
	Sample.RightLR = FVector4f(
		static_cast<float>(Right2D.X),
		static_cast<float>(Right2D.Y),
		FMath::Max(HalfL, 1.0f),
		FMath::Max(HalfR, 1.0f));
	Sample.Falloff = FVector4f(SideOffsetCm, SideFalloffCm, PackedHeightOffset, 0.0f);
	Sample.Extra = FVector4f(SoftArc, SpanId, LayerFactor, static_cast<float>(Center.Z));
	OutSamples.Add(Sample);
}

static bool BuildJunctionProfileSamples(
	AMoKuEditIntersectionActor* Junction,
	const FTransform& LandscapeTransform,
	TArray<FRoadProfileSampleGPU>& OutSamples)
{
	OutSamples.Reset();
	if (!Junction || !Junction->JunctionData)
	{
		return false;
	}

	const FMetaLandRoadJunctionNode& Node = Junction->JunctionData->JunctionNode;
	FVector Hub = Node.AnchorWorld;
	if (Hub.IsNearlyZero())
	{
		Hub = Junction->GetActorLocation();
	}

	const float SideOffsetCm = FMath::Max(Junction->JunctionSampleEdgeOffset, 0.0f);
	const float SideFalloffCm = FMath::Max(Junction->JunctionSampleFalloffWidth, 1.0f);
	const float HeightOffsetCm = FMath::Clamp(Junction->RoadHeightmapSettings.FalloffSettings.ZOffset, -2000.0f, 2000.0f);
	const FVector HubPos(Hub.X, Hub.Y, Junction->GetActorLocation().Z);

	// Weight/height rings follow published cover islands (T-open far strip included).
	// Fig1 EdgePath is audit-only and can omit ExtraFarOuter.
	TArray<TArray<FVector>> StampRings;
	if (!CollectMeshBoundaryRingsWorldXY(Junction->DynamicMeshComponent, StampRings))
	{
		TArray<FVector> FallbackRing;
		if (CollectCornerInfoRing(Junction, FallbackRing))
		{
			StampRings.Add(MoveTemp(FallbackRing));
		}
		else if (Junction->EdgePath.Num() >= 3)
		{
			FallbackRing.Reserve(Junction->EdgePath.Num());
			for (const FVector3d& P : Junction->EdgePath)
			{
				AppendUniqueRingPoint(FallbackRing, FVector(P));
			}
			if (FallbackRing.Num() > 2 && FVector::DistSquared(FallbackRing[0], FallbackRing.Last()) < 25.0)
			{
				FallbackRing.Pop();
			}
			if (FallbackRing.Num() >= 3)
			{
				StampRings.Add(MoveTemp(FallbackRing));
			}
		}
	}

	for (int32 RingIndex = 0; RingIndex < StampRings.Num(); ++RingIndex)
	{
		AppendCoverRingSamples(
			OutSamples, LandscapeTransform, StampRings[RingIndex], HubPos,
			SideOffsetCm, SideFalloffCm, HeightOffsetCm, RingIndex);
	}
	if (OutSamples.Num() >= 2)
	{
		return true;
	}

	struct FJunctionStampArm
	{
		UMetaLandRoadSplineComponent* Spline = nullptr;
		FVector Mouth = FVector::ZeroVector;
		FVector2D Dir = FVector2D::ZeroVector;
		FVector2D Right = FVector2D::ZeroVector;
		float HalfL = 1.0f;
		float HalfR = 1.0f;
		float CutS = 0.0f;
		float Polar = 0.0f;
	};

	TArray<FJunctionStampArm> StampArms;
	auto TryAddStampArm = [&](UMetaLandRoadSplineComponent* Spline, double CutS)
	{
		if (!Spline || CutS < 0.0)
		{
			return;
		}
		FVector Left;
		FVector Right;
		FVector Center;
		if (!Spline->GetRoadEdgePointsAtS(CutS, Left, Right, Center, ESplineCoordinateSpace::World))
		{
			return;
		}
		FVector2D Right2D(Right.X - Center.X, Right.Y - Center.Y);
		if (!Right2D.Normalize())
		{
			Right2D = FVector2D(1.0, 0.0);
		}
		FVector2D Dir(Center.X - Hub.X, Center.Y - Hub.Y);
		if (!Dir.Normalize())
		{
			return;
		}
		FJunctionStampArm Arm;
		Arm.Spline = Spline;
		Arm.Mouth = Center;
		Arm.Dir = Dir;
		Arm.Right = Right2D;
		Arm.HalfL = static_cast<float>(FMath::Max(FVector::Dist2D(Left, Center), 1.0));
		Arm.HalfR = static_cast<float>(FMath::Max(FVector::Dist2D(Right, Center), 1.0));
		Arm.CutS = static_cast<float>(CutS);
		Arm.Polar = FMath::Atan2(Dir.Y, Dir.X);
		StampArms.Add(Arm);
	};

	for (const FMetaLandRoadJunctionArmRef& Arm : Node.Arms)
	{
		UMetaLandRoadSplineComponent* Spline = Arm.Spline.Get();
		TryAddStampArm(Spline, Arm.CutDistanceS);
		if (Arm.bThroughArm
			&& Arm.DepartCutS > KINDA_SMALL_NUMBER
			&& !FMath::IsNearlyEqual(Arm.DepartCutS, Arm.CutDistanceS, 10.0))
		{
			TryAddStampArm(Spline, Arm.DepartCutS);
		}
	}

	if (StampArms.Num() == 0)
	{
		for (AMoKuEditSplineActor* Linked : Junction->JunctionData->LinkedRoadActors)
		{
			if (!Linked || !Linked->GetSplineComponent())
			{
				continue;
			}

			double CutS = -1.0;
			if (Linked->EndOfJunction.Get() == Junction)
			{
				CutS = Linked->EndJunctionMeshTrimS;
			}
			else if (Linked->StartOfJunction.Get() == Junction)
			{
				CutS = Linked->StartJunctionMeshTrimS;
			}
			else
			{
				for (const FMetaLandRoadJunctionTrimZone& Zone : Linked->JunctionTrimZones)
				{
					if (Zone.Junction.Get() == Junction)
					{
						CutS = Zone.ApproachCutS >= 0.0 ? Zone.ApproachCutS : Zone.DepartCutS;
						break;
					}
				}
			}
			TryAddStampArm(Linked->GetSplineComponent(), CutS);
		}
	}

	StampArms.Sort([](const FJunctionStampArm& A, const FJunctionStampArm& B)
	{
		return A.Polar < B.Polar;
	});

	int32 SpanId = 0;
	auto ResolveMouthLandscape = [&](UMetaLandRoadSplineComponent* Spline, float CutS, float& OutOffset, float& OutFalloff)
	{
		OutOffset = SideOffsetCm;
		OutFalloff = SideFalloffCm;
		FMetaLandRoadLandscapeAttributeValue RoadVal;
		if (Spline && EvalRoadLandscapeAtWorldS(Spline, CutS, RoadVal))
		{
			OutOffset = FMath::Max(RoadVal.SideOffset, 0.0f);
			OutFalloff = FMath::Max(RoadVal.SideFalloff, 1.0f);
		}
	};

	for (const FJunctionStampArm& Arm : StampArms)
	{
		if (OutSamples.Num() + 2 > GRoadProfileMaxSamples)
		{
			break;
		}
		float MouthOffset = SideOffsetCm;
		float MouthFalloff = SideFalloffCm;
		ResolveMouthLandscape(Arm.Spline, Arm.CutS, MouthOffset, MouthFalloff);
		const FVector2D HubRight(-Arm.Dir.Y, Arm.Dir.X);
		AppendProfileSampleFromWorld(
			OutSamples, LandscapeTransform, Arm.Mouth, Arm.Right, Arm.HalfL, Arm.HalfR,
			MouthOffset, MouthFalloff, HeightOffsetCm, 1.0f, Arm.CutS, static_cast<float>(SpanId), 0.0f);
		AppendProfileSampleFromWorld(
			OutSamples, LandscapeTransform, HubPos, HubRight, Arm.HalfL, Arm.HalfR,
			SideOffsetCm, SideFalloffCm, HeightOffsetCm, 1.0f, Arm.CutS, static_cast<float>(SpanId), 0.0f);
		++SpanId;
	}

	return OutSamples.Num() >= 2;
}

AWorldSceneManagement::AWorldSceneManagement(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, LandscapeRTRes(0, 0)
, WorldSize(FVector::ZeroVector)
, LandscapeTransform(FTransform::Identity)
{
	Octree = TOctree2<FSplineSegmentOctree, FSegmentsOctreeSemantics>(FVector::ZeroVector, HALF_WORLD_MAX);
}

void AWorldSceneManagement::Initialize_Native(FTransform const& InLandscapeTransform, FIntPoint const& InLandscapeSize, FIntPoint const& InLandscapeRenderTargetSize)
{
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Updated Landscape Transform"));
	bool bNeedsFullUpdate = false;
	if (LandscapeQuads != InLandscapeSize)
	{
		LandscapeQuads = InLandscapeSize;
		bNeedsFullUpdate = true;
	}
	if (LandscapeRTRes != InLandscapeRenderTargetSize)
	{
		LandscapeRTRes = InLandscapeRenderTargetSize;
		bNeedsFullUpdate = true;
	}
	if (!InLandscapeTransform.Equals(LandscapeTransform))
	{
		LandscapeTransform = InLandscapeTransform;
		bNeedsFullUpdate = true;
	}

	if (bNeedsFullUpdate)
	{
		FVector Scale = LandscapeTransform.GetScale3D();
		WorldSize.Set(Scale.X * (float)LandscapeQuads.X, Scale.Y * (float)LandscapeQuads.Y, 0.512f);
	}
}


bool AWorldSceneManagement::AllocateRTs()
{
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		return false;
	}

	if (LandscapeRTRes.X <= 0 || LandscapeRTRes.Y <= 0)
	{
		FVector UnusedLoc, UnusedSize;
		if (!DeprecateWorldLandscapeInfo(UnusedLoc, UnusedSize))
		{
			LandscapeRTRes = FIntPoint(1024, 1024);
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] AllocateRTs: LandscapeRTRes was 0 — using fallback %dx%d"),
				LandscapeRTRes.X, LandscapeRTRes.Y);
		}
	}

	if (WeightmapRTA && WeightmapRTA->RenderTargetFormat != RTF_R8)
	{
		WeightmapRTA = nullptr;
	}
	if (WeightmapRTB && WeightmapRTB->RenderTargetFormat != RTF_R8)
	{
		WeightmapRTB = nullptr;
	}
	WeightmapRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(WeightmapRTA, TEXT("RoadWeightmapRTA"), LandscapeRTRes, RTF_R8);
	WeightmapRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(WeightmapRTB, TEXT("RoadWeightmapRTB"), LandscapeRTRes, RTF_R8);
	return (WeightmapRTA != nullptr) && (WeightmapRTB != nullptr);
}

#if WITH_EDITOR
void AWorldSceneManagement::DebugForceAllocateRenderTargets()
{
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] DebugForceAllocateRenderTargets skipped (CDO/template)"));
		return;
	}

	FVector UnusedLoc, UnusedSize;
	DeprecateWorldLandscapeInfo(UnusedLoc, UnusedSize);

	const bool bOk = AllocateRTs();
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] DebugForceAllocateRenderTargets success=%d LandscapeRTRes=%dx%d"),
		bOk ? 1 : 0, LandscapeRTRes.X, LandscapeRTRes.Y);

	auto LogRT = [](const TCHAR* Name, const UTextureRenderTarget2D* RT)
	{
		if (RT)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape]   %s = %s (%dx%d) outer=%s"),
				Name, *RT->GetName(), RT->SizeX, RT->SizeY, *GetNameSafe(RT->GetOuter()));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RoadLandscape]   %s = None"), Name);
		}
	};
	LogRT(TEXT("WeightmapRTA"), WeightmapRTA);
	LogRT(TEXT("WeightmapRTB"), WeightmapRTB);
	LogRT(TEXT("CSCombinedWriteRT"), CSCombinedWriteRT);
	LogRT(TEXT("CSCombinedWriteRTB"), CSCombinedWriteRTB);

	// Log-only preview: opening Transient RTs in AssetEditor during Live Coding is unsafe.
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RT slots may still show None in Details (Transient). Use Log sizes above; content fills after WorldEditor paint."));
}

void AWorldSceneManagement::DebugDumpProfileRT()
{
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		return;
	}

	UTextureRenderTarget2D* ProfileRT = CSCombinedWriteRT ? CSCombinedWriteRT : CSCombinedWriteRTB;
	if (!ProfileRT)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] DebugDumpProfileRT: no profile RT yet. Drag a road, then click again."));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Live profile RT = %s  (%dx%d)"),
		*ProfileRT->GetPathName(), ProfileRT->SizeX, ProfileRT->SizeY);
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditors->OpenEditorForAsset(ProfileRT);
		}
	}
}
#endif

void AWorldSceneManagement::ComputeWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector)
{

	FVector LandscapeScale = LandscapeTransform.GetScale3D();
	OutRTWorldSizeVector = FVector(LandscapeRTRes) * LandscapeScale;
	OutRTWorldSizeVector.Z = 1.0f;
	OutRTWorldLocation = LandscapeTransform.GetLocation();
	OutRTWorldLocation -= FVector(LandscapeScale.X, LandscapeScale.Y, 0.0f) * 0.5f;

}

bool AWorldSceneManagement::DeprecateWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector)
{
#if WITH_EDITOR
	if (ALandscape* Landscape = GetOwningLandscape())
	{
		FIntPoint LandscapeSize;
		if (Landscape->ComputeLandscapeLayerBrushInfo(LandscapeTransform, LandscapeSize, LandscapeRTRes))
		{
			ComputeWorldLandscapeInfo(OutRTWorldLocation, OutRTWorldSizeVector);
			return true;
		}
	}

	return false;
#endif // WITH_EDITOR
}


void AWorldSceneManagement::PostLoad()
{
	Super::PostLoad();
	if (GetClass()->ClassGeneratedBy == nullptr)
	{
		SetupDefaultMaterials();
	}
	if (UWorld* World = this->GetWorld())
	{
		ULevel* Level = this->GetLevel();
		if (Level)
		{
			TActorIterator<AWorldSceneManagement> It(World);
			if (AWorldSceneManagement* WorldRoadZone = It ? *It : nullptr)
			{
				FVector RTWorldLocation, RTWorldSizeVector;
				if (DeprecateWorldLandscapeInfo(RTWorldLocation, RTWorldSizeVector))
				{
					SetMPCParams();
				}
			}
		}
	}
	//OnLevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddLambda([this](ULevel* Level, UWorld* World)
	//	{

	//	});


}

void AWorldSceneManagement::GetRenderDependencies(TSet<UObject*>& OutDependencies)
{
	Super::GetRenderDependencies(OutDependencies);
}
void AWorldSceneManagement::BeginDestroy()
{
	Super::BeginDestroy();

	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldHandle);
	OnLevelAddedToWorldHandle.Reset();
}


void AWorldSceneManagement::SetMPCParams()
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* LandscapeCollection = nullptr;
	if (const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>())
	{
		LandscapeCollection = Settings->LandscapeMaterialParameterCollection.LoadSynchronous();
	}
	if (LandscapeCollection && World != nullptr)
	{

			FVector RTWorldLocation, RTWorldSizeVector;
			ComputeWorldLandscapeInfo(RTWorldLocation, RTWorldSizeVector);

			UMaterialParameterCollectionInstance* LandscapeCollectionInstance = World->GetParameterCollectionInstance(LandscapeCollection);
			check(LandscapeCollectionInstance != nullptr);


			bool bIsFound = true;

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("RTResX")), (float)LandscapeRTRes.X))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTResX\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("RTResY")), (float)LandscapeRTRes.Y))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTResY\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LSQuadsX")), (float)LandscapeQuads.X))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LSQuadsX\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LSQuadsY")), (float)LandscapeQuads.Y))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LSQuadsY\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("WorldSizeX")), WorldSize.X))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"WorldSizeX\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("WorldSizeY")), WorldSize.Y))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"WorldSizeY\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetVectorParameterValue(FName(TEXT("LandscapeLocation")), FLinearColor(LandscapeTransform.GetLocation())))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LandscapeLocation\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LandscapeZLocation")), LandscapeTransform.GetLocation().Z))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LandscapeZLocation\" on Landscape MaterialParameterCollection"));
			}
			// TODO [jonathan.bard] : find out what this 128.0f corresponds to and put in a constant : ZSCALE in LandscapeLayersPS.usf maybe ??
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LandscapeZScale")), LandscapeTransform.GetScale3D().Z/128))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LandscapeZScale\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetVectorParameterValue(FName(TEXT("RTWorldSize")), FLinearColor(RTWorldSizeVector)))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTWorldSize\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetVectorParameterValue(FName(TEXT("RTWorldLocation")), FLinearColor(RTWorldLocation)))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTWorldLocation\" on Landscape MaterialParameterCollection"));
			}

	}
}

void AWorldSceneManagement::OctreeAddRoad(AMoKuEditSplineActor* InElement)
{
	if (!InElement)return;
	if (!EditRoadsList.Contains(InElement))
	{
		EditRoadsList.Add(InElement);
		//OctreeAddElement(InElement->LeftCurve);
		//OctreeAddElement(InElement->RightCurve);
		//OctreeAddElement(InElement->SplineComponent);

	}
}

void AWorldSceneManagement::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

}
bool AWorldSceneManagement::BrushRenderSetup()
{
	if (!AllocateRTs())
	{
		return false;
	}
	SetMPCParams();
	return true;
}

void AWorldSceneManagement::OnConstruction(const FTransform& Transform)
{

	Super::OnConstruction(Transform);
#if WITH_EDITOR
	if (ALandscape* Landscape = GetOwningLandscape())
	{
		if (Landscape->GetBrushLayer(this) == INDEX_NONE)
		{
			SetTargetLandscape(Landscape);
		}
	}
#endif 
	SetupDefaultMaterials();
	SetMPCParams();
};
void AWorldSceneManagement::AddDependencyIfValid(UObject* Dependency, TSet<UObject*>& OutDependencies)
{
	if (IsValid(Dependency))
	{
		OutDependencies.Add(Dependency);
	}
}

void AWorldSceneManagement::SetupDefaultMaterials()
{
}

void AWorldSceneManagement::OctreeRemoveRoad(AMoKuEditSplineActor* InElement)
{
	//if (!InElement) return;
	//if (EditRoadsList.Contains(InElement))
	//{
	//	OctreeRemoveElement(InElement->LeftCurve);
	//	OctreeRemoveElement(InElement->RightCurve);
	//	OctreeRemoveElement(InElement->SplineComponent);
	//	//EditRoadsList.Remove(InElement);
	//}

}

static FBox ComputeRoadHeightInfluenceBounds(AMoKuEditBaseActor* Actor)
{
	if (!Actor)
	{
		return FBox(ForceInit);
	}

	FBox Box = Actor->GetComponentsBoundingBox(/*bNonColliding=*/true, /*bIncludeFromChildActors=*/false);
	if (!Box.IsValid)
	{
		return Box;
	}

	float PadCm = 400.0f;
	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Actor))
	{
		FMetaLandRoadLandscapeAttributeValue LandscapeValue;
		if (SampleRoadLandscapeAttribute(Road, LandscapeValue))
		{
			PadCm = FMath::Max(
				FMath::Max(LandscapeValue.SideOffset, 0.0f) + FMath::Max(LandscapeValue.SideFalloff, 0.0f),
				PadCm);
		}
		const float HeightSpanCm = FMath::Max(static_cast<float>(Box.GetExtent().Z) * 2.0f, 0.0f);
		PadCm = FMath::Max(PadCm, HeightSpanCm / 0.268f);
		PadCm += 400.0f;
	}
	else if (const AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(Actor))
	{
		PadCm = FMath::Max(Junction->JunctionSampleEdgeOffset, 0.0f)
			+ FMath::Max(Junction->JunctionSampleFalloffWidth, 1.0f)
			+ 400.0f;
	}
	return Box.ExpandBy(FVector(PadCm, PadCm, 0.0f));
}

AActor* AWorldSceneManagement::ResolveHeightStampFocusActor() const
{
	if (bForceFullHeightStamp)
	{
		return nullptr;
	}
	return GetLandscapeStampFocusActor();
}

void AWorldSceneManagement::CollectHeightStampRoads(TArray<AMoKuEditBaseActor*>& InOutRoads) const
{
	// Keep every actor in the list. Per-stamp now scissors to influence∩RenderArea
	// so neighbors still write Combined and do not carve cliffs.
	(void)InOutRoads;
}

void AWorldSceneManagement::BuildLayerRoadList(
	bool bHeightmapRender,
	FName WeightmapLayerName,
	TArray<AMoKuEditBaseActor*>& OutRoads)
{
	OutRoads.Reset();
	GetRoadActors(AMoKuEditBaseActor::StaticClass(), OutRoads);
	OutRoads.RemoveAll([](const AMoKuEditBaseActor* Road)
	{
		return Road == nullptr;
	});

	bool bSoftEdgeRepaired = false;
	for (AMoKuEditBaseActor* Road : OutRoads)
	{
		if (Road->EnsureDefaultTerrainInfluenceSettings())
		{
			bSoftEdgeRepaired = true;
		}
	}
	if (bSoftEdgeRepaired)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Soft-edge repair applied"));
	}

	if (!bHeightmapRender)
	{
		OutRoads.RemoveAll([&WeightmapLayerName](const AMoKuEditBaseActor* Road)
		{
			return !Road->AffectsWeightmapLayer(WeightmapLayerName);
		});
	}

	CollectHeightStampRoads(OutRoads);
}

FBox AWorldSceneManagement::ComputeHeightStampWorldBounds(const TArray<AMoKuEditBaseActor*>& Roads) const
{
	FBox Union(ForceInit);
	for (AMoKuEditBaseActor* Road : Roads)
	{
		const FBox Box = ComputeRoadHeightInfluenceBounds(Road);
		if (Box.IsValid)
		{
			Union += Box;
		}
	}
	return Union;
}

UTextureRenderTarget2D* AWorldSceneManagement::RenderLayer_Native(const FLandscapeBrushParameters& InParameters)
{

	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldSceneManagement::RenderLayer_Native);

	const bool bHeightmapRender = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;

	LandscapeRTRef = InParameters.CombinedResult;

	FEditorBrushRenderContext BrushRenderContext;
	BrushRenderContext.bHeightmapRender = bHeightmapRender;
	BrushRenderContext.WeightmapLayerName = InParameters.WeightmapLayerName;
	BrushRenderContext.RenderAreaWorldTransform = InParameters.RenderAreaWorldTransform;
	BrushRenderContext.RenderAreaSize = InParameters.RenderAreaSize;

	if (!BrushRenderSetup())
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] RenderLayer ABORT: BrushRenderSetup failed (RT allocation)"));
		return nullptr;
	}

	if (!BrushRenderContext.bHeightmapRender)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, WeightmapRTA, FLinearColor::Black);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, WeightmapRTB, FLinearColor::Black);
	}

	UpdateActors(/*bInTriggerEvents=*/false);
	UpdateAffectedWeightmaps();

	TArray<AMoKuEditBaseActor*> RoadList;
	BuildLayerRoadList(bHeightmapRender, BrushRenderContext.WeightmapLayerName, RoadList);

	int32 RoadsDrawn = 0;
	for (AMoKuEditBaseActor* SingleRoad : RoadList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SingleRoad);
		FBrushActorRenderContext BrushActorRenderContext(SingleRoad);
		const int32 RTBefore = BrushRenderContext.RTIndex;
		RenderBrushActorContext(BrushRenderContext, BrushActorRenderContext);
		if (bHeightmapRender || BrushRenderContext.RTIndex > RTBefore)
		{
			++RoadsDrawn;
		}
	}

	UTextureRenderTarget2D* ReturnRT = nullptr;
	if (bHeightmapRender)
	{
		ReturnRT = BrushRenderContext.CSHeightWriteRT
			? BrushRenderContext.CSHeightWriteRT
			: LandscapeRTRef.Get();
	}
	else
	{
		ReturnRT = BrushRenderContext.CSWeightWriteRT
			? BrushRenderContext.CSWeightWriteRT
			: LandscapeRTRef.Get();
		if (ReturnRT && EnqueueRoadWeightBlur(this, ReturnRT))
		{
			BrushRenderContext.CSWeightWriteRT = ReturnRT;
		}
	}
	if (!bHeightmapRender && RoadsDrawn == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Weight layer '%s' drew 0 roads — passthrough Combined (do not zero Grass)."),
			*InParameters.WeightmapLayerName.ToString());
		return InParameters.CombinedResult;
	}
	if (bHeightmapRender)
	{
		bForceFullHeightStamp = false;
	}
	return ReturnRT;
}

static bool EnqueueRoadLandscapeProfileStamp(
	AWorldSceneManagement* Owner,
	const FTransform& RenderAreaWorldTransform,
	const FIntPoint& RenderAreaSize,
	UTextureRenderTarget2D*& InOutWriteRT,
	TArray<FRoadProfileSampleGPU>&& Samples,
	const FString& DebugName,
	uint32 WeightMode = 0)
{
	if (!Owner || Samples.Num() < 2)
	{
		return false;
	}

	UTextureRenderTarget2D* CombinedRT = Owner->LandscapeRTRef;
	if (!CombinedRT || Owner->LandscapeRTRes.X <= 0 || Owner->LandscapeRTRes.Y <= 0)
	{
		return false;
	}

	const FIntPoint CombinedSize(CombinedRT->SizeX, CombinedRT->SizeY);
	const FIntPoint Size = (CombinedSize.X > 0 && CombinedSize.Y > 0) ? CombinedSize : Owner->LandscapeRTRes;

	UTextureRenderTarget2D* ReadRT = InOutWriteRT
		? InOutWriteRT
		: CombinedRT;
	TObjectPtr<UTextureRenderTarget2D>& WriteSlot = (WeightMode > 0)
		? ((ReadRT == Owner->WeightmapRTA) ? Owner->WeightmapRTB : Owner->WeightmapRTA)
		: ((ReadRT == Owner->CSCombinedWriteRT) ? Owner->CSCombinedWriteRTB : Owner->CSCombinedWriteRT);
	const TCHAR* WriteName = (WeightMode > 0)
		? ((ReadRT == Owner->WeightmapRTA) ? TEXT("RoadWeightmapRTB") : TEXT("RoadWeightmapRTA"))
		: ((ReadRT == Owner->CSCombinedWriteRT) ? TEXT("RoadCSCombinedWriteB") : TEXT("RoadCSCombinedWrite"));
	WriteSlot = FWaterUtils::GetOrCreateTransientRenderTarget2D(
		WriteSlot,
		WriteName,
		Size,
		CombinedRT->RenderTargetFormat);
	if (!WriteSlot || WriteSlot == ReadRT)
	{
		return false;
	}

	FTextureRenderTargetResource* ReadRes = ReadRT->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* WriteRes = WriteSlot->GameThread_GetRenderTargetResource();
	if (!ReadRes || !WriteRes)
	{
		return false;
	}

	const float ZScale = static_cast<float>(Owner->LandscapeTransform.GetScale3D().Z);
	const float QuadCm = FMath::Max3(
		FMath::Abs(static_cast<float>(Owner->LandscapeTransform.GetScale3D().X)),
		FMath::Abs(static_cast<float>(Owner->LandscapeTransform.GetScale3D().Y)),
		1.0f);

	const FMatrix44f RenderAreaToWorld = MakeVertexAlignedRenderAreaToWorld(
		RenderAreaWorldTransform,
		Owner->LandscapeTransform);

	FVector2f RoadMin(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2f RoadMax(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());
	float MaxInfluence = 0.0f;
	for (const FRoadProfileSampleGPU& Sample : Samples)
	{
		RoadMin.X = FMath::Min(RoadMin.X, Sample.PosS.X);
		RoadMin.Y = FMath::Min(RoadMin.Y, Sample.PosS.Y);
		RoadMax.X = FMath::Max(RoadMax.X, Sample.PosS.X);
		RoadMax.Y = FMath::Max(RoadMax.Y, Sample.PosS.Y);
		MaxInfluence = FMath::Max(
			MaxInfluence,
			ProfileRibbonHalfForPad(Sample)
				+ FMath::Max(Sample.Falloff.X, 0.0f)
				+ FMath::Max(Sample.Falloff.Y, 1.0f));
	}
	const FVector2f Pad(MaxInfluence + QuadCm * 2.0f, MaxInfluence + QuadCm * 2.0f);
	RoadMin -= Pad;
	RoadMax += Pad;

	const uint32 SampleCount = static_cast<uint32>(Samples.Num());
	const bool bUseRenderArea = RenderAreaSize.X > 0
		&& RenderAreaSize.X < (MAX_int32 / 4);
	const FIntPoint StampSize = bUseRenderArea
		? FIntPoint(
			FMath::Min(RenderAreaSize.X, Size.X),
			FMath::Min(RenderAreaSize.Y, Size.Y))
		: Size;

	const FIntRect InfluenceRect = ComputeWorldBoxPixelRect(RoadMin, RoadMax, RenderAreaToWorld, StampSize);
	if (InfluenceRect.IsEmpty())
	{
		return true;
	}

	const bool bBoxStamp = InfluenceRect.Width() < StampSize.X || InfluenceRect.Height() < StampSize.Y;
	const FIntRect DrawRect = bBoxStamp
		? InfluenceRect
		: FIntRect(0, 0, StampSize.X, StampSize.Y);

	ENQUEUE_RENDER_COMMAND(RoadLandscapeProfilePS)(
		[ReadRes, WriteRes, Samples = MoveTemp(Samples), SampleCount, Size, StampSize, DrawRect, bBoxStamp, RenderAreaToWorld, ZScale, QuadCm, RoadMin, RoadMax, WeightMode]
		(FRHICommandListImmediate& RHICmdList)
		{
			if (!ReadRes->GetRenderTargetTexture() || !WriteRes->GetRenderTargetTexture() || SampleCount < 2)
			{
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("RoadLandscapeProfile"));
			FRDGTextureRef Combined = RegisterExternalTexture(GraphBuilder, ReadRes->GetRenderTargetTexture(), TEXT("RoadProfile.Combined"));
			FRDGTextureRef Dest = RegisterExternalTexture(GraphBuilder, WriteRes->GetRenderTargetTexture(), TEXT("RoadProfile.Dest"));
			const FIntPoint DispatchSize = Combined->Desc.Extent.X > 0 ? Combined->Desc.Extent : Size;
			if (DispatchSize.X <= 0 || DispatchSize.Y <= 0)
			{
				return;
			}

			FIntRect ClipRect = DrawRect;
			ClipRect.Max.X = FMath::Min(ClipRect.Max.X, DispatchSize.X);
			ClipRect.Max.Y = FMath::Min(ClipRect.Max.Y, DispatchSize.Y);
			if (ClipRect.IsEmpty())
			{
				return;
			}

			if (bBoxStamp)
			{
				AddCopyTexturePass(GraphBuilder, Combined, Dest);
			}

			const uint32 SampleBytes = SampleCount * sizeof(FRoadProfileSampleGPU);
			FRDGBufferRef SampleBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FRoadProfileSampleGPU), SampleCount),
				TEXT("RoadProfile.Samples"));
			GraphBuilder.QueueBufferUpload(SampleBuffer, Samples.GetData(), SampleBytes);

			TShaderMapRef<FRoadLandscapeProfilePS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRoadLandscapeProfilePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRoadLandscapeProfilePS::FParameters>();
			PassParameters->T_Combined = Combined;
			PassParameters->Samples = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SampleBuffer));
			PassParameters->NumSamples = SampleCount;
			PassParameters->RTSize = DispatchSize;
			PassParameters->StampSize = StampSize;
			PassParameters->RenderAreaToWorld = RenderAreaToWorld;
			PassParameters->LandscapeZScale = ZScale;
			PassParameters->LandscapeCmPerQuad = QuadCm;
			PassParameters->RoadBoundsMin = RoadMin;
			PassParameters->RoadBoundsMax = RoadMax;
			PassParameters->WeightMode = WeightMode;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(
				Dest,
				bBoxStamp ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(GMaxRHIFeatureLevel),
				RDG_EVENT_NAME("RoadLandscapeProfilePS"),
				PixelShader,
				PassParameters,
				ClipRect);

			GraphBuilder.Execute();
		});

	InOutWriteRT = WriteSlot;
	return true;
}

static bool EnqueueRoadWeightBlurPass(
	AWorldSceneManagement* Owner,
	UTextureRenderTarget2D*& InOutWriteRT,
	FIntPoint BlurAxis)
{
	if (!Owner || !InOutWriteRT || !Owner->WeightmapRTA || !Owner->WeightmapRTB)
	{
		return false;
	}

	UTextureRenderTarget2D* ReadRT = InOutWriteRT;
	TObjectPtr<UTextureRenderTarget2D>& WriteSlot =
		(ReadRT == Owner->WeightmapRTA) ? Owner->WeightmapRTB : Owner->WeightmapRTA;
	if (!WriteSlot || WriteSlot == ReadRT)
	{
		return false;
	}

	FTextureRenderTargetResource* ReadRes = ReadRT->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* WriteRes = WriteSlot->GameThread_GetRenderTargetResource();
	if (!ReadRes || !WriteRes)
	{
		return false;
	}

	const FIntPoint Size(ReadRT->SizeX, ReadRT->SizeY);
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	ENQUEUE_RENDER_COMMAND(RoadWeightBlurPS)(
		[ReadRes, WriteRes, Size, BlurAxis](FRHICommandListImmediate& RHICmdList)
		{
			if (!ReadRes->GetRenderTargetTexture() || !WriteRes->GetRenderTargetTexture())
			{
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("RoadWeightBlur"));
			FRDGTextureRef Source = RegisterExternalTexture(
				GraphBuilder,
				ReadRes->GetRenderTargetTexture(),
				TEXT("RoadWeight.BlurSrc"));
			FRDGTextureRef Dest = RegisterExternalTexture(
				GraphBuilder,
				WriteRes->GetRenderTargetTexture(),
				TEXT("RoadWeight.BlurDst"));

			TShaderMapRef<FRoadWeightBlurPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRoadWeightBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRoadWeightBlurPS::FParameters>();
			PassParameters->T_Weight = Source;
			PassParameters->RTSize = Size;
			PassParameters->BlurAxis = BlurAxis;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(Dest, ERenderTargetLoadAction::ENoAction);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(GMaxRHIFeatureLevel),
				RDG_EVENT_NAME("RoadWeightBlurPS"),
				PixelShader,
				PassParameters,
				FIntRect(FIntPoint::ZeroValue, Size));

			GraphBuilder.Execute();
		});

	InOutWriteRT = WriteSlot;
	return true;
}

static bool EnqueueRoadWeightBlur(AWorldSceneManagement* Owner, UTextureRenderTarget2D*& InOutWriteRT)
{
	// One separable pair: enough to hide Landscape-texel stairs, not a DF rebuild.
	return EnqueueRoadWeightBlurPass(Owner, InOutWriteRT, FIntPoint(1, 0))
		&& EnqueueRoadWeightBlurPass(Owner, InOutWriteRT, FIntPoint(0, 1));
}

bool AWorldSceneManagement::StampRoadCenterlineProfileCS(
	FEditorBrushRenderContext& BrushRenderContext,
	AMoKuEditSplineActor* Road,
	uint32 WeightMode)
{
	TArray<FRoadProfileSampleGPU> Samples;
	if (!BuildRoadProfileSamples(Road, LandscapeTransform, Samples))
	{
		return false;
	}
	if (WeightMode == 1u)
	{
		ApplyRoadWeightFalloffToProfileSamples(Samples, Road->GetRoadWeightFalloffWidth());
	}
	UTextureRenderTarget2D*& WriteRT = (WeightMode > 0)
		? BrushRenderContext.CSWeightWriteRT
		: BrushRenderContext.CSHeightWriteRT;
	return EnqueueRoadLandscapeProfileStamp(
		this,
		BrushRenderContext.RenderAreaWorldTransform,
		BrushRenderContext.RenderAreaSize,
		WriteRT,
		MoveTemp(Samples),
		Road->GetName(),
		WeightMode);
}

bool AWorldSceneManagement::StampJunctionProfileCS(
	FEditorBrushRenderContext& BrushRenderContext,
	AMoKuEditIntersectionActor* Junction,
	uint32 WeightMode)
{
	TArray<FRoadProfileSampleGPU> Samples;
	if (!BuildJunctionProfileSamples(Junction, LandscapeTransform, Samples))
	{
		return false;
	}
	if (WeightMode == 1u)
	{
		ApplyRoadWeightFalloffToProfileSamples(Samples, Junction->GetRoadWeightFalloffWidth());
	}
	UTextureRenderTarget2D*& WriteRT = (WeightMode > 0)
		? BrushRenderContext.CSWeightWriteRT
		: BrushRenderContext.CSHeightWriteRT;
	return EnqueueRoadLandscapeProfileStamp(
		this,
		BrushRenderContext.RenderAreaWorldTransform,
		BrushRenderContext.RenderAreaSize,
		WriteRT,
		MoveTemp(Samples),
		Junction->GetName(),
		WeightMode);
}

void AWorldSceneManagement::RenderBrushActorContext(FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext)
{
	if (!BrushRenderContext.bHeightmapRender)
	{
		if (!BrushActorRenderContext.RoadBrushActor->AffectsWeightmapLayer(BrushRenderContext.WeightmapLayerName))
		{
			UE_LOG(LogTemp, Warning, TEXT("Actor does NOT affect this layer, Skipping"));
			return;
		}
	}

	const uint32 WeightMode = BrushRenderContext.bHeightmapRender
		? 0u
		: GetProfileWeightMode(BrushRenderContext.WeightmapLayerName);

	if (AMoKuEditSplineActor* ProfileRoad = BrushActorRenderContext.TryGetActorAs<AMoKuEditSplineActor>())
	{
		if (StampRoadCenterlineProfileCS(BrushRenderContext, ProfileRoad, WeightMode))
		{
			++BrushRenderContext.RTIndex;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Profile stamp failed for %s"), *ProfileRoad->GetName());
		}
		return;
	}
	if (AMoKuEditIntersectionActor* Junction = BrushActorRenderContext.TryGetActorAs<AMoKuEditIntersectionActor>())
	{
		if (StampJunctionProfileCS(BrushRenderContext, Junction, WeightMode))
		{
			++BrushRenderContext.RTIndex;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Junction stamp skipped for %s — no mouth samples"), *Junction->GetName());
		}
		return;
	}
}



void AWorldSceneManagement::BlueprintRoadBodyChanged_Native(AActor* Actor)
{
	if (::IsValid(Actor))
	{
		RequestLandscapeUpdate();
	}
}

#undef LOCTEXT_NAMESPACE