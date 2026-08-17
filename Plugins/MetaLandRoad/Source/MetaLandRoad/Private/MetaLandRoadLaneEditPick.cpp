#include "MetaLandRoadLaneEditPick.h"

#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadSplineComponent.h"
#include "MoKuEditSplineActor.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "UnrealWidget.h"
#include "SceneView.h"

double MetaLandRoadLaneEditPick::ClampMouthAnchorSToVisibleMesh(
	const UMetaLandRoadSplineComponent* SplineComp,
	double PreferredAnchorS,
	EMetaLandRoadMouthSide MouthSide)
{
	if (!SplineComp)
	{
		return PreferredAnchorS;
	}

	const double Length = SplineComp->GetSplineLength();
	double ClampedPreferred = FMath::Clamp(PreferredAnchorS, 0.0, Length);

	const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	if (!RoadActor)
	{
		return ClampedPreferred;
	}

	TArray<TPair<double, double>> MeshSpans;
	RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
	if (MeshSpans.Num() == 0)
	{
		return ClampedPreferred;
	}

	for (const TPair<double, double>& Span : MeshSpans)
	{
		if (ClampedPreferred >= Span.Key - 1.0 && ClampedPreferred <= Span.Value + 1.0)
		{
			return MouthSide == EMetaLandRoadMouthSide::Start
				? FMath::Max(Span.Key, FMath::Min(ClampedPreferred, Span.Value))
				: FMath::Min(Span.Value, FMath::Max(ClampedPreferred, Span.Key));
		}
	}

	if (MouthSide == EMetaLandRoadMouthSide::End)
	{
		double BestEnd = MeshSpans[0].Value;
		for (const TPair<double, double>& Span : MeshSpans)
		{
			if (Span.Value <= ClampedPreferred + 1.0)
			{
				BestEnd = Span.Value;
			}
		}
		return BestEnd;
	}

	double BestStart = MeshSpans.Last().Key;
	for (int32 Index = MeshSpans.Num() - 1; Index >= 0; --Index)
	{
		if (MeshSpans[Index].Key >= ClampedPreferred - 1.0)
		{
			BestStart = MeshSpans[Index].Key;
		}
	}
	return BestStart;
}

bool MetaLandRoadLaneEditPick::ShouldDeferInputToTranslateWidget(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 MouseX,
	int32 MouseY)
{
	if (ViewportClient && ViewportClient->IsTracking())
	{
		return true;
	}

	if (!Viewport)
	{
		return false;
	}

	HHitProxy* HitProxy = Viewport->GetHitProxy(MouseX, MouseY);
	return HitProxy && HitProxy->IsA(HWidgetAxis::StaticGetType());
}

bool MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SpanSectionIndex,
	int32 LaneIndex)
{
	if (!SplineComp || SpanSectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
	{
		return false;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		SplineComp, SpanSectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE || !SplineComp->GetLaneSections().IsValidIndex(LaneSectionIndex))
	{
		return false;
	}

	if (!SplineComp->GetLaneSection(LaneSectionIndex).CheckLaneIndex(LaneIndex))
	{
		return false;
	}

	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		SplineComp->GetLaneRange(SpanSectionIndex, LaneIndex);
	if (Range.EndS <= Range.StartS + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const double SpanS0 = SplineComp->GetLaneSection(SpanSectionIndex).SOffset;
	const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComp, SpanSectionIndex);
	const double ClipS0 = FMath::Max(SpanS0, Range.StartS);
	const double ClipS1 = FMath::Min(SpanS1, Range.EndS);
	return ClipS1 > ClipS0 + KINDA_SMALL_NUMBER;
}

FVector MetaLandRoadLaneEditPick::RaiseLaneVizPoint(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FVector& WorldLocation)
{
	const float Z = SplineComp ? SplineComp->LaneVisualizationHeightOffset : 15.0f;
	return WorldLocation + FVector(0.0f, 0.0f, Z);
}

bool MetaLandRoadLaneEditPick::PickLaneWidthSegmentAtMouse(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FSceneView& View,
	const FIntPoint& MousePos,
	float MaxScreenDistance,
	FMetaLandRoadLaneWidthSegmentPickResult& OutResult)
{
	OutResult = FMetaLandRoadLaneWidthSegmentPickResult();
	if (!SplineComp || SplineComp->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const FVector2D Mouse(MousePos.X, MousePos.Y);
	const float MaxDistSq = MaxScreenDistance * MaxScreenDistance;
	float BestDistSq = MaxDistSq;
	const FMetaLandRoadLayout& Layout = SplineComp->GetRoadLayout();
	int32 LocalMaxLaneCount = 0;
	for (const FMetaLandRoadLaneSection& Section : Layout.Sections)
	{
		LocalMaxLaneCount = FMath::Max(LocalMaxLaneCount, Section.Left.Num());
		LocalMaxLaneCount = FMath::Max(LocalMaxLaneCount, Section.Right.Num());
	}

	for (int32 AbsLane = 1; AbsLane <= LocalMaxLaneCount; ++AbsLane)
	{
		const int32 LaneIndices[2] = { AbsLane, -AbsLane };
		for (int32 LaneIndex : LaneIndices)
		{
			for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
			{
				const FMetaLandRoadLaneSection& SpanSection = Layout.Sections[SectionIndex];
				if (LaneIndex > 0)
				{
					if (SpanSection.Right.Num() == 0 || !MetaLandRoad::HasRightSide(SpanSection.Side))
					{
						if (Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right) == INDEX_NONE)
						{
							continue;
						}
					}
				}
				else if (LaneIndex < 0)
				{
					if (SpanSection.Left.Num() == 0 || !MetaLandRoad::HasLeftSide(SpanSection.Side))
					{
						if (Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left) == INDEX_NONE)
						{
							continue;
						}
					}
				}

				const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
					SplineComp, SectionIndex, LaneIndex);
				if (LaneSectionIndex == INDEX_NONE
					|| !SplineComp->GetLaneSections().IsValidIndex(LaneSectionIndex))
				{
					continue;
				}

				const FMetaLandRoadLaneSection& LaneSection = SplineComp->GetLaneSection(LaneSectionIndex);
				if (!LaneSection.CheckLaneIndex(LaneIndex))
				{
					continue;
				}

				const double SpanS0 = SpanSection.SOffset;
				const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
				if ((SpanS1 - SpanS0) < KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
				const double LaneStartS = Lane.GetStartOffset();
				const double LaneEndS = Lane.GetEndOffset();
				const double DrawS0 = FMath::Max(SpanS0, LaneStartS);
				const double DrawS1 = FMath::Min(SpanS1, LaneEndS);
				if (DrawS1 <= DrawS0 + KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 NumKeys = Lane.Width.GetNumKeys();
				const int32 NumSegments = FMath::Max(NumKeys, 1);
				for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
				{
					double S0 = DrawS0;
					double S1 = DrawS1;
					if (NumKeys > 0)
					{
						S0 = LaneStartS + Lane.Width.Keys[SegmentIndex].Time;
						S1 = (SegmentIndex + 1 < NumKeys)
							? LaneStartS + Lane.Width.Keys[SegmentIndex + 1].Time
							: LaneEndS;
						S0 = FMath::Max(S0, DrawS0);
						S1 = FMath::Min(S1, DrawS1);
					}

					if ((S1 - S0) < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
					SplineComp->BuildLinearApproximation(
						Points,
						[LaneIndex, &LaneSection, SplineComp](double S)
						{
							return LaneSection.EvalLaneROffset(LaneIndex, S, 1.0) + SplineComp->EvalROffset(S);
						},
						S0,
						S1,
						MetaLandRoad::DefaultReparamStepsPerSegment,
						MetaLandRoad::DefaultMinNumStepsPerSection,
						ESplineCoordinateSpace::World);

					FVector PrevPos = FVector::ZeroVector;
					bool bHasPrev = false;
					const float Z = SplineComp->LaneVisualizationHeightOffset;
					for (const FMetaLandRoadSplinePositionLinearApproximation& Sample : Points)
					{
						FVector Pos = Sample.Position;
						Pos.Z += Z;
						if (bHasPrev)
						{
							FVector2D A2D;
							FVector2D B2D;
							if (View.WorldToPixel(PrevPos, A2D) && View.WorldToPixel(Pos, B2D))
							{
								const float DistSq = FMath::Square(
									MetaLandRoad::DistancePointToSegment2D(Mouse, A2D, B2D));
								if (DistSq < BestDistSq)
								{
									BestDistSq = DistSq;
									OutResult.SectionIndex = SectionIndex;
									OutResult.LaneIndex = LaneIndex;
									OutResult.WidthIndex = SegmentIndex;
								}
							}
						}
						PrevPos = Pos;
						bHasPrev = true;
					}
				}
			}
		}
	}

	return OutResult.SectionIndex != INDEX_NONE;
}

bool MetaLandRoadLaneEditPick::PickLaneBoundaryOnSectionAtMouse(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FSceneView& View,
	const FIntPoint& MousePos,
	float MaxScreenDistance,
	FMetaLandRoadLaneWidthSegmentPickResult& OutResult)
{
	OutResult = FMetaLandRoadLaneWidthSegmentPickResult();
	if (!SplineComp || SplineComp->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const FVector2D Mouse(MousePos.X, MousePos.Y);
	const float MaxDistSq = MaxScreenDistance * MaxScreenDistance;
	float BestDistSq = MaxDistSq;
	const FMetaLandRoadLayout& Layout = SplineComp->GetRoadLayout();
	int32 LocalMaxLaneCount = 0;
	for (const FMetaLandRoadLaneSection& Section : Layout.Sections)
	{
		LocalMaxLaneCount = FMath::Max(LocalMaxLaneCount, Section.Left.Num());
		LocalMaxLaneCount = FMath::Max(LocalMaxLaneCount, Section.Right.Num());
	}

	for (int32 AbsLane = 1; AbsLane <= LocalMaxLaneCount; ++AbsLane)
	{
		const int32 LaneIndices[2] = { AbsLane, -AbsLane };
		for (int32 LaneIndex : LaneIndices)
		{
			for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
			{
				if (!IsLaneValidOnSpanSection(SplineComp, SectionIndex, LaneIndex))
				{
					continue;
				}

				const FMetaLandRoadLaneSection& SpanSection = Layout.Sections[SectionIndex];
				const double SpanS0 = SpanSection.SOffset;
				const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
				if ((SpanS1 - SpanS0) < KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
					SplineComp, SectionIndex, LaneIndex);
				if (LaneSectionIndex == INDEX_NONE
					|| !SplineComp->GetLaneSections().IsValidIndex(LaneSectionIndex))
				{
					continue;
				}

				const FMetaLandRoadLaneSection& LaneSection = SplineComp->GetLaneSection(LaneSectionIndex);
				if (!LaneSection.CheckLaneIndex(LaneIndex))
				{
					continue;
				}

				const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
				const double DrawS0 = FMath::Max(SpanS0, Lane.GetStartOffset());
				const double DrawS1 = FMath::Min(SpanS1, Lane.GetEndOffset());
				if (DrawS1 <= DrawS0 + KINDA_SMALL_NUMBER)
				{
					continue;
				}

				TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
				SplineComp->BuildLinearApproximation(
					Points,
					[LaneIndex, &LaneSection, SplineComp](double S)
					{
						return LaneSection.EvalLaneROffset(LaneIndex, S, 1.0) + SplineComp->EvalROffset(S);
					},
					DrawS0,
					DrawS1,
					MetaLandRoad::DefaultReparamStepsPerSegment,
					MetaLandRoad::DefaultMinNumStepsPerSection,
					ESplineCoordinateSpace::World);

				FVector PrevPos = FVector::ZeroVector;
				bool bHasPrev = false;
				const float Z = SplineComp->LaneVisualizationHeightOffset;
				for (const FMetaLandRoadSplinePositionLinearApproximation& Sample : Points)
				{
					FVector Pos = Sample.Position;
					Pos.Z += Z;
					if (bHasPrev)
					{
						FVector2D A2D;
						FVector2D B2D;
						if (View.WorldToPixel(PrevPos, A2D) && View.WorldToPixel(Pos, B2D))
						{
							const float DistSq = FMath::Square(
								MetaLandRoad::DistancePointToSegment2D(Mouse, A2D, B2D));
							if (DistSq < BestDistSq)
							{
								BestDistSq = DistSq;
								OutResult.SectionIndex = SectionIndex;
								OutResult.LaneIndex = LaneIndex;
								OutResult.WidthIndex = INDEX_NONE;
							}
						}
					}
					PrevPos = Pos;
					bHasPrev = true;
				}
			}
		}
	}

	return OutResult.SectionIndex != INDEX_NONE;
}

bool MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FSceneView& View,
	const FIntPoint& MousePos,
	int32 PrimarySectionIndex,
	int32 PrimaryLaneIndex,
	int32 PrimaryWidthIndex,
	const TArray<FMetaLandRoadSelectedLaneRefU>& MultiSelectedLanes,
	int32 HoveredSectionIndex,
	int32 HoveredLaneIndex,
	float MaxScreenDistance,
	FMetaLandRoadLaneWidthSegmentPickResult& OutResult)
{
	OutResult = FMetaLandRoadLaneWidthSegmentPickResult();
	if (!SplineComp)
	{
		return false;
	}

	TArray<FMetaLandRoadSelectedLaneRefU> LanesForKeys = MultiSelectedLanes;
	auto AddLaneForKeys = [&LanesForKeys](int32 SectionIndex, int32 LaneIndex)
	{
		if (SectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
		{
			return;
		}

		for (const FMetaLandRoadSelectedLaneRefU& Existing : LanesForKeys)
		{
			if (Existing.SectionIndex == SectionIndex && Existing.LaneIndex == LaneIndex)
			{
				return;
			}
		}

		LanesForKeys.Add({ SectionIndex, LaneIndex });
	};

	AddLaneForKeys(PrimarySectionIndex, PrimaryLaneIndex);
	AddLaneForKeys(HoveredSectionIndex, HoveredLaneIndex);

	// Every visible ring is pickable — do not require a prior lane selection.
	for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
		const int32 MaxAbsLane = FMath::Max(Section.Left.Num(), Section.Right.Num());
		for (int32 AbsLane = 1; AbsLane <= MaxAbsLane; ++AbsLane)
		{
			if (IsLaneValidOnSpanSection(SplineComp, SectionIndex, AbsLane))
			{
				AddLaneForKeys(SectionIndex, AbsLane);
			}
			if (IsLaneValidOnSpanSection(SplineComp, SectionIndex, -AbsLane))
			{
				AddLaneForKeys(SectionIndex, -AbsLane);
			}
		}
	}

	const FVector2D Mouse(MousePos.X, MousePos.Y);
	const float MaxDistSq = MaxScreenDistance * MaxScreenDistance;
	float BestDistSq = MaxDistSq;

	for (const FMetaLandRoadSelectedLaneRefU& LaneRef : LanesForKeys)
	{
		const int32 SpanSectionIndex = LaneRef.SectionIndex;
		const int32 LaneIndex = LaneRef.LaneIndex;
		if (!SplineComp->GetLaneSections().IsValidIndex(SpanSectionIndex))
		{
			continue;
		}

		const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, SpanSectionIndex, LaneIndex);
		if (LaneSectionIndex == INDEX_NONE || !SplineComp->GetLaneSections().IsValidIndex(LaneSectionIndex))
		{
			continue;
		}

		const FMetaLandRoadLaneSection& LaneSection = SplineComp->GetLaneSection(LaneSectionIndex);
		if (!LaneSection.CheckLaneIndex(LaneIndex))
		{
			continue;
		}

		const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
		const int32 NumKeys = Lane.Width.GetNumKeys();
		if (NumKeys == 0)
		{
			continue;
		}

		for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
		{
			const double KeyS = Lane.GetStartOffset() + Lane.Width.Keys[KeyIndex].Time;
			FVector KeyPos = SplineComp->EvalLanePosition(
				LaneSectionIndex, LaneIndex, KeyS, 1.0, ESplineCoordinateSpace::World);
			KeyPos.Z += SplineComp->LaneVisualizationHeightOffset;

			FVector2D KeyScreen;
			if (!View.WorldToPixel(KeyPos, KeyScreen))
			{
				continue;
			}

			const float DistSq = FVector2D::DistSquared(Mouse, KeyScreen);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				int32 KeySpanSection = SpanSectionIndex;
				const int32 HostAtKeyS = SplineComp->GetRoadLayout().FindSectionAtSOffset(KeyS);
				if (HostAtKeyS != INDEX_NONE
					&& IsLaneValidOnSpanSection(SplineComp, HostAtKeyS, LaneIndex))
				{
					KeySpanSection = HostAtKeyS;
				}
				else
				{
					for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
					{
						if (!IsLaneValidOnSpanSection(SplineComp, SectionIndex, LaneIndex))
						{
							continue;
						}

						const double SpanS0 = SplineComp->GetLaneSection(SectionIndex).SOffset;
						const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
						if (KeyS >= SpanS0 - KINDA_SMALL_NUMBER && KeyS <= SpanS1 + KINDA_SMALL_NUMBER)
						{
							KeySpanSection = SectionIndex;
							break;
						}
					}
				}

				OutResult.SectionIndex = KeySpanSection;
				OutResult.LaneIndex = LaneIndex;
				OutResult.WidthIndex = KeyIndex;
			}
		}
	}

	return OutResult.SectionIndex != INDEX_NONE;
}

EMetaLandRoadMouthSide MetaLandRoadLaneEditPick::ResolveMouthSideFromMouse(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SpanSectionIndex,
	int32 LaneIndex,
	const FSceneView& View,
	const FIntPoint& MousePos)
{
	if (!SplineComp)
	{
		return EMetaLandRoadMouthSide::End;
	}

	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		SplineComp->GetLaneRange(SpanSectionIndex, LaneIndex);
	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		SplineComp, SpanSectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE)
	{
		return EMetaLandRoadMouthSide::End;
	}

	const FVector StartWorld = SplineComp->EvalLanePosition(
		LaneSectionIndex, LaneIndex, Range.StartS, 1.0, ESplineCoordinateSpace::World);
	const FVector EndWorld = SplineComp->EvalLanePosition(
		LaneSectionIndex, LaneIndex, Range.EndS, 1.0, ESplineCoordinateSpace::World);

	FVector2D StartScreen;
	FVector2D EndScreen;
	const FVector2D Mouse(MousePos.X, MousePos.Y);
	if (!View.WorldToPixel(StartWorld, StartScreen) || !View.WorldToPixel(EndWorld, EndScreen))
	{
		return EMetaLandRoadMouthSide::End;
	}

	return FVector2D::DistSquared(Mouse, StartScreen) <= FVector2D::DistSquared(Mouse, EndScreen)
		? EMetaLandRoadMouthSide::Start
		: EMetaLandRoadMouthSide::End;
}

static void CollectAnchorLaneIndices(
	const FMetaLandRoadDrawAnchor& Anchor,
	TArray<int32>& OutLanes)
{
	OutLanes.Reset();
	OutLanes.Add(Anchor.LaneIndex);
	for (int32 BundledLaneIndex : Anchor.BundledLaneIndices)
	{
		OutLanes.AddUnique(BundledLaneIndex);
	}
}

bool MetaLandRoadLaneEditPick::IsBundleCompatibleWithAnchor(
	const FMetaLandRoadDrawAnchor& ExistingAnchor,
	int32 NewLaneIndex)
{
	if (!ExistingAnchor.IsValid() || NewLaneIndex == METALANDROAD_LANE_INDEX_NONE)
	{
		return false;
	}

	if (NewLaneIndex == ExistingAnchor.LaneIndex)
	{
		return false;
	}

	if (ExistingAnchor.BundledLaneIndices.Contains(NewLaneIndex))
	{
		return false;
	}

	// Same physical mouth: any lane (cross Left|Right, non-contiguous OK).
	// Contiguity / same-side were too rigid for non-modern "strip" roads.
	return NewLaneIndex != METALANDROAD_LANE_INDEX_NONE;
}

bool MetaLandRoadLaneEditPick::IsSamePhysicalMouth(
	const FMetaLandRoadDrawAnchor& Anchor,
	const UMetaLandRoadSplineComponent* Spline,
	const EMetaLandRoadMouthSide MouthSide,
	const double AnchorS,
	const double SToleranceCm)
{
	return Anchor.IsValid()
		&& Spline
		&& Anchor.SourceSpline.Get() == Spline
		&& Anchor.MouthSide == MouthSide
		&& FMath::IsNearlyEqual(Anchor.AnchorS, AnchorS, SToleranceCm);
}

bool MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SpanSectionIndex,
	FMetaLandRoadDrawAnchor& InOutAnchor)
{
	(void)SpanSectionIndex;
	if (!SplineComp || !InOutAnchor.IsValid())
	{
		return false;
	}

	const int32 LaneSpanSection = InOutAnchor.SectionIndex;
	int32 LocalMaxLaneCount = 0;
	for (const FMetaLandRoadLaneSection& Section : SplineComp->GetRoadLayout().Sections)
	{
		LocalMaxLaneCount = FMath::Max(LocalMaxLaneCount, Section.Left.Num());
		LocalMaxLaneCount = FMath::Max(LocalMaxLaneCount, Section.Right.Num());
	}

	// Ctrl+Shift: all lanes on this mouth (both faces), not one side only.
	InOutAnchor.BundledLaneIndices.Reset();
	for (int32 AbsLane = 1; AbsLane <= LocalMaxLaneCount; ++AbsLane)
	{
		for (const int32 LaneIndex : {AbsLane, -AbsLane})
		{
			if (LaneIndex == InOutAnchor.LaneIndex)
			{
				continue;
			}
			if (IsLaneValidOnSpanSection(SplineComp, LaneSpanSection, LaneIndex))
			{
				InOutAnchor.BundledLaneIndices.AddUnique(LaneIndex);
			}
		}
	}
	InOutAnchor.BundledLaneIndices.Sort();
	return InOutAnchor.BundledLaneIndices.Num() > 0;
}

bool MetaLandRoadLaneEditPick::ResolveMouthOutwardTangent(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FMetaLandRoadDrawAnchor& Anchor,
	FVector& OutOutwardTangent)
{
	OutOutwardTangent = FVector::ZeroVector;
	if (!SplineComp || !Anchor.IsValid() || Anchor.SourceSpline.Get() != SplineComp)
	{
		return false;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		SplineComp, Anchor.SectionIndex, Anchor.LaneIndex);
	if (LaneSectionIndex == INDEX_NONE)
	{
		return false;
	}

	// MUST be local tip tangent. Chord(StartS→EndS) on a curved road aims across the
	// arc and makes FromConnection leave with a visible kink (绿轨对、mesh 折).
	const double MouthS = ClampMouthAnchorSToVisibleMesh(
		SplineComp, Anchor.AnchorS, Anchor.MouthSide);
	FVector AlongIncreasingS = SplineComp->GetDirectionAtDistanceAlongSpline(
		MouthS, ESplineCoordinateSpace::World).GetSafeNormal2D();
	if (AlongIncreasingS.IsNearlyZero())
	{
		const double Delta = FMath::Max(50.0, SplineComp->GetSplineLength() * 0.01);
		const double SBefore = FMath::Max(MouthS - Delta, 0.0);
		const double SAfter = FMath::Min(MouthS + Delta, SplineComp->GetSplineLength());
		AlongIncreasingS = (
			SplineComp->GetLocationAtDistanceAlongSpline(SAfter, ESplineCoordinateSpace::World)
			- SplineComp->GetLocationAtDistanceAlongSpline(SBefore, ESplineCoordinateSpace::World))
			.GetSafeNormal2D();
	}
	if (AlongIncreasingS.IsNearlyZero())
	{
		return false;
	}

	OutOutwardTangent = Anchor.MouthSide == EMetaLandRoadMouthSide::End
		? AlongIncreasingS
		: -AlongIncreasingS;
	return !OutOutwardTangent.IsNearlyZero();
}

bool MetaLandRoadLaneEditPick::TryExpandAnchorBundle(
	FMetaLandRoadDrawAnchor& InOutAnchor,
	int32 NewLaneIndex)
{
	if (!IsBundleCompatibleWithAnchor(InOutAnchor, NewLaneIndex))
	{
		return false;
	}

	InOutAnchor.BundledLaneIndices.AddUnique(NewLaneIndex);
	InOutAnchor.BundledLaneIndices.Sort();
	return true;
}

bool MetaLandRoadLaneEditPick::ExpandAnchorBundleToCount(
	const UMetaLandRoadSplineComponent* SplineComp,
	FMetaLandRoadDrawAnchor& InOutAnchor,
	int32 DesiredLaneCount,
	const FVector* PreferWorldPos)
{
	if (!SplineComp || !InOutAnchor.IsValid())
	{
		return false;
	}

	DesiredLaneCount = FMath::Clamp(DesiredLaneCount, 1, 16);
	InOutAnchor.BundledLaneIndices.Reset();

	const int32 SideSign = FMath::Sign(InOutAnchor.LaneIndex);
	if (SideSign == 0 || !SplineComp->GetLaneSections().IsValidIndex(InOutAnchor.SectionIndex))
	{
		return false;
	}

	const FMetaLandRoadLayout& Layout = SplineComp->GetRoadLayout();
	const int32 RightSec = Layout.FindSideSection(
		InOutAnchor.SectionIndex, EMetaLandRoadLaneSectionSide::Right);
	const int32 LeftSec = Layout.FindSideSection(
		InOutAnchor.SectionIndex, EMetaLandRoadLaneSectionSide::Left);

	auto CollectFaceLanes = [&](int32 FaceSec, bool bRight, TArray<int32>& OutLanes)
	{
		OutLanes.Reset();
		if (FaceSec == INDEX_NONE)
		{
			return;
		}
		const FMetaLandRoadLaneSection& Face = SplineComp->GetLaneSection(FaceSec);
		if (bRight)
		{
			for (int32 i = 0; i < Face.Right.Num(); ++i)
			{
				OutLanes.Add(i + 1);
			}
		}
		else
		{
			for (int32 i = 0; i < Face.Left.Num(); ++i)
			{
				OutLanes.Add(-(i + 1));
			}
		}
	};

	TArray<int32> SeedFaceLanes;
	CollectFaceLanes(SideSign > 0 ? RightSec : LeftSec, SideSign > 0, SeedFaceLanes);
	if (SeedFaceLanes.Num() == 0)
	{
		return false;
	}

	int32 SeedRank = SeedFaceLanes.IndexOfByKey(InOutAnchor.LaneIndex);
	if (SeedRank == INDEX_NONE)
	{
		SeedRank = 0;
		InOutAnchor.LaneIndex = SeedFaceLanes[0];
	}

	const int32 TakeSeed = FMath::Min(DesiredLaneCount, SeedFaceLanes.Num());
	int32 Rank0 = FMath::Clamp(SeedRank - (TakeSeed / 2), 0, SeedFaceLanes.Num() - TakeSeed);
	if (PreferWorldPos && TakeSeed < SeedFaceLanes.Num())
	{
		double BestScore = TNumericLimits<double>::Max();
		int32 BestRank0 = Rank0;
		for (int32 Candidate0 = 0; Candidate0 <= SeedFaceLanes.Num() - TakeSeed; ++Candidate0)
		{
			if (SeedRank < Candidate0 || SeedRank >= Candidate0 + TakeSeed)
			{
				continue;
			}
			double Score = 0.0;
			for (int32 r = Candidate0; r < Candidate0 + TakeSeed; ++r)
			{
				const int32 LaneIndex = SeedFaceLanes[r];
				const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
					SplineComp, InOutAnchor.SectionIndex, LaneIndex);
				if (LaneSectionIndex == INDEX_NONE)
				{
					continue;
				}
				const FVector Outer = SplineComp->EvalLanePosition(
					LaneSectionIndex,
					LaneIndex,
					InOutAnchor.AnchorS,
					1.0,
					ESplineCoordinateSpace::World);
				Score += FVector::DistSquared2D(*PreferWorldPos, Outer);
			}
			if (Score < BestScore)
			{
				BestScore = Score;
				BestRank0 = Candidate0;
			}
		}
		Rank0 = BestRank0;
	}

	TArray<int32> Picked;
	Picked.Reserve(DesiredLaneCount);
	for (int32 r = Rank0; r < Rank0 + TakeSeed; ++r)
	{
		Picked.AddUnique(SeedFaceLanes[r]);
	}

	// Source selected 3 but seed face only has 2 (default half): pull nearest lanes
	// from the opposite face so hover/end-snap mouth width matches the selection.
	if (Picked.Num() < DesiredLaneCount)
	{
		TArray<int32> OtherFaceLanes;
		CollectFaceLanes(SideSign > 0 ? LeftSec : RightSec, SideSign <= 0, OtherFaceLanes);

		TArray<TPair<double, int32>> Ranked;
		Ranked.Reserve(OtherFaceLanes.Num());
		const FVector Prefer = PreferWorldPos
			? *PreferWorldPos
			: SplineComp->EvalLanePosition(
				MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
					SplineComp, InOutAnchor.SectionIndex, InOutAnchor.LaneIndex),
				InOutAnchor.LaneIndex,
				InOutAnchor.AnchorS,
				1.0,
				ESplineCoordinateSpace::World);
		for (const int32 LaneIndex : OtherFaceLanes)
		{
			if (Picked.Contains(LaneIndex))
			{
				continue;
			}
			const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
				SplineComp, InOutAnchor.SectionIndex, LaneIndex);
			if (LaneSectionIndex == INDEX_NONE)
			{
				continue;
			}
			const FVector Outer = SplineComp->EvalLanePosition(
				LaneSectionIndex,
				LaneIndex,
				InOutAnchor.AnchorS,
				1.0,
				ESplineCoordinateSpace::World);
			Ranked.Emplace(FVector::DistSquared2D(Prefer, Outer), LaneIndex);
		}
		Ranked.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
		{
			return A.Key < B.Key;
		});
		for (const TPair<double, int32>& Entry : Ranked)
		{
			if (Picked.Num() >= DesiredLaneCount)
			{
				break;
			}
			Picked.Add(Entry.Value);
		}
	}

	if (Picked.Num() == 0)
	{
		return false;
	}

	// Keep seed as primary when still in the set; else nearest picked.
	int32 Primary = InOutAnchor.LaneIndex;
	if (!Picked.Contains(Primary))
	{
		Primary = Picked[0];
	}
	InOutAnchor.LaneIndex = Primary;
	InOutAnchor.BundledLaneIndices.Reset();
	for (const int32 LaneIndex : Picked)
	{
		if (LaneIndex != Primary)
		{
			InOutAnchor.BundledLaneIndices.Add(LaneIndex);
		}
	}
	InOutAnchor.BundledLaneIndices.Sort();
	return true;
}

bool MetaLandRoadLaneEditPick::PickLaneAnchorAtMouse(
	UWorld* World,
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	const FIntPoint& MousePos,
	bool bShiftDown,
	bool bSelectAllLanesOnSide,
	const FMetaLandRoadDrawAnchor* ExistingAnchor,
	FMetaLandRoadDrawAnchor& OutAnchor,
	UMetaLandRoadSplineComponent*& OutSpline)
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	OutSpline = nullptr;
	if (!World || !ViewportClient || !Viewport)
	{
		return false;
	}

	FSceneViewFamilyContext ViewFamily(
		FSceneViewFamily::ConstructionValues(
			Viewport,
			ViewportClient->GetScene(),
			ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	float BestScore = TNumericLimits<float>::Max();
	FMetaLandRoadLaneFacePickResult BestFacePick;
	UMetaLandRoadSplineComponent* BestSpline = nullptr;

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
		if (!MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *View, MousePos, FacePick))
		{
			continue;
		}

		const float Score = static_cast<float>(FacePick.SectionIndex * 1000 + FMath::Abs(FacePick.LaneIndex));
		if (Score < BestScore)
		{
			BestScore = Score;
			BestFacePick = FacePick;
			BestSpline = SplineComp;
		}
	}

	if (!BestSpline || !BestFacePick.IsValid())
	{
		return false;
	}

	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		BestSpline->GetLaneRange(BestFacePick.SectionIndex, BestFacePick.LaneIndex);
	const EMetaLandRoadMouthSide MouthSide = ResolveMouthSideFromMouse(
		BestSpline,
		BestFacePick.SectionIndex,
		BestFacePick.LaneIndex,
		*View,
		MousePos);
	const double RawAnchorS = MouthSide == EMetaLandRoadMouthSide::Start ? Range.StartS : Range.EndS;
	const double AnchorS = ClampMouthAnchorSToVisibleMesh(BestSpline, RawAnchorS, MouthSide);

	const bool bSameMouthContext = ExistingAnchor
		&& MetaLandRoadLaneEditPick::IsSamePhysicalMouth(
			*ExistingAnchor,
			BestSpline,
			MouthSide,
			AnchorS);

	if (bSelectAllLanesOnSide && bSameMouthContext)
	{
		OutAnchor = *ExistingAnchor;
		if (TrySelectAllLanesOnMouthSide(BestSpline, OutAnchor.SectionIndex, OutAnchor))
		{
			OutSpline = BestSpline;
			MetaLandRoadLogDrawAnchor(TEXT("PickAllLanes"), OutAnchor);
			return true;
		}
	}

	if (bShiftDown && bSameMouthContext)
	{
		const int32 ClickedLane = BestFacePick.LaneIndex;
		OutAnchor = *ExistingAnchor;

		if (OutAnchor.LaneIndex == ClickedLane)
		{
			if (OutAnchor.BundledLaneIndices.Num() > 0)
			{
				OutAnchor.LaneIndex = OutAnchor.BundledLaneIndices[0];
				OutAnchor.BundledLaneIndices.RemoveAt(0);
			}
			else
			{
				OutAnchor = FMetaLandRoadDrawAnchor();
			}
		}
		else
		{
			const int32 RemovedCount = OutAnchor.BundledLaneIndices.Remove(ClickedLane);
			if (RemovedCount == 0 && TryExpandAnchorBundle(OutAnchor, ClickedLane))
			{
				OutSpline = BestSpline;
				MetaLandRoadLogDrawAnchor(TEXT("PickBundle"), OutAnchor);
				return true;
			}
			if (RemovedCount == 0 && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					991126,
					2.5f,
					FColor::Orange,
					TEXT("[MetaLandRoad] Shift+click: toggle any lane on this mouth"));
			}
		}

		OutSpline = BestSpline;
		MetaLandRoadLogDrawAnchor(TEXT("PickToggle"), OutAnchor);
		return true;
	}

	// Mouth pick requires Shift (Ctrl+Shift = all lanes on side). Plain LMB must not select lanes.
	if (!bShiftDown && !bSelectAllLanesOnSide)
	{
		return false;
	}

	if (!BestSpline->BuildDrawAnchorForLaneMouth(
		BestFacePick.SectionIndex,
		BestFacePick.LaneIndex,
		AnchorS,
		MouthSide,
		OutAnchor))
	{
		return false;
	}

	OutSpline = BestSpline;
	MetaLandRoadLogDrawAnchor(TEXT("Pick"), OutAnchor);
	return true;
}

static int32 ResolveInnermostLaneIndex(const TArray<int32>& Lanes)
{
	if (Lanes.Num() == 0)
	{
		return LANE_INDEX_NONE;
	}

	int32 Innermost = Lanes[0];
	for (int32 LaneIndex : Lanes)
	{
		if (LaneIndex > 0)
		{
			Innermost = (Innermost > 0) ? FMath::Min(Innermost, LaneIndex) : LaneIndex;
		}
		else if (LaneIndex < 0)
		{
			Innermost = (Innermost < 0) ? FMath::Max(Innermost, LaneIndex) : LaneIndex;
		}
	}
	return Innermost;
}

static int32 ResolveOutermostLaneIndex(const TArray<int32>& Lanes)
{
	if (Lanes.Num() == 0)
	{
		return LANE_INDEX_NONE;
	}

	int32 Outermost = Lanes[0];
	for (int32 LaneIndex : Lanes)
	{
		if (LaneIndex > 0)
		{
			Outermost = FMath::Max(Outermost, LaneIndex);
		}
		else if (LaneIndex < 0)
		{
			Outermost = FMath::Min(Outermost, LaneIndex);
		}
	}
	return Outermost;
}

bool MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FMetaLandRoadDrawAnchor& Anchor,
	FVector& OutTipLeft,
	FVector& OutTipRight,
	FVector& OutTipCenter,
	FVector& OutOutwardTangent)
{
	OutTipLeft = OutTipRight = OutTipCenter = OutOutwardTangent = FVector::ZeroVector;
	if (!SplineComp || !Anchor.IsValid() || Anchor.SourceSpline.Get() != SplineComp)
	{
		return false;
	}

	if (MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, Anchor.SectionIndex, Anchor.LaneIndex)
		== INDEX_NONE)
	{
		return false;
	}

	TArray<int32> Lanes;
	CollectAnchorLaneIndices(Anchor, Lanes);

	FMetaLandRoadDrawAnchor VisibleAnchor = Anchor;
	VisibleAnchor.AnchorS = ClampMouthAnchorSToVisibleMesh(
		SplineComp, Anchor.AnchorS, Anchor.MouthSide);
	const double S = VisibleAnchor.AnchorS;

	if (!ResolveMouthOutwardTangent(SplineComp, VisibleAnchor, OutOutwardTangent))
	{
		return false;
	}

	// Cross-side subset: mouth = leftmost outer ↔ rightmost outer (α=1).
	// Same-side: innermost α=0 ↔ outermost α=1 (ResolveOutermost breaks across signs).
	int32 LeftOuterLane = LANE_INDEX_NONE;
	int32 RightOuterLane = LANE_INDEX_NONE;
	for (const int32 LaneIndex : Lanes)
	{
		if (LaneIndex > 0)
		{
			RightOuterLane = (RightOuterLane == LANE_INDEX_NONE)
				? LaneIndex
				: FMath::Max(RightOuterLane, LaneIndex);
		}
		else if (LaneIndex < 0)
		{
			LeftOuterLane = (LeftOuterLane == LANE_INDEX_NONE)
				? LaneIndex
				: FMath::Min(LeftOuterLane, LaneIndex);
		}
	}

	FVector EdgeA;
	FVector EdgeB;
	if (LeftOuterLane != LANE_INDEX_NONE && RightOuterLane != LANE_INDEX_NONE)
	{
		const int32 LeftSec = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, Anchor.SectionIndex, LeftOuterLane);
		const int32 RightSec = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, Anchor.SectionIndex, RightOuterLane);
		if (LeftSec == INDEX_NONE || RightSec == INDEX_NONE)
		{
			return false;
		}
		EdgeA = SplineComp->EvalLanePosition(
			LeftSec, LeftOuterLane, S, 1.0, ESplineCoordinateSpace::World);
		EdgeB = SplineComp->EvalLanePosition(
			RightSec, RightOuterLane, S, 1.0, ESplineCoordinateSpace::World);
	}
	else
	{
		const int32 InnermostLane = ResolveInnermostLaneIndex(Lanes);
		const int32 OutermostLane = ResolveOutermostLaneIndex(Lanes);
		if (InnermostLane == LANE_INDEX_NONE || OutermostLane == LANE_INDEX_NONE)
		{
			return false;
		}
		const int32 InnerSec = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, Anchor.SectionIndex, InnermostLane);
		const int32 OuterSec = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, Anchor.SectionIndex, OutermostLane);
		if (InnerSec == INDEX_NONE || OuterSec == INDEX_NONE)
		{
			return false;
		}
		EdgeA = SplineComp->EvalLanePosition(
			InnerSec, InnermostLane, S, 0.0, ESplineCoordinateSpace::World);
		EdgeB = SplineComp->EvalLanePosition(
			OuterSec, OutermostLane, S, 1.0, ESplineCoordinateSpace::World);
	}

	OutTipCenter = (EdgeA + EdgeB) * 0.5;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, OutOutwardTangent).GetSafeNormal2D();
	if (FVector::DotProduct(EdgeA - OutTipCenter, Right) <= 0.0)
	{
		OutTipLeft = EdgeA;
		OutTipRight = EdgeB;
	}
	else
	{
		OutTipLeft = EdgeB;
		OutTipRight = EdgeA;
	}

	return true;
}
