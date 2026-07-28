#include "MetaLandRoadLaneVisualization.h"

#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Editor.h"
#include "MetaLandRoadVisProxy.h"
#include "Settings/LevelEditorViewportSettings.h"
#endif

namespace MetaLandRoadLaneVisualization
{
	namespace Private
	{
		static constexpr uint8 LaneLineDepthPriority = SDPG_Foreground;

		static ULineBatchComponent* GetPersistentLineBatcher(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			const UWorld* World = SplineComponent ? SplineComponent->GetWorld() : nullptr;
			return World ? World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr;
		}

		static uint32 GetVisualizationBatchId(const UMetaLandRoadSplineComponent* SplineComponent)
		{
			return static_cast<uint32>(SplineComponent->GetUniqueID());
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
			if (LaneIndex > 0)
			{
				return LaneIndex <= Section.Right.Num();
			}
			return FMath::Abs(LaneIndex) <= Section.Left.Num();
		}

		static int32 ResolveLaneSectionIndex(
			const FMetaLandRoadLayout& Layout,
			int32 SpanSectionIndex,
			int32 LaneIndex)
		{
			if (!Layout.Sections.IsValidIndex(SpanSectionIndex))
			{
				return INDEX_NONE;
			}

			if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
			{
				return SpanSectionIndex;
			}

			const FMetaLandRoadLaneSection& Section = Layout.Sections[SpanSectionIndex];
			if (LaneIndex > 0)
			{
				if (Section.Right.Num() > 0 && MetaLandRoad::HasRightSide(Section.Side))
				{
					return SpanSectionIndex;
				}
				return Layout.FindSideSection(SpanSectionIndex, EMetaLandRoadLaneSectionSide::Right);
			}

			if (Section.Left.Num() > 0 && MetaLandRoad::HasLeftSide(Section.Side))
			{
				return SpanSectionIndex;
			}
			return Layout.FindSideSection(SpanSectionIndex, EMetaLandRoadLaneSectionSide::Left);
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
			TFunctionRef<void(const FVector&, const FVector&)> DrawLineFn)
		{
			if (!Layout.Sections.IsValidIndex(SectionIndex) || SectionIndex <= 0)
			{
				return;
			}

			const float Distance = static_cast<float>(Layout.Sections[SectionIndex].SOffset);
			if (!FMath::IsFinite(Distance))
			{
				return;
			}

			const FMetaLandRoadLaneSection& SectionAtBoundary = Layout.Sections[SectionIndex];
			FVector CenterWorld = Spline->EvalLanePosition(
				SectionIndex,
				LANE_INDEX_NONE,
				Distance,
				0.0,
				ESplineCoordinateSpace::World);
			CenterWorld.Z += Spline->LaneVisualizationHeightOffset;

			FVector LeftWorld = CenterWorld;
			FVector RightWorld = CenterWorld;
			bool bHasLeft = false;
			bool bHasRight = false;

			if (MetaLandRoad::HasLeftSide(SectionAtBoundary.Side))
			{
				const int32 LeftSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
				if (LeftSectionIndex != INDEX_NONE)
				{
					const FMetaLandRoadLaneSection& LeftSection = Layout.Sections[LeftSectionIndex];
					if (!LeftSection.Left.IsEmpty())
					{
						LeftWorld = Spline->EvalLanePosition(
							LeftSectionIndex,
							-LeftSection.Left.Num(),
							Distance,
							1.0,
							ESplineCoordinateSpace::World);
						LeftWorld.Z += Spline->LaneVisualizationHeightOffset;
						bHasLeft = true;
					}
				}
			}

			if (MetaLandRoad::HasRightSide(SectionAtBoundary.Side))
			{
				const int32 RightSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);
				if (RightSectionIndex != INDEX_NONE)
				{
					const FMetaLandRoadLaneSection& RightSection = Layout.Sections[RightSectionIndex];
					if (!RightSection.Right.IsEmpty())
					{
						RightWorld = Spline->EvalLanePosition(
							RightSectionIndex,
							RightSection.Right.Num(),
							Distance,
							1.0,
							ESplineCoordinateSpace::World);
						RightWorld.Z += Spline->LaneVisualizationHeightOffset;
						bHasRight = true;
					}
				}
			}

			switch (SectionAtBoundary.Side)
			{
			case EMetaLandRoadLaneSectionSide::Left:
				if (bHasLeft)
				{
					DrawLineFn(LeftWorld, CenterWorld);
				}
				break;
			case EMetaLandRoadLaneSectionSide::Right:
				if (bHasRight)
				{
					DrawLineFn(CenterWorld, RightWorld);
				}
				break;
			default:
				if (bHasLeft && bHasRight)
				{
					DrawLineFn(LeftWorld, RightWorld);
				}
				else if (bHasLeft)
				{
					DrawLineFn(LeftWorld, CenterWorld);
				}
				else if (bHasRight)
				{
					DrawLineFn(CenterWorld, RightWorld);
				}
				break;
			}
		}

		static void AppendSectionSplitLinesPDI(
			FPrimitiveDrawInterface* PDI,
			const UMetaLandRoadSplineComponent* Spline,
			const FMetaLandRoadLayout& Layout)
		{
			if (!PDI || Layout.Sections.Num() < 2)
			{
				return;
			}

			const FLinearColor SplitColor(1.0f, 0.45f, 0.05f, 1.0f);
			constexpr float SplitThickness = 20.0f;
			for (int32 SectionIndex = 1; SectionIndex < Layout.Sections.Num(); ++SectionIndex)
			{
				DrawSectionSplitLineSideAware(
					Spline,
					Layout,
					SectionIndex,
					SplitColor,
					SplitThickness,
					[PDI, SplitColor, SplitThickness](const FVector& A, const FVector& B)
					{
						DrawSegmentPDI(PDI, A, B, SplitColor, SplitThickness);
					});
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
					? FLinearColor(0.2f, 0.95f, 0.35f, 1.0f)
					: FLinearColor(0.15f, 0.75f, 0.25f, 1.0f);
			}

			return bOuterLane
				? FLinearColor(0.15f, 0.75f, 1.0f, 1.0f)
				: FLinearColor(0.25f, 0.55f, 0.95f, 1.0f);
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
			int32 SectionIndex,
			int32 LaneIndex,
			const FMetaLandRoadLaneEditHighlight& Highlight)
		{
			if (LaneIndex == LANE_INDEX_NONE)
			{
				return false;
			}

			const bool bHovered =
				Highlight.HoveredSectionIndex == SectionIndex
				&& Highlight.HoveredLaneIndex == LaneIndex;
			const bool bSelected =
				Highlight.SelectedSectionIndex == SectionIndex
				&& Highlight.SelectedLaneIndex == LaneIndex;
			return bHovered || bSelected;
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

				const bool bHighlighted = IsLaneEditHighlighted(SectionIndex, LaneIndex, Highlight);
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
			int32 SectionIndex,
			int32 LaneIndex,
			int32 WidthIndex,
			double S0,
			double S1,
			const FLinearColor& Color,
			float Thickness)
		{
			if (!PDI || !Spline || (S1 - S0) < KINDA_SMALL_NUMBER)
			{
				return;
			}

			const FMetaLandRoadLaneSection& LaneSection = Spline->GetLaneSection(SectionIndex);
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

			for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
			{
				FVector Start = Points[PointIndex - 1].Position;
				FVector End = Points[PointIndex].Position;
				Start.Z += Spline->LaneVisualizationHeightOffset;
				End.Z += Spline->LaneVisualizationHeightOffset;
				DrawSegmentPDI(PDI, Start, End, Color, Thickness);
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

	void DrawLaneEditVisualization(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight)
	{
		if (!SplineComponent || !PDI || SplineComponent->GetLaneSectionsNum() == 0)
		{
			return;
		}

		const FLinearColor HighlightColor(1.0f, 1.0f, 0.0f, 1.0f);
		constexpr float NormalThickness = 3.0f;
		constexpr float HighlightThickness = 5.0f;

		for (int32 SectionIndex = 0; SectionIndex < SplineComponent->GetLaneSectionsNum(); ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = SplineComponent->GetLaneSection(SectionIndex);
			const int32 MaxLaneIndex = FMath::Max(Section.Left.Num(), Section.Right.Num());

			for (int32 AbsLane = 1; AbsLane <= MaxLaneIndex; ++AbsLane)
			{
				const int32 LaneIndices[2] = { AbsLane, -AbsLane };
				for (int32 LaneIndex : LaneIndices)
				{
					if (LaneIndex > 0 && LaneIndex > Section.Right.Num())
					{
						continue;
					}
					if (LaneIndex < 0 && -LaneIndex > Section.Left.Num())
					{
						continue;
					}

					const FMetaLandRoadLane& Lane = Section.GetLaneByIndex(LaneIndex);
					const FLinearColor BaseColor = Private::GetLaneEditBaseColor(LaneIndex);
					const bool bLaneSelected =
						Highlight.SelectedSectionIndex == SectionIndex
						&& Highlight.SelectedLaneIndex == LaneIndex;

					const int32 NumKeys = Lane.Width.GetNumKeys();
					const int32 NumSegments = FMath::Max(NumKeys, 1);
					for (int32 WidthIndex = 0; WidthIndex < NumSegments; ++WidthIndex)
					{
						double S0 = Lane.GetStartOffset();
						double S1 = Lane.GetEndOffset();
						if (NumKeys > 0)
						{
							S0 = Section.SOffset + (WidthIndex == 0 ? 0.0 : Lane.Width.Keys[WidthIndex].Time);
							S1 = (WidthIndex + 1 < NumKeys)
								? Section.SOffset + Lane.Width.Keys[WidthIndex + 1].Time
								: Lane.GetEndOffset();
						}

						if ((S1 - S0) < KINDA_SMALL_NUMBER)
						{
							continue;
						}

						const bool bSegmentHighlighted =
							(Highlight.HoveredSectionIndex == SectionIndex
								&& Highlight.HoveredLaneIndex == LaneIndex
								&& Highlight.HoveredWidthIndex == WidthIndex)
							|| bLaneSelected;
						const FLinearColor DrawColor = bSegmentHighlighted ? HighlightColor : BaseColor;
						const float DrawThickness = bSegmentHighlighted ? HighlightThickness : NormalThickness;

						Private::DrawLaneWidthBorderSegmentPDI(
							PDI,
							SplineComponent,
							SectionIndex,
							LaneIndex,
							WidthIndex,
							S0,
							S1,
							DrawColor,
							DrawThickness);
					}

				}
			}
		}
	}
#endif
}
