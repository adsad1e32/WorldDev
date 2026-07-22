#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplinesComponent.h"
#include "CompGeom/PolygonTriangulation.h"
#include "CurveOps/TriangulateCurvesOp.h"
#include "DynamicMesh/MeshNormals.h"
#include "MoKuEditSplineActor.h"
#include "Components/DynamicMeshComponent.h"
#include "MoKuEditorUtils.h"
#include "CurveOp.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"




#define LOCTEXT_NAMESPACE "MoKuEditIntersectionActor"


using namespace UE::Geometry;
AMoKuEditIntersectionActor::AMoKuEditIntersectionActor(const FObjectInitializer& ObjectInitializer)
:Super(ObjectInitializer)
{
	DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));
	SplineComp = CreateDefaultSubobject<UMoKuEditSplinesComponent>(TEXT("SplineComponent"));
	RootComponent = SplineComp;

}


void AMoKuEditIntersectionActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}

void AMoKuEditIntersectionActor::SetMeshTransform(const FTransform& InTransform)
{
	//if (StaticMeshAsset)
	//{
	//	StaticMeshAsset->SetWorldLocation(InTransform.GetLocation());
	//	StaticMeshAsset->SetWorldRotation(InTransform.GetRotation());
	//	StaticMeshAsset->SetWorldScale3D(InTransform.GetScale3D());
	//}

}
void AMoKuEditIntersectionActor::UpdateSplineCornerInfo(bool bIsRecreate)
{
	if (bIsRecreate)
	{
		CornerInfo.Empty();

		auto GetEdgeCenter = [](TArray<FVector3d> EdgePosition)->FVector
			{
				FVector Sum(ForceInit);
				for (auto& Pos : EdgePosition)
				{
					Sum += Pos;
				}
				return Sum /= double(EdgePosition.Num());
			};
		FVector CurActorLocation = this->GetActorLocation();
		TArray<FVector> PositionList;
		for (auto& ConnectActor : ConnectionActors)
		{
			TArray<FVector3d> StartRoadEdge = ConnectActor->JunctionConnections["Start"].ConnectEdge;
			FVector3d StartEdgeCenter = GetEdgeCenter(StartRoadEdge);
			TArray<FVector3d> EndRoadEdge = ConnectActor->JunctionConnections["End"].ConnectEdge;
			FVector3d EndEdgeCenter = GetEdgeCenter(EndRoadEdge);

			double D0 = FVector::Distance(StartEdgeCenter, CurActorLocation);
			double D1 = FVector::Distance(EndEdgeCenter, CurActorLocation);
			FCornerInfo LinkEdge;
			if (D0 < D1)
			{
				LinkEdge.CornerPoints = StartRoadEdge;
			}
			else
			{
				LinkEdge.CornerPoints = EndRoadEdge;
			}
			CornerInfo.Add(LinkEdge);
		}
	}
}
void AMoKuEditIntersectionActor::FillMeshActor(bool bIsRecreate)
{
	UpdateSplineCornerInfo(bIsRecreate);
	EdgePath.Empty();
	TArray<int32>CornerInfoList;
	int32 _Index = 0;


	for (auto& Info : CornerInfo)
	{
		//const FString  DisplayName = StaticEnum<ECornerTag>()->GetDisplayNameTextByValue((int64)Info.Tag).ToString();
		EdgePath.Append(Info.CornerPoints);
		TArray<int32> Ids;
		Ids.SetNum(Info.CornerPoints.Num());
		Ids.Init(_Index, Info.CornerPoints.Num());
		CornerInfoList.Append(Ids);
		_Index++;
	}


	FVector Sum;
	for (int i = 0; i < EdgePath.Num(); i++)
	{
		Sum += EdgePath[i];
	}
	Sum /= EdgePath.Num();

	auto SortEdgePathWithPolar = [&](TArray<FVector>& PositionList)
		{
			PositionList.Sort([&](const FVector3d& A, const FVector3d& B)
			{
				FVector CurActorLocation = Sum;
				FVector A0 = (A - CurActorLocation).GetSafeNormal();
				FVector B0 = (B - CurActorLocation).GetSafeNormal();
				double Value0 = FMath::Atan2(A0.Y, A0.X);
				double Value1 = FMath::Atan2(B0.Y, B0.X);
				return Value0 < Value1;
			});
		};


	TArray<int32> Indices;
	Indices.SetNum(EdgePath.Num());
	for (int32 i = 0; i < EdgePath.Num(); i++)
	{
		Indices[i] = i;
	}
	TArray<FVector> MarkList;
	Indices.Sort([&](const int32& A, const int32& B)
	{
		FVector CurActorLocation = Sum;
		FVector  LocationA = EdgePath[A];
		FVector  LocationB = EdgePath[B];
		FVector A0 = (LocationA - CurActorLocation).GetSafeNormal();
		FVector B0 = (LocationB - CurActorLocation).GetSafeNormal();
		double Value0 = FMath::Atan2(A0.Y, A0.X);
		double Value1 = FMath::Atan2(B0.Y, B0.X);
		if (abs(Value0 - Value1) < 0.001)
		{
			if (!MarkList.Contains(LocationA))
			{
				MarkList.Add(LocationA);
			}
			if (!MarkList.Contains(LocationB))
			{
				MarkList.Add(LocationB);
			}
		}
		return Value0<Value1;
	});

	TArray<int32> TmpCornerInfoList = CornerInfoList;
	TArray<FVector> TempEdgePath = EdgePath;
	EdgePath.Empty();
	CornerInfoList.Empty();
	for (const auto& idx :Indices)
	{
		EdgePath.Add(TempEdgePath[idx]);
		CornerInfoList.Add(TmpCornerInfoList[idx]);
	}

	TArray<FVector> List = EdgePath;
	for (int i=0; i< EdgePath.Num();i++)
	{
		FCornerInfo TmpCorner;
		int32 index = (i + 1) % EdgePath.Num();
		
		if (MarkList.Contains(EdgePath[i])|| MarkList.Contains(EdgePath[index]))
		{
			continue;
		}
		if (CornerInfoList[i] != CornerInfoList[index])
		{
			//if (FVector::Dist(EdgePath[i], EdgePath[index]) < 10)continue;
			TmpCorner.CornerPoints.Add(EdgePath[i]);
			TmpCorner.CornerPoints.Add(EdgePath[index]);
		}
		if(TmpCorner.CornerPoints.Num()>0)
		{
			FVector MidPosition = (TmpCorner.CornerPoints[0] + TmpCorner.CornerPoints[1])/2;
			FVector T = (TmpCorner.CornerPoints[1] - MidPosition).GetSafeNormal();
			FVector BitTangent = FVector::CrossProduct(T, FVector(0, 0, -1));
			FVector CurActorLocation = Sum;
			FVector Dir0 = (CurActorLocation - MidPosition).GetSafeNormal();
			if (FVector::DotProduct(Dir0, BitTangent) < 0)
			{
				BitTangent *= -1;
			}
			float Dist = FVector::Dist(TmpCorner.CornerPoints[0], TmpCorner.CornerPoints[1]);
			Dist = FMath::Clamp(Dist, 0, 600.0f);
			MidPosition += BitTangent * Dist;
			TmpCorner.CornerPoints.Insert(MidPosition, 1);
			TmpCorner.CornerPoints = MoKuSplineEditorUtils::BezierLineConvert(TmpCorner.CornerPoints,15);
			TmpCorner.Tag = ECornerTag::Corner;
			List.Append(TmpCorner.CornerPoints);
			CornerInfo.Add(TmpCorner);
		}
	}
	EdgePath = List;
	SortEdgePath();
}
void AMoKuEditIntersectionActor::SortEdgePath()
{
	FVector StartPos;
	TArray<FCornerInfo> Mark;
	EdgePath.Empty();
	FTransform TransformInfo = this->GetActorTransform();
	if (CornerInfo.Num() > 0)
	{
		if (CornerInfo[0].CornerPoints.Num() > 1)
		{
			StartPos = CornerInfo[0].CornerPoints[0];
		}
		FCornerInfo Temp = CornerInfo[0];

		while (Mark.Num()< CornerInfo.Num())
		{
			for(const FCornerInfo& item:CornerInfo)
			{
				if (item.CornerPoints.Num() == 0)break;
				if (Temp.CornerPoints == item.CornerPoints)continue;
				FVector P0 = item.CornerPoints[0];
				FVector P1 = item.CornerPoints.Last();
				float DistA = FVector::Dist(StartPos, P0);
				float DistB = FVector::Dist(StartPos, P1);
				if (DistA<=50)
				{
					StartPos = P1;
					Mark.Add(item);
					for (int i = 0; i < item.CornerPoints.Num(); i++)
					{
						if (!EdgePath.Contains(item.CornerPoints[i]))
						{
							EdgePath.Add(item.CornerPoints[i]);
						}
					}
					Temp = item;
				}
				else if(DistB<=50)
				{
					StartPos = P0;
					Mark.Add(item);
					for (int i = item.CornerPoints.Num()-1; i >=0; i--)
					{
						if (!EdgePath.Contains(item.CornerPoints[i]))
						{
							EdgePath.Add(item.CornerPoints[i]);
						}

					}
					Temp = item;
				}
			}
		}

	}



	//for (auto& Info : CornerInfo)
	//{
	//	//const FString  DisplayName = StaticEnum<ECornerTag>()->GetDisplayNameTextByValue((int64)Info.Tag).ToString();
	//	EdgePath.Append(Info.CornerPoints);
	//}

	TArray<FIndex3i> Triangles;
	TArray<int32> VertexIDs;

	TUniquePtr<FDynamicMesh3> Mesh = MakeUnique<FDynamicMesh3>();
	Mesh->EnableAttributes();
	Mesh->EnableTriangleGroups();
	Mesh->EnableVertexNormals(FVector3f::ZAxisVector);


	VertexIDs.SetNumUninitialized(EdgePath.Num());

	TArray<FVector3d> PosList;
	for (int32 i = 0; i < EdgePath.Num(); i++)
	{
		FVector3d  Result = TransformInfo.InverseTransformPosition(EdgePath[i]);
		VertexIDs[i] = Mesh->AppendVertex(EdgePath[i]);
		PosList.Add(EdgePath[i]);
	}

	int32 Count = 0;

	while (PosList.Num() > 3&&Count<5000)
	{
		int32 EarIndex = -1;
		double BestValue = -1;
		Count++;
		for (int32 i = 0; i < PosList.Num(); i++)
		{
			int32 PrevIndex = (i - 1 + PosList.Num()) % PosList.Num();
			int32 CurrentIndex = i;
			int32 NextIndex = (i+1)% PosList.Num();

			FVector3d PrevPos = PosList[PrevIndex];
			FVector3d CurrentPos = PosList[CurrentIndex];
			FVector3d NextPos = PosList[NextIndex];
			if (IsConvex(PrevPos, CurrentPos, NextPos))continue;
			double Radius = GetCircumRadiusValue(PrevPos, CurrentPos, NextPos);
			for (int32 j = 0; j < PosList.Num(); j++)
			{
				if (j == PrevIndex || j == CurrentIndex || j == NextIndex)continue;
				FVector3d P = PosList[j];
				if (PointInTriangle(P, PrevPos, CurrentPos, NextPos))continue;
				if (Radius > BestValue)
				{
					EarIndex = CurrentIndex;
					BestValue = Radius;
				}

			}

		}

		if (EarIndex > -1)
		{

			int32 Prev = (EarIndex - 1 + PosList.Num()) % PosList.Num();
			int32 Next = (EarIndex + 1) % PosList.Num();

			Triangles.Add(FIndex3i(VertexIDs[Prev], VertexIDs[EarIndex], VertexIDs[Next]));
			PosList.RemoveAt(EarIndex);
			VertexIDs.RemoveAt(EarIndex);
		}
	}
	if (VertexIDs.Num() == 3)
	{
		Triangles.Add(FIndex3i(VertexIDs[0], VertexIDs[1], VertexIDs[2]));
	}
	for (FIndex3i Tri : Triangles)
	{
		int32 Tid = Mesh->AppendTriangle(Tri);
	}
	FMeshNormals::InitializeMeshToPerTriangleNormals(Mesh.Get());
	if (Mesh->TriangleCount() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TriangulationFailed"));
	}
	UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh();
	if (DynamicMesh)
	{

		DynamicMesh->SetMesh(MoveTemp(*Mesh.Get()));
		//DynamicMeshComponent->SetWorldTransform(TransformInfo);
		DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		DynamicMeshComponent->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
		DynamicMeshComponent->bEnableComplexCollision = true;
		DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
		DynamicMeshComponent->UpdateCollision(false);
		DynamicMeshComponent->NotifyMeshUpdated();
	}
	//MeshBuilder();
}
bool AMoKuEditIntersectionActor::FindClosedLotsByConnector(TArray<TArray<FVector>>&OutConnectionPoints)
{
	for (const FCornerInfo& item : CornerInfo)
	{	
		TArray<FVector> Connections;
		//if (item.Tag == ECornerTag::Corner)
		{
			FVector StartConnection = item.CornerPoints[0];
			FVector EndConnection = item.CornerPoints.Last();
			          
			for (const AMoKuEditSplineActor* ConnectActor : ConnectionActors)
			{
				if (!ConnectActor->StartOfJunction.Get() || !ConnectActor->EndOfJunction.Get())continue;
				TMap<FString, TArray<FVector>> ConnectionList;
				if (ConnectActor->LeftCurve)
				{
					FVector StartLocation = ConnectActor->LeftCurve->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
					int Count = ConnectActor->LeftCurve->GetNumberOfSplinePoints();
					FVector EndLocation = ConnectActor->LeftCurve->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World);
					TArray<FVector> Collection({ StartLocation, EndLocation });
					ConnectionList.Add({ "0",Collection });
				}
				if (ConnectActor->RightCurve)
				{
					FVector StartLocation = ConnectActor->RightCurve->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
					int Count = ConnectActor->RightCurve->GetNumberOfSplinePoints();
					FVector EndLocation = ConnectActor->RightCurve->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World);
					TArray<FVector> Collection({ StartLocation, EndLocation });
					ConnectionList.Add({ "1",Collection });
				}
				TArray<FVector> CurvePoints;
				for (const auto& Pair : ConnectionList)
				{
					if (Pair.Key == "0")
					{
						for (auto& Ref : Pair.Value)
						{
							float DistA = FVector::Dist(Ref, StartConnection);
							//float DistB = FVector::Dist(Ref, EndConnection);
							if (DistA < 50.0f)
							{
								ConnectActor->LeftCurve->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 1.0f, CurvePoints);
								if (CurvePoints.Num() > 0)
								{
									for (const FVector& CurvePoint : item.CornerPoints)
									{
										CurvePoints.Add(CurvePoint);
									}
									OutConnectionPoints.Add(CurvePoints);
								}
							}
						}
					}

					if (Pair.Key == "1")
					{
						for (auto& Ref : Pair.Value)
						{
							float DistA = FVector::Dist(Ref, StartConnection);
							float DistB = FVector::Dist(Ref, EndConnection);
							if (DistA < 50.0f || DistB < 50.0f)
							{
								ConnectActor->RightCurve->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 1.0f, CurvePoints);
								if (CurvePoints.Num() > 0)
								{
									for (const FVector& CurvePoint : item.CornerPoints)
									{
										CurvePoints.Add(CurvePoint);
									}
									OutConnectionPoints.Add(CurvePoints);
								}
							}
						}
					}
					OutConnectionPoints.Add(Connections);
				}
			}
		}
	}

	if (OutConnectionPoints.Num() > 0)return true;

	return false;


}
void AMoKuEditIntersectionActor::MeshBuilder()
{
	FTriangulateCurvesOp TriangulateCurvesOp;
	TriangulateCurvesOp.Thickness = 0.0f;
	TriangulateCurvesOp.FlattenMethod = EFlattenCurveMethod::DoNotFlatten;
	TriangulateCurvesOp.CurveOffset = 0.0f;
	TriangulateCurvesOp.OffsetOpenMethod = EOffsetOpenCurvesMethod::TreatAsClosed;
	TriangulateCurvesOp.OffsetJoinMethod = EOffsetJoinMethod::Miter;
	TriangulateCurvesOp.OpenEndShape = EOpenCurveEndShapes::Square;
	TriangulateCurvesOp.MiterLimit =1.0;
	TriangulateCurvesOp.bFlipResult = false;
	TriangulateCurvesOp.OffsetClosedMethod = EOffsetClosedCurvesMethod::DoNotOffset;
	FTransform TransformInfo  = this->GetActorTransform();

	TriangulateCurvesOp.AddWorldCurve(EdgePath,true,TransformInfo);
	TriangulateCurvesOp.CalculateResult(nullptr);
	TUniquePtr<FDynamicMesh3>ResultMesh = TriangulateCurvesOp.ExtractResult();
	if (!ResultMesh || ResultMesh->TriangleCount() == 0)
	{
		UE_LOG(LogTemp,Warning,TEXT("TriangulationFailed"));
	}
	FDynamicMesh3* DynamicMeshPtr = ResultMesh.Release();
	UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh();


	int32 TriangleCount = DynamicMeshPtr->GetTrianglesRefCounts().GetCount();
	//int32 TriangleCount = UGeometryScriptLibrary_MeshQueryFunctions::GetNumTriangleIDs(DynamicMesh);

	for (int32 TriangleID= 0; TriangleID <TriangleCount; TriangleID++)
	{
		if (!DynamicMeshPtr->IsTriangle(TriangleID))continue;
		FVector3d Normal = DynamicMeshPtr->GetTriNormal(TriangleID);
		if (Normal.Z < 0)
		{
			FIndex3i Verts = DynamicMeshPtr->GetTriangle(TriangleID);
			DynamicMeshPtr->SetTriangle(TriangleID, FIndex3i(Verts.A, Verts.C, Verts.B));
		}
	}
	DynamicMesh->SetMesh(MoveTemp(*DynamicMeshPtr));
	DynamicMeshComponent->SetWorldTransform(TransformInfo);
	DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DynamicMeshComponent->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	DynamicMeshComponent->bEnableComplexCollision = true;
	DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
	DynamicMeshComponent->UpdateCollision(false);
	DynamicMeshComponent->NotifyMeshUpdated();
	
}


//void AMoKuEditIntersectionActor::SPawnGizmoInfo(const FVector& InRefVector)
//{
//	if (StaticMeshAsset)
//	{
//		GizmoTransformInfos.Empty();
//		TArray<FComponentSocketDescription> SocketDescriptions;
//		StaticMeshAsset->QuerySupportedSockets(SocketDescriptions);
//		FSocketGizmoTransformInfo GizmoTransformInfo;
//
//		for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
//		{
//			FName SocketName = SocketDesc.Name;
//			FTransform SocketTransform = StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
//			GizmoTransformInfo.Transform = SocketTransform;
//			FTransform LocalTransform = StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_Component);
//			GizmoTransformInfo.Tangent = LocalTransform.TransformVector(InRefVector);
//			GizmoTransformInfos.Add(GizmoTransformInfo);
//		}
//	}
//}


TArray<FSocketGizmoTransformInfo> AMoKuEditIntersectionActor::GetGizmoInfo()
{
	/*GizmoTransformInfos.Empty();
	if (StaticMeshAsset)
	{
		TArray<FComponentSocketDescription> SocketDescriptions;
		StaticMeshAsset->QuerySupportedSockets(SocketDescriptions);
		FSocketGizmoTransformInfo GizmoTransformInfo;
		for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
		{
			FName SocketName = SocketDesc.Name;
			FVector Tangent(1, 0, 0);
			FTransform SocketTransform = StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
			FVector SocketPosition = SocketTransform.GetLocation();
			FVector Direction = SocketTransform.TransformVector(Tangent);
			GizmoTransformInfo.Tangent = Direction;
			GizmoTransformInfo.Transform = SocketTransform;
			GizmoTransformInfos.Add(GizmoTransformInfo);
		}
		
	}*/
	return GizmoTransformInfos;
}
FEditSplineIntersectInfo AMoKuEditIntersectionActor::CheckIntersectionState(AMoKuEditBaseActor* InEditActor)
{
	FEditSplineIntersectInfo Result;
	if (!InEditActor)
	{
		return FEditSplineIntersectInfo();
	}
	if (AMoKuEditSplineActor* EditActor = Cast<AMoKuEditSplineActor>(InEditActor))
	{
		TArray<FVector> LeftIntersectionPoints;
		TArray<FVector> RightIntersectionPoints;
		int LeftCount = 0;
		int RightCount = 0;
		for (auto& Info : CornerInfo)
		{
			TObjectPtr<USplineComponent>NewSpline = NewObject<USplineComponent>();
			NewSpline->SetSplinePoints(Info.CornerPoints, ESplineCoordinateSpace::World);
			FVector LeftIntersectedPoint;
			bool IntersectedLeft = MoKuSplineEditorUtils::DoSplineIntersect(NewSpline, EditActor->LeftCurve, LeftIntersectedPoint);
			FVector RightIntersectedPoint;
			bool IntersectedRight = MoKuSplineEditorUtils::DoSplineIntersect(NewSpline, EditActor->RightCurve, RightIntersectedPoint);

			if (IntersectedLeft)
			{
				LeftIntersectionPoints.Add(LeftIntersectedPoint);
				LeftCount++;
			}
			if (IntersectedRight)
			{
				LeftIntersectionPoints.Add(RightIntersectedPoint);
				RightCount++;
			}
		}

		if (RightCount > 0 && LeftCount > 0)Result.State = EIntersectionState::Both;
		if (RightCount + LeftCount == 0)Result.State = EIntersectionState::None;
		Result.LeftIntersectPositions = LeftIntersectionPoints;
		Result.RightIntersectPositions = RightIntersectionPoints;
		return Result;
	}
	return Result;
}

void AMoKuEditIntersectionActor::UpdateJunctionInfo(class FCurvesRoadOp* InRoadOp)
{
	//InRoadOp
	//for (const auto& ConnectActor : ConnectionActors)
	//{



	//}

}

double AMoKuEditIntersectionActor::Cross2d(const FVector3d& A, const FVector3d& B)
{

	return A.X * B.Y - A.Y * B.X;
}

bool AMoKuEditIntersectionActor::PointInTriangle(const FVector& P,const FVector& A, const FVector& B, const FVector& C)
{

	double Threshold = 0.00001f;
	double R1 = Cross2d(B - A, P - A);
	double R2 = Cross2d(C - B, P - B);
	double R3 = Cross2d(A - C, P - C);

	return ((R1 >= -Threshold && R2 >= -Threshold && R3 >= -Threshold) || (R1 <= -Threshold && R2 <= -Threshold && R3 <= -Threshold));
}

double AMoKuEditIntersectionActor::GetCircumRadiusValue(const FVector3d& P1, const FVector3d& P2, const FVector3d& P3)
{
	double A = Distance(P1, P2);
	double B=  Distance(P3, P2);
	double C = Distance(P1, P3);

	double AreaValue = FMath::Abs(Cross2d(P2 - P1, P3 - P1));
	if (AreaValue < 0.000001f) return-1;
	double Radius = (A * B * C) / (2 * AreaValue + 0.000001f);

	return 1.0 / (Radius + 0.000001f);


}

bool AMoKuEditIntersectionActor::IsConvex(const FVector3d& A, const FVector3d& B, const FVector3d& C)
{
	FVector A1 = Normalized(B - A);
	FVector A2 = Normalized(C - A);
	FVector Value = Cross(A1, A2);
	return Dot(Value, FVector3d(0, 0, 1)) > 0;
}




#undef LOCTEXT_NAMESPACE