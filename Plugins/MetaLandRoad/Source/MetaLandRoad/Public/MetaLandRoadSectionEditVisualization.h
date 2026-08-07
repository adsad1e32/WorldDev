#pragma once

#include "Math/IntPoint.h"
#include "SceneView.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

class FPrimitiveDrawInterface;
class HHitProxy;
class UMetaLandRoadSplineComponent;

namespace MetaLandRoadSectionEditVisualization
{
	METALANDROAD_API UMetaLandRoadSplineComponent* FindSelectedRoadSplineFromEditor();

	METALANDROAD_API bool IsRoadSectionEditingActive(
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState);

	/** Main center road: UMoKuEditSplinesComponent with Tag MainRoad, or any other UMetaLandRoadSplineComponent. */
	METALANDROAD_API bool IsMainRoadSpline(const UActorComponent* Component);

	METALANDROAD_API bool IsEngineSplineEditProxy(const HHitProxy* HitProxy);

	METALANDROAD_API bool IsMetaLandRoadVisProxy(const HHitProxy* HitProxy);

	METALANDROAD_API bool IsSplineOwnerSelected(const UMetaLandRoadSplineComponent* SplineComp);

	METALANDROAD_API void DrawSectionEditLines(
		UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		int32 HoveredSectionIndex);

	METALANDROAD_API void DrawSectionBoundaryHandles(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		int32 HoveredSectionIndex = INDEX_NONE);

	METALANDROAD_API bool PickSectionBoundaryByScreenDistance(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView& View,
		const FIntPoint& ClickPos,
		float MaxScreenDistance,
		int32& OutSectionIndex);

	METALANDROAD_API float ResolveSplitSplineKey(
		const UMetaLandRoadSplineComponent* SplineComp,
		UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		float HoverSplineKey = -1.0f);
}
