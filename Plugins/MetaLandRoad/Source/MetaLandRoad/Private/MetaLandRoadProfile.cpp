#include "MetaLandRoadProfile.h"
#include "MetaLandRoadSplineComponent.h"

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
}
