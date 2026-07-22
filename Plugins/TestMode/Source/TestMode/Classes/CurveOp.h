#pragma once

#include "MoKuEditorUtils.h"
#include "MoKuEditSplineActor.h"




class FCurvesRoadOp
{
public:
	FCurvesRoadOp(){};
	//const FCurvesRoadOp& operator=(const FCurvesRoadOp& CopyRoad);
	//const FCurvesRoadOp& operator=(FCurvesRoadOp&& MoveMesh);
	EIntersectionState CheckRoadIntersectionState(USplineComponent* InSplineComp, AMoKuEditBaseActor* InRoadActor);
	void GetIntersectionInfo(AMoKuEditIntersectionActor* JunctionActor, AMoKuEditSplineActor* RoadActor,float InstersctInputKey,TArray<FCornerInfo>& LinkCorners, TArray<FCornerInfo>& LinkSideEdge,TArray<FVector>& LinkOppositeEdge);



};