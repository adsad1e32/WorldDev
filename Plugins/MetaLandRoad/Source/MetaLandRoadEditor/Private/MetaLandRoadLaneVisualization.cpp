#include "MetaLandRoadLaneVisualization.h"

#include "MetaLandRoadLaneEditPick.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MoKuEditSplineActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "DynamicMeshBuilder.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Materials/MaterialRenderProxy.h"
#include "MetaLandRoadVisProxy.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Framework/Application/SlateApplication.h"

/** One-frame colored material for PDI mesh fills; freed via ReleasePrimitiveResource. */
class FMetaLandRoadDynamicColoredMaterialRenderProxy final
	: public FColoredMaterialRenderProxy
	, public FDynamicPrimitiveResource
{
public:
	FMetaLandRoadDynamicColoredMaterialRenderProxy(const FMaterialRenderProxy* InParent, const FLinearColor& InColor)
		: FColoredMaterialRenderProxy(InParent, InColor)
	{
	}

	virtual void InitPrimitiveResource(FRHICommandListBase& RHICmdList) override
	{
	}

	virtual void ReleasePrimitiveResource() override
	{
		delete this;
	}
};
#endif

namespace MetaLandRoadLaneVisualization
{
	namespace Private
	{
		static constexpr uint8 LaneLineDepthPriority = SDPG_Foreground;
		static constexpr uint8 LaneFillDepthPriority = SDPG_Foreground;

		static ULineBatchComponent* GetPersistentLineBatcher(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			const UWorld* World = SplineComponent ? SplineComponent->GetWorld() : nullptr;
			return World ? World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr;
		}

		static uint32 GetVisualizationBatchId(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			return static_cast<uint32>(SplineComponent->GetUniqueID());
		}

		static const AMoKuEditSplineActor* GetRoadActorForSpline(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			return Cast<AMoKuEditSplineActor>(SplineComponent ? SplineComponent->GetOwner() : nullptr);
		}

		/** Clip lane/section overlay S range to visible road mesh (excludes junction trim gaps). */
		static bool IntersectDrawRangeWithMeshSpans(
			const UMetaLandRoadSplineComponent* SplineComponent,
			double InS0,
			double InS1,
			double& OutS0,
			double& OutS1)
		{
			OutS0 = InS0;
			OutS1 = InS1;
			if (InS1 <= InS0 + KINDA_SMALL_NUMBER)
			{
				return false;
			}

			const AMoKuEditSplineActor* Road = GetRoadActorForSpline(SplineComponent);
			if (!Road)
			{
				return true;
			}
			return Road->ClipRangeToJunctionMeshSpans(InS0, InS1, OutS0, OutS1);
		}

		static uint32 GetLegacyNativeEditLineBatchId(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			return GetVisualizationBatchId(SplineComponent) ^ 0x00E01E01u;
		}

		static void ClearLegacyLineBatchOverlays(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			ULineBatchComponent* LineBatch = GetPersistentLineBatcher(SplineComponent);
			if (!LineBatch)
			{
				return;
			}

			LineBatch->ClearBatch(GetVisualizationBatchId(SplineComponent));
			LineBatch->ClearBatch(GetLegacyNativeEditLineBatchId(SplineComponent));
		}

		static bool IsLaneBoundaryValidForSection(
			const FMetaLandRoadLaneSection& Section,
			int32 LaneIndex)
		{
			if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
			{
				return true;
			}
			return Section.CheckLaneIndex(LaneIndex);
		}

		/** Match mesh: break a face only when that side appears or disappears. */
		static int32 FaceLaneCount(const FMetaLandRoadLaneSection& Section, bool bLeftFace)
		{
			if (bLeftFace)
			{
				return MetaLandRoad::HasLeftSide(Section.Side) ? Section.Left.Num() : 0;
			}
			return MetaLandRoad::HasRightSide(Section.Side) ? Section.Right.Num() : 0;
		}

		static void CollectFaceBreaks(
			const UMetaLandRoadSplineComponent* Spline,
			bool bLeftFace,
			TArray<double>& OutBreaks)
		{
			OutBreaks.Reset();
			if (!Spline || Spline->GetLaneSectionsNum() == 0)
			{
				return;
			}

			const double SplineLength = Spline->GetSplineLength();
			OutBreaks.Add(0.0);
			bool bHavePrevCount = false;
			int32 PrevCount = 0;
			for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
			{
				const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
				if (Section.SOffsetEnd_Cached <= Section.SOffset + KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 Count = FaceLaneCount(Section, bLeftFace);
				if (bHavePrevCount
					&& Count != PrevCount
					&& Section.SOffset > KINDA_SMALL_NUMBER
					&& Section.SOffset < SplineLength - KINDA_SMALL_NUMBER)
				{
					OutBreaks.Add(Section.SOffset);
				}
				PrevCount = Count;
				bHavePrevCount = true;
			}
			OutBreaks.Add(SplineLength);
			OutBreaks.Sort();

			TArray<double> Unique;
			Unique.Reserve(OutBreaks.Num());
			for (double S : OutBreaks)
			{
				if (Unique.Num() == 0 || !FMath::IsNearlyEqual(Unique.Last(), S, 1.0))
				{
					Unique.Add(S);
				}
			}
			OutBreaks = MoveTemp(Unique);
		}

		static int32 ResolveLaneSectionForFace(
			const FMetaLandRoadLayout& Layout,
			double SMid,
			bool bLeftFace)
		{
			const int32 HostSection = Layout.FindSectionAtSOffset(SMid);
			if (HostSection == INDEX_NONE)
			{
				return INDEX_NONE;
			}

			return Layout.FindSideSection(
				HostSection,
				bLeftFace ? EMetaLandRoadLaneSectionSide::Left : EMetaLandRoadLaneSectionSide::Right);
		}

		static int32 ResolveLaneSectionIndex(
			const FMetaLandRoadLayout& Layout,
			int32 SpanSectionIndex,
			int32 LaneIndex)
		{
			return Layout.ResolveLaneSectionIndex(SpanSectionIndex, LaneIndex);
		}

		static bool IsLaneBoundaryValidForSpanSection(
			const FMetaLandRoadLayout& Layout,
			int32 SpanSectionIndex,
			int32 LaneIndex)
		{
			if (!Layout.Sections.IsValidIndex(SpanSectionIndex))
			{
				return false;
			}

			if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
			{
				return true;
			}

			const int32 LaneSectionIndex = ResolveLaneSectionIndex(Layout, SpanSectionIndex, LaneIndex);
			if (LaneSectionIndex == INDEX_NONE || !Layout.Sections.IsValidIndex(LaneSectionIndex))
			{
				return false;
			}

			return IsLaneBoundaryValidForSection(Layout.Sections[LaneSectionIndex], LaneIndex);
		}

		static int32 FindHighlightSpanSectionIndex(
			const FMetaLandRoadLayout& Layout,
			const UMetaLandRoadSplineComponent* Spline,
			double SMid,
			int32 LaneIndex)
		{
			const int32 HostSection = Layout.FindSectionAtSOffset(SMid);
			if (HostSection != INDEX_NONE
				&& IsLaneBoundaryValidForSpanSection(Layout, HostSection, LaneIndex))
			{
				return HostSection;
			}

			for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
			{
				if (!IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
				{
					continue;
				}

				const double S0 = Spline->GetLaneSection(SectionIndex).SOffset;
				const double S1 = MetaLandRoad::ResolveSectionEndS(Spline, SectionIndex);
				if (SMid >= S0 - KINDA_SMALL_NUMBER && SMid <= S1 + KINDA_SMALL_NUMBER)
				{
					return SectionIndex;
				}
			}

			return HostSection;
		}

		static void GetLaneWidthSegmentRange(
			const FMetaLandRoadLane& Lane,
			int32 SegmentIndex,
			int32 NumKeys,
			double LaneStartS,
			double LaneEndS,
			double& OutS0,
			double& OutS1)
		{
			if (NumKeys <= 0)
			{
				OutS0 = LaneStartS;
				OutS1 = LaneEndS;
				return;
			}

			OutS0 = LaneStartS + Lane.Width.Keys[SegmentIndex].Time;
			OutS1 = (SegmentIndex + 1 < NumKeys)
				? LaneStartS + Lane.Width.Keys[SegmentIndex + 1].Time
				: LaneEndS;
		}

#if WITH_EDITOR
		static void DrawSegmentPDI(
			FPrimitiveDrawInterface* PDI,
			const FVector& Start,
			const FVector& End,
			const FLinearColor& Color,
			float Thickness)
		{
			if (PDI)
			{
				PDI->DrawLine(Start, End, Color, LaneLineDepthPriority, Thickness, 0.0f, true);
			}
		}

		static void CollectAnchorBundleLaneIndices(
			const FMetaLandRoadDrawAnchor& Anchor,
			TArray<int32>& OutLaneIndices)
		{
			OutLaneIndices.Reset();
			if (!Anchor.IsValid())
			{
				return;
			}
			OutLaneIndices.Add(Anchor.LaneIndex);
			for (int32 BundledLaneIndex : Anchor.BundledLaneIndices)
			{
				OutLaneIndices.AddUnique(BundledLaneIndex);
			}
			OutLaneIndices.Sort([](int32 A, int32 B)
			{
				return FMath::Abs(A) < FMath::Abs(B);
			});
		}

		/** Mouth bar: lane-width span only (no outward ticks). */
		static void DrawLaneMouthWidthBarPDI(
			FPrimitiveDrawInterface* PDI,
			const FSceneView* View,
			const FVector& MouthLeft,
			const FVector& MouthRight,
			const FVector& MouthCenter,
			const FVector& MouthOutward,
			const FLinearColor& Color,
			float BaseThickness)
		{
			if (!PDI)
			{
				return;
			}

			(void)MouthCenter;
			(void)MouthOutward;

			(void)View;
			DrawSegmentPDI(PDI, MouthLeft, MouthRight, Color, BaseThickness);
		}

		static bool BuildLaneEdgePolylineAtAlpha(
			const UMetaLandRoadSplineComponent* Spline,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			double S0,
			double S1,
			double Alpha,
			TArray<FVector>& OutPoints)
		{
			OutPoints.Reset();
			if (!Spline || LaneIndex == LANE_INDEX_NONE || (S1 - S0) < KINDA_SMALL_NUMBER
				|| !Spline->GetLaneSections().IsValidIndex(LaneSectionIndex))
			{
				return false;
			}

			const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(LaneSectionIndex);
			TArray<FMetaLandRoadSplinePositionLinearApproximation> Approx;
			Spline->BuildLinearApproximation(
				Approx,
				[Spline, &Section, LaneIndex, Alpha](double S)
				{
					return Section.EvalLaneROffset(LaneIndex, S, Alpha) + Spline->EvalROffset(S);
				},
				S0,
				S1,
				MetaLandRoad::DefaultReparamStepsPerSegment,
				MetaLandRoad::DefaultMinNumStepsPerSection,
				ESplineCoordinateSpace::World);
			if (Approx.Num() < 2)
			{
				return false;
			}

			const float HeightOffset = Spline->LaneVisualizationHeightOffset;
			OutPoints.Reserve(Approx.Num());
			for (const FMetaLandRoadSplinePositionLinearApproximation& Sample : Approx)
			{
				FVector Pos = Sample.Position;
				Pos.Z += HeightOffset;
				OutPoints.Add(Pos);
			}

			return OutPoints.Num() >= 2;
		}

		static void DrawContinuousLaneBoundaryPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			double S0,
			double S1,
			const FLinearColor& Color,
			float Thickness,
			double Alpha = 1.0)
		{
			if (!PDI || !Spline || (S1 - S0) < KINDA_SMALL_NUMBER
				|| !Spline->GetLaneSections().IsValidIndex(LaneSectionIndex))
			{
				return;
			}

			TArray<FVector> Points;
			if (!BuildLaneEdgePolylineAtAlpha(Spline, LaneSectionIndex, LaneIndex, S0, S1, Alpha, Points))
			{
				return;
			}

			for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
			{
				DrawSegmentPDI(PDI, Points[PointIndex - 1], Points[PointIndex], Color, Thickness);
			}
		}

		static void AppendBoundaryPolylinePDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			const FMetaLandRoadLayout& Layout,
			int32 LaneIndex,
			float SampleStepCm,
			const FLinearColor& Color,
			float Thickness)
		{
			if (!PDI || Spline->GetLaneSectionsNum() == 0)
			{
				return;
			}

			(void)SampleStepCm;

			for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
			{
				if (!IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
				{
					continue;
				}

				const FMetaLandRoadLaneSection& SpanSection = Spline->GetLaneSection(SectionIndex);
				const double S0 = SpanSection.SOffset;
				const double S1 = SpanSection.SOffsetEnd_Cached;
				if ((S1 - S0) < KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 LaneSectionIndex = ResolveLaneSectionIndex(Layout, SectionIndex, LaneIndex);
				if (LaneSectionIndex == INDEX_NONE)
				{
					continue;
				}

				const FMetaLandRoadLaneSection& LaneSection = Layout.Sections[LaneSectionIndex];
				TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
				Spline->BuildLinearApproximation(
					Points,
					[LaneIndex, &LaneSection, Spline](double S)
					{
						if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
						{
							return Spline->EvalROffset(S);
						}
						return LaneSection.EvalLaneROffset(LaneIndex, S, 1.0) + Spline->EvalROffset(S);
					},
					S0,
					S1,
					MetaLandRoad::DefaultReparamStepsPerSegment,
					MetaLandRoad::DefaultMinNumStepsPerSection,
					ESplineCoordinateSpace::World);

				for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
				{
					FVector Start = Points[PointIndex - 1].Position;
					FVector End = Points[PointIndex].Position;
					Start.Z += Spline->LaneVisualizationHeightOffset;
					End.Z += Spline->LaneVisualizationHeightOffset;
					DrawSegmentPDI(PDI, Start, End, Color, Thickness);
				}
			}
		}

		static void DrawSectionSplitLineSideAware(
			const UMetaLandRoadSplineComponent* Spline,
			const FMetaLandRoadLayout& Layout,
			int32 SectionIndex,
			const FLinearColor& SplitColor,
			float Thickness,
			TFunctionRef<void(const FVector&, const FVector&)> DrawLineFn,
			EMetaLandRoadLaneSectionSide SideOverride = EMetaLandRoadLaneSectionSide::Both)
		{
			if (!Layout.Sections.IsValidIndex(SectionIndex) || SectionIndex <= 0)
			{
				return;
			}

			double Distance = Layout.Sections[SectionIndex].SOffset;
			if (!FMath::IsFinite(Distance))
			{
				return;
			}

			// Gap-interior markers: hide (do not tip-snap — piles orange mouths on T).
			if (const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
			{
				if (!Road->ContainsJunctionMeshStation(Distance, 1.0))
				{
					return;
				}
				TArray<TPair<double, double>> Spans;
				Road->GetJunctionMeshGenerationSpans(Spans);
				for (const TPair<double, double>& Span : Spans)
				{
					if (Distance + 1.0 < Span.Key || Distance - 1.0 > Span.Value)
					{
						continue;
					}
					const double HalfW = FMath::Max(Spline->GetRoadHalfWidthAtS(Distance), 1.0);
					const double TipSnap = FMath::Max(HalfW * 0.15, 100.0);
					if (FMath::Abs(Distance - Span.Value) <= TipSnap)
					{
						Distance = Span.Value;
					}
					else if (Span.Key > KINDA_SMALL_NUMBER
						&& FMath::Abs(Distance - Span.Key) <= TipSnap)
					{
						Distance = Span.Key;
					}
					break;
				}
			}

			FVector EdgeL;
			FVector EdgeR;
			FVector EdgeC;
			if (!Spline->GetRoadEdgePointsAtS(Distance, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
			{
				return;
			}
			const float Z = Spline->LaneVisualizationHeightOffset;
			EdgeL.Z += Z;
			EdgeR.Z += Z;
			EdgeC.Z += Z;

			const FMetaLandRoadLaneSection& SectionAtBoundary = Layout.Sections[SectionIndex];
			const EMetaLandRoadLaneSectionSide DrawSide =
				SideOverride != EMetaLandRoadLaneSectionSide::Both
					? SideOverride
					: SectionAtBoundary.Side;
			switch (DrawSide)
			{
			case EMetaLandRoadLaneSectionSide::Left:
				DrawLineFn(EdgeL, EdgeC);
				break;
			case EMetaLandRoadLaneSectionSide::Right:
				DrawLineFn(EdgeC, EdgeR);
				break;
			default:
				DrawLineFn(EdgeL, EdgeR);
				break;
			}
		}

		static int32 GetMaxLaneCountOnSpline(const UMetaLandRoadSplineComponent* Spline)
		{
			int32 MaxLeft = 0;
			int32 MaxRight = 0;
			for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
			{
				const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
				MaxLeft = FMath::Max(MaxLeft, Section.Left.Num());
				MaxRight = FMath::Max(MaxRight, Section.Right.Num());
			}
			return FMath::Max(MaxLeft, MaxRight);
		}

		static FLinearColor GetLaneColor(int32 LaneIndex, int32 MaxLaneCount)
		{
			const bool bOuterLane = FMath::Abs(LaneIndex) == MaxLaneCount;
			if (LaneIndex > 0)
			{
				return bOuterLane
					? FLinearColor(0.95f, 0.2f, 0.2f, 1.0f)
					: FLinearColor(0.85f, 0.35f, 0.25f, 1.0f);
			}

			return bOuterLane
				? FLinearColor(0.15f, 0.85f, 0.25f, 1.0f)
				: FLinearColor(0.25f, 0.75f, 0.35f, 1.0f);
		}

		static void DrawAllLanes(
			const UMetaLandRoadSplineComponent* SplineComponent,
			float ThicknessScale,
			TFunctionRef<void(
				const UMetaLandRoadSplineComponent*,
				const FMetaLandRoadLayout&,
				int32 LaneIndex,
				float SampleStep,
				const FLinearColor&,
				float Thickness)> DrawLaneFn)
		{
			if (!SplineComponent || !SplineComponent->bShowLaneVisualization || SplineComponent->GetLaneSectionsNum() == 0)
			{
				return;
			}

			const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
			const float SampleStep = SplineComponent->LaneVisualizationSampleStep;
			const int32 MaxLaneCount = GetMaxLaneCountOnSpline(SplineComponent);
			for (int32 LaneIndex = 1; LaneIndex <= MaxLaneCount; ++LaneIndex)
			{
				const FLinearColor LaneColor = GetLaneColor(LaneIndex, MaxLaneCount);
				DrawLaneFn(SplineComponent, Layout, LaneIndex, SampleStep, LaneColor, 10.0f * ThicknessScale);
				DrawLaneFn(
					SplineComponent,
					Layout,
					-LaneIndex,
					SampleStep,
					GetLaneColor(-LaneIndex, MaxLaneCount),
					10.0f * ThicknessScale);
			}
		}

		static FLinearColor GetLaneEditBaseColor(int32 LaneIndex)
		{
			return LaneIndex < 0
				? FLinearColor(0.15f, 0.85f, 0.25f, 1.0f)
				: FLinearColor(0.95f, 0.2f, 0.2f, 1.0f);
		}

		static bool IsLaneEditHighlighted(
			const UMetaLandRoadSplineComponent* Spline,
			int32 SectionIndex,
			int32 LaneIndex,
			const FMetaLandRoadLaneEditHighlight& Highlight)
		{
			return MetaLandRoadLaneVisualization::IsLaneHighlightSelected(
				Spline, SectionIndex, LaneIndex, Highlight);
		}

		/** World Builder pick: yellow = selected bundle, green = hover candidate. */
		static FLinearColor GetLaneFaceSelectionFillColor(int32 LaneIndex)
		{
			(void)LaneIndex;
			return FLinearColor(1.00f, 0.92f, 0.10f, 0.58f);
		}

		static FLinearColor GetLaneFaceHoverFillColor(int32 LaneIndex)
		{
			(void)LaneIndex;
			return FLinearColor(0.20f, 1.00f, 0.35f, 0.42f);
		}

		static const FMaterialRenderProxy* GetLaneFaceFillMaterialParent()
		{
			if (!GEngine)
			{
				return nullptr;
			}

			if (GEngine->GeomMaterial)
			{
				return GEngine->GeomMaterial->GetRenderProxy();
			}

			if (GEngine->EditorBrushMaterial)
			{
				return GEngine->EditorBrushMaterial->GetRenderProxy();
			}

			return GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial->GetRenderProxy() : nullptr;
		}

		static void ResampleLinearApproximationPoints(
			const TArray<FMetaLandRoadSplinePositionLinearApproximation>& Source,
			int32 TargetCount,
			TArray<FMetaLandRoadSplinePositionLinearApproximation>& OutPoints)
		{
			OutPoints.SetNum(TargetCount);
			if (Source.Num() < 2 || TargetCount < 2)
			{
				return;
			}

			for (int32 i = 0; i < TargetCount; ++i)
			{
				const float Alpha = static_cast<float>(i) / static_cast<float>(TargetCount - 1);
				const float SourceIndex = Alpha * static_cast<float>(Source.Num() - 1);
				const int32 Idx0 = FMath::Clamp(FMath::FloorToInt(SourceIndex), 0, Source.Num() - 2);
				const int32 Idx1 = Idx0 + 1;
				const float T = SourceIndex - static_cast<float>(Idx0);
				OutPoints[i].Position = FMath::Lerp(Source[Idx0].Position, Source[Idx1].Position, T);
				OutPoints[i].SplineParam = FMath::Lerp(Source[Idx0].SplineParam, Source[Idx1].SplineParam, T);
			}
		}

		/** Match GenerateCombinedLaneDynamicMesh / AppendLaneMeshForSection strip geometry. */
		static bool BuildLaneFaceStripPoints(
			const UMetaLandRoadSplineComponent* Spline,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			double S0,
			double S1,
			TArray<FVector>& OutInnerPoints,
			TArray<FVector>& OutOuterPoints)
		{
			OutInnerPoints.Reset();
			OutOuterPoints.Reset();

			if (!Spline || LaneIndex == LANE_INDEX_NONE || (S1 - S0) < KINDA_SMALL_NUMBER
				|| !Spline->GetLaneSections().IsValidIndex(LaneSectionIndex))
			{
				return false;
			}

			const FMetaLandRoadLaneSection& LaneSection = Spline->GetLaneSection(LaneSectionIndex);
			if (!LaneSection.CheckLaneIndex(LaneIndex))
			{
				return false;
			}

			TArray<FMetaLandRoadSplinePositionLinearApproximation> OuterApprox;
			Spline->BuildLinearApproximation(
				OuterApprox,
				[Spline, LaneSectionIndex, LaneIndex](double S)
				{
					int32 Sec = Spline->GetRoadLayout().FindSectionForLaneAtS(S, LaneIndex);
					if (Sec == INDEX_NONE || !Spline->GetLaneSections().IsValidIndex(Sec))
					{
						Sec = LaneSectionIndex;
					}
					return Spline->GetLaneSection(Sec).EvalLaneROffset(LaneIndex, S, 1.0)
						+ Spline->EvalROffset(S);
				},
				S0,
				S1,
				MetaLandRoad::DefaultReparamStepsPerSegment,
				MetaLandRoad::DefaultMinNumStepsPerSection,
				ESplineCoordinateSpace::World);

			if (OuterApprox.Num() < 2)
			{
				return false;
			}

			TArray<FMetaLandRoadSplinePositionLinearApproximation> InnerApprox;
			const int32 AbsLaneIndex = FMath::Abs(LaneIndex);
			if (AbsLaneIndex <= 1)
			{
				Spline->BuildLinearApproximation(
					InnerApprox,
					[Spline](double S) { return Spline->EvalROffset(S); },
					S0,
					S1,
					MetaLandRoad::DefaultReparamStepsPerSegment,
					MetaLandRoad::DefaultMinNumStepsPerSection,
					ESplineCoordinateSpace::World);
			}
			else
			{
				const int32 PrevLaneIndex = LaneIndex > 0 ? (AbsLaneIndex - 1) : -(AbsLaneIndex - 1);
				Spline->BuildLinearApproximation(
					InnerApprox,
					[Spline, LaneSectionIndex, PrevLaneIndex](double S)
					{
						int32 Sec = Spline->GetRoadLayout().FindSectionForLaneAtS(S, PrevLaneIndex);
						if (Sec == INDEX_NONE || !Spline->GetLaneSections().IsValidIndex(Sec))
						{
							Sec = LaneSectionIndex;
						}
						return Spline->GetLaneSection(Sec).EvalLaneROffset(PrevLaneIndex, S, 1.0)
							+ Spline->EvalROffset(S);
					},
					S0,
					S1,
					MetaLandRoad::DefaultReparamStepsPerSegment,
					MetaLandRoad::DefaultMinNumStepsPerSection,
					ESplineCoordinateSpace::World);
			}

			if (InnerApprox.Num() < 2)
			{
				return false;
			}

			if (InnerApprox.Num() != OuterApprox.Num())
			{
				ResampleLinearApproximationPoints(InnerApprox, OuterApprox.Num(), InnerApprox);
			}

			const float HeightOffset = Spline->LaneVisualizationHeightOffset;
			OutInnerPoints.Reserve(InnerApprox.Num());
			OutOuterPoints.Reserve(OuterApprox.Num());
			for (int32 PointIndex = 0; PointIndex < OuterApprox.Num(); ++PointIndex)
			{
				FVector InnerPos = InnerApprox[PointIndex].Position;
				FVector OuterPos = OuterApprox[PointIndex].Position;
				InnerPos.Z += HeightOffset;
				OuterPos.Z += HeightOffset;
				OutInnerPoints.Add(InnerPos);
				OutOuterPoints.Add(OuterPos);
			}

			return OutInnerPoints.Num() >= 2;
		}

		static bool IsPointInScreenTriangle(
			const FVector2D& Point,
			const FVector2D& A,
			const FVector2D& B,
			const FVector2D& C)
		{
			auto CrossSign = [&Point](const FVector2D& P0, const FVector2D& P1) -> float
			{
				return (Point.X - P1.X) * (P0.Y - P1.Y) - (P0.X - P1.X) * (Point.Y - P1.Y);
			};

			const float D1 = CrossSign(A, B);
			const float D2 = CrossSign(B, C);
			const float D3 = CrossSign(C, A);
			const bool bHasNeg = (D1 < 0.0f) || (D2 < 0.0f) || (D3 < 0.0f);
			const bool bHasPos = (D1 > 0.0f) || (D2 > 0.0f) || (D3 > 0.0f);
			return !(bHasNeg && bHasPos);
		}

		static bool IsPointInScreenQuad(
			const FVector2D& Point,
			const FVector2D& Inner0,
			const FVector2D& Inner1,
			const FVector2D& Outer1,
			const FVector2D& Outer0)
		{
			return IsPointInScreenTriangle(Point, Inner0, Inner1, Outer1)
				|| IsPointInScreenTriangle(Point, Inner0, Outer1, Outer0);
		}

		/** Narrow lanes can be sub-pixel wide — fall back to centerline distance with a minimum half-width. */
		static bool ScoreMouseAgainstScreenLaneStrip(
			const FVector2D& Point,
			const FVector2D& Inner0,
			const FVector2D& Inner1,
			const FVector2D& Outer1,
			const FVector2D& Outer0,
			float MinHalfWidthPx,
			float ExtraMarginPx,
			float& OutScore)
		{
			OutScore = TNumericLimits<float>::Max();

			const float ActualHalf0 = FVector2D::Distance(Inner0, Outer0) * 0.5f;
			const float ActualHalf1 = FVector2D::Distance(Inner1, Outer1) * 0.5f;

			// Geometric interior must always beat expanded centerline hits (negative Dist-HalfWidth
			// previously stole Right/red faces when Left/green inflate overlapped).
			if (IsPointInScreenQuad(Point, Inner0, Inner1, Outer1, Outer0))
			{
				const float ActualHalf = FMath::Max(0.5f * (ActualHalf0 + ActualHalf1), 1.0f);
				OutScore = -1000.0f + ActualHalf;
				return true;
			}

			const FVector2D Center0 = (Inner0 + Outer0) * 0.5f;
			const FVector2D Center1 = (Inner1 + Outer1) * 0.5f;
			const float HalfWidth0 = FMath::Max(ActualHalf0, MinHalfWidthPx);
			const float HalfWidth1 = FMath::Max(ActualHalf1, MinHalfWidthPx);

			const FVector2D CenterDelta = Center1 - Center0;
			const float Denom = CenterDelta.SizeSquared();
			float T = 0.0f;
			if (Denom > KINDA_SMALL_NUMBER)
			{
				T = FMath::Clamp(FVector2D::DotProduct(Point - Center0, CenterDelta) / Denom, 0.0f, 1.0f);
			}

			const FVector2D ClosestCenter = Center0 + CenterDelta * T;
			const float DistToCenter = FVector2D::Distance(Point, ClosestCenter);
			const float HalfWidth = FMath::Lerp(HalfWidth0, HalfWidth1, T);
			if (DistToCenter > HalfWidth + ExtraMarginPx)
			{
				return false;
			}

			// Expanded hits stay in [0, +inf) so they never outrank a true interior hit.
			OutScore = DistToCenter;
			return true;
		}

		static void AppendLaneFaceStripToMeshBuilder(
			FDynamicMeshBuilder& MeshBuilder,
			int32 LaneIndex,
			const TArray<FVector>& InnerPoints,
			const TArray<FVector>& OuterPoints)
		{
			const int32 NumSteps = InnerPoints.Num() - 1;
			for (int32 StepIdx = 0; StepIdx < NumSteps; ++StepIdx)
			{
				const FVector& Inner0 = InnerPoints[StepIdx];
				const FVector& Inner1 = InnerPoints[StepIdx + 1];
				const FVector& Outer0 = OuterPoints[StepIdx];
				const FVector& Outer1 = OuterPoints[StepIdx + 1];

				const FVector Normal = FVector::CrossProduct(Outer1 - Outer0, Inner0 - Outer0).GetSafeNormal();
				const FVector Tangent = (Inner1 - Inner0).GetSafeNormal();
				const FVector Binormal = FVector::CrossProduct(Normal, Tangent).GetSafeNormal();

				const int32 Inner0Vid = MeshBuilder.AddVertex(
					FVector3f(Inner0),
					FVector2f(0.0f, 0.0f),
					FVector3f(Tangent),
					FVector3f(Binormal),
					FVector3f(Normal),
					FColor::White);
				const int32 Inner1Vid = MeshBuilder.AddVertex(
					FVector3f(Inner1),
					FVector2f(0.0f, 1.0f),
					FVector3f(Tangent),
					FVector3f(Binormal),
					FVector3f(Normal),
					FColor::White);
				const int32 Outer1Vid = MeshBuilder.AddVertex(
					FVector3f(Outer1),
					FVector2f(1.0f, 1.0f),
					FVector3f(Tangent),
					FVector3f(Binormal),
					FVector3f(Normal),
					FColor::White);
				const int32 Outer0Vid = MeshBuilder.AddVertex(
					FVector3f(Outer0),
					FVector2f(1.0f, 0.0f),
					FVector3f(Tangent),
					FVector3f(Binormal),
					FVector3f(Normal),
					FColor::White);

				// Match BuildLaneRoadMesh winding (left vs right face).
				if (LaneIndex < 0)
				{
					MeshBuilder.AddTriangle(Inner0Vid, Inner1Vid, Outer0Vid);
					MeshBuilder.AddTriangle(Outer0Vid, Inner1Vid, Outer1Vid);
				}
				else
				{
					MeshBuilder.AddTriangle(Inner0Vid, Outer0Vid, Inner1Vid);
					MeshBuilder.AddTriangle(Outer0Vid, Outer1Vid, Inner1Vid);
				}
			}
		}

		static void DrawLaneFaceFillPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			double S0,
			double S1,
			const FLinearColor& FillColor)
		{
			if (!PDI || !PDI->View || PDI->IsHitTesting())
			{
				return;
			}

			const FMaterialRenderProxy* MaterialParent = GetLaneFaceFillMaterialParent();
			if (!MaterialParent)
			{
				return;
			}

			TArray<FVector> InnerPoints;
			TArray<FVector> OuterPoints;
			if (!BuildLaneFaceStripPoints(Spline, LaneSectionIndex, LaneIndex, S0, S1, InnerPoints, OuterPoints))
			{
				return;
			}

			FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
			AppendLaneFaceStripToMeshBuilder(MeshBuilder, LaneIndex, InnerPoints, OuterPoints);

			FMetaLandRoadDynamicColoredMaterialRenderProxy* MaterialProxy =
				new FMetaLandRoadDynamicColoredMaterialRenderProxy(MaterialParent, FillColor);
			PDI->RegisterDynamicResource(MaterialProxy);
			// Match lane boundary lines: draw on top of road mesh (lines use bDisableDepthTest).
			MeshBuilder.Draw(PDI, FMatrix::Identity, MaterialProxy, LaneFillDepthPriority, false, true);
		}

		static void DrawLaneFaceHitProxyPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			int32 SpanSectionIndex,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			double S0,
			double S1)
		{
			if (!PDI || !PDI->View || !PDI->IsHitTesting() || LaneIndex == LANE_INDEX_NONE)
			{
				return;
			}

			const FMaterialRenderProxy* MaterialParent = GetLaneFaceFillMaterialParent();
			if (!MaterialParent)
			{
				return;
			}

			TArray<FVector> InnerPoints;
			TArray<FVector> OuterPoints;
			if (!BuildLaneFaceStripPoints(Spline, LaneSectionIndex, LaneIndex, S0, S1, InnerPoints, OuterPoints))
			{
				return;
			}

			FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
			AppendLaneFaceStripToMeshBuilder(MeshBuilder, LaneIndex, InnerPoints, OuterPoints);

			HMetaLandRoadLaneVisProxy* HitProxy = new HMetaLandRoadLaneVisProxy(
				Spline, SpanSectionIndex, LaneIndex, HPP_Foreground);
			PDI->SetHitProxy(HitProxy);
			MeshBuilder.Draw(
				PDI,
				FMatrix::Identity,
				MaterialParent,
				LaneFillDepthPriority,
				false,
				false,
				HitProxy->Id);
			PDI->SetHitProxy(nullptr);
		}

		static void DrawLaneModeSectionCenterLinePDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			int32 SectionIndex,
			const FLinearColor& Color,
			float Thickness)
		{
			if (!PDI || !Spline || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
			{
				return;
			}

			const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			double StartS = Section.SOffset;
			double EndS = MetaLandRoad::ResolveSectionEndS(Spline, SectionIndex);
			if (!IntersectDrawRangeWithMeshSpans(Spline, StartS, EndS, StartS, EndS))
			{
				return;
			}
			if (EndS <= StartS + KINDA_SMALL_NUMBER)
			{
				return;
			}

			// Geometric spline center (ROffset≡0) — EvalROffset drifts and overlaps lane boundaries.
			TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
			Spline->BuildLinearApproximation(
				Points,
				[&](double /*S*/)
				{
					return 0.0;
				},
				StartS,
				EndS,
				MetaLandRoad::DefaultReparamStepsPerSegment,
				MetaLandRoad::DefaultMinNumStepsPerSection,
				ESplineCoordinateSpace::World);

			for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
			{
				FVector Start = Points[PointIndex - 1].Position;
				FVector End = Points[PointIndex].Position;
				Start.Z += Spline->LaneVisualizationHeightOffset;
				End.Z += Spline->LaneVisualizationHeightOffset;
				DrawSegmentPDI(PDI, Start, End, Color, Thickness);
			}
		}

		static void AppendSectionSplitLinesPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			const FMetaLandRoadLayout& Layout,
			float SplitThickness = 20.0f,
			int32 HoveredSectionBoundaryIndex = INDEX_NONE,
			int32 SelectedSectionBoundaryIndex = INDEX_NONE)
		{
			if (!PDI || Layout.Sections.Num() < 2)
			{
				return;
			}

			const FLinearColor DefaultSplitColor(1.0f, 0.45f, 0.05f, 1.0f);
			const FLinearColor HoveredSplitColor(1.0f, 1.0f, 0.0f, 1.0f);
			const FLinearColor SelectedSplitColor(1.0f, 0.78f, 0.25f, 1.0f);

			struct FSplitGroup
			{
				double S = 0.0;
				bool bLeft = false;
				bool bRight = false;
				bool bBoth = false;
				int32 HoveredIndex = INDEX_NONE;
				int32 SelectedIndex = INDEX_NONE;
			};
			TArray<FSplitGroup> Groups;
			auto FindOrAddGroup = [&Groups](double SplitS) -> FSplitGroup&
			{
				for (FSplitGroup& Group : Groups)
				{
					if (FMath::IsNearlyEqual(Group.S, SplitS, 5.0))
					{
						return Group;
					}
				}
				FSplitGroup& NewGroup = Groups.AddDefaulted_GetRef();
				NewGroup.S = SplitS;
				return NewGroup;
			};

			for (int32 SectionIndex = 1; SectionIndex < Layout.Sections.Num(); ++SectionIndex)
			{
				const double SplitS = Layout.Sections[SectionIndex].SOffset;
				FSplitGroup& Group = FindOrAddGroup(SplitS);
				switch (Layout.Sections[SectionIndex].Side)
				{
				case EMetaLandRoadLaneSectionSide::Left:
					Group.bLeft = true;
					break;
				case EMetaLandRoadLaneSectionSide::Right:
					Group.bRight = true;
					break;
				default:
					Group.bBoth = true;
					break;
				}
				if (HoveredSectionBoundaryIndex == SectionIndex)
				{
					Group.HoveredIndex = SectionIndex;
				}
				if (SelectedSectionBoundaryIndex == SectionIndex)
				{
					Group.SelectedIndex = SectionIndex;
				}
			}

			for (const FSplitGroup& Group : Groups)
			{
				int32 RepIndex = INDEX_NONE;
				for (int32 SectionIndex = 1; SectionIndex < Layout.Sections.Num(); ++SectionIndex)
				{
					if (FMath::IsNearlyEqual(Layout.Sections[SectionIndex].SOffset, Group.S, 5.0))
					{
						RepIndex = SectionIndex;
						break;
					}
				}
				if (RepIndex == INDEX_NONE)
				{
					continue;
				}

				const bool bHovered = Group.HoveredIndex != INDEX_NONE;
				const bool bSelected = Group.SelectedIndex != INDEX_NONE;
				const FLinearColor SplitColor = bHovered
					? HoveredSplitColor
					: (bSelected ? SelectedSplitColor : DefaultSplitColor);
				const float LineThickness = (bHovered || bSelected)
					? FMath::Max(SplitThickness * 1.6f, 8.0f)
					: SplitThickness;

				// Left@CutS + Right@CutS co-located → one full mouth bar (EdgeL–EdgeR).
				const EMetaLandRoadLaneSectionSide DrawSide =
					Group.bBoth || (Group.bLeft && Group.bRight)
						? EMetaLandRoadLaneSectionSide::Both
						: (Group.bLeft
							? EMetaLandRoadLaneSectionSide::Left
							: EMetaLandRoadLaneSectionSide::Right);

				DrawSectionSplitLineSideAware(
					Spline,
					Layout,
					RepIndex,
					SplitColor,
					LineThickness,
					[PDI, SplitColor, LineThickness](const FVector& A, const FVector& B)
					{
						DrawSegmentPDI(PDI, A, B, SplitColor, LineThickness);
					},
					DrawSide);
			}
		}

		static void AppendLaneEditBoundaryPolylinePDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			const FMetaLandRoadLayout& Layout,
			int32 LaneIndex,
			float SampleStepCm,
			const FMetaLandRoadLaneEditHighlight& Highlight)
		{
			if (!PDI || !Spline || Spline->GetLaneSectionsNum() == 0 || LaneIndex == LANE_INDEX_NONE)
			{
				return;
			}

			const FLinearColor BaseColor = GetLaneEditBaseColor(LaneIndex);
			constexpr float NormalThickness = 3.0f;
			constexpr float HighlightThickness = 5.0f;
			const FLinearColor HighlightColor(1.0f, 1.0f, 0.0f, 1.0f);

			for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
			{
				if (!IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
				{
					continue;
				}

				const FMetaLandRoadLaneSection& SpanSection = Spline->GetLaneSection(SectionIndex);
				const double S0 = SpanSection.SOffset;
				const double S1 = SpanSection.SOffsetEnd_Cached;
				if ((S1 - S0) < KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 LaneSectionIndex = ResolveLaneSectionIndex(Layout, SectionIndex, LaneIndex);
				if (LaneSectionIndex == INDEX_NONE)
				{
					continue;
				}

				const FMetaLandRoadLaneSection& LaneSection = Layout.Sections[LaneSectionIndex];
				TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
				Spline->BuildLinearApproximation(
					Points,
					[LaneIndex, &LaneSection, Spline](double S)
					{
						return LaneSection.EvalLaneROffset(LaneIndex, S, 1.0) + Spline->EvalROffset(S);
					},
					S0,
					S1,
					MetaLandRoad::DefaultReparamStepsPerSegment,
					MetaLandRoad::DefaultMinNumStepsPerSection,
					ESplineCoordinateSpace::World);

				if (Points.Num() < 2)
				{
					continue;
				}

				const bool bHighlighted = IsLaneEditHighlighted(Spline, SectionIndex, LaneIndex, Highlight);
				const FLinearColor DrawColor = bHighlighted ? HighlightColor : BaseColor;
				const float DrawThickness = bHighlighted ? HighlightThickness : NormalThickness;

				auto DrawSpan = [&](const FLinearColor& Color, float Thickness, bool bProxy)
				{
					if (bProxy)
					{
						PDI->SetHitProxy(new HMetaLandRoadLaneVisProxy(Spline, SectionIndex, LaneIndex, HPP_Foreground));
					}

					for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
					{
						FVector Start = Points[PointIndex - 1].Position;
						FVector End = Points[PointIndex].Position;
						Start.Z += Spline->LaneVisualizationHeightOffset;
						End.Z += Spline->LaneVisualizationHeightOffset;
						DrawSegmentPDI(PDI, Start, End, Color, Thickness);
					}

					if (bProxy)
					{
						PDI->SetHitProxy(nullptr);
					}
				};

				if (PDI->IsHitTesting())
				{
					DrawSpan(FLinearColor::White, FMath::Max(DrawThickness * 2.5f, 36.0f), true);
				}
				DrawSpan(DrawColor, DrawThickness, PDI->IsHitTesting());
			}
		}

		static void DrawLaneWidthBorderSegmentPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			int32 SpanSectionIndex,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			int32 WidthIndex,
			double S0,
			double S1,
			const FLinearColor& Color,
			float Thickness,
			float HitPickThickness)
		{
			if (!PDI || !Spline || (S1 - S0) < KINDA_SMALL_NUMBER)
			{
				return;
			}

			if (!Spline->GetLaneSections().IsValidIndex(LaneSectionIndex))
			{
				return;
			}

			const FMetaLandRoadLaneSection& LaneSection = Spline->GetLaneSection(LaneSectionIndex);
			TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
			Spline->BuildLinearApproximation(
				Points,
				[LaneIndex, &LaneSection, Spline](double S)
				{
					return LaneSection.EvalLaneROffset(LaneIndex, S, 1.0) + Spline->EvalROffset(S);
				},
				S0,
				S1,
				MetaLandRoad::DefaultReparamStepsPerSegment,
				MetaLandRoad::DefaultMinNumStepsPerSection,
				ESplineCoordinateSpace::World);

			if (Points.Num() < 2)
			{
				return;
			}

			if (PDI->IsHitTesting())
			{
				PDI->SetHitProxy(new HMetaLandRoadLaneWidthSegmentVisProxy(
					Spline, SpanSectionIndex, LaneIndex, WidthIndex, HPP_Foreground));
			}

			const float DrawThickness = PDI->IsHitTesting() ? HitPickThickness : Thickness;
			for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
			{
				FVector Start = Points[PointIndex - 1].Position;
				FVector End = Points[PointIndex].Position;
				Start.Z += Spline->LaneVisualizationHeightOffset;
				End.Z += Spline->LaneVisualizationHeightOffset;
				DrawSegmentPDI(PDI, Start, End, Color, DrawThickness);
			}

			if (PDI->IsHitTesting())
			{
				PDI->SetHitProxy(nullptr);
			}
		}

		static bool IsLaneFaceHoveredAtLaneSection(
			const UMetaLandRoadSplineComponent* SplineComponent,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			const FMetaLandRoadLaneEditHighlight& Highlight)
		{
			if (Highlight.HoveredLaneIndex == LANE_INDEX_NONE
				|| Highlight.HoveredSectionIndex == INDEX_NONE
				|| Highlight.bHoveredLaneBoundaryLine
				|| Highlight.bHoveredWidthIsKey
				|| Highlight.HoveredWidthIndex != INDEX_NONE
				|| LaneIndex != Highlight.HoveredLaneIndex)
			{
				return false;
			}

			const int32 HoverLaneDataSection = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
				SplineComponent, Highlight.HoveredSectionIndex, Highlight.HoveredLaneIndex);
			return HoverLaneDataSection == LaneSectionIndex;
		}

		static bool IsLaneFaceSelectedAtLaneSection(
			const UMetaLandRoadSplineComponent* SplineComponent,
			int32 LaneSectionIndex,
			int32 LaneIndex,
			const FMetaLandRoadLaneEditHighlight& Highlight)
		{
			for (int32 SpanSectionIndex = 0; SpanSectionIndex < SplineComponent->GetLaneSectionsNum(); ++SpanSectionIndex)
			{
				if (ResolveLaneSectionIndex(
					SplineComponent->GetRoadLayout(), SpanSectionIndex, LaneIndex) != LaneSectionIndex)
				{
					continue;
				}
				if (MetaLandRoadLaneVisualization::IsLaneHighlightSelected(
					SplineComponent, SpanSectionIndex, LaneIndex, Highlight))
				{
					return true;
				}
			}
			return false;
		}

		/** Hover / select face fill — face-break timeline (same as pick / mesh), lane-data keyed. */
		static void DrawLaneEditFaceHighlightFillsPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* SplineComponent,
			const FMetaLandRoadLaneEditHighlight& Highlight)
		{
			if (!PDI || PDI->IsHitTesting() || !SplineComponent || SplineComponent->GetLaneSectionsNum() == 0)
			{
				return;
			}

			// Width-key edit owns the viewport — no lane-face fill while gizmo is active.
			if (Highlight.SelectedWidthIndex != INDEX_NONE)
			{
				return;
			}

			const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
			const int32 MaxLaneCount = GetMaxLaneCountOnSpline(SplineComponent);

			for (bool bLeftFace : { false, true })
			{
				const int32 LaneSign = bLeftFace ? -1 : +1;
				TArray<double> Breaks;
				CollectFaceBreaks(SplineComponent, bLeftFace, Breaks);

				for (int32 BreakIdx = 0; BreakIdx + 1 < Breaks.Num(); ++BreakIdx)
				{
					const double FaceS0 = Breaks[BreakIdx];
					const double FaceS1 = Breaks[BreakIdx + 1];
					if ((FaceS1 - FaceS0) < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const double SMid = 0.5 * (FaceS0 + FaceS1);
					const int32 LaneSectionIndex = ResolveLaneSectionForFace(Layout, SMid, bLeftFace);
					if (LaneSectionIndex == INDEX_NONE || !Layout.Sections.IsValidIndex(LaneSectionIndex))
					{
						continue;
					}

					const FMetaLandRoadLaneSection& LaneSection = Layout.Sections[LaneSectionIndex];
					for (int32 AbsLane = 1; AbsLane <= MaxLaneCount; ++AbsLane)
					{
						const int32 LaneIndex = LaneSign * AbsLane;
						if (!LaneSection.CheckLaneIndex(LaneIndex))
						{
							continue;
						}

						const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
						const double ClipS0 = FMath::Max(FaceS0, Lane.GetStartOffset());
						const double ClipS1 = FMath::Min(FaceS1, Lane.GetEndOffset());
						if (ClipS1 <= ClipS0 + KINDA_SMALL_NUMBER)
						{
							continue;
						}

						const bool bSelected = IsLaneFaceSelectedAtLaneSection(
							SplineComponent, LaneSectionIndex, LaneIndex, Highlight);
						const bool bHovered = !bSelected && IsLaneFaceHoveredAtLaneSection(
							SplineComponent, LaneSectionIndex, LaneIndex, Highlight);

						if (bSelected)
						{
							DrawLaneFaceFillPDI(
								PDI,
								SplineComponent,
								LaneSectionIndex,
								LaneIndex,
								ClipS0,
								ClipS1,
								GetLaneFaceSelectionFillColor(LaneIndex));
						}
						else if (bHovered)
						{
							double HoverS0 = ClipS0;
							double HoverS1 = ClipS1;
							if (Layout.Sections.IsValidIndex(Highlight.HoveredSectionIndex))
							{
								HoverS0 = FMath::Max(HoverS0, Layout.Sections[Highlight.HoveredSectionIndex].SOffset);
								HoverS1 = FMath::Min(
									HoverS1,
									MetaLandRoad::ResolveSectionEndS(SplineComponent, Highlight.HoveredSectionIndex));
							}
							if (HoverS1 > HoverS0 + KINDA_SMALL_NUMBER)
							{
								DrawLaneFaceFillPDI(
									PDI,
									SplineComponent,
									LaneSectionIndex,
									LaneIndex,
									HoverS0,
									HoverS1,
									GetLaneFaceHoverFillColor(LaneIndex));
							}
						}
					}
				}
			}
		}
#endif
	}

	void RefreshLaneVisualization(
		UMetaLandRoadSplineComponent* SplineComponent,
		const TSet<int32>* SelectedKeys,
		int32 HoveredKey)
	{
		if (!SplineComponent)
		{
			return;
		}

		(void)SelectedKeys;
		(void)HoveredKey;

		Private::ClearLegacyLineBatchOverlays(SplineComponent);

#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
#endif
	}

#if WITH_EDITOR
	void DrawLaneVisualization(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		float ThicknessScale)
	{
		if (!SplineComponent || !PDI)
		{
			return;
		}

		const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
		Private::DrawAllLanes(
			SplineComponent,
			ThicknessScale,
			[PDI](
				const UMetaLandRoadSplineComponent* Spline,
				const FMetaLandRoadLayout& InLayout,
				int32 LaneIndex,
				float SampleStep,
				const FLinearColor& Color,
				float Thickness)
			{
				Private::AppendBoundaryPolylinePDI(PDI, Spline, InLayout, LaneIndex, SampleStep, Color, Thickness);
			});
		Private::AppendSectionSplitLinesPDI(PDI, SplineComponent, Layout);
	}

	void DrawLaneEditMeshUnderlay(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI)
	{
		if (!SplineComponent || !PDI || SplineComponent->GetLaneSectionsNum() == 0)
		{
			return;
		}

		const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
		const int32 MaxLaneCount = Private::GetMaxLaneCountOnSpline(SplineComponent);
		constexpr float UnderlayThickness = 1.5f;

		for (int32 AbsLane = 1; AbsLane <= MaxLaneCount; ++AbsLane)
		{
			const int32 LaneIndices[2] = { AbsLane, -AbsLane };
			for (int32 LaneIndex : LaneIndices)
			{
				const FLinearColor UnderlayColor = LaneIndex < 0
					? FLinearColor(0.15f, 0.85f, 0.25f, 0.35f)
					: FLinearColor(0.95f, 0.2f, 0.2f, 0.35f);

				for (int32 SectionIndex = 0; SectionIndex < SplineComponent->GetLaneSectionsNum(); ++SectionIndex)
				{
					if (!Private::IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
					{
						continue;
					}

					const FMetaLandRoadLaneSection& SpanSection = SplineComponent->GetLaneSection(SectionIndex);
					double SpanS0 = SpanSection.SOffset;
					double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComponent, SectionIndex);
					if (!Private::IntersectDrawRangeWithMeshSpans(SplineComponent, SpanS0, SpanS1, SpanS0, SpanS1))
					{
						continue;
					}
					if ((SpanS1 - SpanS0) < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const int32 LaneSectionIndex = Private::ResolveLaneSectionIndex(Layout, SectionIndex, LaneIndex);
					if (LaneSectionIndex == INDEX_NONE)
					{
						continue;
					}

					const FMetaLandRoadLaneSection& LaneSection = Layout.Sections[LaneSectionIndex];
					TArray<FVector> Points;
					if (!Private::BuildLaneEdgePolylineAtAlpha(
						SplineComponent,
						LaneSectionIndex,
						LaneIndex,
						SpanS0,
						SpanS1,
						1.0,
						Points))
					{
						continue;
					}

					for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
					{
						Private::DrawSegmentPDI(
							PDI,
							Points[PointIndex - 1],
							Points[PointIndex],
							UnderlayColor,
							UnderlayThickness);
					}
				}
			}
		}
	}

	void DrawLaneEditVisualization(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComponent || !PDI || SplineComponent->GetLaneSectionsNum() == 0)
		{
			return;
		}

		if (!PDI->IsHitTesting())
		{
			Private::DrawLaneEditFaceHighlightFillsPDI(PDI, SplineComponent, Highlight);
		}

		const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
		const FLinearColor HighlightColor(1.0f, 1.0f, 0.0f, 1.0f);
		constexpr float NormalThickness = 3.0f;
		constexpr float HitPickThickness = 3.0f;

		const int32 MaxLaneCount = Private::GetMaxLaneCountOnSpline(SplineComponent);
		for (int32 AbsLane = 1; AbsLane <= MaxLaneCount; ++AbsLane)
		{
			const int32 LaneIndices[2] = { AbsLane, -AbsLane };
			for (int32 LaneIndex : LaneIndices)
			{
				for (int32 SectionIndex = 0; SectionIndex < SplineComponent->GetLaneSectionsNum(); ++SectionIndex)
				{
					if (!Private::IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
					{
						continue;
					}

					const FMetaLandRoadLaneSection& SpanSection = SplineComponent->GetLaneSection(SectionIndex);
					const double SpanS0 = SpanSection.SOffset;
					const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComponent, SectionIndex);
					if ((SpanS1 - SpanS0) < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const int32 LaneSectionIndex = Private::ResolveLaneSectionIndex(Layout, SectionIndex, LaneIndex);
					if (LaneSectionIndex == INDEX_NONE)
					{
						continue;
					}

					const FMetaLandRoadLaneSection& LaneSection = Layout.Sections[LaneSectionIndex];
					if (!LaneSection.CheckLaneIndex(LaneIndex))
					{
						continue;
					}

					const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
					const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange LaneRange =
						SplineComponent->GetLaneRange(SectionIndex, LaneIndex);
					double DrawS0 = FMath::Max(SpanS0, LaneRange.StartS);
					double DrawS1 = FMath::Min(SpanS1, LaneRange.EndS);
					if (!Private::IntersectDrawRangeWithMeshSpans(SplineComponent, DrawS0, DrawS1, DrawS0, DrawS1))
					{
						continue;
					}
					if (DrawS1 <= DrawS0 + KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const double LaneStartS = Lane.GetStartOffset();
					const double LaneEndS = Lane.GetEndOffset();

					const FLinearColor BaseColor = Private::GetLaneEditBaseColor(LaneIndex);

					const int32 NumKeys = Lane.Width.GetNumKeys();
					const int32 NumSegments = FMath::Max(NumKeys, 1);

					if (!PDI->IsHitTesting())
					{
						const bool bHoveredSectionBoundary =
							!Highlight.bHoveredWidthIsKey
							&& Highlight.HoveredSectionIndex == SectionIndex
							&& Highlight.HoveredLaneIndex == LaneIndex;
						const bool bHoverElsewhere =
							Highlight.HoveredSectionIndex != INDEX_NONE
							&& (Highlight.HoveredSectionIndex != SectionIndex
								|| Highlight.HoveredLaneIndex != LaneIndex);
						const bool bSelectedWidthKey =
							!bHoverElsewhere
							&& Highlight.SelectedWidthIndex != INDEX_NONE
							&& Highlight.SelectedSectionIndex == SectionIndex
							&& Highlight.SelectedLaneIndex == LaneIndex;
						const bool bLineHighlighted = bHoveredSectionBoundary || bSelectedWidthKey;
						const FLinearColor LineColor = bLineHighlighted ? HighlightColor : BaseColor;
						const float DrawThickness = bLineHighlighted ? 5.0f : NormalThickness;

						Private::DrawContinuousLaneBoundaryPDI(
							PDI,
							SplineComponent,
							LaneSectionIndex,
							LaneIndex,
							DrawS0,
							DrawS1,
							LineColor,
							DrawThickness);
					}
					else
					{
						Private::DrawLaneFaceHitProxyPDI(
							PDI,
							SplineComponent,
							SectionIndex,
							LaneSectionIndex,
							LaneIndex,
							DrawS0,
							DrawS1);

						for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
						{
							double S0 = DrawS0;
							double S1 = DrawS1;
							Private::GetLaneWidthSegmentRange(
								Lane,
								SegmentIndex,
								NumKeys,
								LaneStartS,
								LaneEndS,
								S0,
								S1);
							S0 = FMath::Max(S0, DrawS0);
							S1 = FMath::Min(S1, DrawS1);
							if ((S1 - S0) < KINDA_SMALL_NUMBER)
							{
								continue;
							}

							Private::DrawLaneWidthBorderSegmentPDI(
								PDI,
								SplineComponent,
								SectionIndex,
								LaneSectionIndex,
								LaneIndex,
								SegmentIndex,
								S0,
								S1,
								FLinearColor::White,
								HitPickThickness,
								HitPickThickness);
						}
					}
				}
			}
		}
	}

	void DrawLaneFaceHighlightFills(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComponent || !PDI || SplineComponent->GetLaneSectionsNum() == 0 || PDI->IsHitTesting())
		{
			return;
		}

		Private::DrawLaneEditFaceHighlightFillsPDI(PDI, SplineComponent, Highlight);
	}

	static void AppendEditTargetLanesToHighlight(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadEditTarget& EditTarget,
		FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComp || !EditTarget.bHasPendingAnchor || EditTarget.Spline.Get() != SplineComp)
		{
			return;
		}

		const FMetaLandRoadDrawAnchor& Anchor = EditTarget.PendingAnchor;
		if (!Anchor.IsValid())
		{
			return;
		}

		Highlight.SelectedSectionIndex = Anchor.SectionIndex;
		Highlight.SelectedLaneIndex = Anchor.LaneIndex;
		Highlight.bLaneFacePrimSelected = true;

		auto AddSelectedLane = [&Highlight](int32 SectionIndex, int32 LaneIndex)
		{
			if (SectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
			{
				return;
			}

			for (const FMetaLandRoadSelectedLaneRef& Existing : Highlight.SelectedLanes)
			{
				if (Existing.SectionIndex == SectionIndex && Existing.LaneIndex == LaneIndex)
				{
					return;
				}
			}

			Highlight.SelectedLanes.Add({ SectionIndex, LaneIndex });
		};

		auto AddHighlightLaneRef = [&](int32 StoredSectionIndex, int32 LaneIndex)
		{
			if (StoredSectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
			{
				return;
			}

			const int32 LaneDataSection = ResolveLaneSectionIndexForSpan(
				SplineComp, StoredSectionIndex, LaneIndex);
			if (LaneDataSection == INDEX_NONE)
			{
				AddSelectedLane(StoredSectionIndex, LaneIndex);
				return;
			}

			for (int32 SpanSectionIndex = 0; SpanSectionIndex < SplineComp->GetLaneSectionsNum(); ++SpanSectionIndex)
			{
				if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(SplineComp, SpanSectionIndex, LaneIndex))
				{
					continue;
				}

				const int32 SpanLaneDataSection = ResolveLaneSectionIndexForSpan(
					SplineComp, SpanSectionIndex, LaneIndex);
				if (SpanLaneDataSection != LaneDataSection)
				{
					continue;
				}

				AddSelectedLane(SpanSectionIndex, LaneIndex);
			}
		};

		AddHighlightLaneRef(Anchor.SectionIndex, Anchor.LaneIndex);
		for (int32 BundledLaneIndex : Anchor.BundledLaneIndices)
		{
			AddHighlightLaneRef(Anchor.SectionIndex, BundledLaneIndex);
		}
	}

	static void DrawWorldBuilderLaneBoundaryHighlights(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComp || !PDI || PDI->IsHitTesting() || SplineComp->GetLaneSectionsNum() == 0)
		{
			return;
		}

		TSet<int32> LaneIndices;
		auto AddLane = [&LaneIndices](int32 LaneIndex)
		{
			if (LaneIndex != LANE_INDEX_NONE)
			{
				LaneIndices.Add(LaneIndex);
			}
		};

		AddLane(Highlight.HoveredLaneIndex);
		AddLane(Highlight.SelectedLaneIndex);
		for (const FMetaLandRoadSelectedLaneRef& Ref : Highlight.SelectedLanes)
		{
			AddLane(Ref.LaneIndex);
		}

		if (LaneIndices.Num() == 0)
		{
			return;
		}

		const FLinearColor HoverEdgeColor(0.20f, 1.0f, 0.35f, 1.0f);
		const FLinearColor SelectEdgeColor(1.0f, 0.92f, 0.10f, 1.0f);
		constexpr float HoverEdgeThickness = 5.0f;
		constexpr float SelectEdgeThickness = 7.0f;

		for (int32 LaneIndex : LaneIndices)
		{
			for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
			{
				if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(SplineComp, SectionIndex, LaneIndex))
				{
					continue;
				}

				const bool bSelected = MetaLandRoadLaneVisualization::IsLaneHighlightSelected(
					SplineComp, SectionIndex, LaneIndex, Highlight);
				const bool bHovered = !bSelected
					&& Highlight.HoveredLaneIndex == LaneIndex
					&& Highlight.HoveredSectionIndex == SectionIndex;
				if (!bSelected && !bHovered)
				{
					continue;
				}

				const FMetaLandRoadLaneSection& SpanSection = SplineComp->GetLaneSection(SectionIndex);
				const double S0 = SpanSection.SOffset;
				const double S1 = SpanSection.SOffsetEnd_Cached;
				if ((S1 - S0) < KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 LaneSectionIndex = ResolveLaneSectionIndexForSpan(
					SplineComp, SectionIndex, LaneIndex);
				if (LaneSectionIndex == INDEX_NONE)
				{
					continue;
				}

				const FLinearColor EdgeColor = bSelected ? SelectEdgeColor : HoverEdgeColor;
				const float EdgeThickness = bSelected ? SelectEdgeThickness : HoverEdgeThickness;

				TArray<FVector> InnerPoints;
				TArray<FVector> OuterPoints;
				if (Private::BuildLaneEdgePolylineAtAlpha(
					SplineComp, LaneSectionIndex, LaneIndex, S0, S1, 0.0, InnerPoints))
				{
					for (int32 PointIndex = 1; PointIndex < InnerPoints.Num(); ++PointIndex)
					{
						Private::DrawSegmentPDI(
							PDI,
							InnerPoints[PointIndex - 1],
							InnerPoints[PointIndex],
							EdgeColor,
							EdgeThickness);
					}
				}

				if (Private::BuildLaneEdgePolylineAtAlpha(
					SplineComp, LaneSectionIndex, LaneIndex, S0, S1, 1.0, OuterPoints))
				{
					for (int32 PointIndex = 1; PointIndex < OuterPoints.Num(); ++PointIndex)
					{
						Private::DrawSegmentPDI(
							PDI,
							OuterPoints[PointIndex - 1],
							OuterPoints[PointIndex],
							EdgeColor,
							EdgeThickness);
					}
				}
			}
		}
	}

	static void AppendEndSnapMixLanesToHighlight(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadDrawAnchor* EndSnapMixAnchor,
		FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComp || !EndSnapMixAnchor || !EndSnapMixAnchor->IsValid()
			|| EndSnapMixAnchor->SourceSpline.Get() != SplineComp)
		{
			return;
		}

		TArray<int32> LaneIndices;
		Private::CollectAnchorBundleLaneIndices(*EndSnapMixAnchor, LaneIndices);
		for (int32 LaneIndex : LaneIndices)
		{
			Highlight.SelectedSectionIndex = EndSnapMixAnchor->SectionIndex;
			Highlight.SelectedLaneIndex = EndSnapMixAnchor->LaneIndex;
			Highlight.bLaneFacePrimSelected = true;

			bool bAlready = false;
			for (const FMetaLandRoadSelectedLaneRef& Existing : Highlight.SelectedLanes)
			{
				if (Existing.SectionIndex == EndSnapMixAnchor->SectionIndex && Existing.LaneIndex == LaneIndex)
				{
					bAlready = true;
					break;
				}
			}
			if (!bAlready)
			{
				Highlight.SelectedLanes.Add({ EndSnapMixAnchor->SectionIndex, LaneIndex });
			}
		}
	}

	static void ApplyHoverAnchorToHighlight(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadDrawAnchor* HoverAnchor,
		FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComp || !HoverAnchor || !HoverAnchor->IsValid()
			|| HoverAnchor->SourceSpline.Get() != SplineComp)
		{
			return;
		}
		if (Highlight.HoveredLaneIndex != LANE_INDEX_NONE)
		{
			return;
		}
		Highlight.HoveredSectionIndex = HoverAnchor->SectionIndex;
		Highlight.HoveredLaneIndex = HoverAnchor->LaneIndex;
	}

	void DrawAllWorldBuilderLaneHighlights(
		UWorld* World,
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadEditTarget& EditTarget,
		const FMetaLandRoadDrawAnchor* EndSnapMixAnchor,
		const FMetaLandRoadDrawAnchor* EndSnapHoverAnchor)
	{
		if (!World || !View || !Viewport || !PDI || PDI->IsHitTesting())
		{
			return;
		}

		FIntPoint MousePos;
		Viewport->GetMousePos(MousePos);

		float BestScore = TNumericLimits<float>::Max();
		FMetaLandRoadLaneFacePickResult BestHover;
		const UMetaLandRoadSplineComponent* HoveredSpline = nullptr;

		for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
		{
			AMoKuEditSplineActor* RoadActor = *It;
			if (!IsValid(RoadActor))
			{
				continue;
			}

			UMetaLandRoadSplineComponent* SplineComp = RoadActor->GetSplineComponent();
			if (!IsValid(SplineComp) || SplineComp->GetLaneSectionsNum() == 0)
			{
				continue;
			}

			FMetaLandRoadLaneFacePickResult FacePick;
			if (!PickLaneFaceAtMouse(SplineComp, *View, MousePos, FacePick))
			{
				continue;
			}

			if (FacePick.ScreenScore < BestScore)
			{
				BestScore = FacePick.ScreenScore;
				BestHover = FacePick;
				HoveredSpline = SplineComp;
			}
		}

		for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
		{
			AMoKuEditSplineActor* RoadActor = *It;
			if (!IsValid(RoadActor))
			{
				continue;
			}

			UMetaLandRoadSplineComponent* SplineComp = RoadActor->GetSplineComponent();
			if (!IsValid(SplineComp) || SplineComp->GetLaneSectionsNum() == 0)
			{
				continue;
			}

			FMetaLandRoadLaneEditHighlight Highlight;
			if (SplineComp == HoveredSpline && BestHover.IsValid())
			{
				Highlight.HoveredSectionIndex = BestHover.SectionIndex;
				Highlight.HoveredLaneIndex = BestHover.LaneIndex;
			}
			ApplyHoverAnchorToHighlight(SplineComp, EndSnapHoverAnchor, Highlight);
			AppendEditTargetLanesToHighlight(SplineComp, EditTarget, Highlight);
			AppendEndSnapMixLanesToHighlight(SplineComp, EndSnapMixAnchor, Highlight);
			DrawLaneFaceHighlightFills(SplineComp, PDI, Highlight);
			DrawWorldBuilderLaneBoundaryHighlights(SplineComp, PDI, Highlight);
		}
	}

	void DrawWorldBuilderMouthCrossSections(
		UWorld* World,
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadEditTarget& EditTarget,
		const FMetaLandRoadDrawAnchor* EndSnapMixAnchor,
		bool bSkipEditTargetAndHover,
		const FMetaLandRoadDrawAnchor* EndSnapHoverAnchor)
	{
		if (!World || !View || !Viewport || !PDI || PDI->IsHitTesting())
		{
			return;
		}

		const FLinearColor SelectColor(1.0f, 0.92f, 0.10f, 1.0f);
		const FLinearColor EndSnapMixColor(0.25f, 0.95f, 1.0f, 1.0f);
		const FLinearColor EndSnapHoverColor(1.0f, 0.82f, 0.12f, 1.0f);
		// Thin per-lane bars (not full-road yellow slabs).
		constexpr float SelectThickness = 4.5f;
		constexpr float EndSnapMixThickness = 6.0f;
		constexpr float EndSnapHoverThickness = 3.5f;

		auto DrawMouthLine = [&View, &PDI](
			const UMetaLandRoadSplineComponent* SplineComp,
			const FMetaLandRoadDrawAnchor& Anchor,
			const FLinearColor& Color,
			float BaseThickness) -> bool
		{
			if (!SplineComp)
			{
				return false;
			}

			FVector MouthLeft;
			FVector MouthRight;
			FVector MouthCenter;
			FVector MouthTangent;
			const bool bFullRoadMouth = SplineComp->GetRoadEdgePointsAtS(
				Anchor.AnchorS,
				MouthLeft,
				MouthRight,
				MouthCenter,
				ESplineCoordinateSpace::World);
			if (!bFullRoadMouth
				&& !MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
					SplineComp, Anchor, MouthLeft, MouthRight, MouthCenter, MouthTangent))
			{
				return false;
			}
			if (MouthTangent.IsNearlyZero())
			{
				FVector UnusedL, UnusedR, UnusedC;
				MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
					SplineComp, Anchor, UnusedL, UnusedR, UnusedC, MouthTangent);
			}

			const float ZLift = SplineComp->LaneVisualizationHeightOffset + 3.0f;
			MouthLeft.Z += ZLift;
			MouthRight.Z += ZLift;
			MouthCenter.Z += ZLift;
			Private::DrawLaneMouthWidthBarPDI(
				PDI, View, MouthLeft, MouthRight, MouthCenter, MouthTangent, Color, BaseThickness);
			return true;
		};

		// One mouth bar per selected lane (Shift bundle = multiple yellow/cyan lines).
		auto DrawAnchorMouthsPerLane = [&DrawMouthLine](
			const UMetaLandRoadSplineComponent* SplineComp,
			const FMetaLandRoadDrawAnchor& Anchor,
			const FLinearColor& Color,
			float BaseThickness)
		{
			if (!SplineComp || !Anchor.IsValid())
			{
				return;
			}

			TArray<int32> LaneIndices;
			Private::CollectAnchorBundleLaneIndices(Anchor, LaneIndices);
			for (int32 LaneIndex : LaneIndices)
			{
				FMetaLandRoadDrawAnchor SingleLaneAnchor = Anchor;
				SingleLaneAnchor.SourceSpline = const_cast<UMetaLandRoadSplineComponent*>(SplineComp);
				SingleLaneAnchor.LaneIndex = LaneIndex;
				SingleLaneAnchor.BundledLaneIndices.Reset();
				// Bundled lanes may live on another side-section than the pick host.
				int32 SpanForLane = Anchor.SectionIndex;
				if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(SplineComp, SpanForLane, LaneIndex)
					|| ResolveLaneSectionIndexForSpan(SplineComp, SpanForLane, LaneIndex) == INDEX_NONE)
				{
					SpanForLane = INDEX_NONE;
					for (int32 SpanIndex = 0; SpanIndex < SplineComp->GetLaneSectionsNum(); ++SpanIndex)
					{
						if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(
							SplineComp, SpanIndex, LaneIndex))
						{
							continue;
						}
						if (ResolveLaneSectionIndexForSpan(SplineComp, SpanIndex, LaneIndex) != INDEX_NONE)
						{
							SpanForLane = SpanIndex;
							break;
						}
					}
				}
				if (SpanForLane == INDEX_NONE)
				{
					continue;
				}
				SingleLaneAnchor.SectionIndex = SpanForLane;
				DrawMouthLine(SplineComp, SingleLaneAnchor, Color, BaseThickness);
			}
		};

		// EditTarget bundle: one thin bar per selected lane (Shift multi-select feedback).
		if (!bSkipEditTargetAndHover && EditTarget.bHasPendingAnchor && EditTarget.Spline.IsValid())
		{
			FMetaLandRoadDrawAnchor SelectAnchor = EditTarget.PendingAnchor;
			SelectAnchor.AnchorS = MetaLandRoadLaneEditPick::ClampMouthAnchorSToVisibleMesh(
				EditTarget.Spline.Get(),
				SelectAnchor.AnchorS,
				SelectAnchor.MouthSide);
			TArray<int32> SelectLanes;
			Private::CollectAnchorBundleLaneIndices(SelectAnchor, SelectLanes);
			if (SelectLanes.Num() > 1)
			{
				MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
					EditTarget.Spline.Get(), SelectAnchor.SectionIndex, SelectAnchor);
				DrawMouthLine(
					EditTarget.Spline.Get(),
					SelectAnchor,
					SelectColor,
					SelectThickness);
			}
			else
			{
				DrawAnchorMouthsPerLane(
					EditTarget.Spline.Get(),
					SelectAnchor,
					SelectColor,
					SelectThickness);
			}
		}

		if (EndSnapMixAnchor && EndSnapMixAnchor->IsValid() && EndSnapMixAnchor->SourceSpline.IsValid())
		{
			FMetaLandRoadDrawAnchor MixAnchor = *EndSnapMixAnchor;
			MixAnchor.AnchorS = MetaLandRoadLaneEditPick::ClampMouthAnchorSToVisibleMesh(
				EndSnapMixAnchor->SourceSpline.Get(),
				MixAnchor.AnchorS,
				MixAnchor.MouthSide);
			MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
				EndSnapMixAnchor->SourceSpline.Get(), MixAnchor.SectionIndex, MixAnchor);
			DrawMouthLine(
				EndSnapMixAnchor->SourceSpline.Get(),
				MixAnchor,
				EndSnapMixColor,
				EndSnapMixThickness);
		}

		if (EndSnapHoverAnchor
			&& EndSnapHoverAnchor->IsValid()
			&& EndSnapHoverAnchor->SourceSpline.IsValid())
		{
			bool bHoverAlreadySelected = false;
			auto MarkHoverCoveredBy = [&](const FMetaLandRoadDrawAnchor& SelectedAnchor,
				const UMetaLandRoadSplineComponent* SelectedSpline)
			{
				if (bHoverAlreadySelected || !SelectedSpline
					|| SelectedSpline != EndSnapHoverAnchor->SourceSpline.Get())
				{
					return;
				}
				TArray<int32> SelectedLanes;
				Private::CollectAnchorBundleLaneIndices(SelectedAnchor, SelectedLanes);
				TArray<int32> HoverLanes;
				Private::CollectAnchorBundleLaneIndices(*EndSnapHoverAnchor, HoverLanes);
				if (HoverLanes.Num() == 0)
				{
					return;
				}
				for (int32 HoverLane : HoverLanes)
				{
					if (!SelectedLanes.Contains(HoverLane))
					{
						return;
					}
				}
				bHoverAlreadySelected = true;
			};
			if (EditTarget.bHasPendingAnchor)
			{
				MarkHoverCoveredBy(EditTarget.PendingAnchor, EditTarget.Spline.Get());
			}
			if (EndSnapMixAnchor && EndSnapMixAnchor->IsValid())
			{
				MarkHoverCoveredBy(*EndSnapMixAnchor, EndSnapMixAnchor->SourceSpline.Get());
			}
			if (!bHoverAlreadySelected)
			{
				FMetaLandRoadDrawAnchor HoverAnchor = *EndSnapHoverAnchor;
				HoverAnchor.AnchorS = MetaLandRoadLaneEditPick::ClampMouthAnchorSToVisibleMesh(
					EndSnapHoverAnchor->SourceSpline.Get(),
					HoverAnchor.AnchorS,
					HoverAnchor.MouthSide);
				MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
					EndSnapHoverAnchor->SourceSpline.Get(), HoverAnchor.SectionIndex, HoverAnchor);
				DrawMouthLine(
					EndSnapHoverAnchor->SourceSpline.Get(),
					HoverAnchor,
					EndSnapHoverColor,
					EndSnapHoverThickness);
			}
		}
	}

	int32 ResolveLaneSectionIndexForSpan(
		const UMetaLandRoadSplineComponent* SplineComponent,
		int32 SpanSectionIndex,
		int32 LaneIndex)
	{
		return MetaLandRoad::ResolveLaneSectionIndexForSpan(SplineComponent, SpanSectionIndex, LaneIndex);
	}

	bool IsLaneHighlightSelected(
		const UMetaLandRoadSplineComponent* SplineComponent,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		const FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComponent || LaneIndex == LANE_INDEX_NONE || SpanSectionIndex == INDEX_NONE)
		{
			return false;
		}

		if (Highlight.SelectedWidthIndex != INDEX_NONE)
		{
			return false;
		}

		if (Highlight.IsLaneSelected(SpanSectionIndex, LaneIndex))
		{
			return true;
		}

		const int32 SpanLaneDataSection = ResolveLaneSectionIndexForSpan(
			SplineComponent, SpanSectionIndex, LaneIndex);
		if (SpanLaneDataSection == INDEX_NONE)
		{
			return false;
		}

		auto MatchesStored = [&](int32 StoredSectionIndex, int32 StoredLaneIndex) -> bool
		{
			if (StoredLaneIndex != LaneIndex)
			{
				return false;
			}
			if (StoredSectionIndex == SpanSectionIndex)
			{
				return true;
			}
			const int32 StoredLaneDataSection = ResolveLaneSectionIndexForSpan(
				SplineComponent, StoredSectionIndex, StoredLaneIndex);
			return StoredLaneDataSection != INDEX_NONE && StoredLaneDataSection == SpanLaneDataSection;
		};

		for (const FMetaLandRoadSelectedLaneRef& Ref : Highlight.SelectedLanes)
		{
			if (MatchesStored(Ref.SectionIndex, Ref.LaneIndex))
			{
				return true;
			}
		}

		return MatchesStored(Highlight.SelectedSectionIndex, Highlight.SelectedLaneIndex);
	}

	bool PickLaneFaceAtMouse(
		const UMetaLandRoadSplineComponent* SplineComponent,
		const FSceneView& View,
		const FIntPoint& MousePos,
		FMetaLandRoadLaneFacePickResult& OutResult)
	{
		OutResult = FMetaLandRoadLaneFacePickResult();
		if (!SplineComponent || SplineComponent->GetLaneSectionsNum() == 0)
		{
			return false;
		}

		const FVector2D Mouse(MousePos.X, MousePos.Y);
		const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
		const int32 MaxLaneCount = Private::GetMaxLaneCountOnSpline(SplineComponent);
		float BestScore = TNumericLimits<float>::Max();
		bool bFound = false;

		constexpr float MinHalfWidthPx = 18.0f;
		constexpr float ExtraMarginPx = 4.0f;

		// Per-span strips so a narrow-section lane cannot "ghost" through a wider span
		// and steal hover / Add Width Key (SMid of a long face break was the old host).
		for (int32 SectionIndex = 0; SectionIndex < SplineComponent->GetLaneSectionsNum(); ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& SpanSection = Layout.Sections[SectionIndex];
			const double SpanS0 = SpanSection.SOffset;
			const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComponent, SectionIndex);
			if ((SpanS1 - SpanS0) < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			for (int32 AbsLane = 1; AbsLane <= MaxLaneCount; ++AbsLane)
			{
				const int32 LaneIndices[2] = { AbsLane, -AbsLane };
				for (int32 LaneIndex : LaneIndices)
				{
					if (!Private::IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
					{
						continue;
					}

					const int32 LaneSectionIndex = Private::ResolveLaneSectionIndex(Layout, SectionIndex, LaneIndex);
					if (LaneSectionIndex == INDEX_NONE || !Layout.Sections.IsValidIndex(LaneSectionIndex))
					{
						continue;
					}

					const FMetaLandRoadLaneSection& LaneSection = Layout.Sections[LaneSectionIndex];
					if (!LaneSection.CheckLaneIndex(LaneIndex))
					{
						continue;
					}

					const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
					const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange LaneRange =
						SplineComponent->GetLaneRange(SectionIndex, LaneIndex);
					double ClipS0 = FMath::Max3(SpanS0, LaneRange.StartS, Lane.GetStartOffset());
					double ClipS1 = FMath::Min3(SpanS1, LaneRange.EndS, Lane.GetEndOffset());
					if (ClipS1 <= ClipS0 + KINDA_SMALL_NUMBER)
					{
						continue;
					}

					TArray<FVector> InnerPoints;
					TArray<FVector> OuterPoints;
					if (!Private::BuildLaneFaceStripPoints(
						SplineComponent, LaneSectionIndex, LaneIndex, ClipS0, ClipS1, InnerPoints, OuterPoints))
					{
						continue;
					}

					for (int32 StepIdx = 0; StepIdx < InnerPoints.Num() - 1; ++StepIdx)
					{
						FVector2D Inner0Screen;
						FVector2D Inner1Screen;
						FVector2D Outer0Screen;
						FVector2D Outer1Screen;
						if (!View.WorldToPixel(InnerPoints[StepIdx], Inner0Screen)
							|| !View.WorldToPixel(InnerPoints[StepIdx + 1], Inner1Screen)
							|| !View.WorldToPixel(OuterPoints[StepIdx + 1], Outer1Screen)
							|| !View.WorldToPixel(OuterPoints[StepIdx], Outer0Screen))
						{
							continue;
						}

						float StripScore = TNumericLimits<float>::Max();
						if (Private::ScoreMouseAgainstScreenLaneStrip(
							Mouse,
							Inner0Screen,
							Inner1Screen,
							Outer1Screen,
							Outer0Screen,
							MinHalfWidthPx,
							ExtraMarginPx,
							StripScore)
							&& StripScore < BestScore)
						{
							BestScore = StripScore;
							OutResult.SectionIndex = SectionIndex;
							OutResult.LaneIndex = LaneIndex;
							OutResult.ScreenScore = StripScore;
							bFound = true;
						}
					}
				}
			}
		}

		return bFound;
	}

	void DrawLaneModeSectionGuides(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComponent || !PDI || SplineComponent->GetLaneSectionsNum() == 0)
		{
			return;
		}

		const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
		const FLinearColor Pink(1.0f, 0.25f, 0.75f, 1.0f);
		constexpr float CenterThickness = 4.0f;
		constexpr float SplitThickness = 2.5f;
		SplineComponent->DrawMainCenterLinePDI(PDI, Pink, CenterThickness);

		Private::AppendSectionSplitLinesPDI(
			PDI,
			SplineComponent,
			Layout,
			SplitThickness,
			Highlight.HoveredSectionBoundaryIndex,
			Highlight.SelectedSectionBoundaryIndex);
	}

	void DrawSectionModeLaneGuides(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI)
	{
		if (!SplineComponent || !PDI || PDI->IsHitTesting() || SplineComponent->GetLaneSectionsNum() == 0)
		{
			return;
		}

		const FMetaLandRoadLayout& Layout = SplineComponent->GetRoadLayout();
		constexpr float LineThickness = 4.0f;
		constexpr float UnderlayThickness = 6.0f;
		const int32 MaxLaneCount = Private::GetMaxLaneCountOnSpline(SplineComponent);

		for (int32 AbsLane = 1; AbsLane <= MaxLaneCount; ++AbsLane)
		{
			const int32 LaneIndices[2] = { AbsLane, -AbsLane };
			for (int32 LaneIndex : LaneIndices)
			{
				const FLinearColor LineColor = Private::GetLaneEditBaseColor(LaneIndex);
				const FLinearColor UnderlayColor = LaneIndex < 0
					? FLinearColor(0.15f, 0.85f, 0.25f, 0.40f)
					: FLinearColor(0.95f, 0.2f, 0.2f, 0.40f);

				// Per-span draw: merging whole S with one LaneSectionIndex skewed lines
				// after Left|Right face splits / ROffset changes (Fig2 misalignment).
				for (int32 SectionIndex = 0; SectionIndex < SplineComponent->GetLaneSectionsNum(); ++SectionIndex)
				{
					if (!Private::IsLaneBoundaryValidForSpanSection(Layout, SectionIndex, LaneIndex))
					{
						continue;
					}

					const FMetaLandRoadLaneSection& SpanSection = SplineComponent->GetLaneSection(SectionIndex);
					const double SpanS0 = SpanSection.SOffset;
					const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComponent, SectionIndex);
					if ((SpanS1 - SpanS0) < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const int32 ResolvedLaneSection = Private::ResolveLaneSectionIndex(Layout, SectionIndex, LaneIndex);
					if (ResolvedLaneSection == INDEX_NONE)
					{
						continue;
					}

					const FMetaLandRoadLaneSection& LaneDataSection =
						SplineComponent->GetLaneSection(ResolvedLaneSection);
					if (!LaneDataSection.CheckLaneIndex(LaneIndex))
					{
						continue;
					}

					const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange LaneRange =
						SplineComponent->GetLaneRange(SectionIndex, LaneIndex);
					double DrawS0 = FMath::Max(SpanS0, LaneRange.StartS);
					double DrawS1 = FMath::Min(SpanS1, LaneRange.EndS);
					if (!Private::IntersectDrawRangeWithMeshSpans(SplineComponent, DrawS0, DrawS1, DrawS0, DrawS1))
					{
						continue;
					}
					if ((DrawS1 - DrawS0) < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					Private::DrawContinuousLaneBoundaryPDI(
						PDI,
						SplineComponent,
						ResolvedLaneSection,
						LaneIndex,
						DrawS0,
						DrawS1,
						UnderlayColor,
						UnderlayThickness,
						1.0);
					Private::DrawContinuousLaneBoundaryPDI(
						PDI,
						SplineComponent,
						ResolvedLaneSection,
						LaneIndex,
						DrawS0,
						DrawS1,
						LineColor,
						LineThickness,
						1.0);
				}
			}
		}
	}
#endif
}
