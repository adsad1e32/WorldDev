#pragma once

#include "Math/IntPoint.h"
#include "MetaLandRoadTypes.h"

class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class UMetaLandRoadSplineComponent;
class UWorld;

/** One lane entry in Alt+T multi-selection. */
struct FMetaLandRoadSelectedLaneRef
{
	int32 SectionIndex = INDEX_NONE;
	int32 LaneIndex = LANE_INDEX_NONE;

	bool operator==(const FMetaLandRoadSelectedLaneRef& Other) const
	{
		return SectionIndex == Other.SectionIndex && LaneIndex == Other.LaneIndex;
	}
};

/** Hover / selection state for Alt+T lane width edit draw. */
struct FMetaLandRoadLaneEditHighlight
{
	int32 HoveredSectionIndex = INDEX_NONE;
	int32 HoveredLaneIndex = LANE_INDEX_NONE;
	int32 HoveredWidthIndex = INDEX_NONE;
	int32 SelectedSectionIndex = INDEX_NONE;
	int32 SelectedLaneIndex = LANE_INDEX_NONE;
	int32 SelectedWidthIndex = INDEX_NONE;
	int32 HoveredSectionBoundaryIndex = INDEX_NONE;
	int32 SelectedSectionBoundaryIndex = INDEX_NONE;
	bool bHoveredWidthIsKey = false;
	/** Hovering lane outer boundary on a section span (not width-key segment). */
	bool bHoveredLaneBoundaryLine = false;
	TArray<FMetaLandRoadSelectedLaneRef> SelectedLanes;
	/** Ctrl+LMB lane face fill — not set for boundary/width RMB context. */
	bool bLaneFacePrimSelected = false;

	bool HasLaneFaceSelection() const
	{
		if (SelectedWidthIndex != INDEX_NONE || !bLaneFacePrimSelected)
		{
			return false;
		}
		if (SelectedLanes.Num() > 0)
		{
			return true;
		}
		return SelectedLaneIndex != LANE_INDEX_NONE;
	}

	bool IsLaneSelected(int32 SectionIndex, int32 LaneIndex) const
	{
		if (LaneIndex == LANE_INDEX_NONE)
		{
			return false;
		}

		for (const FMetaLandRoadSelectedLaneRef& Ref : SelectedLanes)
		{
			if (Ref.SectionIndex == SectionIndex && Ref.LaneIndex == LaneIndex)
			{
				return true;
			}
		}

		return SelectedSectionIndex == SectionIndex && SelectedLaneIndex == LaneIndex;
	}

	bool IsLaneHighlighted(int32 SectionIndex, int32 LaneIndex) const
	{
		return IsLaneSelected(SectionIndex, LaneIndex);
	}
};

class FSceneView;

/** Screen-space lane face pick (Alt+T hover). */
struct FMetaLandRoadLaneFacePickResult
{
	int32 SectionIndex = INDEX_NONE;
	int32 LaneIndex = LANE_INDEX_NONE;

	bool IsValid() const
	{
		return SectionIndex != INDEX_NONE && LaneIndex != LANE_INDEX_NONE;
	}
};

namespace MetaLandRoadLaneVisualization
{
	/** Clears legacy LineBatch batches and requests a viewport redraw. Overlays draw in ComponentVisualizer PDI only. */
	METALANDROAD_API void RefreshLaneVisualization(
		UMetaLandRoadSplineComponent* SplineComponent,
		const TSet<int32>* SelectedKeys = nullptr,
		int32 HoveredKey = INDEX_NONE);

#if WITH_EDITOR
	METALANDROAD_API void DrawLaneVisualization(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		float ThicknessScale = 1.0f);

	/** Alt+T: lane boundaries + per-lane face selection fill; hover highlights width segment only. */
	METALANDROAD_API void DrawLaneEditVisualization(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight);

	/** World Builder / debug: yellow lane-face hover + selection fills only. */
	METALANDROAD_API void DrawLaneFaceHighlightFills(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight);

	/** World Builder: mouth bars + lane-face fills for hover/selection (Shift bundle). */
	METALANDROAD_API void DrawAllWorldBuilderLaneHighlights(
		UWorld* World,
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadEditTarget& EditTarget,
		const FMetaLandRoadDrawAnchor* EndSnapMixAnchor = nullptr);

	/** World Builder: mouth cross-section lines for EditTarget (yellow); optional EndSnap hover (green). */
	METALANDROAD_API void DrawWorldBuilderMouthCrossSections(
		UWorld* World,
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadEditTarget& EditTarget,
		const FMetaLandRoadDrawAnchor* EndSnapMixAnchor = nullptr,
		bool bSkipEditTargetAndHover = false,
		const FMetaLandRoadDrawAnchor* EndSnapHoverAnchor = nullptr);

	/** Alt+T: faint full-road lane underlay (all boundaries, side-aware). */
	METALANDROAD_API void DrawLaneEditMeshUnderlay(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI);

	/** Alt+T: section center + split guides at half thickness, no hit proxies. */
	METALANDROAD_API void DrawLaneModeSectionGuides(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight);

	/** Alt+E: lane boundary guides only (left green / right red), no hover or hit proxies. */
	METALANDROAD_API void DrawSectionModeLaneGuides(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI);

	/** Alt+T: screen-space lane interior pick (no hit-proxy re-entry). */
	METALANDROAD_API bool PickLaneFaceAtMouse(
		const UMetaLandRoadSplineComponent* SplineComponent,
		const FSceneView& View,
		const FIntPoint& MousePos,
		FMetaLandRoadLaneFacePickResult& OutResult);

	/** Span section → lane-data section (side-aware). */
	METALANDROAD_API int32 ResolveLaneSectionIndexForSpan(
		const UMetaLandRoadSplineComponent* SplineComponent,
		int32 SpanSectionIndex,
		int32 LaneIndex);

	/** Highlight lookup: span section + signed LaneIndex vs selection state. */
	METALANDROAD_API bool IsLaneHighlightSelected(
		const UMetaLandRoadSplineComponent* SplineComponent,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		const FMetaLandRoadLaneEditHighlight& Highlight);
#endif
}
