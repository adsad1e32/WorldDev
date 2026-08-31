#pragma once

#include "Math/IntPoint.h"
#include "SceneView.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

class FPrimitiveDrawInterface;
class HHitProxy;
class UMetaLandRoadSplineComponent;

namespace MetaLandRoadSectionEditVisualization
{
	METALANDROADEDITOR_API UMetaLandRoadSplineComponent* FindSelectedRoadSplineFromEditor();

	METALANDROADEDITOR_API bool IsRoadSectionEditingActive(
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState);

	/** Main center road: UMoKuEditSplinesComponent with Tag MainRoad, or any other UMetaLandRoadSplineComponent. */
	METALANDROADEDITOR_API bool IsMainRoadSpline(const UActorComponent* Component);

	METALANDROADEDITOR_API bool IsEngineSplineEditProxy(const HHitProxy* HitProxy);

	METALANDROADEDITOR_API bool IsMetaLandRoadVisProxy(const HHitProxy* HitProxy);

	METALANDROADEDITOR_API bool IsSplineOwnerSelected(const UMetaLandRoadSplineComponent* SplineComp);

	METALANDROADEDITOR_API void DrawSectionEditLines(
		UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		int32 HoveredSectionIndex);

	METALANDROADEDITOR_API void DrawSectionBoundaryHandles(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		int32 HoveredSectionIndex = INDEX_NONE);

	METALANDROADEDITOR_API bool PickSectionBoundaryByScreenDistance(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView& View,
		const FIntPoint& ClickPos,
		float MaxScreenDistance,
		int32& OutSectionIndex);

	METALANDROADEDITOR_API float ResolveSplitSplineKey(
		const UMetaLandRoadSplineComponent* SplineComp,
		UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		float HoverSplineKey = -1.0f);
}
