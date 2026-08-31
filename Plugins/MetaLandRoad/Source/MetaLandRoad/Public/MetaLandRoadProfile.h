#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadProfile.generated.h"

class UMetaLandRoadSplineComponent;
class AMoKuEditSplineActor;
class AMoKuEditIntersectionActor;

/** Lightweight lane description used when creating a new road (MetaRoad FRoadLaneProfile equivalent). */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	double Width = MetaLandRoad::DefaultRoadLaneWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	EMetaLandRoadLaneDirection Direction = EMetaLandRoadLaneDirection::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	EMetaLandRoadDriveableLaneType DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;
};

/** Lane section profile applied on first road placement (MetaRoad FRoadLaneSectionProfile equivalent). */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneSectionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	TArray<FMetaLandRoadLaneProfile> Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	TArray<FMetaLandRoadLaneProfile> Right;
};

namespace MetaLandRoadProfile
{
	METALANDROAD_API FMetaLandRoadLaneSectionProfile GetDefaultRoadProfile();

	METALANDROAD_API void SetRoadProfile(UMetaLandRoadSplineComponent* TargetSpline, const FMetaLandRoadLaneSectionProfile& RoadProfile);

	/** Optional: when enabled and exactly 2 sections exist, keep the second section at 50% spline length. */
	METALANDROAD_API void ApplyDefaultLongitudinalSectionSplit(UMetaLandRoadSplineComponent* TargetSpline);

	/** Apply default lanes when the spline has no sections yet. */
	METALANDROAD_API void EnsureDefaultRoadProfile(UMetaLandRoadSplineComponent* TargetSpline);

	/** One door: Begin/End on every section. bIncludeMouthKeys = cook only: through/T-through CutS mid-keys. */
	METALANDROAD_API void PrepareLandscapeKeys(UMetaLandRoadSplineComponent* TargetSpline, bool bIncludeMouthKeys = false);

	/** Overlay: remaining-road S. Forwards to AMoKuEditSplineActor::ContainsJunctionMeshStation. */
	METALANDROAD_API bool IsLandscapeOverlayStationVisible(
		const class AMoKuEditSplineActor* Road,
		double WorldS,
		double TolCm = 80.0);

	/** Stored S stays; Begin/End inside the hub draw at the cut. */
	METALANDROAD_API double GetLandscapeAttributeDisplayS(
		const class AMoKuEditSplineActor* Road,
		const struct FMetaLandRoadLaneAttribute* Track,
		int32 KeyIndex,
		double StoredWorldS);

	/** Hide hub-interior keys and extra CutS duplicates. Begin/End stay pickable (drawn at cut). */
	METALANDROAD_API bool ShouldHideLandscapeAttributeKey(
		const class AMoKuEditSplineActor* Road,
		const struct FMetaLandRoadLaneAttribute* Track,
		int32 KeyIndex,
		double WorldS,
		double TolCm = 80.0);

	/**
	 * Overlay dots along one actor, sorted by World S: Begin, Key1, Key2, … End.
	 * Junction splits still share this sequence. WorldS is the draw station.
	 */
	METALANDROAD_API void ForEachVisibleLandscapeKey(
		const UMetaLandRoadSplineComponent* Spline,
		const class AMoKuEditSplineActor* Road,
		TFunctionRef<bool(int32 SectionIndex, int32 KeyIndex, double WorldS)> Visit);

	/** Find or add a Landscape key on the section that owns WorldS. */
	METALANDROAD_API bool EnsureLandscapeKeyAtWorldS(
		UMetaLandRoadSplineComponent* Spline,
		double WorldS,
		int32& OutSectionIndex,
		int32& OutKeyIndex,
		double DuplicateTolCm = 50.0);

	/** Write a real key at every visible overlay station so mouths are selectable. */
	METALANDROAD_API void EnsureLandscapeOverlayDisplayKeys(UMetaLandRoadSplineComponent* Spline);

	/** 0-based rank in the actor-wide overlay sequence (Begin=0, End=Count-1). */
	METALANDROAD_API bool GetLandscapeOverlayKeyOrdinal(
		const class AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* Spline,
		int32 SectionIndex,
		int32 KeyIndex,
		int32& OutIndex,
		int32& OutCount);

	/**
	 * [KEY-ALGO] Auto-write Left + Right faces on every road (1 lane or N lanes).
	 * Total width is preserved (split existing width). Required before Add Left/Right Section.
	 */
	METALANDROAD_API void EnsureBothFacesOnRoad(UMetaLandRoadSplineComponent* TargetSpline);

	/** Refresh layout bounds after spline edits. */
	METALANDROAD_API void SyncRoadLayout(UMetaLandRoadSplineComponent* TargetSpline);

	/** Seed new branch spline lanes/widths from a World Builder draw anchor. */
	METALANDROAD_API void ApplyRoadProfileFromDrawAnchor(
		UMetaLandRoadSplineComponent* TargetSpline,
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor);

	/**
	 * Ensure TargetSpline lane counts match the anchor bundle (N parallel lanes).
	 * Mutates existing sections in place so start-mouth RoadConnections survive EndConnection.
	 * Falls back to ApplyRoadProfileFromDrawAnchor when the target has no sections.
	 */
	METALANDROAD_API void EnsureLaneCountFromDrawAnchor(
		UMetaLandRoadSplineComponent* TargetSpline,
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor);

	/** Sorted bundle lane indices + mouth widths (from BuildProfileFromDrawAnchor). */
	METALANDROAD_API bool GetDrawAnchorBundleLaneWidths(
		const UMetaLandRoadSplineComponent* SourceSpline,
		const FMetaLandRoadDrawAnchor& Anchor,
		TArray<int32>& OutLaneIndices,
		TArray<float>& OutWidths);

	/** Section that owns lane arrays for Add/Delete (prefers Both — Side markers get wiped by UpdateLayout). */
	METALANDROAD_API int32 ResolveLaneMutationSectionIndex(
		const UMetaLandRoadSplineComponent* TargetSpline,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		bool bOnLeft);

	/** Keep the opposite road edge fixed when inserting an outermost lane. */
	METALANDROAD_API void ShiftROffsetForOuterLaneAdd(
		UMetaLandRoadSplineComponent* TargetSpline,
		bool bAddedOnLeft,
		double LaneWidthCm);

	/** After layout undo/edit, refresh this spline and any connected parent/child road mesh. */
	METALANDROAD_API void RefreshConnectedRoadMeshes(UMetaLandRoadSplineComponent* TargetSpline);

	/** Re-cut + rebuild cover for junction hubs linked to this road only (not full-world scan). */
	METALANDROAD_API void RecookLocalJunctionsForSpline(UMetaLandRoadSplineComponent* TargetSpline);

	/** Undo/redo: queue derived mesh rebuild after the transaction finishes. Never Recook mid-undo. */
	METALANDROAD_API void NotifyUndoDerivedMeshDirty(AMoKuEditSplineActor* Road);
	METALANDROAD_API void NotifyUndoDerivedMeshDirty(AMoKuEditIntersectionActor* Junction);

	/** Add one left/right lane on the selected span section only (not the whole road). */
	struct FMetaLandRoadAddLaneResult
	{
		bool bSuccess = false;
		int32 NewSelectedLaneIndex = LANE_INDEX_NONE;
	};

	METALANDROAD_API FMetaLandRoadAddLaneResult AddLaneToRoadLayout(
		UMetaLandRoadSplineComponent* TargetSpline,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		bool bOnLeft);
}
