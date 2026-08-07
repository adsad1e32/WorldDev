#include "MetaLandRoadProfile.h"
#include "MetaLandRoadEditorMode.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplineActor.h"
#include "Misc/ScopeExit.h"

namespace MetaLandRoadProfile
{
	/** Default longitudinal lane-section count (each covers Both sides). */
	static constexpr int32 DefaultLongitudinalSectionCount = 2;

	static FMetaLandRoadLane CreateLane(const FMetaLandRoadLaneProfile& Src)
	{
		FMetaLandRoadLane NewLane;
		// MetaRoad FRoadLane: constant width via default value only. A single cubic key at S=0
		// extrapolates toward zero over long distances and collapses lane boundaries at road ends.
		NewLane.Width.SetDefaultValue(static_cast<float>(Src.Width));
		NewLane.Direction = Src.Direction;

		FMetaLandRoadLaneDriving DrivingLane;
		DrivingLane.DriveableLaneType = Src.DriveableLaneType;
		NewLane.LaneInstance.InitializeAs<FMetaLandRoadLaneDriving>(DrivingLane);
		return NewLane;
	}

	static FMetaLandRoadLaneSection BuildLaneSection(const FMetaLandRoadLaneSectionProfile& RoadProfile)
	{
		FMetaLandRoadLaneSection NewSection;
		NewSection.Side = EMetaLandRoadLaneSectionSide::Both;

		for (const FMetaLandRoadLaneProfile& LaneProfile : RoadProfile.Left)
		{
			NewSection.Left.Add(CreateLane(LaneProfile));
		}
		for (const FMetaLandRoadLaneProfile& LaneProfile : RoadProfile.Right)
		{
			NewSection.Right.Add(CreateLane(LaneProfile));
		}

		return NewSection;
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

	static void CenterRoadLayoutOnSplineForLaneIndices(
		UMetaLandRoadSplineComponent* TargetSpline,
		const TArray<int32>& LaneIndices)
	{
		if (!TargetSpline || LaneIndices.Num() == 0 || TargetSpline->GetLaneSectionsNum() == 0)
		{
			return;
		}

		bool bHasLeft = false;
		bool bHasRight = false;
		int32 LeftOuterLane = LANE_INDEX_NONE;
		int32 RightOuterLane = LANE_INDEX_NONE;
		for (const int32 LaneIndex : LaneIndices)
		{
			if (LaneIndex > 0)
			{
				bHasRight = true;
				RightOuterLane = (RightOuterLane == LANE_INDEX_NONE)
					? LaneIndex
					: FMath::Max(RightOuterLane, LaneIndex);
			}
			else if (LaneIndex < 0)
			{
				bHasLeft = true;
				LeftOuterLane = (LeftOuterLane == LANE_INDEX_NONE)
					? LaneIndex
					: FMath::Min(LeftOuterLane, LaneIndex);
			}
		}

		const FMetaLandRoadLaneSection& Section = TargetSpline->GetLaneSection(0);
		double BundleCenterR = 0.0;
		if (bHasLeft && bHasRight
			&& LeftOuterLane != LANE_INDEX_NONE
			&& RightOuterLane != LANE_INDEX_NONE)
		{
			// Dual L|R: center between both outer edges (α=1). Do NOT use
			// ResolveOutermost across signs — Min(+3,-1) collapses to Left-only.
			const double LeftOuterR = Section.EvalLaneROffset(LeftOuterLane, 0.0, 1.0);
			const double RightOuterR = Section.EvalLaneROffset(RightOuterLane, 0.0, 1.0);
			BundleCenterR = (LeftOuterR + RightOuterR) * 0.5;
		}
		else
		{
			const int32 InnermostLane = ResolveInnermostLaneIndex(LaneIndices);
			const int32 OutermostLane = ResolveOutermostLaneIndex(LaneIndices);
			if (InnermostLane == LANE_INDEX_NONE || OutermostLane == LANE_INDEX_NONE)
			{
				return;
			}
			const double InnerR = Section.EvalLaneROffset(InnermostLane, 0.0, 0.0);
			const double OuterR = Section.EvalLaneROffset(OutermostLane, 0.0, 1.0);
			BundleCenterR = (InnerR + OuterR) * 0.5;
		}

		FMetaLandRoadLayout& Layout = TargetSpline->GetRoadLayoutMutable();
		Layout.ROffset.Reset();
		Layout.ROffset.AddKey(0.0f, static_cast<float>(-BundleCenterR));
		TargetSpline->UpdateLaneSectionBounds();
	}

	void ApplyDefaultLongitudinalSectionSplit(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline || !TargetSpline->bAutoMaintainMidpointSectionSplit || TargetSpline->GetLaneSectionsNum() != DefaultLongitudinalSectionCount)
		{
			return;
		}

		const double SplineLength = TargetSpline->GetSplineLength();
		if (SplineLength <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		Sections[0].SOffset = 0.0;
		Sections[1].SOffset = SplineLength * 0.5;
	}

	FMetaLandRoadLaneSectionProfile GetDefaultRoadProfile()
	{
		FMetaLandRoadLaneSectionProfile Profile;

		FMetaLandRoadLaneProfile DrivingLane;
		DrivingLane.Width = MetaLandRoad::DefaultRoadLaneWidth;
		DrivingLane.DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;

		Profile.Right.Add(DrivingLane);
		Profile.Right.Add(DrivingLane);
		Profile.Left.Add(DrivingLane);
		Profile.Left.Add(DrivingLane);
		return Profile;
	}

	void SetRoadProfile(UMetaLandRoadSplineComponent* TargetSpline, const FMetaLandRoadLaneSectionProfile& RoadProfile)
	{
		if (!TargetSpline)
		{
			return;
		}

		TargetSpline->Modify();
		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		Sections.Empty();

		FMetaLandRoadLaneSection BaseSection = BuildLaneSection(RoadProfile);
		BaseSection.SOffset = 0.0;
		Sections.Add(BaseSection);

		TargetSpline->UpdateRoadLayout();
	}

	void EnsureDefaultRoadProfile(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}

		if (TargetSpline->GetLaneSectionsNum() == 0)
		{
			SetRoadProfile(TargetSpline, GetDefaultRoadProfile());
			return;
		}

		FMetaLandRoadLaneSection& Section = TargetSpline->GetLaneSection(0);
		if (Section.Left.IsEmpty() && Section.Right.IsEmpty())
		{
			SetRoadProfile(TargetSpline, GetDefaultRoadProfile());
		}
	}

	void SyncRoadLayout(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}

		if (TargetSpline->GetLaneSectionsNum() == 0)
		{
			EnsureDefaultRoadProfile(TargetSpline);
			return;
		}

		TargetSpline->GetRoadLayoutMutable().UpdateLayout(TargetSpline);
		TargetSpline->UpdateLaneSectionBounds();
	}

	static bool TryMakeLaneProfileAtS(
		const UMetaLandRoadSplineComponent* SourceSpline,
		int32 LaneSectionIndex,
		int32 LaneIndex,
		float WidthS,
		FMetaLandRoadLaneProfile& OutLaneProfile)
	{
		OutLaneProfile = FMetaLandRoadLaneProfile();
		if (!SourceSpline || !SourceSpline->GetLaneSections().IsValidIndex(LaneSectionIndex))
		{
			return false;
		}
		const FMetaLandRoadLaneSection& LaneSection = SourceSpline->GetLaneSection(LaneSectionIndex);
		if (!LaneSection.CheckLaneIndex(LaneIndex))
		{
			return false;
		}

		const FMetaLandRoadLane& SourceLane = LaneSection.GetLaneByIndex(LaneIndex);
		OutLaneProfile.Width = SourceLane.Width.Eval(WidthS);
		if (OutLaneProfile.Width <= KINDA_SMALL_NUMBER)
		{
			OutLaneProfile.Width = MetaLandRoad::DefaultRoadLaneWidth;
		}
		OutLaneProfile.Direction = SourceLane.Direction;
		if (SourceLane.LaneInstance.IsValid())
		{
			if (const FMetaLandRoadLaneDriving* Driving =
				SourceLane.LaneInstance.GetPtr<FMetaLandRoadLaneDriving>())
			{
				OutLaneProfile.DriveableLaneType = Driving->DriveableLaneType;
			}
		}
		return true;
	}

	static void RebuildProfileLaneIndices(
		const FMetaLandRoadLaneSectionProfile& Profile,
		TArray<int32>& OutNewLaneIndices)
	{
		OutNewLaneIndices.Reset();
		OutNewLaneIndices.Reserve(Profile.Right.Num() + Profile.Left.Num());
		for (int32 Index = 0; Index < Profile.Right.Num(); ++Index)
		{
			OutNewLaneIndices.Add(Index + 1);
		}
		for (int32 Index = 0; Index < Profile.Left.Num(); ++Index)
		{
			OutNewLaneIndices.Add(-(Index + 1));
		}
	}

	static bool BuildProfileFromDrawAnchor(
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor,
		FMetaLandRoadLaneSectionProfile& OutProfile,
		TArray<int32>& OutNewLaneIndices)
	{
		OutProfile = FMetaLandRoadLaneSectionProfile();
		OutNewLaneIndices.Reset();
		if (!SourceSpline || !Anchor.IsValid() || Anchor.SourceSpline.Get() != SourceSpline)
		{
			return false;
		}

		const float WidthS = static_cast<float>(Anchor.AnchorS);

		TArray<int32> LaneIndices;
		LaneIndices.Add(Anchor.LaneIndex);
		for (int32 BundledLaneIndex : Anchor.BundledLaneIndices)
		{
			LaneIndices.AddUnique(BundledLaneIndex);
		}
		LaneIndices.Sort([](int32 A, int32 B)
		{
			return FMath::Abs(A) < FMath::Abs(B);
		});

		// Exact selected subset only (Shift bundle). Resolve EACH lane to its own face
		// section — do NOT force-fill the opposite face when user picked 3-of-4 / one side.
		for (int32 LaneIndex : LaneIndices)
		{
			const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
				SourceSpline, Anchor.SectionIndex, LaneIndex);
			FMetaLandRoadLaneProfile LaneProfile;
			if (!TryMakeLaneProfileAtS(SourceSpline, LaneSectionIndex, LaneIndex, WidthS, LaneProfile))
			{
				continue;
			}

			if (LaneIndex > 0)
			{
				OutProfile.Right.Add(LaneProfile);
			}
			else if (LaneIndex < 0)
			{
				OutProfile.Left.Add(LaneProfile);
			}
		}

		if (OutProfile.Left.Num() == 0 && OutProfile.Right.Num() == 0)
		{
			return false;
		}

		RebuildProfileLaneIndices(OutProfile, OutNewLaneIndices);
		return OutNewLaneIndices.Num() > 0;
	}

	static void MatchFaceLaneCountsToProfile(
		TArray<FMetaLandRoadLane>& FaceLanes,
		const TArray<FMetaLandRoadLaneProfile>& FaceProfile)
	{
		while (FaceLanes.Num() < FaceProfile.Num())
		{
			FaceLanes.Add(CreateLane(FaceProfile[FaceLanes.Num()]));
		}
		while (FaceLanes.Num() > FaceProfile.Num() && FaceLanes.Num() > 0)
		{
			FaceLanes.RemoveAt(FaceLanes.Num() - 1);
		}
		for (int32 Index = 0; Index < FaceLanes.Num() && Index < FaceProfile.Num(); ++Index)
		{
			const float Width = static_cast<float>(FaceProfile[Index].Width);
			FaceLanes[Index].Width.Reset();
			FaceLanes[Index].Width.AddKey(0.0f, Width);
			MetaLandRoad::EnsureLaneWidthCurveCubic(FaceLanes[Index].Width);
			FaceLanes[Index].Direction = FaceProfile[Index].Direction;
		}
	}

	/** Only touch faces this section owns — never put Right lanes on a Left-only section. */
	static void MatchSectionLaneCountsToProfile(
		FMetaLandRoadLaneSection& Section,
		const FMetaLandRoadLaneSectionProfile& Profile)
	{
		if (MetaLandRoad::HasRightSide(Section.Side))
		{
			MatchFaceLaneCountsToProfile(Section.Right, Profile.Right);
		}
		if (MetaLandRoad::HasLeftSide(Section.Side))
		{
			MatchFaceLaneCountsToProfile(Section.Left, Profile.Left);
		}
	}

	void ApplyRoadProfileFromDrawAnchor(
		UMetaLandRoadSplineComponent* TargetSpline,
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor)
	{
		if (!TargetSpline)
		{
			return;
		}

		FMetaLandRoadLaneSectionProfile Profile;
		TArray<int32> NewLaneIndices;
		if (!BuildProfileFromDrawAnchor(SourceSpline, Anchor, Profile, NewLaneIndices))
		{
			EnsureDefaultRoadProfile(TargetSpline);
			return;
		}

		SetRoadProfile(TargetSpline, Profile);
		CenterRoadLayoutOnSplineForLaneIndices(TargetSpline, NewLaneIndices);
	}

	void EnsureLaneCountFromDrawAnchor(
		UMetaLandRoadSplineComponent* TargetSpline,
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor)
	{
		if (!TargetSpline)
		{
			return;
		}

		FMetaLandRoadLaneSectionProfile Profile;
		TArray<int32> NewLaneIndices;
		if (!BuildProfileFromDrawAnchor(SourceSpline, Anchor, Profile, NewLaneIndices))
		{
			EnsureDefaultRoadProfile(TargetSpline);
			return;
		}

		if (TargetSpline->GetLaneSectionsNum() == 0)
		{
			SetRoadProfile(TargetSpline, Profile);
			CenterRoadLayoutOnSplineForLaneIndices(TargetSpline, NewLaneIndices);
			return;
		}

		TargetSpline->Modify();
		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		int32 BothHost = INDEX_NONE;
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
			// Side-only markers with empty arrays inherit lane data — skip them.
			if (Section.Side != EMetaLandRoadLaneSectionSide::Both
				&& Section.Left.Num() == 0
				&& Section.Right.Num() == 0)
			{
				continue;
			}
			if (Section.Side == EMetaLandRoadLaneSectionSide::Both && BothHost == INDEX_NONE)
			{
				BothHost = SectionIndex;
			}
			MatchSectionLaneCountsToProfile(Section, Profile);
		}

		// 1->2 mix: Profile needs both faces. Promote a host to Both and fill the missing face
		// so Right is not left as a zero-width pink edge on a Left-only layout.
		if (Profile.Left.Num() > 0 && Profile.Right.Num() > 0)
		{
			if (BothHost == INDEX_NONE)
			{
				for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
				{
					if (Sections[SectionIndex].Left.Num() > 0 || Sections[SectionIndex].Right.Num() > 0)
					{
						BothHost = SectionIndex;
						break;
					}
				}
			}
			if (Sections.IsValidIndex(BothHost))
			{
				Sections[BothHost].Side = EMetaLandRoadLaneSectionSide::Both;
				MatchFaceLaneCountsToProfile(Sections[BothHost].Left, Profile.Left);
				MatchFaceLaneCountsToProfile(Sections[BothHost].Right, Profile.Right);
			}
		}

		CenterRoadLayoutOnSplineForLaneIndices(TargetSpline, NewLaneIndices);
		TargetSpline->UpdateRoadLayout();
	}

	bool GetDrawAnchorBundleLaneWidths(
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor,
		TArray<int32>& OutLaneIndices,
		TArray<float>& OutWidths)
	{
		OutLaneIndices.Reset();
		OutWidths.Reset();

		FMetaLandRoadLaneSectionProfile Profile;
		TArray<int32> NewLaneIndices;
		if (!BuildProfileFromDrawAnchor(SourceSpline, Anchor, Profile, NewLaneIndices))
		{
			return false;
		}

		OutLaneIndices = MoveTemp(NewLaneIndices);
		OutWidths.Reserve(OutLaneIndices.Num());
		for (int32 LaneIndex : OutLaneIndices)
		{
			if (LaneIndex > 0)
			{
				const int32 ProfileIndex = LaneIndex - 1;
				if (!Profile.Right.IsValidIndex(ProfileIndex))
				{
					return false;
				}
				OutWidths.Add(static_cast<float>(Profile.Right[ProfileIndex].Width));
			}
			else if (LaneIndex < 0)
			{
				const int32 ProfileIndex = -LaneIndex - 1;
				if (!Profile.Left.IsValidIndex(ProfileIndex))
				{
					return false;
				}
				OutWidths.Add(static_cast<float>(Profile.Left[ProfileIndex].Width));
			}
			else
			{
				return false;
			}
		}
		return OutLaneIndices.Num() > 0;
	}

	int32 ResolveLaneMutationSectionIndex(
		const UMetaLandRoadSplineComponent* TargetSpline,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		bool bOnLeft)
	{
		if (!TargetSpline || !TargetSpline->GetLaneSections().IsValidIndex(SpanSectionIndex))
		{
			return INDEX_NONE;
		}

		const FMetaLandRoadLayout& Layout = TargetSpline->GetRoadLayout();
		const EMetaLandRoadLaneSectionSide TargetSide = bOnLeft
			? EMetaLandRoadLaneSectionSide::Left
			: EMetaLandRoadLaneSectionSide::Right;

		auto SectionCanMutateSide = [bOnLeft](const FMetaLandRoadLaneSection& Section) -> bool
		{
			if (Section.Side == EMetaLandRoadLaneSectionSide::Both)
			{
				return true;
			}
			return bOnLeft
				? Section.Side != EMetaLandRoadLaneSectionSide::Right
				: Section.Side != EMetaLandRoadLaneSectionSide::Left;
		};

		if (LaneIndex != LANE_INDEX_NONE)
		{
			const int32 Resolved = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
				TargetSpline, SpanSectionIndex, LaneIndex);
			if (Resolved != INDEX_NONE
				&& Layout.Sections.IsValidIndex(Resolved)
				&& SectionCanMutateSide(Layout.Sections[Resolved])
				&& Layout.Sections[Resolved].CheckLaneIndex(LaneIndex))
			{
				return Resolved;
			}
		}

		for (int32 SectionIndex = FMath::Min(SpanSectionIndex, Layout.Sections.Num() - 1);
			SectionIndex >= 0;
			--SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = Layout.Sections[SectionIndex];
			if (Section.Side == EMetaLandRoadLaneSectionSide::Both
				&& (Section.Left.Num() > 0 || Section.Right.Num() > 0))
			{
				return SectionIndex;
			}
		}

		// Left-only bundle → Add Right: target the section that actually owns Left lanes.
		if (!bOnLeft)
		{
			for (int32 SectionIndex = FMath::Min(SpanSectionIndex, Layout.Sections.Num() - 1);
				SectionIndex >= 0;
				--SectionIndex)
			{
				if (Layout.Sections[SectionIndex].Left.Num() > 0)
				{
					return SectionIndex;
				}
			}
		}
		else
		{
			for (int32 SectionIndex = FMath::Min(SpanSectionIndex, Layout.Sections.Num() - 1);
				SectionIndex >= 0;
				--SectionIndex)
			{
				if (Layout.Sections[SectionIndex].Right.Num() > 0)
				{
					return SectionIndex;
				}
			}
		}

		const int32 SideSection = Layout.FindSideSection(SpanSectionIndex, TargetSide);
		if (Layout.Sections.IsValidIndex(SideSection) && SectionCanMutateSide(Layout.Sections[SideSection]))
		{
			return SideSection;
		}

		return SpanSectionIndex;
	}

	void ShiftROffsetForOuterLaneAdd(
		UMetaLandRoadSplineComponent* TargetSpline,
		bool bAddedOnLeft,
		double LaneWidthCm)
	{
		if (!TargetSpline || LaneWidthCm <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		// Junction hub anchor is fixed in world — shifting ROffset slides the cut mouth off the cover.
		if (const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()))
		{
			if (RoadActor->StartOfJunction.IsValid()
				|| RoadActor->EndOfJunction.IsValid()
				|| RoadActor->JunctionTrimZones.Num() > 0)
			{
				return;
			}
		}

		const double HalfWidth = LaneWidthCm * 0.5;
		const float Delta = static_cast<float>(bAddedOnLeft ? HalfWidth : -HalfWidth);

		FMetaLandRoadLayout& Layout = TargetSpline->GetRoadLayoutMutable();
		if (Layout.ROffset.GetNumKeys() == 0)
		{
			Layout.ROffset.AddKey(0.0f, Delta);
		}
		else
		{
			for (FRichCurveKey& Key : Layout.ROffset.Keys)
			{
				Key.Value += Delta;
			}
		}
	}

	static void RefreshRoadActorMesh(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return;
		}
		Spline->UpdateRoadLayout();
		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			RoadActor->RefreshDynamicRoadMesh();
		}
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(Spline);
	}

	void RecookLocalJunctionsForSpline(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}

		AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner());
		if (!RoadActor || !RoadActor->GetWorld() || GIsEditorLoadingPackage)
		{
			return;
		}

		TSet<AMoKuEditIntersectionActor*> LocalJunctions;
		if (AMoKuEditIntersectionActor* StartJunction = RoadActor->StartOfJunction.Get())
		{
			LocalJunctions.Add(StartJunction);
		}
		if (AMoKuEditIntersectionActor* EndJunction = RoadActor->EndOfJunction.Get())
		{
			LocalJunctions.Add(EndJunction);
		}
		for (const FMetaLandRoadJunctionTrimZone& TrimZone : RoadActor->JunctionTrimZones)
		{
			if (AMoKuEditIntersectionActor* Junction = TrimZone.Junction.Get())
			{
				LocalJunctions.Add(Junction);
			}
		}

		if (LocalJunctions.Num() == 0)
		{
			return;
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[LocalJunction] recook %d hub(s) for road=%s (lane layout changed)"),
			LocalJunctions.Num(),
			*RoadActor->GetName());

		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		for (AMoKuEditIntersectionActor* Junction : LocalJunctions)
		{
			UMetaLandRoadEditorMode::RecookJunctionActorStatic(Junction, true);
		}
	}

	void RefreshConnectedRoadMeshes(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}

		RefreshRoadActorMesh(TargetSpline);

		auto RefreshViaConnection = [](UMetaLandRoadConnection* Connection)
		{
			if (!Connection || !Connection->IsConnected())
			{
				return;
			}
			if (UMetaLandRoadLaneConnection* LaneConn = Connection->OuterLaneConnection.Get())
			{
				if (UMetaLandRoadSplineComponent* OtherSpline = LaneConn->OwnedRoadSpline.Get())
				{
					RefreshRoadActorMesh(OtherSpline);
				}
			}
		};

		RefreshViaConnection(TargetSpline->GetPredecessorConnection());
		RefreshViaConnection(TargetSpline->GetSuccessorConnection());
	}

	static void EnsureSectionSideForLaneAdd(FMetaLandRoadLaneSection& Section, bool bOnLeft)
	{
		if (bOnLeft && Section.Side == EMetaLandRoadLaneSectionSide::Right)
		{
			Section.Side = EMetaLandRoadLaneSectionSide::Both;
		}
		else if (!bOnLeft && Section.Side == EMetaLandRoadLaneSectionSide::Left)
		{
			Section.Side = EMetaLandRoadLaneSectionSide::Both;
		}
	}

	static FMetaLandRoadLane MakeNewLaneFromSelection(
		const FMetaLandRoadLaneSection& Section,
		int32 LaneIndex)
	{
		FMetaLandRoadLane NewLane;
		if (LaneIndex == LANE_INDEX_NONE || !Section.CheckLaneIndex(LaneIndex))
		{
			FMetaLandRoadLaneDriving DrivingLane;
			DrivingLane.DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;
			NewLane.LaneInstance.InitializeAs<FMetaLandRoadLaneDriving>(DrivingLane);
			NewLane.Width.SetDefaultValue(static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth));
			return NewLane;
		}

		const FMetaLandRoadLane& SelectedLane = Section.GetLaneByIndex(LaneIndex);
		NewLane.LaneInstance = SelectedLane.LaneInstance;
		NewLane.Direction = SelectedLane.Direction;
		float LaneWidth = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);
		if (SelectedLane.Width.GetNumKeys() > 0)
		{
			LaneWidth = SelectedLane.Width.Keys[0].Value;
		}
		else
		{
			LaneWidth = static_cast<float>(SelectedLane.Width.GetDefaultValue());
		}
		NewLane.Width.SetDefaultValue(
			LaneWidth > UE_KINDA_SMALL_NUMBER
			? LaneWidth
			: static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth));
		return NewLane;
	}

	static int32 InsertLaneIntoSection(
		FMetaLandRoadLaneSection& Section,
		int32 LaneIndex,
		bool bOnLeft,
		const FMetaLandRoadLane& NewLane)
	{
		EnsureSectionSideForLaneAdd(Section, bOnLeft);

		if (LaneIndex == LANE_INDEX_NONE)
		{
			if (bOnLeft)
			{
				Section.Left.Insert(NewLane, 0);
				return Section.Left.Num() > 0 ? -Section.Left.Num() : -1;
			}
			Section.Right.Insert(NewLane, 0);
			return Section.Right.Num();
		}

		if (LaneIndex > 0)
		{
			Section.Right.Insert(NewLane, LaneIndex - 1 + (bOnLeft ? 0 : 1));
			return LaneIndex + (bOnLeft ? 0 : 1);
		}

		Section.Left.Insert(NewLane, -LaneIndex - 1 + (bOnLeft ? 1 : 0));
		return LaneIndex - (bOnLeft ? 1 : 0);
	}

	static bool SectionShouldReceiveLaneAdd(const FMetaLandRoadLaneSection& Section)
	{
		return Section.Left.Num() > 0
			|| Section.Right.Num() > 0
			|| Section.Side == EMetaLandRoadLaneSectionSide::Both;
	}

	FMetaLandRoadAddLaneResult AddLaneToRoadLayout(
		UMetaLandRoadSplineComponent* TargetSpline,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		bool bOnLeft)
	{
		FMetaLandRoadAddLaneResult Result;
		if (!TargetSpline || !TargetSpline->GetLaneSections().IsValidIndex(SpanSectionIndex))
		{
			return Result;
		}

		const int32 ReferenceSectionIndex = ResolveLaneMutationSectionIndex(
			TargetSpline, SpanSectionIndex, LaneIndex, bOnLeft);
		if (!TargetSpline->GetLaneSections().IsValidIndex(ReferenceSectionIndex))
		{
			return Result;
		}

		const FMetaLandRoadLaneSection& ReferenceSection = TargetSpline->GetLaneSection(ReferenceSectionIndex);
		int32 ReferenceLaneIndex = LaneIndex;
		if (ReferenceLaneIndex != LANE_INDEX_NONE && !ReferenceSection.CheckLaneIndex(ReferenceLaneIndex))
		{
			ReferenceLaneIndex = LANE_INDEX_NONE;
		}

		const FMetaLandRoadLane NewLaneTemplate = MakeNewLaneFromSelection(ReferenceSection, ReferenceLaneIndex);
		const double NewLaneWidthCm = NewLaneTemplate.Width.GetDefaultValue();

		const bool bReferenceOuterAdd = (ReferenceLaneIndex == LANE_INDEX_NONE)
			|| (bOnLeft && ReferenceLaneIndex < 0
				&& FMath::Abs(ReferenceLaneIndex) == ReferenceSection.Left.Num())
			|| (!bOnLeft && ReferenceLaneIndex > 0
				&& ReferenceLaneIndex == ReferenceSection.Right.Num());

		const bool bFirstOppositeSideAdd = !bOnLeft
			&& ReferenceSection.Right.Num() == 0
			&& ReferenceSection.Left.Num() > 0;
		const bool bFirstLeftOnRightOnly = bOnLeft
			&& ReferenceSection.Left.Num() == 0
			&& ReferenceSection.Right.Num() > 0;

		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		int32 SectionsUpdated = 0;
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
			if (!SectionShouldReceiveLaneAdd(Section))
			{
				continue;
			}

			int32 EffectiveLaneIndex = ReferenceLaneIndex;
			if (EffectiveLaneIndex != LANE_INDEX_NONE && !Section.CheckLaneIndex(EffectiveLaneIndex))
			{
				EffectiveLaneIndex = LANE_INDEX_NONE;
			}

			const int32 NewLaneIndexOnSection = InsertLaneIntoSection(
				Section,
				EffectiveLaneIndex,
				bOnLeft,
				NewLaneTemplate);

			++SectionsUpdated;
			if (SectionIndex == ReferenceSectionIndex)
			{
				Result.NewSelectedLaneIndex = NewLaneIndexOnSection;
			}
		}

		if (bReferenceOuterAdd || bFirstOppositeSideAdd || bFirstLeftOnRightOnly)
		{
			ShiftROffsetForOuterLaneAdd(TargetSpline, bOnLeft, NewLaneWidthCm);
		}

		Result.bSuccess = SectionsUpdated > 0;
		return Result;
	}
}
