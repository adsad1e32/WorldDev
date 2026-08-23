#include "EditSplineRoadInfo.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadSplineComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "Components/SceneCaptureComponent2D.h"
#include "FloodComponent2D.h"
#include "SplineRoadEditorSubsystem.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "WaterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Engine/Canvas.h"
#include "WorldEditorSettings.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Selection.h"
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
	if (!Spline->GetLaneSection(SectionIndex).EvalLandscapeAt(
		WorldS - Spline->GetLaneSection(SectionIndex).SOffset,
		OutValue))
	{
		return false;
	}

	OutValue.MigrateLegacyFields();
	return true;
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

struct FLandscapeHeightSlice
{
	double S0 = 0.0;
	double S1 = 0.0;
	FMetaLandRoadLandscapeAttributeValue Value;
};

static bool GSkipAutoLandscapeKeyApply = false;
static bool GCaptureClipEnabled = false;
static double GCaptureClipS0 = 0.0;
static double GCaptureClipS1 = 0.0;
static float GCaptureLateralDilateCm = 0.0f;
static float GCaptureFalloffSkirtCm = 0.0f;
static float GCaptureTipExtendCm = 0.0f;
static bool GCaptureFlattenToSplineZ = false;
static bool GHasLandscapeKeyFalloffOverride = false;
static FRoadFalloffSettings GLandscapeKeyFalloffOverride;

static void SetLandscapeKeyFalloffOverride(const FMetaLandRoadLandscapeAttributeValue& LandscapeValue)
{
	FMetaLandRoadLandscapeAttributeValue Synced = LandscapeValue;
	Synced.MigrateLegacyFields();
	Synced.SyncFalloffFromMetaRoadFields();
	GHasLandscapeKeyFalloffOverride = true;
	GLandscapeKeyFalloffOverride = Synced.FalloffSettings;
}

static void ClearLandscapeKeyFalloffOverride()
{
	GHasLandscapeKeyFalloffOverride = false;
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

	const FRoadFalloffSettings& ActorFalloff = Road->RoadHeightmapSettings.FalloffSettings;
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

			float SideOffsetCm = FMath::Max(ActorFalloff.EdgeOffset, 0.0f);
			float SideFalloffCm = FMath::Max(ActorFalloff.FalloffWidth, 1.0f);
			float HeightOffsetCm = FMath::Clamp(ActorFalloff.ZOffset, -2000.0f, 2000.0f);
			float LayerFactor = 1.0f;
			FMetaLandRoadLandscapeAttributeValue LandscapeValue;
			if (EvalRoadLandscapeAtWorldS(Spline, S, LandscapeValue))
			{
				SideOffsetCm = FMath::Max(LandscapeValue.SideOffset, 0.0f);
				SideFalloffCm = FMath::Max(LandscapeValue.SideFalloff, 0.0f);
				HeightOffsetCm = FMath::Clamp(LandscapeValue.HeightOffset, -2000.0f, 2000.0f);
				LayerFactor = FMath::Clamp(LandscapeValue.LayerFactor, 0.0f, 1.0f);
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
				0.0f);
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

static void ApplyWeightmapToProfileSamples(
	TArray<FRoadProfileSampleGPU>& Samples,
	const FRoadBodyWeightmapSettings& WMSettings)
{
	const float Offset = FMath::Max(WMSettings.EdgeOffset, 0.0f);
	const float Falloff = FMath::Max(WMSettings.FalloffWidth, 1.0f);
	const float Opacity = FMath::Clamp(WMSettings.FinalOpacity, 0.0f, 1.0f);
	for (FRoadProfileSampleGPU& Sample : Samples)
	{
		Sample.Falloff.X = Offset;
		Sample.Falloff.Y = Falloff;
		Sample.Extra.Z = Opacity;
	}
}

static uint32 GetProfileWeightMode(FName LayerName)
{
	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayer = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	return (LayerName == RoadLayer) ? 1u : 2u;
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
	float SoftArc = 1.0f)
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

	const FRoadFalloffSettings& HubFalloff = Junction->RoadHeightmapSettings.FalloffSettings;
	const float SideOffsetCm = FMath::Max(Junction->JunctionSampleEdgeOffset, 0.0f);
	const float SideFalloffCm = FMath::Max(Junction->JunctionSampleFalloffWidth, 1.0f);
	const float HeightOffsetCm = FMath::Clamp(HubFalloff.ZOffset, -2000.0f, 2000.0f);
	const FVector HubPos(Hub.X, Hub.Y, Junction->GetActorLocation().Z);

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
			MouthOffset, MouthFalloff, HeightOffsetCm, 1.0f, Arm.CutS, static_cast<float>(SpanId));
		AppendProfileSampleFromWorld(
			OutSamples, LandscapeTransform, HubPos, HubRight, Arm.HalfL, Arm.HalfR,
			SideOffsetCm, SideFalloffCm, HeightOffsetCm, 1.0f, Arm.CutS, static_cast<float>(SpanId));
		++SpanId;
	}

	if (Junction->EdgePath.Num() >= 3)
	{
		const FVector2D Hub2D(HubPos.X, HubPos.Y);
		const int32 RingCount = Junction->EdgePath.Num();
		for (int32 Index = 0; Index <= RingCount; ++Index)
		{
			if (OutSamples.Num() >= GRoadProfileMaxSamples)
			{
				break;
			}
			const FVector3d& P = Junction->EdgePath[Index % RingCount];
			const FVector Center(P.X, P.Y, HubPos.Z);
			FVector2D Radial(P.X - Hub2D.X, P.Y - Hub2D.Y);
			if (!Radial.Normalize())
			{
				Radial = FVector2D(1.0f, 0.0f);
			}
			const float InW = FMath::Max(FVector2D::Distance(FVector2D(P.X, P.Y), Hub2D), 1.0f);
			AppendProfileSampleFromWorld(
				OutSamples, LandscapeTransform, Center, Radial, 2.0f, InW,
				SideOffsetCm, SideFalloffCm, HeightOffsetCm, 1.0f, 0.0f, static_cast<float>(SpanId));
		}
		++SpanId;
	}

	return OutSamples.Num() >= 2;
}

static const FRoadFalloffSettings& ResolveStampFalloff(const FSplineRoadHeightmapSettings& HeightmapSettings)
{
	return GHasLandscapeKeyFalloffOverride ? GLandscapeKeyFalloffOverride : HeightmapSettings.FalloffSettings;
}

static bool LandscapeFalloffsNearlyEqual(const FRoadFalloffSettings& A, const FRoadFalloffSettings& B)
{
	return A.FalloffMode == B.FalloffMode
		&& FMath::IsNearlyEqual(A.FalloffWidth, B.FalloffWidth, 1.0f)
		&& FMath::IsNearlyEqual(A.EdgeOffset, B.EdgeOffset, 1.0f)
		&& FMath::IsNearlyEqual(A.ZOffset, B.ZOffset, 0.5f)
		&& FMath::IsNearlyEqual(A.FalloffAngle, B.FalloffAngle, 0.5f);
}

static bool LandscapeTrackFalloffIsUniform(const FMetaLandRoadLaneAttribute& Track)
{
	FRoadFalloffSettings First;
	if (!Track.GetKeyFalloff(0, First))
	{
		return true;
	}

	for (int32 KeyIndex = 1; KeyIndex < Track.Keys.Num(); ++KeyIndex)
	{
		FRoadFalloffSettings Other;
		if (!Track.GetKeyFalloff(KeyIndex, Other) || !LandscapeFalloffsNearlyEqual(First, Other))
		{
			return false;
		}
	}
	return true;
}

static void CollectLandscapeHeightSlices(AMoKuEditSplineActor* Road, TArray<FLandscapeHeightSlice>& OutSlices)
{
	OutSlices.Reset();
	if (!Road)
	{
		return;
	}

	UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent();
	if (!Spline)
	{
		return;
	}

	// One stamp per key span — not 128cm clips. Short clips made Offset/Width look
	// dead (DF of a 1.3m stick) and made actor 3000/3500 rebuilds take minutes.
	const TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSections();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
		const FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
		if (!Track || Track->Keys.Num() < 2 || LandscapeTrackFalloffIsUniform(*Track))
		{
			continue;
		}

		for (int32 KeyIndex = 0; KeyIndex < Track->Keys.Num() - 1; ++KeyIndex)
		{
			const double Local0 = Track->Keys[KeyIndex].SOffset;
			const double Local1 = Track->Keys[KeyIndex + 1].SOffset;
			const double Span = Local1 - Local0;
			if (Span <= 1.0)
			{
				continue;
			}

			FRoadFalloffSettings Falloff0;
			FRoadFalloffSettings Falloff1;
			const bool bHaveEnds = Track->GetKeyFalloff(KeyIndex, Falloff0)
				&& Track->GetKeyFalloff(KeyIndex + 1, Falloff1);
			const float FalloffDelta = bHaveEnds
				? (FMath::Abs(Falloff0.FalloffWidth - Falloff1.FalloffWidth)
					+ FMath::Abs(Falloff0.EdgeOffset - Falloff1.EdgeOffset))
				: 0.0f;
			const int32 Num = FMath::Clamp(FMath::CeilToInt(FalloffDelta / 400.0f), 1, 6);
			for (int32 StepIndex = 0; StepIndex < Num; ++StepIndex)
			{
				const double A0 = Local0 + Span * (static_cast<double>(StepIndex) / static_cast<double>(Num));
				const double A1 = Local0 + Span * (static_cast<double>(StepIndex + 1) / static_cast<double>(Num));
				FLandscapeHeightSlice Slice;
				Slice.S0 = Section.SOffset + A0;
				Slice.S1 = Section.SOffset + A1;
				if (!Section.EvalLandscapeAt((A0 + A1) * 0.5, Slice.Value))
				{
					continue;
				}
				Slice.Value.MigrateLegacyFields();
				OutSlices.Add(Slice);
			}
		}
	}
}

static float FindSplineInputKeyClosestXY(const USplineComponent* Spline, const FVector& WorldPos)
{
	if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
	{
		return 0.0f;
	}

	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const int32 NumSamples = FMath::Clamp(FMath::CeilToInt(Length / 50.0f), 16, 256);
	float BestKey = 0.0f;
	double BestDistSq = TNumericLimits<double>::Max();
	for (int32 SampleIndex = 0; SampleIndex <= NumSamples; ++SampleIndex)
	{
		const float Distance = Length * (static_cast<float>(SampleIndex) / static_cast<float>(NumSamples));
		const float Key = Spline->GetInputKeyValueAtDistanceAlongSpline(Distance);
		const FVector Sample = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const double DistSq = FVector::DistSquared2D(Sample, WorldPos);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestKey = Key;
		}
	}

	const float RefineSpan = Length / static_cast<float>(NumSamples);
	const float BestDistance = Spline->GetDistanceAlongSplineAtSplineInputKey(BestKey);
	for (int32 RefineIndex = -4; RefineIndex <= 4; ++RefineIndex)
	{
		const float Distance = FMath::Clamp(BestDistance + RefineSpan * 0.25f * RefineIndex, 0.0f, Length);
		const float Key = Spline->GetInputKeyValueAtDistanceAlongSpline(Distance);
		const FVector Sample = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const double DistSq = FVector::DistSquared2D(Sample, WorldPos);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestKey = Key;
		}
	}
	return BestKey;
}

static bool ClipCaptureMeshToSRange(
	UDynamicMeshComponent* MeshComp,
	UMetaLandRoadSplineComponent* Spline,
	double S0,
	double S1,
	TArray<FVector3d>& OutSavedLocal)
{
	if (!MeshComp || !Spline || S1 <= S0)
	{
		return false;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh || DynMesh->GetTriangleCount() <= 0)
	{
		return false;
	}

	const FTransform CompToWorld = MeshComp->GetComponentTransform();
	int32 Moved = 0;

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			OutSavedLocal.SetNum(Mesh.MaxVertexID());
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector3d Local = Mesh.GetVertex(Vid);
				OutSavedLocal[Vid] = Local;

				const FVector World = CompToWorld.TransformPosition(FVector(Local));
				const float InputKey = FindSplineInputKeyClosestXY(Spline, World);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				if (WorldS >= S0 && WorldS <= S1)
				{
					continue;
				}

				const double ClampedS = FMath::Clamp(WorldS, S0, S1);
				const FVector OldClosest = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				const FVector NewClosest = Spline->GetLocationAtDistanceAlongSpline(ClampedS, ESplineCoordinateSpace::World);
				const FVector NewWorld = NewClosest + (World - OldClosest);
				Mesh.SetVertex(Vid, FVector3d(CompToWorld.InverseTransformPosition(NewWorld)));
				++Moved;
			}
		},
		EDynamicMeshChangeType::DeformationEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	if (Moved > 0)
	{
		MeshComp->NotifyMeshUpdated();
		FlushRenderingCommands();
	}
	return Moved > 0;
}

static bool DilateCaptureMeshLateral(
	UDynamicMeshComponent* MeshComp,
	UMetaLandRoadSplineComponent* Spline,
	float DilateCm)
{
	if (!MeshComp || !Spline || FMath::Abs(DilateCm) < 0.5f)
	{
		return false;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh || DynMesh->GetTriangleCount() <= 0)
	{
		return false;
	}

	const FTransform CompToWorld = MeshComp->GetComponentTransform();
	int32 Moved = 0;

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector3d Local = Mesh.GetVertex(Vid);
				const FVector World = CompToWorld.TransformPosition(FVector(Local));
				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(World);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				const FVector Center = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				Right.Z = 0.0f;
				if (!Right.Normalize())
				{
					continue;
				}

				const float Side = static_cast<float>(FVector::DotProduct(World - Center, Right));
				if (FMath::Abs(Side) < 1.0f)
				{
					continue;
				}

				float ApplyCm = DilateCm;
				if (ApplyCm < 0.0f)
				{
					ApplyCm = FMath::Max(ApplyCm, -(FMath::Abs(Side) - 1.0f));
					if (ApplyCm >= -0.5f)
					{
						continue;
					}
				}

				const FVector NewWorld = World + Right * FMath::Sign(Side) * ApplyCm;
				Mesh.SetVertex(Vid, FVector3d(CompToWorld.InverseTransformPosition(NewWorld)));
				++Moved;
			}
		},
		EDynamicMeshChangeType::DeformationEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	if (Moved > 0)
	{
		MeshComp->NotifyMeshUpdated();
		FlushRenderingCommands();
	}
	return Moved > 0;
}

static bool FlattenCaptureMeshToSplineZ(
	UDynamicMeshComponent* MeshComp,
	UMetaLandRoadSplineComponent* Spline)
{
	if (!MeshComp || !Spline)
	{
		return false;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh || DynMesh->GetTriangleCount() <= 0)
	{
		return false;
	}

	const FTransform CompToWorld = MeshComp->GetComponentTransform();
	int32 Moved = 0;

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector World = CompToWorld.TransformPosition(FVector(Mesh.GetVertex(Vid)));
				const float InputKey = FindSplineInputKeyClosestXY(Spline, World);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				const FVector Center = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				const float RelZ = World.Z - Center.Z;
				const float TargetZ = (RelZ < -8.0f) ? (Center.Z - 4.0f) : Center.Z;
				if (FMath::Abs(World.Z - TargetZ) < 0.5f)
				{
					continue;
				}

				FVector NewWorld = World;
				NewWorld.Z = TargetZ;
				Mesh.SetVertex(Vid, FVector3d(CompToWorld.InverseTransformPosition(NewWorld)));
				++Moved;
			}
		},
		EDynamicMeshChangeType::DeformationEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	if (Moved > 0)
	{
		MeshComp->NotifyMeshUpdated();
		FlushRenderingCommands();
	}
	return Moved > 0;
}

static float SampleFalloffGroundZ(UWorld* World, AActor* IgnoreActor, const FVector& WorldXY, float FallbackZ);

static bool AppendCaptureFalloffSkirt(
	UDynamicMeshComponent* MeshComp,
	UMetaLandRoadSplineComponent* Spline,
	AActor* IgnoreActor,
	float FalloffCm)
{
	if (!MeshComp || !Spline || FalloffCm < 1.0f)
	{
		return false;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh || DynMesh->GetTriangleCount() <= 0)
	{
		return false;
	}

	UWorld* World = MeshComp->GetWorld();
	const FTransform CompToWorld = MeshComp->GetComponentTransform();
	int32 Added = 0;

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			TArray<float> Sides;
			Sides.SetNum(Mesh.MaxVertexID());
			float MaxAbsSide = 0.0f;
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector WorldPos = CompToWorld.TransformPosition(FVector(Mesh.GetVertex(Vid)));
				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldPos);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				const FVector Center = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				Right.Z = 0.0f;
				Right.Normalize();
				const float Side = Right.IsNearlyZero()
					? 0.0f
					: static_cast<float>(FVector::DotProduct(WorldPos - Center, Right));
				Sides[Vid] = Side;
				MaxAbsSide = FMath::Max(MaxAbsSide, FMath::Abs(Side));
			}

			if (MaxAbsSide < 2.0f)
			{
				return;
			}

			const float OuterThresh = MaxAbsSide * 0.88f;
			TMap<int32, int32> OuterToSkirt;
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const float Side = Sides.IsValidIndex(Vid) ? Sides[Vid] : 0.0f;
				if (FMath::Abs(Side) < OuterThresh)
				{
					continue;
				}

				const FVector WorldPos = CompToWorld.TransformPosition(FVector(Mesh.GetVertex(Vid)));
				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldPos);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				const FVector Center = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				Right.Z = 0.0f;
				if (!Right.Normalize())
				{
					continue;
				}

				FVector NewWorld = Center + Right * (FMath::Sign(Side) * (FMath::Abs(Side) + FalloffCm));
				// Probe past the old carve so 500 vs 1000 both drop to the same ground Z.
				const FVector Probe = Center + Right * (FMath::Sign(Side) * (FMath::Abs(Side) + FalloffCm + 3000.0f));
				NewWorld.Z = SampleFalloffGroundZ(World, IgnoreActor, Probe, Center.Z);
				const int32 NewVid = Mesh.AppendVertex(FVector3d(CompToWorld.InverseTransformPosition(NewWorld)));
				OuterToSkirt.Add(Vid, NewVid);
				++Added;
			}

			for (const int32 Eid : Mesh.EdgeIndicesItr())
			{
				if (!Mesh.IsBoundaryEdge(Eid))
				{
					continue;
				}
				const UE::Geometry::FIndex2i EdgeV = Mesh.GetEdgeV(Eid);
				const int32* SkirtA = OuterToSkirt.Find(EdgeV.A);
				const int32* SkirtB = OuterToSkirt.Find(EdgeV.B);
				if (!SkirtA || !SkirtB)
				{
					continue;
				}

				const FVector3d PA = Mesh.GetVertex(EdgeV.A);
				const FVector3d PB = Mesh.GetVertex(EdgeV.B);
				const FVector3d PC = Mesh.GetVertex(*SkirtB);
				const FVector3d Cross = (PB - PA).Cross(PC - PA);
				if (Cross.Z >= 0.0)
				{
					Mesh.AppendTriangle(EdgeV.A, EdgeV.B, *SkirtB);
					Mesh.AppendTriangle(EdgeV.A, *SkirtB, *SkirtA);
				}
				else
				{
					Mesh.AppendTriangle(EdgeV.A, *SkirtB, EdgeV.B);
					Mesh.AppendTriangle(EdgeV.A, *SkirtA, *SkirtB);
				}
			}
		},
		EDynamicMeshChangeType::GeneralEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	if (Added > 0)
	{
		MeshComp->NotifyMeshUpdated();
		FlushRenderingCommands();
	}
	return Added > 0;
}

static float SampleFalloffGroundZ(UWorld* World, AActor* IgnoreActor, const FVector& WorldXY, float FallbackZ)
{
	if (!World)
	{
		return FallbackZ;
	}

	FCollisionQueryParams Params(NAME_None, false, IgnoreActor);
	FHitResult Hit;
	const FVector Start(WorldXY.X, WorldXY.Y, WorldXY.Z + 8000.0f);
	const FVector End(WorldXY.X, WorldXY.Y, WorldXY.Z - 20000.0f);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return Hit.Location.Z;
	}
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return Hit.Location.Z;
	}
	return FallbackZ;
}

static bool SlopeCaptureMeshFalloff(
	UDynamicMeshComponent* MeshComp,
	UMetaLandRoadSplineComponent* Spline,
	AActor* IgnoreActor,
	float FalloffCm)
{
	if (!MeshComp || !Spline || FalloffCm < 1.0f)
	{
		return false;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh || DynMesh->GetTriangleCount() <= 0)
	{
		return false;
	}

	UWorld* World = MeshComp->GetWorld();
	const FTransform CompToWorld = MeshComp->GetComponentTransform();
	int32 Moved = 0;

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			TArray<float> Sides;
			Sides.SetNum(Mesh.MaxVertexID());
			float MaxAbsSide = 0.0f;
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector WorldPos = CompToWorld.TransformPosition(FVector(Mesh.GetVertex(Vid)));
				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldPos);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				const FVector Center = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				Right.Z = 0.0f;
				Right.Normalize();
				const float Side = Right.IsNearlyZero()
					? 0.0f
					: static_cast<float>(FVector::DotProduct(WorldPos - Center, Right));
				Sides[Vid] = Side;
				MaxAbsSide = FMath::Max(MaxAbsSide, FMath::Abs(Side));
			}

			if (MaxAbsSide < 2.0f)
			{
				return;
			}

			const float OuterThresh = MaxAbsSide * 0.88f;
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const float Side = Sides.IsValidIndex(Vid) ? Sides[Vid] : 0.0f;
				if (FMath::Abs(Side) < OuterThresh)
				{
					continue;
				}

				const FVector WorldPos = CompToWorld.TransformPosition(FVector(Mesh.GetVertex(Vid)));
				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldPos);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				const FVector Center = Spline->GetLocationAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(WorldS, ESplineCoordinateSpace::World);
				Right.Z = 0.0f;
				if (!Right.Normalize())
				{
					continue;
				}

				const FVector NewXY = Center + Right * (FMath::Sign(Side) * (FMath::Abs(Side) + FalloffCm));
				FVector NewWorld = WorldPos;
				NewWorld.X = NewXY.X;
				NewWorld.Y = NewXY.Y;
				NewWorld.Z = SampleFalloffGroundZ(World, IgnoreActor, NewWorld, Center.Z);
				Mesh.SetVertex(Vid, FVector3d(CompToWorld.InverseTransformPosition(NewWorld)));
				++Moved;
			}
		},
		EDynamicMeshChangeType::DeformationEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	if (Moved > 0)
	{
		MeshComp->NotifyMeshUpdated();
		FlushRenderingCommands();
	}
	return Moved > 0;
}

static bool ExtendCaptureMeshTips(
	UDynamicMeshComponent* MeshComp,
	UMetaLandRoadSplineComponent* Spline,
	float ExtendCm)
{
	if (!MeshComp || !Spline || ExtendCm < 1.0f)
	{
		return false;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh || DynMesh->GetTriangleCount() <= 0)
	{
		return false;
	}

	const float SplineLen = Spline->GetSplineLength();
	if (SplineLen <= ExtendCm * 2.0f)
	{
		return false;
	}

	const FTransform CompToWorld = MeshComp->GetComponentTransform();
	int32 Moved = 0;

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector3d Local = Mesh.GetVertex(Vid);
				const FVector World = CompToWorld.TransformPosition(FVector(Local));
				const float InputKey = Spline->FindInputKeyClosestToWorldLocation(World);
				const double WorldS = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				FVector Tangent = Spline->GetTangentAtDistanceAlongSpline(
					static_cast<float>(WorldS), ESplineCoordinateSpace::World);
				if (!Tangent.Normalize())
				{
					continue;
				}

				FVector NewWorld = World;
				if (WorldS <= ExtendCm)
				{
					NewWorld -= Tangent * ExtendCm;
				}
				else if (WorldS >= SplineLen - ExtendCm)
				{
					NewWorld += Tangent * ExtendCm;
				}
				else
				{
					continue;
				}

				Mesh.SetVertex(Vid, FVector3d(CompToWorld.InverseTransformPosition(NewWorld)));
				++Moved;
			}
		},
		EDynamicMeshChangeType::DeformationEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	if (Moved > 0)
	{
		MeshComp->NotifyMeshUpdated();
		FlushRenderingCommands();
	}
	return Moved > 0;
}

static void RestoreCaptureMeshVerts(UDynamicMeshComponent* MeshComp, const TArray<FVector3d>& SavedLocal)
{
	if (!MeshComp || SavedLocal.Num() == 0)
	{
		return;
	}

	UDynamicMesh* DynMesh = MeshComp->GetDynamicMesh();
	if (!DynMesh)
	{
		return;
	}

	DynMesh->EditMesh(
		[&](FDynamicMesh3& Mesh)
		{
			for (int32 Vid : Mesh.VertexIndicesItr())
			{
				if (SavedLocal.IsValidIndex(Vid))
				{
					Mesh.SetVertex(Vid, SavedLocal[Vid]);
				}
			}
		},
		EDynamicMeshChangeType::DeformationEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);
	MeshComp->NotifyMeshUpdated();
}

static bool ApplyRoadLandscapeKeysToBrushSettings(AMoKuEditBaseActor* Actor)
{
	if (GSkipAutoLandscapeKeyApply)
	{
		return false;
	}

	FMetaLandRoadLandscapeAttributeValue LandscapeValue;
	if (!SampleRoadLandscapeAttribute(Actor, LandscapeValue))
	{
		return false;
	}

	SetLandscapeKeyFalloffOverride(LandscapeValue);
	return true;
}

namespace MetaLandRoadBrushMaterials
{
	// Height falloff stays on official Water MeshBrush. LandmassBrush_Width is not a
	// drop-in: different height packing / landscape UV sampling, and it rewrote the
	// entire WorldEditor layer (spikes / flattened void). Offset+Width semantics
	// cannot be fixed by swapping that material into this pipeline.
	static const TCHAR* BrushAngleLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Angle.MeshBrush_Angle");
	static const TCHAR* BrushWidthLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Width.MeshBrush_Width");
	static const TCHAR* BrushWeightmapLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Weightmap.MeshBrush_Weightmap");
	static const TCHAR* CacheDistanceFieldLocal = TEXT("/MetaLandRoad/Material/Landscape/CacheDistanceField.CacheDistanceField");
	static const TCHAR* CompositeBodyLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/CompositeMeshBodyTexture.CompositeMeshBodyTexture");
	static const TCHAR* RenderSplineDepthsLocal = TEXT("/MetaLandRoad/Material/Landscape/RenderMeshDepths.RenderMeshDepths");
	static const TCHAR* JumpFloodStepLocal = TEXT("/MetaLandRoad/Material/Landscape/DistanceFields/JumpFloodStep.JumpFloodStep");
	static const TCHAR* BlurEdgesLocal = TEXT("/MetaLandRoad/Material/Landscape/DistanceFields/BlurEdges.BlurEdges");
	static const TCHAR* FindEdgesLocal = TEXT("/MetaLandRoad/Material/Landscape/DistanceFields/DetectEdges.DetectEdges");
	static const TCHAR* MPCLocal = TEXT("/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape");

	static const TCHAR* BrushAngleOfficial = TEXT("/Water/Materials/Brushes/MeshBrush_Angle.MeshBrush_Angle");
	static const TCHAR* BrushWidthOfficial = TEXT("/Water/Materials/Brushes/MeshBrush_Width.MeshBrush_Width");
	static const TCHAR* BrushWeightmapOfficial = TEXT("/Water/Materials/Brushes/MeshBrush_Weightmap.MeshBrush_Weightmap");
	static const TCHAR* CacheDistanceFieldOfficial = TEXT("/Landmass/Landscape/BlueprintBrushes/Materials/CacheDistanceField.CacheDistanceField");
	static const TCHAR* CompositeBodyOfficial = TEXT("/Water/Materials/Brushes/CompositeWaterBodyTexture.CompositeWaterBodyTexture");
	static const TCHAR* RenderSplineDepthsOfficial = TEXT("/Landmass/Landscape/BlueprintBrushes/Materials/RenderSplineDepths.RenderSplineDepths");
	static const TCHAR* JumpFloodStepOfficial = TEXT("/Landmass/DistanceFields/Materials/JumpFloodStep.JumpFloodStep");
	static const TCHAR* BlurEdgesOfficial = TEXT("/Landmass/DistanceFields/Materials/BlurEdges.BlurEdges");
	static const TCHAR* FindEdgesOfficial = TEXT("/Landmass/DistanceFields/Materials/DetectEdges.DetectEdges");
	static const TCHAR* MPCOfficial = TEXT("/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape");

	// Prefer official Water/Landmass first. Local /MetaLandRoad/ Landscape fork assets still
	// embed /TestMode/ soft paths; loading them makes shaders never compile and Landscape
	// stuck on "Waiting for edit layer resources". Use local only after
	// CopyBrushMaterialsToMetaLandRoad.py has overwritten those forks with clean duplicates.
	static UMaterialInterface* LoadMaterial(const TCHAR* LocalPath, const TCHAR* OfficialPath, const TCHAR* Label)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, OfficialPath));
		if (Material != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Brush material '%s' -> %s (official)"), Label, OfficialPath);
			return Material;
		}

		Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, LocalPath));
		if (Material != nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Brush material '%s' -> %s (local fallback; run CopyBrushMaterialsToMetaLandRoad.py if shaders fail)"), Label, LocalPath);
			return Material;
		}

		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] Failed to load brush material '%s' (official=%s, local=%s)"), Label, OfficialPath, LocalPath);
		return nullptr;
	}

	static UMaterialParameterCollection* LoadMPC()
	{
		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(StaticLoadObject(UMaterialParameterCollection::StaticClass(), nullptr, MPCOfficial));
		if (MPC != nullptr)
		{
			return MPC;
		}
		return Cast<UMaterialParameterCollection>(StaticLoadObject(UMaterialParameterCollection::StaticClass(), nullptr, MPCLocal));
	}
}

AWorldSceneManagement::AWorldSceneManagement(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, LandscapeRTRes(0, 0)
, WorldSize(FVector::ZeroVector)
, LandscapeTransform(FTransform::Identity)
{

	TArray<FEngineShowFlagsSetting> ShowFlagSettings;

	JumpFloodComponent2D = CreateDefaultSubobject<UFloodComponent2D>(TEXT("JumpFloodComponent2D"));
	Octree = TOctree2<FSplineSegmentOctree, FSegmentsOctreeSemantics>(FVector::ZeroVector, HALF_WORLD_MAX);
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent2D"));
	SceneCapture->CreationMethod = EComponentCreationMethod::Native;
	SceneCapture->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	SceneCapture->ProjectionType = ECameraProjectionMode::Type::Orthographic;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bExcludeFromSceneTextureExtents = true;  // Don't let this influence the render target size used for other purposes.
	SceneCapture->SetRelativeRotation(FRotator(-90.0f, 0.0f, -90.0f));
	SceneCapture->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("NaniteMeshes"), false });
	// These also need to be disabled to get a clean capture of just the water info material output
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Atmosphere"), false });
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Bloom"), false });
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Lighting"), false });
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Fog"), false });
	SceneCapture->SetShowFlagSettings(ShowFlagSettings);


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
		check(SceneCapture != nullptr);

		FVector Scale = LandscapeTransform.GetScale3D();
		WorldSize.Set(Scale.X * (float)LandscapeQuads.X, Scale.Y * (float)LandscapeQuads.Y, 0.512f);

		const FVector Temp(Scale.X * (float)LandscapeRTRes.X, Scale.Y * (float)LandscapeRTRes.Y, 0.512f);
		SceneCapture->OrthoWidth = FMath::Max(Temp.X, Temp.Y);

		FVector LocationVector(Temp - Scale);
		LocationVector *= 0.5f;
		LocationVector = LandscapeTransform.GetRotation().RotateVector(LocationVector);
		LocationVector += LandscapeTransform.GetLocation();
		LocationVector.Z = 50000.0f;
		SceneCapture->SetWorldLocation(LocationVector);

		// If the transform or resolution changes, the distance fields need to be recomputed entirely : 
		bKillCache = true;
	}
}


void AWorldSceneManagement::InitSceneCaptureAndParam()
{

		SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));

		if (RootComponent)
		{
			SceneCapture->SetupAttachment(RootComponent);
		}
		else
		{
			RootComponent = SceneCapture;
		}

		JumpFloodComponent2D = CreateDefaultSubobject<UFloodComponent2D>(TEXT("JumpFloodComponent2D"));
		SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent2D"));
		SceneCapture->CreationMethod = EComponentCreationMethod::Native;
		SceneCapture->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		SceneCapture->ProjectionType = ECameraProjectionMode::Type::Orthographic;
		SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
		SceneCapture->bCaptureEveryFrame = false;
		SceneCapture->bCaptureOnMovement = false;
		SceneCapture->bExcludeFromSceneTextureExtents = true;
		SceneCapture->SetRelativeRotation(FRotator(-90.0f, 0.0f, -90.0f));
		SceneCapture->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

		TArray<FEngineShowFlagsSetting> ShowFlagSettings;

		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("NaniteMeshes"), false });

		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Atmosphere"), false });
		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Bloom"), false });
		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Lighting"), false });
		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Fog"), false });
		SceneCapture->SetShowFlagSettings(ShowFlagSettings);

		PrimaryActorTick.TickGroup = ETickingGroup::TG_PrePhysics;
		bIsEditorOnlyActor = false;

}

bool AWorldSceneManagement::AllocateRTs()
{
	// Live Coding / Blueprint reinstancer creates CDOs — never allocate RTs on templates.
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		return false;
	}

	// Details shows None if we never painted (LandscapeRTRes still 0,0) — resolve size first.
	if (LandscapeRTRes.X <= 0 || LandscapeRTRes.Y <= 0)
	{
		FVector UnusedLoc, UnusedSize;
		if (!DeprecateWorldLandscapeInfo(UnusedLoc, UnusedSize))
		{
			LandscapeRTRes = FIntPoint(1024, 1024);
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] AllocateRTs: LandscapeRTRes was 0 — using fallback %dx%d (trigger Landscape WorldEditor paint for real size)"),
				LandscapeRTRes.X, LandscapeRTRes.Y);
		}
	}

	bool bSuccess = true;
	HeightmapRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(HeightmapRTA, TEXT("HeightmapRTA"), LandscapeRTRes, RTF_RGBA8);
	HeightmapRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(HeightmapRTB, TEXT("HeightmapRTB"), LandscapeRTRes, RTF_RGBA8);
	if ((HeightmapRTA == nullptr) || (HeightmapRTB == nullptr))
	{
		bSuccess = false;
	}

	JumpFloodRTA = nullptr;
	JumpFloodRTB = nullptr;
	DepthAndShapeRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(DepthAndShapeRT, TEXT("DepthAndShapeRT"), LandscapeRTRes, RTF_RG32f);
	if (DepthAndShapeRT != nullptr && SceneCapture != nullptr)
	{
		SceneCapture->TextureTarget = DepthAndShapeRT;
	}
	else if (DepthAndShapeRT == nullptr)
	{
		bSuccess = false;
	}

	RoadDepthRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(RoadDepthRT, TEXT("RoadDepthRT"), LandscapeRTRes, RTF_RGBA32f);
	if (RoadDepthRT == nullptr)
	{	
		bSuccess = false;
	}

	// Must match Water MeshBrush_Weightmap + Landscape weight CombinedResult (R8). R16f can be ignored by merge.
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
	if ((WeightmapRTA == nullptr) || (WeightmapRTB == nullptr))
	{
		bSuccess = false;
	}

	CombinedShapeAndHeightRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(CombinedShapeAndHeightRTA, TEXT("CombinedShapeAndHeightRTA"), LandscapeRTRes, RTF_RGBA16f);
	CombinedShapeAndHeightRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(CombinedShapeAndHeightRTB, TEXT("CombinedShapeAndHeightRTB"), LandscapeRTRes, RTF_RGBA16f);
	if ((CombinedShapeAndHeightRTA == nullptr) || (CombinedShapeAndHeightRTB == nullptr))
	{
		bSuccess = false;
	}

	// Solid 1.0 — a 0→1 ramp here faded weight/height from spline head to tail.
	DefaultCurveRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(DefaultCurveRT, TEXT("RoadDefaultCurveRT"), FIntPoint(256, 1), RTF_R16f);
	if (DefaultCurveRT != nullptr)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, DefaultCurveRT, FLinearColor::White);
	}
	else
	{
		bSuccess = false;
	}

	// Do NOT Rename/re-outer RTs onto this actor — breaks Live Coding Blueprint reinstancer / CDO.
	return bSuccess;
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
	LogRT(TEXT("HeightmapRTA"), HeightmapRTA);
	LogRT(TEXT("HeightmapRTB"), HeightmapRTB);
	LogRT(TEXT("JumpFloodRTA"), JumpFloodRTA);
	LogRT(TEXT("JumpFloodRTB"), JumpFloodRTB);
	LogRT(TEXT("DepthAndShapeRT"), DepthAndShapeRT);
	LogRT(TEXT("RoadDepthRT"), RoadDepthRT);
	LogRT(TEXT("CombinedShapeAndHeightRTA"), CombinedShapeAndHeightRTA);
	LogRT(TEXT("CombinedShapeAndHeightRTB"), CombinedShapeAndHeightRTB);
	LogRT(TEXT("WeightmapRTA"), WeightmapRTA);
	LogRT(TEXT("WeightmapRTB"), WeightmapRTB);
	LogRT(TEXT("CSCombinedWriteRT"), CSCombinedWriteRT);
	LogRT(TEXT("CSCombinedWriteRTB"), CSCombinedWriteRTB);

	// Log-only preview: opening Transient RTs in AssetEditor during Live Coding is unsafe.
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RT slots may still show None in Details (Transient). Use Log sizes above; content fills after WorldEditor paint."));
}

void AWorldSceneManagement::DebugOpenHeightFalloffRT()
{
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		return;
	}

	AllocateRTs();
	UTextureRenderTarget2D* SolidRT = RoadDepthRT;
	UTextureRenderTarget2D* BlendRT = CombinedShapeAndHeightRTA ? CombinedShapeAndHeightRTA : HeightmapRTA;
	UTextureRenderTarget2D* ProfileRT = CSCombinedWriteRT ? CSCombinedWriteRT : CSCombinedWriteRTB;
	UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Debug RTs on '%s':"), *GetActorLabel());
	UE_LOG(LogTemp, Warning, TEXT("  SOLID (Edge Offset)    = RoadDepthRT / %s"),
		SolidRT ? *SolidRT->GetName() : TEXT("None — refresh landscape first"));
	UE_LOG(LogTemp, Warning, TEXT("  BLEND (Falloff Width)  = CombinedShapeAndHeightRTA / %s"),
		BlendRT ? *BlendRT->GetName() : TEXT("None — refresh landscape first"));
	UE_LOG(LogTemp, Warning, TEXT("  PROFILE (height CS)    = %s"),
		ProfileRT ? *ProfileRT->GetPathName() : TEXT("None — drag a road first"));
	UE_LOG(LogTemp, Warning, TEXT("  Details path: World Outliner → %s → Render Targets"), *GetActorLabel());

	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			if (ProfileRT)
			{
				AssetEditors->OpenEditorForAsset(ProfileRT);
			}
			if (BlendRT)
			{
				AssetEditors->OpenEditorForAsset(BlendRT);
			}
			if (SolidRT)
			{
				AssetEditors->OpenEditorForAsset(SolidRT);
			}
		}
	}
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

	AddDependencyIfValid(BrushAngleFalloffMaterial, OutDependencies);
	AddDependencyIfValid(BrushWidthFalloffMaterial, OutDependencies);
	AddDependencyIfValid(DistanceFieldCacheMaterial, OutDependencies);
	AddDependencyIfValid(RenderRoadSplineDepthMaterial, OutDependencies);
	AddDependencyIfValid(DebugDistanceFieldMaterial, OutDependencies);
	AddDependencyIfValid(WeightmapMaterial, OutDependencies);
	AddDependencyIfValid(CompositeRoadBodyTextureMaterial, OutDependencies);
	AddDependencyIfValid(IslandFalloffMaterial, OutDependencies);
	AddDependencyIfValid(JumpStepMaterial, OutDependencies);
	AddDependencyIfValid(FindEdgesMaterial, OutDependencies);
	AddDependencyIfValid(BlurEdgesMaterial, OutDependencies);

	if (DrawCanvasMaterial != nullptr)
	{
		AddDependencyIfValid(DrawCanvasMaterial, OutDependencies);
	}

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
	USplineRoadEditorSubsystem* RoadEditorSubsystem = GEditor->GetEditorSubsystem<USplineRoadEditorSubsystem>();
	if (RoadEditorSubsystem&& World != nullptr)
	{

			FVector RTWorldLocation, RTWorldSizeVector;
			ComputeWorldLandscapeInfo(RTWorldLocation, RTWorldSizeVector);


			UMaterialParameterCollection* LandscapeCollection = RoadEditorSubsystem->GetLandscapeMaterialParameterCollection();
			if (!LandscapeCollection)return;

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
	SetupDefaultMaterials();

	if (!AllocateRTs())
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Invalid Render Target for Water Brush. Aborting BrushRenderSetup."));
		return false;
	}

	if (::IsValid(DebugDistanceFieldMaterial))
	{
		//UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(AActor::AddComponent(FName(TEXT("NODE_AddStaticMeshComponent-0")), false, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, WorldSize), this), ECastCheckedType::NullAllowed);
		if (DebugDistanceFieldMaterial->IsA<UMaterialInstanceDynamic>())
		{
			//UE_LOG(LogWaterEditor, Error, TEXT("Invalid DebugDistanceFieldMaterial Material : must be either a Material Instance Constant or a Material"));
		}
		else
		{
			// Transient MID : no outer, no name : 
			DebugDistanceFieldMID = UMaterialInstanceDynamic::Create(DebugDistanceFieldMaterial, nullptr);
			//check((DebugDistanceFieldMID != nullptr) && (DebugDistanceFieldMID->GetMaterial() == DebugDistanceFieldMaterial->GetMaterial()));
			//StaticMeshComponent->SetMaterial(0, DebugDistanceFieldMID);

			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("ShowGrid")), (float)ShowGrid);
			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("ShowDistance")), (float)ShowDistance);
			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("ShowGradient")), (float)ShowGradient);
			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("DistanceDivisor")), DistanceDivisor);
		}
	}

	UpdateBrushCacheKeys();
	//UpdateCurveCacheKeys();
	//UpdateCurves();
	SetMPCParams();

	if (!CreateMIDs())
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid material setup for Road Brush. Aborting BrushRenderSetup."));
		return false;
	}

	// Success at last!
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
bool AWorldSceneManagement::CreateMIDs()
{
	auto LogBrushMaterial = [](const TCHAR* Label, UMaterialInterface* Material)
	{
		if (Material == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] Material MISSING: %s"), Label);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Material OK: %s -> %s"), Label, *Material->GetPathName());
		}
	};

	LogBrushMaterial(TEXT("BrushAngleFalloff"), BrushAngleFalloffMaterial);
	LogBrushMaterial(TEXT("BrushWidthFalloff"), BrushWidthFalloffMaterial);
	LogBrushMaterial(TEXT("Weightmap"), WeightmapMaterial);
	LogBrushMaterial(TEXT("DistanceFieldCache"), DistanceFieldCacheMaterial);
	LogBrushMaterial(TEXT("CompositeRoadBody"), CompositeRoadBodyTextureMaterial);
	LogBrushMaterial(TEXT("RenderRoadDepth"), RenderRoadSplineDepthMaterial);
	LogBrushMaterial(TEXT("DrawCanvas"), DrawCanvasMaterial);

	BrushAngleFalloffMID = FWaterUtils::GetOrCreateTransientMID(BrushAngleFalloffMID, TEXT("BrushAngleFalloffMID"), BrushAngleFalloffMaterial);
	//IslandFalloffMID = FWaterUtils::GetOrCreateTransientMID(IslandFalloffMID, TEXT("IslandFalloffMID"), IslandFalloffMaterial);
	BrushWidthFalloffMID = FWaterUtils::GetOrCreateTransientMID(BrushWidthFalloffMID, TEXT("BrushWidthFalloffMID"), BrushWidthFalloffMaterial);
	WeightmapMID = FWaterUtils::GetOrCreateTransientMID(WeightmapMID, TEXT("WeightmapMID"), WeightmapMaterial);
	DistanceFieldCacheMID = FWaterUtils::GetOrCreateTransientMID(DistanceFieldCacheMID, TEXT("DistanceFieldCacheMID"), DistanceFieldCacheMaterial);
	CompositeRoadBodyTextureMID = FWaterUtils::GetOrCreateTransientMID(CompositeRoadBodyTextureMID, TEXT("CompositeRoadrBodyTextureMID"), CompositeRoadBodyTextureMaterial);
	DrawCanvasMID = FWaterUtils::GetOrCreateTransientMID(DrawCanvasMID, TEXT("DrawCanvasMID"), DrawCanvasMaterial);

	if ((BrushAngleFalloffMID == nullptr)
		//|| (IslandFalloffMID == nullptr)
		|| (BrushWidthFalloffMID == nullptr)
		|| (WeightmapMID == nullptr)
		|| (DistanceFieldCacheMID == nullptr)
		|| (CompositeRoadBodyTextureMID == nullptr))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid Road brush materials."));
		return false;
	}

	if (DrawCanvasMID == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] DrawCanvas material unavailable (optional for mesh-based roads)."));
	}

	return true;
}
void AWorldSceneManagement::ModifyCaptureToTerrain(TObjectPtr<ALandscape> InOwningLandscape)
{
	if (InOwningLandscape)
	{
		 
		const FBox LandscapeBounds = InOwningLandscape->GetCompleteBounds();
		FVector Center = LandscapeBounds.GetCenter();
		FVector Extent =  LandscapeBounds.GetExtent();
		float SafeHeight = 50000.0f;
		if (SceneCapture)
		{
			SceneCapture->SetWorldLocation(FVector(Center.X, Center.Y, SafeHeight));
		}
		float MaxSide = FMath::Max(Extent.X, Extent.Y);
		float CalculatedWidth = MaxSide * 2.0f;
		SceneCapture->OrthoWidth = CalculatedWidth * 1.05f;
	}


}

void AWorldSceneManagement::AddDependencyIfValid(UObject* Dependency, TSet<UObject*>& OutDependencies)
{
	if (IsValid(Dependency))
	{
		OutDependencies.Add(Dependency);
	}
}

void AWorldSceneManagement::SetupDefaultMaterials()
{
	using namespace MetaLandRoadBrushMaterials;

	BrushAngleFalloffMaterial = LoadMaterial(BrushAngleLocal, BrushAngleOfficial, TEXT("BrushAngleFalloff"));
	BrushWidthFalloffMaterial = LoadMaterial(BrushWidthLocal, BrushWidthOfficial, TEXT("BrushWidthFalloff"));
	DistanceFieldCacheMaterial = LoadMaterial(CacheDistanceFieldLocal, CacheDistanceFieldOfficial, TEXT("CacheDistanceField"));
	RenderRoadSplineDepthMaterial = LoadMaterial(RenderSplineDepthsLocal, RenderSplineDepthsOfficial, TEXT("RenderSplineDepths"));
	WeightmapMaterial = LoadMaterial(BrushWeightmapLocal, BrushWeightmapOfficial, TEXT("BrushWeightmap"));
	DrawCanvasMaterial = nullptr;
	CompositeRoadBodyTextureMaterial = LoadMaterial(CompositeBodyLocal, CompositeBodyOfficial, TEXT("CompositeMeshBodyTexture"));
	JumpStepMaterial = LoadMaterial(JumpFloodStepLocal, JumpFloodStepOfficial, TEXT("JumpFloodStep"));
	BlurEdgesMaterial = LoadMaterial(BlurEdgesLocal, BlurEdgesOfficial, TEXT("BlurEdges"));
	FindEdgesMaterial = LoadMaterial(FindEdgesLocal, FindEdgesOfficial, TEXT("DetectEdges"));
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
		InvalidateBrushDistanceFieldCache();
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Soft-edge repair applied — distance-field cache invalidated"));
	}

	if (!bHeightmapRender)
	{
		OutRoads.RemoveAll([&WeightmapLayerName](const AMoKuEditBaseActor* Road)
		{
			return !Road->GetLayerWeightmapSettings().Contains(WeightmapLayerName);
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

#if WITH_EDITOR
TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> AWorldSceneManagement::GetRenderItems(
	const UE::Landscape::EditLayers::FMergeContext* InMergeContext) const
{
	// Do not shrink InputWorldArea to the road AABB. That partition leaves Grass
	// (LB_WEIGHT_BLEND) on one side of the box and a remixed weight on the other —
	// a hard seam and the wrong grass color. Engine RenderArea per component is enough.
	return Super::GetRenderItems(InMergeContext);
}
#endif

UTextureRenderTarget2D* AWorldSceneManagement::RenderLayer_Native(const FLandscapeBrushParameters& InParameters)
{

	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldSceneManagement::RenderLayer_Native);

	const bool bHeightmapRender = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RenderLayer BEGIN type=%s layer=%s"),
		bHeightmapRender ? TEXT("Height") : TEXT("Weight"),
		*InParameters.WeightmapLayerName.ToString());

	LandscapeRTRef = InParameters.CombinedResult;

	FEditorBrushRenderContext BrushRenderContext;
	BrushRenderContext.bHeightmapRender = bHeightmapRender;
	BrushRenderContext.WeightmapLayerName = InParameters.WeightmapLayerName;
	BrushRenderContext.RenderAreaWorldTransform = InParameters.RenderAreaWorldTransform;
	BrushRenderContext.RenderAreaSize = InParameters.RenderAreaSize;

	if (!BrushRenderSetup())
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] RenderLayer ABORT: BrushRenderSetup failed (check materials / RT allocation)"));
		return nullptr;
	}

	if (BrushRenderContext.bHeightmapRender)
	{
		const FLinearColor ClearColor(0.000000, 0.000000, RoadClearHeight, 0.000000);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CombinedShapeAndHeightRTA, ClearColor);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CombinedShapeAndHeightRTB, ClearColor);
	}
	else
	{
		// Clear weight ping-pong once per layer pass (not per-road — that wiped previous strokes).
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
			: HeightPingPongRead(BrushRenderContext);
	}
	else
	{
		ReturnRT = BrushRenderContext.CSWeightWriteRT
			? BrushRenderContext.CSWeightWriteRT
			: WeightPingPongRead(BrushRenderContext);
	}
	
	bKillCache = false;
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RenderLayer END type=%s layer=%s roads=%d drawn=%d return=%s"),
		bHeightmapRender ? TEXT("Height") : TEXT("Weight"),
		*InParameters.WeightmapLayerName.ToString(),
		RoadList.Num(),
		RoadsDrawn,
		ReturnRT ? *ReturnRT->GetName() : TEXT("null"));
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
//void AWorldSceneManagement::OctreeRemoveElement(UMoKuEditSplinesComponent* InElement)
//{
//	if (!InElement)return;
//	TArray<FOctreeElementId2> OctreeIds = InElement->OctreeIds;
//	for (int i = 0; i < OctreeIds.Num(); i++)
//	{
//		if (Octree.IsValidElementId(OctreeIds[i]))
//		{	
//			Octree.RemoveElement(OctreeIds[i]);
//		}
//	}
//	InElement->OctreeIds.Empty();
//
//}
//
//void AWorldSceneManagement::OctreeAddElement(UMoKuEditSplinesComponent* InElement)
//{
//	if (!InElement)return;
//	const TArray<FMoKuSplineInterpPoint>PointsData = InElement->GetProceduralPoints();
//	for (int32 i = 0; i < PointsData.Num(); i++)
//	{
//		FSplineSegmentOctree NewElement(InElement, i);
//		Octree.AddElement(NewElement);
//	}
//}
void AWorldSceneManagement::StampOneRoadBrushPass(
	FEditorBrushRenderContext& BrushRenderContext,
	FBrushActorRenderContext& BrushActorRenderContext,
	bool bForceRecapture)
{
	if (BrushActorRenderContext.CacheContainer && !BrushActorRenderContext.CacheContainer->Cache.CacheIsValid)
	{
		UseDynamicPreviewRT = true;
	}

	AMoKuEditBaseActor* BasicBody = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();
	const bool bNeedCapture = bForceRecapture
		|| !BrushActorRenderContext.CacheContainer
		|| !BrushActorRenderContext.CacheContainer->Cache.CacheIsValid
		|| bKillCache;
	if (bNeedCapture)
	{
		const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
		const FRoadBrushEffectCurlNoise& CurlNoise = HeightmapSettings.Effects.CurlNoise;
		const FLinearColor CurlColor = DisableBrushTextureEffects
			? FLinearColor(CurlNoise.Curl1Tiling, 0.0f, CurlNoise.Curl2Tiling, 0.0f)
			: FLinearColor(CurlNoise.Curl1Tiling, CurlNoise.Curl1Amount, CurlNoise.Curl2Tiling, CurlNoise.Curl2Amount);
		if (BasicBody != nullptr)
		{
			// Capture the visible mesh Z so the landscape can sit under the road.
			GCaptureFlattenToSplineZ = false;
			GCaptureLateralDilateCm = 0.0f;
			GCaptureFalloffSkirtCm = 0.0f;
			GCaptureTipExtendCm = 0.0f;
			ON_SCOPE_EXIT
			{
				GCaptureFlattenToSplineZ = false;
				GCaptureLateralDilateCm = 0.0f;
				GCaptureFalloffSkirtCm = 0.0f;
				GCaptureTipExtendCm = 0.0f;
			};
			CaptureRoadDepth(BrushActorRenderContext);
			if (JumpFloodComponent2D != nullptr)
			{
				JumpFloodComponent2D->JumpFlood(DepthAndShapeRT, 50000.0f, CurlColor, true, 0.0f);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Canvas shape render"));
			DrawCanvasShape(BrushActorRenderContext);
			UKismetRenderingLibrary::ClearRenderTarget2D(this, DepthAndShapeRT, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
			if (JumpFloodComponent2D != nullptr)
			{
				JumpFloodComponent2D->JumpFlood(DepthAndShapeRT, 50000.0f, CurlColor, false, BrushActorRenderContext.GetActor()->GetActorLocation().Z);
			}
		}
		CacheBrushDistanceField(BrushActorRenderContext);
	}

	DrawBrushMaterial(BrushRenderContext, BrushActorRenderContext);
	++BrushRenderContext.RTIndex;
	ApplyToCompositeRoadBodyTexture(BrushRenderContext, BrushActorRenderContext);
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
			FMath::Max(Sample.RightLR.Z, Sample.RightLR.W)
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
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Profile skip %s — influence box misses RenderArea"), *DebugName);
		return true;
	}

	const bool bBoxStamp = InfluenceRect.Width() < StampSize.X || InfluenceRect.Height() < StampSize.Y;
	const FIntRect DrawRect = bBoxStamp
		? InfluenceRect
		: FIntRect(0, 0, StampSize.X, StampSize.Y);

	const FRoadProfileSampleGPU Sample0 = Samples[0];
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Profile %s samples=%u box=%s rect=%d,%d %dx%d Offset=%.1f Falloff=%.1f ZOff=%.1f Stamp=%dx%d"),
		*DebugName,
		SampleCount,
		bBoxStamp ? TEXT("yes") : TEXT("full"),
		DrawRect.Min.X,
		DrawRect.Min.Y,
		DrawRect.Width(),
		DrawRect.Height(),
		Sample0.Falloff.X,
		Sample0.Falloff.Y,
		Sample0.Falloff.Z,
		StampSize.X,
		StampSize.Y);

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

bool AWorldSceneManagement::StampRoadCenterlineProfileCS(
	FEditorBrushRenderContext& BrushRenderContext,
	AMoKuEditSplineActor* Road,
	uint32 WeightMode,
	const FRoadBodyWeightmapSettings* WMSettings)
{
	TArray<FRoadProfileSampleGPU> Samples;
	if (!BuildRoadProfileSamples(Road, LandscapeTransform, Samples))
	{
		return false;
	}
	if (WeightMode > 0 && WMSettings)
	{
		ApplyWeightmapToProfileSamples(Samples, *WMSettings);
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
	uint32 WeightMode,
	const FRoadBodyWeightmapSettings* WMSettings)
{
	TArray<FRoadProfileSampleGPU> Samples;
	if (!BuildJunctionProfileSamples(Junction, LandscapeTransform, Samples))
	{
		return false;
	}
	if (WeightMode > 0 && WMSettings)
	{
		ApplyWeightmapToProfileSamples(Samples, *WMSettings);
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
		if (!(BrushActorRenderContext.RoadBrushActor->GetLayerWeightmapSettings().Find(BrushRenderContext.WeightmapLayerName)))
		{
			UE_LOG(LogTemp,Warning, TEXT("Actor does NOT affect this layer, Skipping"));
			return;
		}
	}

	const uint32 WeightMode = BrushRenderContext.bHeightmapRender
		? 0u
		: GetProfileWeightMode(BrushRenderContext.WeightmapLayerName);
	const FRoadBodyWeightmapSettings* WMSettings = BrushRenderContext.bHeightmapRender
		? nullptr
		: BrushActorRenderContext.RoadBrushActor->GetLayerWeightmapSettings().Find(BrushRenderContext.WeightmapLayerName);

	if (AMoKuEditSplineActor* ProfileRoad = BrushActorRenderContext.TryGetActorAs<AMoKuEditSplineActor>())
	{
		if (StampRoadCenterlineProfileCS(BrushRenderContext, ProfileRoad, WeightMode, WMSettings))
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
		if (StampJunctionProfileCS(BrushRenderContext, Junction, WeightMode, WMSettings))
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



void AWorldSceneManagement::GetRoadCacheKey(AActor* RoadBrush, /*out*/ URoadBodyBrushCacheContainer*& ContainerObject, /*out*/ FRoadBodyBrushCache& Value)
{
	ContainerObject = CastChecked<URoadBodyBrushCacheContainer>(GetActorCache(RoadBrush, URoadBodyBrushCacheContainer::StaticClass()), ECastCheckedType::NullAllowed);
	if (IsValid(ContainerObject))
	{
		Value = ContainerObject->Cache;
	}
}
void AWorldSceneManagement::UpdateBrushCacheKeys()
{
	for (TWeakInterfacePtr<IRoadBrushActorInterface> BrushActor : GetActorsAffectingLandscape())
	{
		if (BrushActor.IsValid())
		{
			//��������
			ETextureRenderTargetFormat Format = ETextureRenderTargetFormat::RTF_RGBA32f;

			AActor* Actor = CastChecked<AActor>(BrushActor.GetObject());
			URoadBodyBrushCacheContainer* RoadBrushCacheContainer = Cast<URoadBodyBrushCacheContainer>(GetActorCache(Actor, URoadBodyBrushCacheContainer::StaticClass()));
			if (!::IsValid(RoadBrushCacheContainer))
			{
				RoadBrushCacheContainer = NewObject<URoadBodyBrushCacheContainer>(this, NAME_None, RF_Transactional);
				check(!RoadBrushCacheContainer->Cache.CacheIsValid);
				SetActorCache(Actor, RoadBrushCacheContainer);
			}
			// Make sure there's an appropriate render target in that cache : 
			RoadBrushCacheContainer->Cache.CacheRenderTarget = FWaterUtils::GetOrCreateTransientRenderTarget2D(RoadBrushCacheContainer->Cache.CacheRenderTarget, FName(*FString::Printf(TEXT("BrushCacheRT_%s"), *Actor->GetActorNameOrLabel())), LandscapeRTRes, Format);
			check(RoadBrushCacheContainer->Cache.CacheRenderTarget != nullptr);
		}
	}

}


void AWorldSceneManagement::DrawCanvasShape(const FBrushActorRenderContext& BrushActorRenderContext)
{

	TArray<FCanvasUVTri> CanvasUVTris;

	//UE_LOG(LogWaterEditor, Verbose, TEXT("Actor used for Spline Canvas Render: %s"), *UKismetSystemLibrary::GetDisplayName(BrushActorRenderContext.WaterBrushActor.GetObject()));

	UMoKuEditSplinesComponent* SplineComponent = CastChecked<UMoKuEditSplinesComponent>(BrushActorRenderContext.GetActor()->GetComponentByClass(UMoKuEditSplinesComponent::StaticClass()));
	ensure(SplineComponent);

	int32 TruncSegments = FMath::TruncToInt(SplineComponent->GetSplineLength() / CanvasSegmentSize);
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Spline Segment Canvas Segments: %d"), TruncSegments);

	for (int32 ii = 0; ii < TruncSegments; ++ii)
	{
		FVector LocationA = SplineComponent->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
		FVector LocationB = SplineComponent->GetLocationAtDistanceAlongSpline(float(ii + 1) * CanvasSegmentSize, ESplineCoordinateSpace::World);
		FVector LocationC = SplineComponent->GetLocationAtDistanceAlongSpline(float(ii + 2) * CanvasSegmentSize, ESplineCoordinateSpace::World);

		LocationA = LandscapeTransform.InverseTransformPosition(LocationA) + 0.5f;
		LocationB = LandscapeTransform.InverseTransformPosition(LocationB) + 0.5f;
		LocationC = LandscapeTransform.InverseTransformPosition(LocationC) + 0.5f;

		FCanvasUVTri& CanvasUVTri = CanvasUVTris.AddDefaulted_GetRef();
		CanvasUVTri.V0_Pos = FVector2D(LocationA);
		CanvasUVTri.V0_UV = FVector2D::ZeroVector;
		CanvasUVTri.V0_Color = FLinearColor::Red;
		CanvasUVTri.V1_Pos = FVector2D(LocationC);
		CanvasUVTri.V1_UV = FVector2D::ZeroVector;
		CanvasUVTri.V1_Color = FLinearColor::Green;
		CanvasUVTri.V2_Pos = FVector2D(LocationB);
		CanvasUVTri.V2_UV = FVector2D::ZeroVector;
		CanvasUVTri.V2_Color = FLinearColor::Blue;

	}


}


UTextureRenderTarget2D* AWorldSceneManagement::HeightPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex == 0)
	{
		return LandscapeRTRef;
	}
	else if (BrushRenderContext.RTIndex % 2)// Odd
	{
		return HeightmapRTA;
	}
	else // Even
	{
		return HeightmapRTB;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement::HeightPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex % 2)// Odd
	{
		return HeightmapRTB;
	}
	else // Even
	{
		return HeightmapRTA;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement::WeightPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex == 0)
	{
		UE_LOG(LogTemp,Warning,TEXT("1234"))
		return LandscapeRTRef;
	}
	else if (BrushRenderContext.RTIndex % 2)// Odd
	{
		UE_LOG(LogTemp, Warning, TEXT("0"))
		return WeightmapRTA;
	}
	else // Even
	{
		UE_LOG(LogTemp, Warning, TEXT("1"))
		return WeightmapRTB;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement::WeightPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex % 2)// Odd
	{
		return WeightmapRTB;
	}
	else // Even
	{
		return WeightmapRTA;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement:: RoadDepthPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RoadRTIndex % 2)
	{
		return CombinedShapeAndHeightRTA;
	}
	else
	{
		return CombinedShapeAndHeightRTB;
	}

}
UTextureRenderTarget2D* AWorldSceneManagement::RoadDepthPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RoadRTIndex % 2)
	{
		return CombinedShapeAndHeightRTB;
	}
	else
	{
		return CombinedShapeAndHeightRTA;
	}

}

void AWorldSceneManagement::ApplyToCompositeRoadBodyTexture(FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext)
{

	AMoKuEditBaseActor* RoadBody = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();
	if (BrushRenderContext.bHeightmapRender && (RoadBody != nullptr))
	{
		check(::IsValid(BrushActorRenderContext.CacheContainer));
		const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();

		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("CombinedShapeAndHeight")), RoadDepthPingPongRead(BrushRenderContext));
		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("LandscapeHeight")), HeightPingPongRead(BrushRenderContext));
		// Clamp: user/serialized ZOffset of tens of thousands breaks Min blend (terrain collapses → road looks floating).
		const float SafeZOffset = FMath::Clamp(ResolveStampFalloff(HeightmapSettings).ZOffset, -2000.f, 2000.f);
		CompositeRoadBodyTextureMID->SetScalarParameterValue(FName(TEXT("ZOffset")), SafeZOffset);
		//CompositeWaterBodyTextureMID->SetScalarParameterValue(FName(TEXT("Shape Dilation")), RoadBody->GetWaterBodyComponent()->ShapeDilation);
		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("RoadDepthRT")), RoadDepthRT);
		//UE_LOG(LogTemp, Warning, TEXT("Rendering Water Body Velocity/Height to Combined Texture: %s"), *UKismetSystemLibrary::GetDisplayName(VelocityPingPongWrite(BrushRenderContext)));

		UKismetRenderingLibrary::ClearRenderTarget2D(this, RoadDepthPingPongWrite(BrushRenderContext), FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RoadDepthPingPongWrite(BrushRenderContext), CompositeRoadBodyTextureMID);

		++BrushRenderContext.RoadRTIndex;
	}








}

void AWorldSceneManagement::SetBrushMIDParams(const FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext)
{
	if (BrushRenderContext.bHeightmapRender)
	{
		ApplyRoadLandscapeKeysToBrushSettings(BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>());
	}
	const FSplineRoadHeightmapSettings&  HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadFalloffSettings& StampFalloff = ResolveStampFalloff(HeightmapSettings);
	if (JumpFloodComponent2D != nullptr)
	{
		if (BrushRenderContext.bHeightmapRender)
		{
			// Height only: a few blur passes kill landscape-quad stair-steps on the falloff.
			// Weight stays sharp — blur there was the speckled pavement edge.
			JumpFloodComponent2D->UseBlur = true;
			JumpFloodComponent2D->BlurPasses = FMath::Clamp(
				FMath::Max(HeightmapSettings.Effects.Blurring.Radius, 3), 3, 8);
		}
		else if (DisableBrushTextureEffects)
		{
			JumpFloodComponent2D->UseBlur = false;
			JumpFloodComponent2D->BlurPasses = 0;
		}
		else
		{
			JumpFloodComponent2D->UseBlur = HeightmapSettings.Effects.Blurring.bBlurShape;
			JumpFloodComponent2D->BlurPasses = FMath::Clamp(HeightmapSettings.Effects.Blurring.Radius, 0, 16);
		}
	}
	if (BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>() != nullptr)
	{
		if (StampFalloff.FalloffMode == ERoadBrushFalloffMode::Angle)
		{
			BrushActorRenderContext.MID = BrushAngleFalloffMID;
		}
		else
		{
			BrushActorRenderContext.MID = BrushWidthFalloffMID;
		}
	}
	DistanceFieldCaching(BrushActorRenderContext);
	CurvesSmoothingAndTerracing(BrushActorRenderContext);

	if (BrushRenderContext.bHeightmapRender)
	{
		BrushActorRenderContext.MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("HeightRT")), HeightPingPongRead(BrushRenderContext));
		BrushActorRenderContext.MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("CombinedShapeAndHeightRT")), RoadDepthPingPongRead(BrushRenderContext));

		FalloffAndBlendMode(BrushActorRenderContext);
		DisplacementSettings(BrushActorRenderContext);
	}
	else
	{
		const FRoadBodyWeightmapSettings* WMSettings = BrushActorRenderContext.RoadBrushActor->GetLayerWeightmapSettings().Find(BrushRenderContext.WeightmapLayerName);
		if (WMSettings)
		{
			ApplyWeightmapSettings(BrushRenderContext, BrushActorRenderContext, *WMSettings);
			// Max blend: keep the hardest pavement core when multiple roads overlap (avoid diluted mix).
			if (WeightmapMID)
			{
				WeightmapMID->SetScalarParameterValue(FName(TEXT("BlendMode")), 2.0f);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Weight layer '%s' not in road RoadBodyWeightmapSettings — skipped"),
				*BrushRenderContext.WeightmapLayerName.ToString());
		}
	}

}
void AWorldSceneManagement::DrawBrushMaterial(const FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext)
{
	if (BrushRenderContext.bHeightmapRender)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, HeightPingPongWrite(BrushRenderContext), FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, HeightPingPongWrite(BrushRenderContext), BrushActorRenderContext.MID);
	}
	else
	{
		// Do not Clear here — layer pass cleared once; each road composites via HeightRT=prev ping-pong.
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, WeightPingPongWrite(BrushRenderContext), WeightmapMID);
	}
}

void AWorldSceneManagement::DistanceFieldCaching(const FBrushActorRenderContext& BrushActorRenderContext)
{
	check(::IsValid(BrushActorRenderContext.MID));
	check(::IsValid(DistanceFieldCacheMID));
	check(::IsValid(BrushActorRenderContext.CacheContainer));

	BrushActorRenderContext.MID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("MeshDepth")), DepthAndShapeRT);

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();

	float JumpFloodRTValue = LandscapeRTRes.GetMax();
	JumpFloodRTValue = FMath::Log2(JumpFloodRTValue);

	// Must match the blur pass count actually used by JumpFlood (weight path may boost BlurPasses).
	const int32 EffectiveBlurPasses = (JumpFloodComponent2D != nullptr)
		? FMath::Clamp(JumpFloodComponent2D->BlurPasses, 0, 16)
		: FMath::Clamp(HeightmapSettings.Effects.Blurring.Radius, 0, 16);
	const bool bEffectiveUseBlur = (JumpFloodComponent2D != nullptr)
		? JumpFloodComponent2D->UseBlur
		: HeightmapSettings.Effects.Blurring.bBlurShape;

	int32 Ceiling = FMath::CeilToInt(JumpFloodRTValue);
	Ceiling += EffectiveBlurPasses;

	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("JumpFloodRT")), (Ceiling % 2 == 0) ? JumpFloodRTA : JumpFloodRTB);

	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("UseBlur")), bEffectiveUseBlur ? 1.0f : 0.0f);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("Bluroffset")), (float)EffectiveBlurPasses);

	AMoKuEditBaseActor* RoadBodyBasic = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();

	// Match Water: Invert only for ocean-like exterior shapes. Roads stay 0 (same as River).
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("Invert")), 0.0f);

	const FRoadBrushEffectCurlNoise& CurlNoise = HeightmapSettings.Effects.CurlNoise;
	const FLinearColor NoiseColor = DisableBrushTextureEffects
		? FLinearColor(CurlNoise.Curl1Tiling, 0.0f, CurlNoise.Curl2Tiling, 0.0f)
		: FLinearColor(CurlNoise.Curl1Tiling, CurlNoise.Curl1Amount, CurlNoise.Curl2Tiling, CurlNoise.Curl2Amount);
	DistanceFieldCacheMID->SetVectorParameterValue(FName(TEXT("Curl")), NoiseColor);

	const float SafeZOffset = FMath::Clamp(ResolveStampFalloff(HeightmapSettings).ZOffset, -2000.f, 2000.f);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("ZOffset")), SafeZOffset);
	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("ChannelDepth")), RoadDepthRT);

	// Weight/DF can use mesh silhouette; height raise only when |ZOffset| > 0 (see FalloffAndBlendMode).
	const bool bDoMeshDepth = (RoadBodyBasic != nullptr);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), bDoMeshDepth ? 1.0f : 0.0f);
}

void AWorldSceneManagement::FalloffAndBlendMode(const FBrushActorRenderContext& BrushActorRenderContext)
{
	check(::IsValid(BrushActorRenderContext.MID));

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadFalloffSettings& FalloffSettings = ResolveStampFalloff(HeightmapSettings);

	const bool bWidthMode = FalloffSettings.FalloffMode == ERoadBrushFalloffMode::Width;
	// Width mode: MeshBrush often does min(EdgeWidth, height/tan). The greyed 35° was
	// locking the slope. 1° makes the angle limit huge so Width actually changes grade.
	const float FalloffTangentValue = FMath::Tan(FMath::DegreesToRadians(
		bWidthMode ? 1.0f : FMath::Clamp(FalloffSettings.FalloffAngle, 1.0f, 89.0f)));

	const float LandscapeCmPerQuad = FMath::Max3(
		FMath::Abs((float)LandscapeTransform.GetScale3D().X),
		FMath::Abs((float)LandscapeTransform.GetScale3D().Y),
		1.0f);
	const float MinHeightFalloffCm = LandscapeCmPerQuad;
	const float SolidShoulderCm = FMath::Max(FalloffSettings.EdgeOffset, 0.0f);
	const float GradientCm = bWidthMode
		? FMath::Max(FalloffSettings.FalloffWidth, MinHeightFalloffCm)
		: MinHeightFalloffCm;

	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("FalloffTangent")), FalloffTangentValue);
	// Always mesh-depth for height MID — toggling this off produced cliff/wall corruption.
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), 1.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("CapShapeInterior")), 1.0f);
	// Water MeshBrush_Width keeps one contour (EdgeCenterOffset) and fades a band
	// CENTERED on it. CapShapeInterior fills everything inside that contour, so the
	// inner half of EdgeWidth is hidden. Double the user Width so the visible outer
	// half equals Falloff Width. Do not move the contour by Width/2 — that grew the cap.
	const float EdgeWidthValue = bWidthMode ? (GradientCm * 2.0f) : GradientCm;
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("EdgeWidth")), EdgeWidthValue);
	const float EdgeCenterOffsetValue = SolidShoulderCm - (SplineMeshExtension / 2.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("EdgeCenterOffset")), EdgeCenterOffsetValue);

	// Height blender: AlphaBlend writes mesh+ZOffset both up and down, with side falloff.
	// Min/Max here made a pit on the cut end and a floating slab on the raise end.
	const float ModeValue = 0.0f;
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("BlendMode")), ModeValue);

	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] HEIGHT actor=%s mode=%s Solid=%.1f OuterGradient=%.1f EdgeWidth=%.1f TotalExtra=%.1f ZOffset=%.1f"),
		*BrushActorRenderContext.GetActor()->GetName(),
		bWidthMode ? TEXT("Width") : TEXT("Angle"),
		SolidShoulderCm, GradientCm, EdgeWidthValue, SolidShoulderCm + GradientCm, FalloffSettings.ZOffset);
}

void AWorldSceneManagement::CurvesSmoothingAndTerracing(const FBrushActorRenderContext& BrushActorRenderContext)
{
	// Match AWaterBrushManager::CurvesSmoothingAndTerracing — Smooth + Terrace + CurveRT pins.
	check(::IsValid(BrushActorRenderContext.MID));
	check(::IsValid(DistanceFieldCacheMID));

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadBrushEffectSmoothBlending& SmoothBlending = HeightmapSettings.Effects.SmoothBlending;
	const FRoadBrushEffectTerracing& Terracing = HeightmapSettings.Effects.Terracing;
	const FRoadFalloffSettings& FalloffSettings = ResolveStampFalloff(HeightmapSettings);

	const FLinearColor SmoothingParamsColor(
		FMath::Max(SmoothBlending.InnerSmoothDistance, 0.01f),
		FMath::Max(SmoothBlending.OuterSmoothDistance, 0.01f),
		0.0f,
		0.0f);
	BrushActorRenderContext.MID->SetVectorParameterValue(FName(TEXT("SmoothingParams")), SmoothingParamsColor);

	// Full elevation-curve channels are Water/River-specific; roads keep channel depth off.
	const float CurveChannelDepthValue = 0.0f;
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("CurveChannelDepth")), CurveChannelDepthValue);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("CurveChannelDepth")), CurveChannelDepthValue);

	const float ChannelEdgeOffsetValue = 0.0f - (SplineMeshExtension / 2.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("ChannelEdgeOffset")), ChannelEdgeOffsetValue);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("ChannelEdgeOffset")), ChannelEdgeOffsetValue);

	const float CurveRampWidth = 512.0f;
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("CurveRampWidth")), CurveRampWidth);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("CurveRampWidth")), CurveRampWidth);

	if (DefaultCurveRT)
	{
		BrushActorRenderContext.MID->SetTextureParameterValue(FName(TEXT("CurveRT")), DefaultCurveRT);
		DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("CurveRT")), DefaultCurveRT);
	}

	const FLinearColor TerraceParamsColor(Terracing.TerraceAlpha, Terracing.TerraceSmoothness, Terracing.TerraceSpacing, Terracing.MaskLength);
	BrushActorRenderContext.MID->SetVectorParameterValue(FName(TEXT("TerraceParams")), TerraceParamsColor);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("TerraceOffset")), Terracing.MaskStartOffset);

	// Keep ZOffset consistent on the height brush MID as well.
	const float SafeZOffset = FMath::Clamp(FalloffSettings.ZOffset, -2000.f, 2000.f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("ZOffset")), SafeZOffset);
}

void AWorldSceneManagement::DisplacementSettings(const FBrushActorRenderContext& BrushActorRenderContext)
{
	// Match AWaterBrushManager::DisplacementSettings (MeshBrush_Width / Angle).
	check(::IsValid(BrushActorRenderContext.MID));

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadBrushEffectDisplacement& Displacement = HeightmapSettings.Effects.Displacement;

	UTexture2D* DisplacementTexture = Displacement.Texture.LoadSynchronous();
	const float BrushTexHeightValue = (!DisableBrushTextureEffects && DisplacementTexture != nullptr)
		? Displacement.DisplacementHeight
		: 0.0f;

	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("BrushTexHeight")), BrushTexHeightValue);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("T")), Displacement.DisplacementTiling);
	BrushActorRenderContext.MID->SetTextureParameterValue(FName(TEXT("BrushRT")), DisplacementTexture);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("Displacement Midpoint")), Displacement.Midpoint);
	BrushActorRenderContext.MID->SetVectorParameterValue(FName(TEXT("DisplacementChannel")), Displacement.Channel);

	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Displacement Height=%.1f Tiling=%.2f Tex=%s Influence(weight)=%.2f"),
		BrushTexHeightValue,
		Displacement.DisplacementTiling,
		DisplacementTexture ? *DisplacementTexture->GetName() : TEXT("None"),
		Displacement.WeightmapInfluence);
}

void AWorldSceneManagement::BlueprintRoadBodyChanged_Native(AActor* Actor)
{
	if (::IsValid(Actor))
	{
		URoadBodyBrushCacheContainer* ContainerObject;
		FRoadBodyBrushCache WaterBrushCache;
		GetRoadCacheKey(Actor, /*out*/ ContainerObject, /*out*/ WaterBrushCache);

		if (::IsValid(ContainerObject))
		{
			ContainerObject->Cache.CacheIsValid = false;
		}
		RequestLandscapeUpdate();
	}
}

bool AWorldSceneManagement::SetupRoadSplineRenderMIDs(const FBrushActorRenderContext& BrushActorRenderContext, bool bRestoreMIDs, TArray<UMaterialInterface*>& InOutMIDs)
{
	AMoKuEditBaseActor* RoadBasicActor = BrushActorRenderContext.GetActorAs<AMoKuEditBaseActor>();
	if (!RoadBasicActor)
	{
		return false;
	}

	// Roads need a spline with >=2 points; junctions use cover DynamicMesh only (no spline).
	UPrimitiveComponent* RenderComponents = nullptr;
	if (AMoKuEditSplineActor* CurrentActor = Cast<AMoKuEditSplineActor>(RoadBasicActor))
	{
		if (CurrentActor->GetSplineComponent() && CurrentActor->GetSplineComponent()->GetNumberOfSplinePoints() > 1)
		{
			RenderComponents = CurrentActor->GetBrushRenderableComponents();
		}
	}
	else
	{
		// AMoKuEditIntersectionActor (and other mesh brush actors)
		RenderComponents = RoadBasicActor->GetBrushRenderableComponents();
		if (UDynamicMeshComponent* DynMesh = Cast<UDynamicMeshComponent>(RenderComponents))
		{
			const UDynamicMesh* MeshObj = DynMesh->GetDynamicMesh();
			if (!MeshObj || MeshObj->GetTriangleCount() <= 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs: junction/mesh actor %s has empty DynamicMesh — skip depth capture"),
					*RoadBasicActor->GetName());
				return false;
			}
		}
	}
	if (!RenderComponents)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs: no brush mesh on %s (%s)"),
			*RoadBasicActor->GetName(), *RoadBasicActor->GetClass()->GetName());
		return false;
	}

	if (bRestoreMIDs)
	{
		int32 NumMats = FMath::Max(1, RenderComponents->GetNumMaterials());
		for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
		{
			//FSoftObjectPath SoftPath(InOutMIDs[MatIdx]->GetMaterial());
			//FString MySoftPathString = SoftPath.ToString();
			//UE_LOG(LogTemp, Warning, TEXT("SoftPathString==%s"), *MySoftPathString)
			// ȷ�������������ж�������ֹԽ�����
			if (InOutMIDs.IsValidIndex(MatIdx))
			{
				RenderComponents->SetMaterial(MatIdx, InOutMIDs[MatIdx]);
			}
		}
	}
	// 4. �滻���ʵ��߼������������ǰ����·�����ǿ�ƻ����Ǹ����߶�ͼ���ʣ�
	else
	{
		// ��̬������������߶�ͼ�Ĳ���ʵ��
		UMaterialInstanceDynamic* TempMID = FWaterUtils::GetOrCreateTransientMID(RoadSplineMID, TEXT("RoadSplineMID"), RenderRoadSplineDepthMaterial);

		// ���޸��߼���ת��������ʴ����ɹ�����ȥ�滻
		if (TempMID)
		{
			InOutMIDs.Empty();
			const int32 NumMats = FMath::Max(1, RenderComponents->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				InOutMIDs.Add(RenderComponents->GetMaterial(MatIdx));
				RenderComponents->SetMaterial(MatIdx, TempMID);
			}
		}
		else
		{
			// �������û��ȡ������ӡ�����沢������Ⱦ
			UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs: failed to create depth MID (check RenderRoadDepth material)"));
			return false;
		}
	}

	return true;
}
void AWorldSceneManagement::CacheBrushDistanceField(const FBrushActorRenderContext& BrushActorRenderContext)
{
	check(::IsValid(BrushActorRenderContext.CacheContainer));
	UKismetRenderingLibrary::ClearRenderTarget2D(this, BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget, FLinearColor::Black);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget, DistanceFieldCacheMID);
	BrushActorRenderContext.CacheContainer->Cache.CacheIsValid = true;
}


void AWorldSceneManagement::ApplyWeightmapSettings(const FEditorBrushRenderContext& BrushRenderContext ,const FBrushActorRenderContext& BrushActorRenderContext, const FRoadBodyWeightmapSettings& WMSettings)
{
	// Match AWaterBrushManager::ApplyWeightmapSettings (MeshBrush_Weightmap).
	check(::IsValid(WeightmapMID));
	check(::IsValid(BrushActorRenderContext.CacheContainer));

	SetMPCParams();

	const float LandscapeCmPerQuad = FMath::Max3(
		FMath::Abs((float)LandscapeTransform.GetScale3D().X),
		FMath::Abs((float)LandscapeTransform.GetScale3D().Y),
		1.0f);

	const UWorldEditorSettings* EditorSettings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayerName = EditorSettings ? EditorSettings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	const FName ShoulderLayerName = EditorSettings ? EditorSettings->GetDefaultShoulderWeightmapLayerName() : FName(TEXT("Dirty"));
	const bool bIsRoadPavement = BrushRenderContext.WeightmapLayerName == RoadLayerName;
	const bool bIsShoulder = BrushRenderContext.WeightmapLayerName == ShoulderLayerName;

	// Road: ~2 quads AA only. Dirty: wider soft ok for slopes (do not force 6 quads onto Road —
	// that turned a whole carriageway into a soft gray mix).
	const float MinSoftFalloffCm = LandscapeCmPerQuad * (bIsRoadPavement ? 2.0f : 4.0f);
	const float MaxFalloffCm = bIsRoadPavement ? 600.f : 2500.f;
	const float SourceFalloffWidth = WMSettings.FalloffWidth;
	const float SourceEdgeOffset = WMSettings.EdgeOffset;
	const float SourceInfluence = WMSettings.TextureInfluence;
	const float SafeFalloffWidth = FMath::Clamp(SourceFalloffWidth, 0.1f, MaxFalloffCm);
	const float GradientWidth = FMath::Max(SafeFalloffWidth, MinSoftFalloffCm);

	// Shoulder may use EdgeOffset up to ±Falloff*2 (push Dirty onto side slopes).
	const float EdgeClamp = bIsShoulder ? (SafeFalloffWidth * 2.0f) : SafeFalloffWidth;
	float SafeEdgeOffset = FMath::Clamp(SourceEdgeOffset, -EdgeClamp, EdgeClamp);
	if (bIsShoulder && FMath::IsNearlyZero(SafeEdgeOffset, 1.0f))
	{
		// Same silhouette as Road + CapShapeInterior → Dirty fills pavement (brown arms in screenshot).
		// Nudge shoulder outward so Dirt sits on the rim/slope; Road keeps the hard core.
		SafeEdgeOffset = SafeFalloffWidth * 0.5f;
	}

	const bool bInfluenceBroken = !FMath::IsFinite(SourceInfluence)
		|| SourceInfluence > 1.0f + KINDA_SMALL_NUMBER
		|| SourceInfluence < 0.0f;
	float SafeInfluence = 0.0f;
	if (bInfluenceBroken)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Weightmap Influence=%.1f broken — using 0 (sawtooth / no material). Set 0..1 in Details."),
			SourceInfluence);
	}
	else
	{
		SafeInfluence = FMath::Clamp(SourceInfluence, 0.0f, 0.35f);
	}

	// Road = solid core + edge AA. Dirty = soft band (no Cap) so it cannot paint-over pavement.
	const float CapShapeInterior = bIsRoadPavement ? 1.0f : 0.0f;

	const float SoftQuads = GradientWidth / LandscapeCmPerQuad;
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Weightmap layer=%s role=%s GradientWidth=%.1f EdgeOffset=%.1f CapInterior=%.0f Opacity=%.2f (~%.1f quads soft)"),
		*BrushRenderContext.WeightmapLayerName.ToString(),
		bIsRoadPavement ? TEXT("Pavement") : (bIsShoulder ? TEXT("Shoulder") : TEXT("Other")),
		GradientWidth, SafeEdgeOffset, CapShapeInterior, WMSettings.FinalOpacity, SoftQuads);

	WeightmapMID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("GradientWidth")), GradientWidth);

	const float EdgeOffsetValue = SafeEdgeOffset - (SplineMeshExtension / 2.0f);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("EdgeOffset")), EdgeOffsetValue);

	WeightmapMID->SetScalarParameterValue(FName(TEXT("CapShapeInterior")), CapShapeInterior);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), 1.0f);

	// Prefer per-layer ModulationTexture; else Landmass Displacement texture for edge noise.
	const FRoadBrushEffectDisplacement& DisplacementFx = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings().Effects.Displacement;
	UTexture2D* ModulationTexture = WMSettings.ModulationTexture.LoadSynchronous();
	if (ModulationTexture == nullptr)
	{
		ModulationTexture = DisplacementFx.Texture.LoadSynchronous();
	}
	const float TextureTiling = WMSettings.ModulationTexture.ToSoftObjectPath().IsValid()
		? WMSettings.TextureTiling
		: DisplacementFx.DisplacementTiling;
	WeightmapMID->SetTextureParameterValue(FName(TEXT("BrushRT")), ModulationTexture);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("T")), TextureTiling);

	// No along-spline 0→1 texture blend on the root line. Lateral AA is GradientWidth only.
	WeightmapMID->SetScalarParameterValue(FName(TEXT("WeightmapInfluence")), 0.0f);

	const float MidpointValue = WMSettings.ModulationTexture.ToSoftObjectPath().IsValid()
		? WMSettings.Midpoint
		: DisplacementFx.Midpoint;
	WeightmapMID->SetScalarParameterValue(FName(TEXT("Displacement Midpoint")), MidpointValue);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("Opacity")), WMSettings.FinalOpacity);

	// CRITICAL: Water MeshBrush_Weightmap reads previous weight as "HeightRT" (engine naming).
	UTextureRenderTarget2D* PrevWeight = WeightPingPongRead(BrushRenderContext);
	WeightmapMID->SetTextureParameterValue(FName(TEXT("HeightRT")), PrevWeight);
	// Some local forks renamed the pin — bind both.
	WeightmapMID->SetTextureParameterValue(FName(TEXT("WeightRT")), PrevWeight);
}


void AWorldSceneManagement::CaptureRoadDepth(const FBrushActorRenderContext& BrushActorRenderContext)
{
	TArray<UMaterialInterface*> MIDsToRestore;

	if (!SetupRoadSplineRenderMIDs(BrushActorRenderContext, /*bRestoreMIDs=*/false, MIDsToRestore))
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs failed before depth capture."));
		return;
	}
	AMoKuEditBaseActor* RoadBody = BrushActorRenderContext.GetActorAs<AMoKuEditBaseActor>();
	const bool Hidden = RoadBody->IsTemporarilyHiddenInEditor();
	RoadBody->SetIsTemporarilyHiddenInEditor(false);

	UPrimitiveComponent* BrushRenderableComponent = RoadBody->GetBrushRenderableComponents();
	if (BrushRenderableComponent && SceneCapture)
	{
		UDynamicMeshComponent* DynMeshComp = Cast<UDynamicMeshComponent>(BrushRenderableComponent);
		AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(RoadBody);
		UMetaLandRoadSplineComponent* RoadSpline = (Road && Road->GetSplineComponent())
			? Road->GetSplineComponent()
			: nullptr;
		UDynamicMesh* DynMesh = DynMeshComp ? DynMeshComp->GetDynamicMesh() : nullptr;
		const bool bNeedFlatten = DynMeshComp && RoadSpline && GCaptureFlattenToSplineZ;
		const bool bNeedDilate = DynMeshComp && RoadSpline && FMath::Abs(GCaptureLateralDilateCm) >= 0.5f;
		const bool bNeedSkirt = DynMeshComp && RoadSpline && GCaptureFalloffSkirtCm >= 1.0f;
		const bool bNeedTipExtend = DynMeshComp && RoadSpline && GCaptureTipExtendCm >= 1.0f;
		FDynamicMesh3 SavedCaptureMesh;
		bool bSavedFullMesh = false;
		if (DynMesh && RoadSpline && (GCaptureClipEnabled || bNeedFlatten || bNeedDilate || bNeedSkirt || bNeedTipExtend))
		{
			DynMesh->ProcessMesh([&](const FDynamicMesh3& Mesh)
			{
				SavedCaptureMesh = Mesh;
				bSavedFullMesh = true;
			});
		}
		TArray<FVector3d> ClipDummyVerts;
		const bool bClipped = (GCaptureClipEnabled && DynMeshComp && RoadSpline)
			? ClipCaptureMeshToSRange(DynMeshComp, RoadSpline, GCaptureClipS0, GCaptureClipS1, ClipDummyVerts)
			: false;
		(void)bClipped;
		if (bNeedFlatten)
		{
			FlattenCaptureMeshToSplineZ(DynMeshComp, RoadSpline);
		}
		if (bNeedDilate)
		{
			DilateCaptureMeshLateral(DynMeshComp, RoadSpline, GCaptureLateralDilateCm);
		}
		if (bNeedSkirt)
		{
			AppendCaptureFalloffSkirt(DynMeshComp, RoadSpline, RoadBody, GCaptureFalloffSkirtCm);
		}
		if (bNeedTipExtend)
		{
			ExtendCaptureMeshTips(DynMeshComp, RoadSpline, GCaptureTipExtendCm);
		}
		ON_SCOPE_EXIT
		{
			if (bSavedFullMesh && DynMesh)
			{
				DynMesh->SetMesh(MoveTemp(SavedCaptureMesh));
				DynMeshComp->NotifyMeshUpdated();
			}
		};

		const bool bPreviousVisibility = BrushRenderableComponent->GetVisibleFlag();
		const bool bPreviousHiddenInGame = BrushRenderableComponent->bHiddenInGame;
		BrushRenderableComponent->SetVisibility(true);
		BrushRenderableComponent->SetHiddenInGame(false);

		SceneCapture->ClearShowOnlyComponents();
		SceneCapture->ShowOnlyComponent(BrushRenderableComponent);
		SceneCapture->TextureTarget = RoadDepthRT;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorSceneDepth;
		SceneCapture->CaptureScene();

		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		SceneCapture->TextureTarget = DepthAndShapeRT;
		SceneCapture->CaptureScene();

		BrushRenderableComponent->SetVisibility(bPreviousVisibility);
		BrushRenderableComponent->SetHiddenInGame(bPreviousHiddenInGame);
		SceneCapture->ClearShowOnlyComponents();

		UE_LOG(LogTemp, Verbose, TEXT("[RoadLandscape] CaptureRoadDepth actor=%s comp=%s"),
			*RoadBody->GetName(), *BrushRenderableComponent->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] CaptureRoadDepth skipped actor=%s (comp=%s SceneCapture=%s) — weight/height stay empty/black"),
			RoadBody ? *RoadBody->GetName() : TEXT("null"),
			BrushRenderableComponent ? *BrushRenderableComponent->GetName() : TEXT("null"),
			SceneCapture ? TEXT("ok") : TEXT("null"));
	}

	RoadBody->SetIsTemporarilyHiddenInEditor(Hidden);
	SetupRoadSplineRenderMIDs(BrushActorRenderContext, /*bRestoreMIDs=*/true, MIDsToRestore);
}


#undef LOCTEXT_NAMESPACE