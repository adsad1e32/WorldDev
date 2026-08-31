#include "MetaLandRoadProfile.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadModule.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplineActor.h"
#include "Misc/ScopeExit.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#endif

namespace MetaLandRoadProfile
{
	/** Default longitudinal lane-section count (each covers Both sides). */
	static constexpr int32 DefaultLongitudinalSectionCount = 2;

	static void RefreshRoadActorMesh(UMetaLandRoadSplineComponent* Spline);
	void EnsureLandscapeOverlayDisplayKeys(UMetaLandRoadSplineComponent* Spline);

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

	static int32 ProfileResolveInnermostLaneIndex(const TArray<int32>& Lanes)
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

	static int32 ProfileResolveOutermostLaneIndex(const TArray<int32>& Lanes)
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
			const int32 InnermostLane = ProfileResolveInnermostLaneIndex(LaneIndices);
			const int32 OutermostLane = ProfileResolveOutermostLaneIndex(LaneIndices);
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
		BaseSection.PinAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
		Sections.Add(BaseSection);

		TargetSpline->UpdateRoadLayout();
	}

	static double GetSectionLocalLength(const UMetaLandRoadSplineComponent* TargetSpline, int32 SectionIndex)
	{
		if (!TargetSpline || !TargetSpline->GetLaneSections().IsValidIndex(SectionIndex))
		{
			return 0.0;
		}

		const TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSections();
		const double StartS = Sections[SectionIndex].SOffset;
		const double EndWorldS = (SectionIndex + 1 < Sections.Num())
			? Sections[SectionIndex + 1].SOffset
			: TargetSpline->GetSplineLength();
		return FMath::Max(EndWorldS - StartS, 0.0);
	}

	static void ClampLandscapeKeysToVisibleSpans(UMetaLandRoadSplineComponent* TargetSpline)
	{
		AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(TargetSpline ? TargetSpline->GetOwner() : nullptr);
		if (!Road)
		{
			return;
		}

		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			FMetaLandRoadLaneAttribute* Track = Sections[SectionIndex].PinAttribute(
				FMetaLandRoadAttributeRegistry::TypeLandscape);
			if (!Track)
			{
				continue;
			}

			for (int32 KeyIndex = 0; KeyIndex < Track->Keys.Num(); ++KeyIndex)
			{
				if (Track->IsBeginKey(KeyIndex) || Track->IsEndKey(KeyIndex))
				{
					continue;
				}

				const double WorldS = Sections[SectionIndex].SOffset + Track->Keys[KeyIndex].SOffset;
				if (Road->ContainsJunctionMeshStation(WorldS, 1.0))
				{
					continue;
				}

				const double BestS = Road->ClampToNearestJunctionMeshSpan(WorldS);
				Track->Keys[KeyIndex].SOffset = BestS - Sections[SectionIndex].SOffset;
			}
			Track->SortKeys();
		}
	}

	static void EnsureLandscapeAttributeOnSections(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}

		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		bool bChanged = false;
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
			const FMetaLandRoadLaneAttribute* Existing =
				Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
			const int32 PrevKeyCount = Existing ? Existing->Keys.Num() : 0;
			FMetaLandRoadLaneAttribute* Track = Section.PinAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
			if (!Track)
			{
				continue;
			}

			Track->EnsureBeginAndEndKeys(GetSectionLocalLength(TargetSpline, SectionIndex));
			if (Track->Keys.Num() != PrevKeyCount)
			{
				bChanged = true;
			}
		}

		if (bChanged)
		{
			TargetSpline->UpdateRoadLayout();
		}

		ClampLandscapeKeysToVisibleSpans(TargetSpline);
	}

	static void SeedUnsetRoadWeightFalloffOnKeys(UMetaLandRoadSplineComponent* TargetSpline)
	{
		AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(TargetSpline ? TargetSpline->GetOwner() : nullptr);
		if (!Road)
		{
			return;
		}
		const float ActorW = Road->GetRoadWeightFalloffWidth();
		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		for (FMetaLandRoadLaneSection& Section : Sections)
		{
			FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(
				FMetaLandRoadAttributeRegistry::TypeLandscape);
			if (!Track)
			{
				continue;
			}
			for (int32 KeyIndex = 0; KeyIndex < Track->Keys.Num(); ++KeyIndex)
			{
				FMetaLandRoadLandscapeAttributeValue Value;
				if (const FMetaLandRoadLandscapeAttributeValue* Existing =
					Track->Keys[KeyIndex].Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>())
				{
					Value = *Existing;
				}
				if (Value.RoadWeightFalloffWidth >= 1.0f)
				{
					continue;
				}
				Value.RoadWeightFalloffWidth = ActorW;
				Track->Keys[KeyIndex].Value.InitializeAs<FMetaLandRoadLandscapeAttributeValue>(Value);
			}
		}
	}

	static void EnsureJunctionMouthLandscapeKeys(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}

		AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner());
		if (!Road)
		{
			return;
		}

		EnsureLandscapeAttributeOnSections(TargetSpline);

		// Through / T-through / Cross-through: two mouths on one spline → two mid keys.
		// Stub / T-stem: one cut → Begin or End is drawn at CutS; do not insert a third key.
		TArray<double> MouthStations;
		for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
		{
			if (Zone.ApproachCutS >= 0.0 && Zone.DepartCutS >= 0.0 && Zone.ApproachCutS < Zone.DepartCutS)
			{
				MouthStations.Add(Zone.ApproachCutS);
				MouthStations.Add(Zone.DepartCutS);
			}
		}
		const bool bThroughSameHub = Road->StartOfJunction.IsValid()
			&& Road->EndOfJunction.IsValid()
			&& Road->StartOfJunction.Get() == Road->EndOfJunction.Get()
			&& Road->StartJunctionMeshTrimS >= 0.0
			&& Road->EndJunctionMeshTrimS >= 0.0
			&& Road->EndJunctionMeshTrimS < Road->StartJunctionMeshTrimS;
		if (bThroughSameHub)
		{
			MouthStations.Add(Road->EndJunctionMeshTrimS);
			MouthStations.Add(Road->StartJunctionMeshTrimS);
		}
		if (MouthStations.Num() == 0)
		{
			return;
		}

		bool bChanged = false;
		for (const double WorldS : MouthStations)
		{
			const int32 SectionIndex = TargetSpline->GetRoadLayout().FindSectionAtSOffset(WorldS);
			if (SectionIndex == INDEX_NONE)
			{
				continue;
			}
			FMetaLandRoadLaneSection& Section = TargetSpline->GetLaneSection(SectionIndex);
			FMetaLandRoadLaneAttribute* Track = Section.PinAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
			if (!Track)
			{
				continue;
			}
			const int32 PrevNum = Track->Keys.Num();
			Track->AddKey(WorldS - Section.SOffset, 50.0);
			bChanged |= Track->Keys.Num() != PrevNum;
		}
		if (bChanged)
		{
			TargetSpline->UpdateRoadLayout();
			ClampLandscapeKeysToVisibleSpans(TargetSpline);
		}
	}

	void PrepareLandscapeKeys(UMetaLandRoadSplineComponent* TargetSpline, bool bIncludeMouthKeys)
	{
		if (bIncludeMouthKeys)
		{
			EnsureJunctionMouthLandscapeKeys(TargetSpline);
			SeedUnsetRoadWeightFalloffOnKeys(TargetSpline);
			return;
		}

		EnsureLandscapeAttributeOnSections(TargetSpline);
		EnsureLandscapeOverlayDisplayKeys(TargetSpline);
		SeedUnsetRoadWeightFalloffOnKeys(TargetSpline);
	}

	bool IsLandscapeOverlayStationVisible(
		const AMoKuEditSplineActor* Road,
		double WorldS,
		double TolCm)
	{
		return !Road || Road->ContainsJunctionMeshStation(WorldS, TolCm);
	}

	double GetLandscapeAttributeDisplayS(
		const AMoKuEditSplineActor* Road,
		const FMetaLandRoadLaneAttribute* Track,
		int32 KeyIndex,
		double StoredWorldS)
	{
		if (!Road || !Track)
		{
			return StoredWorldS;
		}
		if (Road->ContainsJunctionMeshStation(StoredWorldS, 1.0))
		{
			return StoredWorldS;
		}

		TArray<TPair<double, double>> Spans;
		Road->GetJunctionMeshGenerationSpans(Spans);
		if (Spans.Num() == 0)
		{
			return StoredWorldS;
		}
		if (Track->IsBeginKey(KeyIndex))
		{
			return Spans[0].Key;
		}
		if (Track->IsEndKey(KeyIndex))
		{
			return Spans.Last().Value;
		}
		return StoredWorldS;
	}

	bool ShouldHideLandscapeAttributeKey(
		const AMoKuEditSplineActor* Road,
		const FMetaLandRoadLaneAttribute* Track,
		int32 KeyIndex,
		double WorldS,
		double TolCm)
	{
		if (Track && (Track->IsBeginKey(KeyIndex) || Track->IsEndKey(KeyIndex)))
		{
			return false;
		}
		if (!IsLandscapeOverlayStationVisible(Road, WorldS, TolCm))
		{
			return true;
		}
		if (!Road)
		{
			return false;
		}

		TArray<TPair<double, double>> Spans;
		Road->GetJunctionMeshGenerationSpans(Spans);
		if (Spans.Num() == 0)
		{
			return false;
		}

		const double Tol = FMath::Max(TolCm, 1.0);
		if (FMath::Abs(WorldS - Spans[0].Key) <= Tol || FMath::Abs(WorldS - Spans.Last().Value) <= Tol)
		{
			return true;
		}
		return false;
	}

	static void CollectLandscapeOverlaySpans(
		const AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* Spline,
		TArray<TPair<double, double>>& OutSpans)
	{
		OutSpans.Reset();
		if (Road)
		{
			Road->GetJunctionMeshGenerationSpans(OutSpans);
		}
		if (OutSpans.Num() > 0 || !Spline)
		{
			return;
		}
		const double Length = Spline->GetSplineLength();
		if (Length > 1.0)
		{
			OutSpans.Emplace(0.0, Length);
		}
	}

	static void CollectVisibleLandscapeOverlayStations(
		const AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* Spline,
		TArray<double>& OutStations,
		double MergeTolCm = 80.0)
	{
		OutStations.Reset();
		TArray<TPair<double, double>> Spans;
		CollectLandscapeOverlaySpans(Road, Spline, Spans);

		auto AddUnique = [&OutStations, MergeTolCm](double StationS)
		{
			for (const double Existing : OutStations)
			{
				if (FMath::Abs(Existing - StationS) <= MergeTolCm)
				{
					return;
				}
			}
			OutStations.Add(StationS);
		};

		for (const TPair<double, double>& Span : Spans)
		{
			AddUnique(Span.Key);
			AddUnique(Span.Value);
		}

		if (Spline)
		{
			const TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSections();
			for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
			{
				const FMetaLandRoadLaneAttribute* Track =
					Sections[SectionIndex].FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
				if (!Track)
				{
					continue;
				}
				for (int32 KeyIndex = 0; KeyIndex < Track->Keys.Num(); ++KeyIndex)
				{
					if (Track->IsBeginKey(KeyIndex) || Track->IsEndKey(KeyIndex))
					{
						continue;
					}
					const double StoredWorldS = Sections[SectionIndex].SOffset + Track->Keys[KeyIndex].SOffset;
					if (ShouldHideLandscapeAttributeKey(Road, Track, KeyIndex, StoredWorldS))
					{
						continue;
					}
					AddUnique(GetLandscapeAttributeDisplayS(Road, Track, KeyIndex, StoredWorldS));
				}
			}
		}

		OutStations.Sort();
	}

	static bool FindNearestLandscapeKeyAtS(
		const UMetaLandRoadSplineComponent* Spline,
		const AMoKuEditSplineActor* Road,
		double TargetS,
		int32& OutSectionIndex,
		int32& OutKeyIndex,
		double& OutDist)
	{
		OutSectionIndex = INDEX_NONE;
		OutKeyIndex = INDEX_NONE;
		OutDist = TNumericLimits<double>::Max();
		if (!Spline)
		{
			return false;
		}

		const TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSections();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FMetaLandRoadLaneAttribute* Track =
				Sections[SectionIndex].FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
			if (!Track)
			{
				continue;
			}
			for (int32 KeyIndex = 0; KeyIndex < Track->Keys.Num(); ++KeyIndex)
			{
				const double StoredWorldS = Sections[SectionIndex].SOffset + Track->Keys[KeyIndex].SOffset;
				const double DisplayS = GetLandscapeAttributeDisplayS(Road, Track, KeyIndex, StoredWorldS);
				const double Dist = FMath::Min(
					FMath::Abs(StoredWorldS - TargetS),
					FMath::Abs(DisplayS - TargetS));
				if (Dist < OutDist)
				{
					OutDist = Dist;
					OutSectionIndex = SectionIndex;
					OutKeyIndex = KeyIndex;
				}
			}
		}
		return OutSectionIndex != INDEX_NONE;
	}

	void ForEachVisibleLandscapeKey(
		const UMetaLandRoadSplineComponent* Spline,
		const AMoKuEditSplineActor* Road,
		TFunctionRef<bool(int32 SectionIndex, int32 KeyIndex, double WorldS)> Visit)
	{
		if (!Spline)
		{
			return;
		}

		TArray<double> Stations;
		CollectVisibleLandscapeOverlayStations(Road, Spline, Stations);
		if (Stations.Num() == 0)
		{
			return;
		}

		for (const double StationS : Stations)
		{
			int32 SectionIndex = INDEX_NONE;
			int32 KeyIndex = INDEX_NONE;
			double Dist = TNumericLimits<double>::Max();
			FindNearestLandscapeKeyAtS(Spline, Road, StationS, SectionIndex, KeyIndex, Dist);
			if (SectionIndex == INDEX_NONE)
			{
				continue;
			}
			if (!Visit(SectionIndex, KeyIndex, StationS))
			{
				return;
			}
		}
	}

	bool EnsureLandscapeKeyAtWorldS(
		UMetaLandRoadSplineComponent* Spline,
		double WorldS,
		int32& OutSectionIndex,
		int32& OutKeyIndex,
		double DuplicateTolCm)
	{
		OutSectionIndex = INDEX_NONE;
		OutKeyIndex = INDEX_NONE;
		if (!Spline)
		{
			return false;
		}

		const int32 SectionIndex = Spline->GetRoadLayout().FindSectionAtSOffset(WorldS);
		if (SectionIndex == INDEX_NONE)
		{
			return false;
		}

		FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
		FMetaLandRoadLaneAttribute* Track = Section.PinAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
		if (!Track)
		{
			return false;
		}

		const double LocalS = WorldS - Section.SOffset;
		const int32 Existing = Track->FindKeyIndex(LocalS, DuplicateTolCm);
		if (Existing != INDEX_NONE)
		{
			OutSectionIndex = SectionIndex;
			OutKeyIndex = Existing;
			return true;
		}

		Spline->Modify();
		OutKeyIndex = Track->AddKey(LocalS, DuplicateTolCm);
		OutSectionIndex = SectionIndex;
		return OutKeyIndex != INDEX_NONE;
	}

	void EnsureLandscapeOverlayDisplayKeys(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return;
		}

		const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner());
		TArray<TPair<double, double>> Spans;
		CollectLandscapeOverlaySpans(Road, Spline, Spans);
		for (const TPair<double, double>& Span : Spans)
		{
			int32 SectionIndex = INDEX_NONE;
			int32 KeyIndex = INDEX_NONE;
			EnsureLandscapeKeyAtWorldS(Spline, Span.Key, SectionIndex, KeyIndex);
			EnsureLandscapeKeyAtWorldS(Spline, Span.Value, SectionIndex, KeyIndex);
		}
	}

	bool GetLandscapeOverlayKeyOrdinal(
		const AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* Spline,
		int32 SectionIndex,
		int32 KeyIndex,
		int32& OutIndex,
		int32& OutCount)
	{
		OutIndex = 0;
		OutCount = 0;
		if (!Spline || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
		{
			return false;
		}

		const FMetaLandRoadLaneSection& Section = Spline->GetLaneSections()[SectionIndex];
		const FMetaLandRoadLaneAttribute* Track =
			Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
		if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
		{
			return false;
		}

		TArray<double> Stations;
		CollectVisibleLandscapeOverlayStations(Road, Spline, Stations);
		OutCount = Stations.Num();
		if (OutCount == 0)
		{
			OutIndex = Track->IsEndKey(KeyIndex) ? 1 : 0;
			OutCount = Track->Keys.Num() > 1 ? 2 : 1;
			return true;
		}

		const double StoredWorldS = Section.SOffset + Track->Keys[KeyIndex].SOffset;
		const double DisplayS = GetLandscapeAttributeDisplayS(Road, Track, KeyIndex, StoredWorldS);
		int32 BestIndex = 0;
		double BestDist = TNumericLimits<double>::Max();
		for (int32 StationIndex = 0; StationIndex < Stations.Num(); ++StationIndex)
		{
			const double Dist = FMath::Abs(DisplayS - Stations[StationIndex]);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestIndex = StationIndex;
			}
		}
		OutIndex = BestIndex;
		return true;
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

		EnsureBothFacesOnRoad(TargetSpline);
		EnsureLandscapeAttributeOnSections(TargetSpline);
	}

	static void HalveLaneWidth(FMetaLandRoadLane& Lane)
	{
		if (Lane.Width.GetNumKeys() == 0)
		{
			Lane.Width.SetDefaultValue(FMath::Max(Lane.Width.GetDefaultValue() * 0.5f, 8.0f));
			return;
		}

		for (FRichCurveKey& Key : Lane.Width.Keys)
		{
			Key.Value = FMath::Max(Key.Value * 0.5f, 8.0f);
		}
		MetaLandRoad::EnsureLaneWidthCurveCubic(Lane.Width);
	}

	// =============================================================================
	// [KEY-ALGO] EnsureBothFacesOnRoad
	// Alt+E 细分的前提：整条路必须同时有 Left 幅和 Right 幅。
	// 单条 / 多条都一样 —— 只拆已有宽度，不增加总宽。
	// 调用链: EnsureDefaultRoadProfile / Alt+E·T 进模式 / TrySplitSectionSide / SplitSection
	// =============================================================================
	void EnsureBothFacesOnRoad(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline || TargetSpline->GetLaneSectionsNum() == 0)
		{
			return;
		}

		bool bRoadHasLeft = false;
		bool bRoadHasRight = false;
		for (const FMetaLandRoadLaneSection& Section : TargetSpline->GetLaneSections())
		{
			bRoadHasLeft |= Section.Left.Num() > 0;
			bRoadHasRight |= Section.Right.Num() > 0;
		}

		bool bChanged = false;
		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();

		if (bRoadHasLeft && bRoadHasRight)
		{
			for (FMetaLandRoadLaneSection& Section : Sections)
			{
				if (Section.Left.Num() > 0 && Section.Right.Num() > 0
					&& Section.Side != EMetaLandRoadLaneSectionSide::Both)
				{
					Section.Side = EMetaLandRoadLaneSectionSide::Both;
					bChanged = true;
				}
			}
		}
		else if (bRoadHasLeft && !bRoadHasRight)
		{
			TargetSpline->Modify();
			for (FMetaLandRoadLaneSection& Section : Sections)
			{
				if (Section.Left.Num() == 0)
				{
					continue;
				}

				HalveLaneWidth(Section.Left[0]);
				Section.Right.Add(Section.Left[0]);
				Section.Side = EMetaLandRoadLaneSectionSide::Both;
				bChanged = true;
			}
		}
		else if (bRoadHasRight && !bRoadHasLeft)
		{
			TargetSpline->Modify();
			for (FMetaLandRoadLaneSection& Section : Sections)
			{
				if (Section.Right.Num() == 0)
				{
					continue;
				}

				HalveLaneWidth(Section.Right[0]);
				Section.Left.Add(Section.Right[0]);
				Section.Side = EMetaLandRoadLaneSectionSide::Both;
				bChanged = true;
			}
		}

		if (!bChanged)
		{
			return;
		}

		bool bJunctionLocked = false;
		if (const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()))
		{
			bJunctionLocked = RoadActor->StartOfJunction.IsValid()
				|| RoadActor->EndOfJunction.IsValid()
				|| RoadActor->JunctionTrimZones.Num() > 0;
		}

		if (!bJunctionLocked)
		{
			FMetaLandRoadLayout& Layout = TargetSpline->GetRoadLayoutMutable();
			Layout.ROffset.Reset();
			Layout.ROffset.AddKey(0.0f, 0.0f);
		}

		TargetSpline->UpdateLaneSectionBounds();
		TargetSpline->UpdateRoadLayout();
		RefreshRoadActorMesh(TargetSpline);
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
			const int32 LaneSectionIndex = MetaLandRoad::ResolveLaneSectionIndexForSpan(
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
			const int32 Resolved = MetaLandRoad::ResolveLaneSectionIndexForSpan(
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
#if WITH_EDITOR
		if (FMetaLandRoadEditorHooks::RefreshLaneVisualization)
		{
			FMetaLandRoadEditorHooks::RefreshLaneVisualization(Spline);
		}
#endif
	}

#if WITH_EDITOR
	static TSet<TWeakObjectPtr<AMoKuEditSplineActor>> GUndoRoads;
	static TSet<TWeakObjectPtr<AMoKuEditIntersectionActor>> GUndoJunctions;
	static bool bUndoMeshSyncQueued = false;

	static void FlushUndoDerivedMeshes()
	{
		bUndoMeshSyncQueued = false;
		TSet<TWeakObjectPtr<AMoKuEditIntersectionActor>> Junctions = GUndoJunctions;
		TArray<TWeakObjectPtr<AMoKuEditSplineActor>> Roads;
		for (const TWeakObjectPtr<AMoKuEditSplineActor>& RoadWeak : GUndoRoads)
		{
			Roads.Add(RoadWeak);
			if (AMoKuEditSplineActor* Road = RoadWeak.Get())
			{
				if (AMoKuEditIntersectionActor* StartJ = Road->StartOfJunction.Get())
				{
					Junctions.Add(StartJ);
				}
				if (AMoKuEditIntersectionActor* EndJ = Road->EndOfJunction.Get())
				{
					Junctions.Add(EndJ);
				}
				for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
				{
					if (AMoKuEditIntersectionActor* J = Zone.Junction.Get())
					{
						Junctions.Add(J);
					}
				}
			}
		}
		GUndoRoads.Reset();
		GUndoJunctions.Reset();

		TSet<AMoKuEditSplineActor*> RoadsCoveredByJunction;
		TGuardValue<bool> SuppressRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);

		for (const TWeakObjectPtr<AMoKuEditIntersectionActor>& JWeak : Junctions)
		{
			AMoKuEditIntersectionActor* Junction = JWeak.Get();
			if (!Junction || !Junction->JunctionData)
			{
				continue;
			}
			for (AMoKuEditSplineActor* Linked : Junction->JunctionData->LinkedRoadActors)
			{
				RoadsCoveredByJunction.Add(Linked);
			}
			if (Junction->JunctionData->JunctionNode.Arms.Num() >= 2)
			{
				Junction->RefreshJunctionCoverMesh();
			}
			else
			{
				Junction->ClearJunctionCoverMesh();
			}
		}

		for (const TWeakObjectPtr<AMoKuEditSplineActor>& RoadWeak : Roads)
		{
			AMoKuEditSplineActor* Road = RoadWeak.Get();
			if (Road && !RoadsCoveredByJunction.Contains(Road))
			{
				Road->RefreshDynamicRoadMesh();
			}
		}
	}

	static void QueueUndoDerivedMeshSync()
	{
		if (bUndoMeshSyncQueued)
		{
			return;
		}
		bUndoMeshSyncQueued = true;
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([](float)
			{
				FlushUndoDerivedMeshes();
				return false;
			}),
			0.0f);
	}
#endif

	void NotifyUndoDerivedMeshDirty(AMoKuEditSplineActor* Road)
	{
#if WITH_EDITOR
		if (Road)
		{
			GUndoRoads.Add(Road);
			QueueUndoDerivedMeshSync();
		}
#endif
	}

	void NotifyUndoDerivedMeshDirty(AMoKuEditIntersectionActor* Junction)
	{
#if WITH_EDITOR
		if (Junction)
		{
			GUndoJunctions.Add(Junction);
			QueueUndoDerivedMeshSync();
		}
#endif
	}

	void RecookLocalJunctionsForSpline(UMetaLandRoadSplineComponent* TargetSpline)
	{
		if (!TargetSpline)
		{
			return;
		}
#if WITH_EDITOR
		if (GIsTransacting)
		{
			return;
		}
#endif

		AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner());
		if (!RoadActor || !RoadActor->GetWorld() || UE::GetIsEditorLoadingPackage())
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
#if WITH_EDITOR
			if (FMetaLandRoadEditorHooks::RecookJunction)
			{
				FMetaLandRoadEditorHooks::RecookJunction(Junction, true);
			}
#endif
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

	static int32 InsertLaneOntoFace(
		FMetaLandRoadLaneSection& Section,
		bool bOnLeft,
		const FMetaLandRoadLane& NewLane)
	{
		EnsureSectionSideForLaneAdd(Section, bOnLeft);
		if (bOnLeft)
		{
			Section.Left.Add(NewLane);
			return -Section.Left.Num();
		}

		Section.Right.Add(NewLane);
		return Section.Right.Num();
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

		TArray<FMetaLandRoadLaneSection>& Sections = TargetSpline->GetLaneSectionsMutable();
		FMetaLandRoadLaneSection& TargetSection = Sections[SpanSectionIndex];

		int32 TemplateSectionIndex = SpanSectionIndex;
		int32 TemplateLaneIndex = LaneIndex;
		if (TemplateLaneIndex == LANE_INDEX_NONE || !TargetSection.CheckLaneIndex(TemplateLaneIndex))
		{
			const int32 Resolved = MetaLandRoad::ResolveLaneSectionIndexForSpan(
				TargetSpline, SpanSectionIndex, LaneIndex);
			if (Resolved != INDEX_NONE
				&& Sections.IsValidIndex(Resolved)
				&& Sections[Resolved].CheckLaneIndex(LaneIndex))
			{
				TemplateSectionIndex = Resolved;
				TemplateLaneIndex = LaneIndex;
			}
			else
			{
				TemplateLaneIndex = LANE_INDEX_NONE;
			}
		}

		const FMetaLandRoadLane NewLaneTemplate = MakeNewLaneFromSelection(
			Sections[TemplateSectionIndex], TemplateLaneIndex);
		Result.NewSelectedLaneIndex = InsertLaneOntoFace(TargetSection, bOnLeft, NewLaneTemplate);
		Result.bSuccess = true;
		return Result;
	}
}
