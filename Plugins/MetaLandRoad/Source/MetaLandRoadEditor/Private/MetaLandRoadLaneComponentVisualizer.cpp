#include "MetaLandRoadLaneComponentVisualizer.h"

#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadSplineComponent.h"
#include "Modules/ModuleManager.h"

void FMetaLandRoadLaneComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Component);
	if (!RoadSpline || !PDI)
	{
		return;
	}

	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		// Spline draw / lane-extend: mouth bars + green guidance only (no lane boundary spam).
		if (FMetaLandRoadEditorModule::Get().IsInteractiveSplineDrawToolActive())
		{
			return;
		}

		// Alt+E / Alt+T use CompVis overlays  - skip persistent lane LineBatch/PDI.
		if (FMetaLandRoadEditorModule::Get().IsSectionMode() || FMetaLandRoadEditorModule::Get().IsLaneEditMode())
		{
			return;
		}
	}

	MetaLandRoadLaneVisualization::DrawLaneVisualization(RoadSpline, PDI);
}
