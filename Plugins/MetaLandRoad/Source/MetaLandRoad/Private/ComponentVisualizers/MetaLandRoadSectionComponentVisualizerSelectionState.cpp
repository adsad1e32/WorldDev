#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

#include "MetaLandRoadLaneEditPick.h"
#include "MetaLandRoadLaneVisualization.h"

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedData(
	const FVector& Position,
	const FQuat& Rotation,
	float SplineKey)
{
	CachedPosition = Position;
	CachedRotation = Rotation;
	CachedSplineKey = SplineKey;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::RecordPickSplineKey(float Key)
{
	LastPickSplineKey = Key;
	bSectionBoundarySelected = false;
	SetCachedDataAtSplineInputKey(Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetLastPickSplineKey(float Key)
{
	LastPickSplineKey = Key;
	bSectionBoundarySelected = false;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedDataAtSplineInputKey(float Key)
{
	const UMetaLandRoadSplineComponent* SplineComp = GetSelectedSpline();
	if (!IsValid(SplineComp) || SplineComp->GetLaneSectionsNum() == 0)
	{
		ResetCachedData();
		return;
	}

	const float S = SplineComp->GetDistanceAlongSplineAtSplineInputKey(Key);
	const int32 SectionIndex = FMath::Clamp(
		SplineComp->GetRoadLayout().FindSectionAtSOffset(S),
		0,
		SplineComp->GetLaneSectionsNum() - 1);
	const FVector Loc = SplineComp->EvalLanePosition(
		SectionIndex, LANE_INDEX_NONE, S, 1.0, ESplineCoordinateSpace::World);
	const FQuat Rot = SplineComp->GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	SetCachedData(Loc, Rot, Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedDataAtLane(
	int32 SectionIndex,
	int32 LaneIndex,
	double SOffset,
	double Alpha)
{
	const UMetaLandRoadSplineComponent* SplineComp = GetSelectedSpline();
	if (!IsValid(SplineComp)
		|| !SplineComp->GetLaneSections().IsValidIndex(SectionIndex)
		|| LaneIndex == LANE_INDEX_NONE)
	{
		ResetCachedData();
		return;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		SplineComp,
		SectionIndex,
		LaneIndex);
	if (LaneSectionIndex == INDEX_NONE)
	{
		ResetCachedData();
		return;
	}

	const FVector Loc = MetaLandRoadLaneEditPick::RaiseLaneVizPoint(
		SplineComp,
		SplineComp->EvalLanePosition(LaneSectionIndex, LaneIndex, SOffset, Alpha, ESplineCoordinateSpace::World));
	const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(SOffset);
	const FQuat Rot = SplineComp->GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	SetCachedData(Loc, Rot, Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedDataAtSectionStart(int32 SectionIndex)
{
	const UMetaLandRoadSplineComponent* SplineComp = GetSelectedSpline();
	if (!IsValid(SplineComp) || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		ResetCachedData();
		return;
	}

	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(Section.SOffset);
	const FVector Loc = MetaLandRoadLaneEditPick::RaiseLaneVizPoint(
		SplineComp,
		SplineComp->EvalLanePosition(SectionIndex, LANE_INDEX_NONE, Section.SOffset, 0.0, ESplineCoordinateSpace::World));
	const FQuat Rot = SplineComp->GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	SetCachedData(Loc, Rot, Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ResetCachedData()
{
	CachedRotation = FQuat::Identity;
	CachedPosition = FVector::ZeroVector;
	CachedSplineKey = 0.0f;
	LastPickSplineKey = -1.0f;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::FixState()
{
	const EMetaLandRoadSectionSelectionState StateVerified = GetStateVerified();
	if (StateVerified != State)
	{
		if (StateVerified < EMetaLandRoadSectionSelectionState::Key)
		{
			SelectedKeyIndex = INDEX_NONE;
		}
		if (StateVerified < EMetaLandRoadSectionSelectionState::Lane)
		{
			SelectedLaneIndex = LANE_INDEX_NONE;
			MultiSelectedLanes.Reset();
		}
		if (StateVerified < EMetaLandRoadSectionSelectionState::Section)
		{
			SelectedSectionIndex = INDEX_NONE;
		}
		if (StateVerified < EMetaLandRoadSectionSelectionState::Component)
		{
			SplinePropertyPath = FComponentPropertyPath();
		}
		State = StateVerified;
	}
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ResetSelection(bool bSaveSplineSelection)
{
	SelectedSectionIndex = INDEX_NONE;
	SelectedLaneIndex = LANE_INDEX_NONE;
	MultiSelectedLanes.Reset();
	SelectedKeyIndex = INDEX_NONE;
	bSectionBoundarySelected = false;
	bLaneFacePrimSelected = false;
	bLaneWidthMenuContext = false;

	if (bSaveSplineSelection && SplinePropertyPath.IsValid())
	{
		State = EMetaLandRoadSectionSelectionState::Component;
	}
	else
	{
		State = EMetaLandRoadSectionSelectionState::None;
		SplinePropertyPath = FComponentPropertyPath();
	}

	ResetCachedData();
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedSpline(
	FComponentPropertyPath& InSplinePropertyPath)
{
	ResetSelection(false);
	if (!InSplinePropertyPath.IsValid())
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Component;
	SplinePropertyPath = InSplinePropertyPath;
	SelectedSectionIndex = INDEX_NONE;
	SelectedLaneIndex = LANE_INDEX_NONE;
	MultiSelectedLanes.Reset();
	SelectedKeyIndex = INDEX_NONE;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::AssignSelectedSplinePath(
	const FComponentPropertyPath& InSplinePropertyPath)
{
	if (!InSplinePropertyPath.IsValid())
	{
		return;
	}

	SplinePropertyPath = InSplinePropertyPath;
	if (uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Component))
	{
		State = EMetaLandRoadSectionSelectionState::Component;
	}
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedSection(int32 InSelectedSectionIndex)
{
	if (InSelectedSectionIndex < 0 || !SplinePropertyPath.IsValid()
		|| uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Component))
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Section;
	SelectedSectionIndex = InSelectedSectionIndex;
	SelectedLaneIndex = LANE_INDEX_NONE;
	MultiSelectedLanes.Reset();
	SelectedKeyIndex = INDEX_NONE;
	bSectionBoundarySelected = false;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ClearLaneMultiSelection()
{
	MultiSelectedLanes.Reset();
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetPrimaryLaneSelection(
	int32 SectionIndex,
	int32 LaneIndex,
	bool bAddToMultiSelection)
{
	if (!SplinePropertyPath.IsValid() || SectionIndex == INDEX_NONE
		|| uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Section))
	{
		return;
	}

	UMetaLandRoadSplineComponent* Component = GetSelectedSpline();
	if (!IsValid(Component) || !Component->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return;
	}

	if (LaneIndex != LANE_INDEX_NONE
		&& !MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(Component, SectionIndex, LaneIndex))
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Lane;
	SelectedSectionIndex = SectionIndex;
	SelectedLaneIndex = LaneIndex;
	SelectedKeyIndex = INDEX_NONE;
	bLaneWidthMenuContext = false;

	if (!bAddToMultiSelection)
	{
		MultiSelectedLanes.Reset();
	}

	if (LaneIndex != LANE_INDEX_NONE)
	{
		const FMetaLandRoadSelectedLaneRefU Ref{ SectionIndex, LaneIndex };
		MultiSelectedLanes.RemoveAll([&Ref](const FMetaLandRoadSelectedLaneRefU& Entry)
		{
			return Entry.SectionIndex == Ref.SectionIndex && Entry.LaneIndex == Ref.LaneIndex;
		});
		MultiSelectedLanes.Add(Ref);
	}
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ToggleLaneInMultiSelection(
	int32 SectionIndex,
	int32 LaneIndex)
{
	if (!SplinePropertyPath.IsValid() || SectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
	{
		return;
	}

	UMetaLandRoadSplineComponent* Component = GetSelectedSpline();
	if (!IsValid(Component) || !Component->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return;
	}

	if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(Component, SectionIndex, LaneIndex))
	{
		return;
	}

	const int32 PrevSectionIndex = SelectedSectionIndex;
	const int32 PrevLaneIndex = SelectedLaneIndex;

	SelectedKeyIndex = INDEX_NONE;

	const int32 ExistingIndex = MultiSelectedLanes.IndexOfByPredicate(
		[SectionIndex, LaneIndex](const FMetaLandRoadSelectedLaneRefU& Entry)
		{
			return Entry.SectionIndex == SectionIndex && Entry.LaneIndex == LaneIndex;
		});

	if (ExistingIndex != INDEX_NONE)
	{
		MultiSelectedLanes.RemoveAt(ExistingIndex);
		if (MultiSelectedLanes.Num() > 0)
		{
			const FMetaLandRoadSelectedLaneRefU& Last = MultiSelectedLanes.Last();
			SelectedSectionIndex = Last.SectionIndex;
			SelectedLaneIndex = Last.LaneIndex;
			State = EMetaLandRoadSectionSelectionState::Lane;
		}
		else
		{
			SelectedLaneIndex = LANE_INDEX_NONE;
			State = EMetaLandRoadSectionSelectionState::Section;
		}
		return;
	}

	if (MultiSelectedLanes.Num() == 0
		&& PrevLaneIndex != LANE_INDEX_NONE
		&& !(PrevSectionIndex == SectionIndex && PrevLaneIndex == LaneIndex))
	{
		MultiSelectedLanes.Add({ PrevSectionIndex, PrevLaneIndex });
	}

	MultiSelectedLanes.Add({ SectionIndex, LaneIndex });
	SelectedSectionIndex = SectionIndex;
	SelectedLaneIndex = LaneIndex;
	State = EMetaLandRoadSectionSelectionState::Lane;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsLaneInMultiSelection(
	int32 SectionIndex,
	int32 LaneIndex) const
{
	for (const FMetaLandRoadSelectedLaneRefU& Entry : MultiSelectedLanes)
	{
		if (Entry.SectionIndex == SectionIndex && Entry.LaneIndex == LaneIndex)
		{
			return true;
		}
	}
	return SelectedSectionIndex == SectionIndex && SelectedLaneIndex == LaneIndex;
}

const TArray<FMetaLandRoadSelectedLaneRefU>& UMetaLandRoadSectionComponentVisualizerSelectionState::GetMultiSelectedLanes() const
{
	return MultiSelectedLanes;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedLane(int32 InSelectedLaneIndex)
{
	SetPrimaryLaneSelection(SelectedSectionIndex, InSelectedLaneIndex, false);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedKeyIndex(int32 KeyIndex)
{
	if (!SplinePropertyPath.IsValid() || SelectedSectionIndex == INDEX_NONE
		|| uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Section))
	{
		return;
	}

	if (KeyIndex != INDEX_NONE)
	{
		UMetaLandRoadSplineComponent* Component = GetSelectedSpline();
		if (!IsValid(Component) || SelectedLaneIndex == LANE_INDEX_NONE)
		{
			return;
		}

		const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			Component,
			SelectedSectionIndex,
			SelectedLaneIndex);
		if (LaneSectionIndex == INDEX_NONE)
		{
			return;
		}

		const FMetaLandRoadLane& Lane = Component->GetLaneSection(LaneSectionIndex).GetLaneByIndex(SelectedLaneIndex);
		if (!Lane.Width.Keys.IsValidIndex(KeyIndex))
		{
			return;
		}

		State = EMetaLandRoadSectionSelectionState::Key;
	}
	else
	{
		State = SelectedLaneIndex != LANE_INDEX_NONE
			? EMetaLandRoadSectionSelectionState::Lane
			: EMetaLandRoadSectionSelectionState::Section;
	}

	SelectedKeyIndex = KeyIndex;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetWidthKeySelection(
	int32 SectionIndex,
	int32 LaneIndex,
	int32 KeyIndex)
{
	if (!SplinePropertyPath.IsValid() || SectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
	{
		return;
	}

	UMetaLandRoadSplineComponent* Component = GetSelectedSpline();
	if (!IsValid(Component) || !Component->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		Component, SectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE
		|| !Component->GetLaneSection(LaneSectionIndex).CheckLaneIndex(LaneIndex))
	{
		return;
	}

	const FMetaLandRoadLane& Lane = Component->GetLaneSection(LaneSectionIndex).GetLaneByIndex(LaneIndex);
	ClearLaneMultiSelection();
	SelectedSectionIndex = SectionIndex;
	SelectedLaneIndex = LaneIndex;

	if (KeyIndex != INDEX_NONE && Lane.Width.Keys.IsValidIndex(KeyIndex))
	{
		SelectedKeyIndex = KeyIndex;
		State = EMetaLandRoadSectionSelectionState::Key;
		bLaneWidthMenuContext = true;
	}
	else
	{
		SelectedKeyIndex = INDEX_NONE;
		State = EMetaLandRoadSectionSelectionState::Lane;
		bLaneWidthMenuContext = true;
	}
	bLaneFacePrimSelected = false;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ClearWidthKeySelection()
{
	SelectedKeyIndex = INDEX_NONE;
	if (SelectedLaneIndex != LANE_INDEX_NONE)
	{
		State = EMetaLandRoadSectionSelectionState::Lane;
	}
	else if (SelectedSectionIndex != INDEX_NONE)
	{
		State = EMetaLandRoadSectionSelectionState::Section;
	}
}

EMetaLandRoadSectionSelectionState UMetaLandRoadSectionComponentVisualizerSelectionState::GetStateVerified() const
{
	UMetaLandRoadSplineComponent* Component = GetSelectedSpline();

	if (State > EMetaLandRoadSectionSelectionState::None)
	{
		if (!IsValid(Component))
		{
			return EMetaLandRoadSectionSelectionState::None;
		}

		if (State > EMetaLandRoadSectionSelectionState::Component)
		{
			if (SelectedSectionIndex < 0 || SelectedSectionIndex >= Component->GetLaneSectionsNum())
			{
				return EMetaLandRoadSectionSelectionState::Component;
			}

			if (State == EMetaLandRoadSectionSelectionState::Section && SelectedLaneIndex != LANE_INDEX_NONE)
			{
				return EMetaLandRoadSectionSelectionState::Component;
			}

			if (State >= EMetaLandRoadSectionSelectionState::Lane)
			{
				const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
					Component,
					SelectedSectionIndex,
					SelectedLaneIndex);
				if (SelectedLaneIndex != LANE_INDEX_NONE
					&& (LaneSectionIndex == INDEX_NONE
						|| !Component->GetLaneSection(LaneSectionIndex).CheckLaneIndex(SelectedLaneIndex)))
				{
					return EMetaLandRoadSectionSelectionState::Component;
				}

				if (State >= EMetaLandRoadSectionSelectionState::Key)
				{
					if (SelectedKeyIndex == INDEX_NONE)
					{
						return SelectedLaneIndex != LANE_INDEX_NONE
							? EMetaLandRoadSectionSelectionState::Lane
							: EMetaLandRoadSectionSelectionState::Section;
					}

					if (LaneSectionIndex == INDEX_NONE)
					{
						return EMetaLandRoadSectionSelectionState::Lane;
					}

					const FMetaLandRoadLane& Lane = Component->GetLaneSection(LaneSectionIndex).GetLaneByIndex(SelectedLaneIndex);
					if (!Lane.Width.Keys.IsValidIndex(SelectedKeyIndex))
					{
						return EMetaLandRoadSectionSelectionState::Lane;
					}
				}
			}
		}
	}

	return State;
}

const FComponentPropertyPath UMetaLandRoadSectionComponentVisualizerSelectionState::GetSplinePropertyPath() const
{
	return SplinePropertyPath;
}

UMetaLandRoadSplineComponent* UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedSpline() const
{
	return SplinePropertyPath.IsValid()
		? Cast<UMetaLandRoadSplineComponent>(SplinePropertyPath.GetComponent())
		: nullptr;
}

int32 UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedSectionIndex() const
{
	return SelectedSectionIndex;
}

int32 UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedLaneIndex() const
{
	return SelectedLaneIndex;
}

int32 UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedKeyIndex() const
{
	return SelectedKeyIndex;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSectionBoundarySelected() const
{
	return bSectionBoundarySelected;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSectionBoundarySelected(bool bInSelected)
{
	bSectionBoundarySelected = bInSelected;
}

FVector UMetaLandRoadSectionComponentVisualizerSelectionState::GetCachedPosition() const
{
	return CachedPosition;
}

FQuat UMetaLandRoadSectionComponentVisualizerSelectionState::GetCachedRotation() const
{
	return CachedRotation;
}

float UMetaLandRoadSectionComponentVisualizerSelectionState::GetCachedSplineKey() const
{
	return CachedSplineKey;
}

float UMetaLandRoadSectionComponentVisualizerSelectionState::GetLastPickSplineKey() const
{
	return LastPickSplineKey;
}

EMetaLandRoadSectionSelectionState UMetaLandRoadSectionComponentVisualizerSelectionState::GetState() const
{
	return State;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSelected(
	const UMetaLandRoadSplineComponent* Spline,
	int SectionIndex) const
{
	return Spline == GetSelectedSpline() && SectionIndex == SelectedSectionIndex;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSelected(
	const UMetaLandRoadSplineComponent* Spline,
	int SectionIndex,
	int LaneIndex) const
{
	if (Spline != GetSelectedSpline() || LaneIndex == LANE_INDEX_NONE)
	{
		return false;
	}

	for (const FMetaLandRoadSelectedLaneRefU& Entry : MultiSelectedLanes)
	{
		if (Entry.SectionIndex == SectionIndex && Entry.LaneIndex == LaneIndex)
		{
			return true;
		}
	}

	return SectionIndex == SelectedSectionIndex && LaneIndex == SelectedLaneIndex;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSelected(
	const UMetaLandRoadSplineComponent* Spline,
	int SectionIndex,
	int LaneIndex,
	int WidthIndex) const
{
	return Spline == GetSelectedSpline()
		&& SectionIndex == SelectedSectionIndex
		&& LaneIndex == SelectedLaneIndex
		&& WidthIndex == SelectedKeyIndex;
}
