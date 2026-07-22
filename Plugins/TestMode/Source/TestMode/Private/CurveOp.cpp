#include "CurveOp.h"


EIntersectionState FCurvesRoadOp::CheckRoadIntersectionState(USplineComponent* InSplineComp, AMoKuEditBaseActor* InRoadActor)
{

	if (!InSplineComp || !InRoadActor)
	{
		return EIntersectionState::None;
	}
	if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(InRoadActor))
	{
		USplineComponent* LeftCurve = RoadActor->LeftCurve;
		USplineComponent* RightCurve = RoadActor->RightCurve;
		FVector LeftIntersectionPoint;
		FVector RightIntersectionPoint;
		bool IntersectedLeft = MoKuSplineEditorUtils::DoSplineIntersect(LeftCurve, InSplineComp, LeftIntersectionPoint);
		bool IntersectedRight = MoKuSplineEditorUtils::DoSplineIntersect(RightCurve, InSplineComp, RightIntersectionPoint);
		if (IntersectedLeft && IntersectedRight)
		{
			return EIntersectionState::Both;
		}
		if (IntersectedLeft && !IntersectedRight)
		{
			return EIntersectionState::Left;
		}
		if (!IntersectedLeft && IntersectedRight)
		{
			return EIntersectionState::Right;
		}
		return EIntersectionState::None;
	}
	else if(AMoKuEditIntersectionActor* JunctionActor = Cast<AMoKuEditIntersectionActor>(InRoadActor))
	{
		return EIntersectionState::Both;
	}


	return EIntersectionState::None;


}

void FCurvesRoadOp::GetIntersectionInfo(AMoKuEditIntersectionActor* JunctionActor,
	AMoKuEditSplineActor* RoadActor,
	float InstersectInputKey,
	TArray<FCornerInfo>& LinkCorners,
	TArray<FCornerInfo>& LinkSideEdge,
	TArray<FVector>& LinkOppositeEdge)
{
	if (!JunctionActor || !RoadActor)return;

	TArray<FVector> RoadEdge;
	FCornerInfo SelfLinkEdge;
	if (InstersectInputKey < 0.5)
	{
		RoadEdge = RoadActor->JunctionConnections["Start"].ConnectEdge;
	}
	else
	{
		RoadEdge = RoadActor->JunctionConnections["End"].ConnectEdge;
	}
	SelfLinkEdge.CornerPoints = RoadEdge;
	auto GetEdgeCenter = [](TArray<FVector3d> EdgePosition)->FVector
		{
			FVector Sum(ForceInit);
			for (auto& Pos : EdgePosition)
			{
				Sum += Pos;
			}
			return Sum /= double(EdgePosition.Num());
		};
	FVector SelfCenter = GetEdgeCenter(SelfLinkEdge.CornerPoints);
	TArray<FCornerInfo> ConnectedEdges;
	for (const auto& ConnectActor : JunctionActor->ConnectionActors)
	{
		float PrevDist = 1000000;
		FCornerInfo ConnectedEdge;
		FVector MarkPosition;
		for (auto&& Pair : ConnectActor->JunctionConnections)
		{
			FConnectionInfo Connection = Pair.Value;
			FVector ConnectedCenter = GetEdgeCenter(Connection.ConnectEdge);
			float CurrentDist = FVector::Dist(ConnectedCenter, SelfCenter);
			if (CurrentDist < PrevDist)
			{
				PrevDist = CurrentDist;
				ConnectedEdge.CornerPoints = Connection.ConnectEdge;
				float Dist0 = FVector::Dist(ConnectedEdge.CornerPoints[0], SelfCenter);
				float Dist1 = FVector::Dist(ConnectedEdge.CornerPoints[1], SelfCenter);
				if (Dist0 > Dist1)
				{
					MarkPosition = ConnectedEdge.CornerPoints[0];
				}
				else
				{
					MarkPosition = ConnectedEdge.CornerPoints[1];
				}
			}
		}
		
		LinkOppositeEdge.Add(MarkPosition);
		ConnectedEdges.Add(ConnectedEdge);

	}
	LinkSideEdge = ConnectedEdges;
	LinkSideEdge.Add(SelfLinkEdge);
}
