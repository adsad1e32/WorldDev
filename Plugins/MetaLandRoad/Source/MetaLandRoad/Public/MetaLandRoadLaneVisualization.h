#pragma once

#include "MetaLandRoadTypes.h"

class FPrimitiveDrawInterface;
class UMetaLandRoadSplineComponent;

/** Hover / selection state for Alt+T lane width edit draw. */
struct FMetaLandRoadLaneEditHighlight
{
	int32 HoveredSectionIndex = INDEX_NONE;
	int32 HoveredLaneIndex = LANE_INDEX_NONE;
	int32 HoveredWidthIndex = INDEX_NONE;
	int32 SelectedSectionIndex = INDEX_NONE;
	int32 SelectedLaneIndex = LANE_INDEX_NONE;
	int32 SelectedWidthIndex = INDEX_NONE;
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

	/** Alt+T: lane boundaries only (left green / right red), with hit proxies and hover/select highlight. */
	METALANDROAD_API void DrawLaneEditVisualization(
		const UMetaLandRoadSplineComponent* SplineComponent,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight);
#endif
}
