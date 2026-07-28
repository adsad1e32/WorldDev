#include "MetaLandRoadLaneComponentVisualizer.h"

#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadModule.h"
#include "MetaLandRoadSplineComponent.h"
#include "Modules/ModuleManager.h"
#include "MoKuEditSplinesComponent.h"

void FMetaLandRoadLaneComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Component);
	if (!RoadSpline || !PDI)
	{
		return;
	}

	// Alt+E shows CompVis main/section only — skip lane LineBatch/PDI overlays.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
		&& (FMetaLandRoadModule::Get().IsSectionMode() || FMetaLandRoadModule::Get().IsLaneEditMode()))
	{
		return;
	}

	MetaLandRoadLaneVisualization::DrawLaneVisualization(RoadSpline, PDI);
}
