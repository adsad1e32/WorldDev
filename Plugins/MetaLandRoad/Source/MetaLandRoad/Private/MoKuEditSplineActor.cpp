#include "MoKuEditSplineActor.h"
#include "Kismet/KismetMathLibrary.h"
#include "UnrealEdGlobals.h"
#include "EditorModeManager.h"
#include "MetaLandRoadEditorMode.h"
#include "Tools/MetaLandRoadInteractiveTool.h"
#include "SocketMarkbleGizmo.h"
#include "UObject/UnrealTypePrivate.h"
#include "MoKuEditorUtils.h"
#include "LevelEditor.h"
#include "EdModeInteractiveToolsContext.h"
#include "MoKuEditSplinesComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "EditSplineRoadInfo.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/KismetRenderingLibrary.h"


using namespace UE::Geometry;
#include UE_INLINE_GENERATED_CPP_BY_NAME(MoKuEditSplineActor)
#define LOCTEXT_NAMESPACE "MoKuEditSplineActor"

AMoKuEditSplineActor::AMoKuEditSplineActor(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	SplineComponent = CreateDefaultSubobject<UMoKuEditSplinesComponent>(TEXT("MoKuEditSplinesComponent"));
	DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));

	//SurfaceComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::ExternallyProvided);

	if (SplineComponent)
	{
		SetRootComponent(SplineComponent);
		SplineComponent->SetSplinePointType(0, ESplinePointType::Linear);
		FVector FirstTangent = SplineComponent->GetTangentAtSplinePoint(0, ESplineCoordinateSpace::Local).GetSafeNormal();
		FVector Start = SplineComponent->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
		FVector EndPos = Start + FirstTangent * 10;
		SplineComponent->SetLocationAtSplinePoint(1, EndPos, ESplineCoordinateSpace::World);
		DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("MeshObjectResult"));

		StartAsset = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StartStaticMesh"));
		EndAsset = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndStaticMesh"));
		StartAsset->SetupAttachment(RootComponent);
		EndAsset->SetupAttachment(RootComponent);
		DynamicMeshComponent->SetupAttachment(RootComponent);

	}
	IsInit = true;
	WidgetHelper = CreateWidgetHelper();


}


//操作方式待优化
TArray<FVector> AMoKuEditSplineActor::UpdateInterpolateType(int32 PointIndex,int32 SegmentNum,float Radius)
{
	TArray<FVector> OutInterpolatePoints;
	if (SplineComponent)
	{
		if (SplineComponent->GetNumberOfSplineSegments() > 1)
		{
			//for (int i = PointIndex; i < SplineComponent->GetNumberOfSplinePoints(); i++)
			{

				int32 PrevPoint = PointIndex - 1;
				FVector PrevLocation = SplineComponent->GetLocationAtSplinePoint(PrevPoint, ESplineCoordinateSpace::World);
				//if (i == 1)
				//{
				//	OutInterpolatePoints.Add(PrevLocation);
				//}

				int32 NextPoint = PointIndex + 1;
				FVector NextLocation = SplineComponent->GetLocationAtSplinePoint(NextPoint, ESplineCoordinateSpace::World);

				FVector Location = SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);

				FVector InDir = (Location - PrevLocation).GetSafeNormal();
				FVector OutDir = (NextLocation - Location).GetSafeNormal();

				float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(InDir, OutDir)));

				if (Angle > 160.0 || Angle < 30.0f)
				{
					//OutInterpolatePoints.Add(Location);
					//OutInterpolatePoints;
					//continue;
					return OutInterpolatePoints;
				}

				FVector PointStartLocation = Location - InDir * Radius;
				FVector PointEndLocation = Location + OutDir * Radius;
				SplineComponent->RemoveSplinePoint(PointIndex);
				int32 Step = 0;
				for (int32 j= 0; j <= SegmentNum; j++)
				{
					Step++;
					float TValue = (float)j / (float)SegmentNum;
					float Invt = 1.0f - TValue;
					FVector InsertPoint = (Invt * Invt) * PointStartLocation + (2 * Invt * TValue) * Location + (TValue * TValue) * PointEndLocation;
					SplineComponent->AddSplinePointAtIndex(InsertPoint, Step + PrevPoint, ESplineCoordinateSpace::World);
					SplineComponent->SetSplinePointType(Step + PrevPoint, ESplinePointType::Linear);
					//SplineComponent->SetTangentAtSplinePoint(InsertPoint, Step + PrevPoint, ESplineCoordinateSpace::World);
					//OutInterpolatePoints.Add(InsertPoint);
				}
				SplineComponent->UpdateSpline();
				RefreshDynamicRoadMesh();
			}
		}
		//else
		//{
		//	for (int32 i = 0; i < SplineComponent->GetNumberOfSplinePoints(); i++)
		//	{
		//		OutInterpolatePoints.Add(SplineComponent->GetLocationAtSplinePoint(i,ESplineCoordinateSpace::Local));
		//	}

		//}
	}

	return OutInterpolatePoints;


}

TSharedRef<FLegacyEdModeWidgetHelper> AMoKuEditSplineActor::CreateWidgetHelper()
{
	if (WidgetHelper.IsValid())
	{
		return WidgetHelper.ToSharedRef();
	}
	return MakeShared<FLegacyEdModeWidgetHelper>();
}

void AMoKuEditSplineActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_EDITOR
	if (Ar.IsSaving()&& Ar.IsPersistent())
	{
		const int32 CurrentExtraMeshListCount = ExtraLocalMeshComponents.Num();
		int32 ListArrayNum = CurrentExtraMeshListCount;
		Ar << ListArrayNum;
		for (int i = 0; i < ListArrayNum; i++)
		{
			SerializeLocalMeshArray(Ar, ExtraLocalMeshComponents[i]);
			SerializeInterPointArray(Ar, ExtraLocalMeshInterPointList[i]);
		}
	}

	else if (Ar.IsLoading()&&Ar.IsPersistent())
	{
		ExtraLocalMeshComponents.Empty();
		ExtraLocalMeshInterPointList.Empty();
		int32 ListArrayNum = 0;
		Ar << ListArrayNum;
		for (int i = 0; i < ListArrayNum; i++)
		{
			TArray<UStaticMeshComponent*>ExtraLocalMesh;
			TArray<FMoKuSplineInterpPoint>ExtraMeshPointInfo;
			SerializeLocalMeshArray(Ar, ExtraLocalMesh);
			SerializeInterPointArray(Ar, ExtraMeshPointInfo);
			ExtraLocalMeshInterPointList.Add(ExtraMeshPointInfo);
			ExtraLocalMeshComponents.Add(ExtraLocalMesh);
		}
	}
#endif
}
void AMoKuEditSplineActor::SerializeLocalMeshArray(FArchive& Ar, TArray<UStaticMeshComponent*>& InExtraLocalMeshComponents)
{
	if (Ar.IsSaving())
	{
		int32 ArrayNum = InExtraLocalMeshComponents.Num();
		Ar << ArrayNum;
		for (int32 LocalMeshIndex = 0; LocalMeshIndex < ArrayNum; LocalMeshIndex++)
		{
			UStaticMeshComponent* LocalMesh = InExtraLocalMeshComponents[LocalMeshIndex];
			FName Name = LocalMesh->GetStaticMesh()->GetFName();
			if(LocalMesh)
			{
				LocalMesh->Serialize(Ar);
			}

		}
	}
	else if(Ar.IsLoading())
	{
		int32 ArrayNum = 0;
		Ar << ArrayNum;
		bool bIsSplineMesh = true;
		for (int i = 0; i <ArrayNum;i++)
		{		
			UStaticMeshComponent* Element = NewObject<UStaticMeshComponent>();
			Element->Serialize(Ar);
			InExtraLocalMeshComponents.Add(Element);
		}
	}
}
void AMoKuEditSplineActor::SerializeInterPointArray(FArchive& Ar, TArray<FMoKuSplineInterpPoint>& InExtraLocalMeshInterPointList)
{
	if (Ar.IsSaving())
	{
		int32 ArrayNum = InExtraLocalMeshInterPointList.Num();
		Ar << ArrayNum;
		for (int32 LocalMeshIndex = 0; LocalMeshIndex < ArrayNum; LocalMeshIndex++)
		{
			FMoKuSplineInterpPoint InterpPoint = InExtraLocalMeshInterPointList[LocalMeshIndex];
			Ar<<InterpPoint;
		}
	}
	else if (Ar.IsLoading())
	{
		int32 ArrayNum = 0;
		Ar << ArrayNum;

		for (int i = 0; i < ArrayNum; i++)
		{
			FMoKuSplineInterpPoint InterpPoint;
			Ar << InterpPoint;
			InExtraLocalMeshInterPointList.Add(InterpPoint);
		}
	}

}
void AMoKuEditSplineActor::SetSplineComponent(UMoKuEditSplinesComponent* InSplineComponent)
{
	SplineComponent = InSplineComponent;
	SplineComponent->UpdateSpline();
	//SplineComponent->DistributeSegmentToSpline();
	AddCapAssetToSpline();
	GetGizmoInfo();
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();

		if (LevelEditorPtr.IsValid())
		{
			UMetaLandRoadEditorMode* EditMode = Cast<UMetaLandRoadEditorMode>(LevelEditorPtr->GetEditorModeManager().GetActiveScriptableMode("EM_MetaLandRoadEditorMode"));
			UMetaLandRoadInteractiveTool* CurTool = Cast<UMetaLandRoadInteractiveTool>(EditMode->GetToolManager()->GetActiveTool(EToolSide::Left));
			TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>> GizmoInfo;
			EditMode->CreateActiveGizmos(this);
			GizmoInfo.Add(this, GizmoTransformInfos);
			TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>> GizmoInfos = CurTool->GetGizmoInfo();
			CurTool->UpdateGizmoInfo(GizmoInfo);
		}
	}
}
void AMoKuEditSplineActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitSceneManagement();
	//if (bSkipConstruction)
	//{
	//	return;
	//}

	//if (SplineComponent)
	//{
	//	//SplineComponent->UpdateSplineMesh();
	//}
	//if (SplineMeshInfoList.Num() == 0)SplineMeshInfoList.Add(FSplineMeshInfoDetails());

	//if (EnableExtraMesh)
	//{
	//	RefreshExtraMesh();
	//}
	//if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	//{
	//	TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();

	//	if (LevelEditorPtr.IsValid())
	//	{
	//		UMetaLandRoadEditorMode* EditMode = Cast<UMetaLandRoadEditorMode>(LevelEditorPtr->GetEditorModeManager().GetActiveScriptableMode("EM_MetaLandRoadEditorMode"));
	//		if (EditMode)
	//		{
	//			UMetaLandRoadInteractiveTool* CurTool = Cast<UMetaLandRoadInteractiveTool>(EditMode->GetToolManager()->GetActiveTool(EToolSide::Left));
	//			TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>> GizmoInfo;
	//			//EditMode->CreateActiveGizmos(this);
	//		}

	//	}
	//}
}

void AMoKuEditSplineActor::PostLoad()
{
	Super::PostLoad();
	//int ExtraMeshIndex = 0;
	//for (auto MeshComponents : ExtraLocalMeshComponents)
	//{
	//	int SubExtraMeshIndex = 0;
	//	for (auto MeshComponent : MeshComponents)
	//	{
	//		if (MeshComponent != nullptr)
	//		{
	//			TArray<FMoKuSplineInterpPoint>ExtraLocalMeshInterPoints = ExtraLocalMeshInterPointList[ExtraMeshIndex];
	//			UpdateExtraMesh(MeshComponents[SubExtraMeshIndex], ExtraLocalMeshInterPoints[SubExtraMeshIndex]);
	//			ExtraLocalMeshComponents[ExtraMeshIndex][SubExtraMeshIndex] = MeshComponents[SubExtraMeshIndex];
	//			SubExtraMeshIndex++;
	//		}

	//	}
	//	ExtraMeshIndex++;
	//}
	//if (SplineComponent)
	//{
	//	SplineComponent->UpdateSplineMesh();
	//}


}
void AMoKuEditSplineActor::AddCapAssetToSpline()
 {

	 int SPlinePoints = SplineComponent->GetNumberOfSplinePoints();
	 if (SplineComponent && SPlinePoints > 1)
	 {
		 if (StartAssetMesh.Mesh != nullptr&&EnableStartAssetMesh)
		 {
			 //测试代码未优化
			 FVector  StartAssetInitScale(1.0f, 1.0f, 1.0f);
			 FVector StartLocation = SplineComponent->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
			 FVector StartTangent = SplineComponent->GetTangentAtSplinePoint(0, ESplineCoordinateSpace::World);
			 StartAsset->SetStaticMesh(StartAssetMesh.Mesh);
			 StartAsset->SetWorldLocation(StartLocation);
			 StartAsset->SetWorldRotation((-StartTangent.GetSafeNormal()).Rotation());
			 if (StartAssetMesh.EnableFlipY&&StartAssetMesh.ForwardAxis == ESplineMeshAlign::Type::Y)
			 {
				 StartAssetInitScale.X *= -1;
			 }
			 if (StartAssetMesh.EnableFlipY && StartAssetMesh.ForwardAxis == ESplineMeshAlign::Type::X)
			 {
				 StartAssetInitScale.Y *= -1;
			 }

			 StartAsset->SetRelativeScale3D(StartAssetInitScale);
			 if(StartAssetMesh.ForwardAxis == ESplineMeshAlign::Type::Y)
			 {
				 FRotator Rotate = FRotator(0.0f, 270, 0.0f);
				 if (StartAssetMesh.RotateValue == 0)
				 {
					 Rotate = FRotator(0.0f, 90.0f, 0.0f);
				 }

				 StartAsset->SetWorldRotation((-StartTangent).Rotation()+ Rotate);


				 TArray<FComponentSocketDescription> SocketDescriptions;
				 StartAsset->QuerySupportedSockets(SocketDescriptions);

				 for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
				 {
					 FName SocketName = SocketDesc.Name;
					 FTransform SocketTransform = StartAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
					 float value = FVector::DotProduct((SocketTransform.GetLocation() - StartLocation).GetSafeNormal(), StartTangent.GetSafeNormal());

					if (value > 0.5)
					{
						StartAsset->SetWorldLocation(StartLocation - SocketTransform.GetLocation() + StartLocation);
						break;
					}
					else
					{
						StartAsset->SetWorldRotation((-StartTangent.GetSafeNormal()).Rotation());
					}
				 }
			 }
		 }
		 else 
		 {
			 StartAsset->SetStaticMesh(nullptr);
		 }

		 if (EndAssetMesh.Mesh != nullptr && EnableEndAssetMesh)
		 {
			 FVector EndAssetInitScale(1.0f, 1.0f, 1.0f);
			 FVector EndLocation = SplineComponent->GetLocationAtSplinePoint(SPlinePoints-1, ESplineCoordinateSpace::World);
			 FVector EndTangent = SplineComponent->GetTangentAtSplinePoint(SPlinePoints-1, ESplineCoordinateSpace::World);
			 EndAsset->SetStaticMesh(EndAssetMesh.Mesh);
			 EndAsset->SetWorldLocation(EndLocation);
			 EndAsset->SetWorldRotation(EndTangent.Rotation());
			 EndAsset->SetRelativeScale3D(EndAssetInitScale);
			 if (EndAssetMesh.EnableFlipY)
			 {
				 EndAssetInitScale.Y *= -1;
				 EndAsset->SetRelativeScale3D(EndAssetInitScale);
			 }
		 }
		 else
		 {
			 EndAsset->SetStaticMesh(nullptr);

		 }
	 }

}

void AMoKuEditSplineActor::AddGizmoInfoToSocket(const TObjectPtr<UStaticMeshComponent>& InAsset, TArray<FSocketGizmoTransformInfo>& OutGizmoTransform, bool EndMesh)
 {

	FSocketGizmoTransformInfo GizmoTransformInfo;
	if (!InAsset->GetStaticMesh())
	{
		if(SplineComponent)
		{
			if (!EndMesh)
			{
				GizmoTransformInfo.Transform = SplineComponent->GetTransformAtSplinePoint(0, ESplineCoordinateSpace::World);
				GizmoTransformInfo.Tangent = SplineComponent->GetTangentAtSplinePoint(0, ESplineCoordinateSpace::World);
				GizmoTransformInfo.IndexOfPoint = 0;
				GizmoTransformInfo.Tangent *= -1;
			}
			else
			{
				int32 PointIndex = SplineComponent->GetNumberOfSplinePoints() - 1;
				GizmoTransformInfo.Transform = SplineComponent->GetTransformAtSplinePoint(PointIndex,ESplineCoordinateSpace::World);
				GizmoTransformInfo.Tangent = SplineComponent->GetTangentAtSplinePoint(PointIndex,ESplineCoordinateSpace::World);
				GizmoTransformInfo.IndexOfPoint = PointIndex;

			}
			OutGizmoTransform.Add(GizmoTransformInfo);
		}
	}
	else
	{
		TArray<FComponentSocketDescription> SocketDescriptions;
		InAsset->QuerySupportedSockets(SocketDescriptions);

		for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
		{
			FName SocketName = SocketDesc.Name;
			FTransform SocketTransform = InAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
			GizmoTransformInfo.Transform = SocketTransform;
			FVector CurvelTangent;
			int PointIndex = SplineComponent->GetNumberOfSplinePoints() - 1;
			CurvelTangent = SplineComponent->GetTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
			if (!EndMesh)
			{
				PointIndex = 0;
				CurvelTangent = SplineComponent->GetTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
				CurvelTangent*=-1;
				
			}

			FTransform LocalTransform = InAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_Component);
			GizmoTransformInfo.Tangent = LocalTransform.TransformVector(CurvelTangent);
			OutGizmoTransform.Add(GizmoTransformInfo);
		}
	}
 }

void AMoKuEditSplineActor::RefreshExtraMesh()
{
	RemoveAllExtraMesh();
	ExtraLocalMeshInterPointList.Empty();
	for (int j = 0; j < ExtraSplineInfo.Num(); j++)
	{
		TArray<FMoKuSplineInterpPoint> ExtraAssetPoint = MoKuSplineEditorUtils::SplitSegmentToSpline(SplineComponent, ExtraSplineInfo[j].ExtraMesh, ExtraSplineInfo[j]);
		TArray<UStaticMeshComponent*> NewExtraLocalMeshComponents;
		MoKuSplineEditorUtils::UpdateSplineMesh(NewExtraLocalMeshComponents, ExtraAssetPoint, GetRootComponent());
		ExtraLocalMeshComponents.Add(NewExtraLocalMeshComponents);
		ExtraLocalMeshInterPointList.Add(ExtraAssetPoint);
	}
	
}

//UV还未设置
FDynamicMesh3* AMoKuEditSplineActor::GenerateRoadFromSplineAsDynamicMesh
(
	USplineComponent* InSplineComponent,
	float RoadWidth,
	float RoadHeightOffset ,
	bool bGenerateUVs)
{
	TUniquePtr<FDynamicMesh3> Mesh = MakeUnique<FDynamicMesh3>();
	Mesh->EnableAttributes();
	Mesh->EnableTriangleGroups();
	Mesh->EnableVertexNormals(FVector3f::ZAxisVector);
	Mesh->EnableVertexUVs(FVector2f());
	if (!InSplineComponent ||  RoadWidth <= 0 || InSplineComponent->GetNumberOfSplinePoints() < 1)
	{
		return Mesh.Release();
	}

	const float SplineLength = InSplineComponent->GetSplineLength();
	const float HalfWidth = RoadWidth / 2;

	TArray<FVector3d> Vertices;
	//TArray<FVector3f> Normals;
	TArray<FVector2f> UVs;
	TArray<int32> Triangles;
	SideCurvePaths.Empty();
	for (int32 i = 0; i < SplinesCache.Vertices.Num(); i++)
	{
		FVector Position = SplinesCache.Vertices[i];
		//FVector NextPosition = SplinesCache.Vertices[i+1];
		float Distance = InSplineComponent->GetDistanceAlongSplineAtLocation(Position, ESplineCoordinateSpace::Local);
		//FVector Tangent = (NextPosition - Position).GetSafeNormal();
		FVector Tangent  = InSplineComponent->GetTangentAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
		if (i == SplinesCache.Vertices.Num() - 1)
		{
			int32 LastIndex =  InSplineComponent->GetNumberOfSplinePoints() - 1;
			Tangent = SplineComponent->GetTangentAtSplinePoint(LastIndex, ESplineCoordinateSpace::Local);
			//FVector PrevPosition = SplinesCache.Vertices[i - 1];
			//Tangent = (Position - PrevPosition).GetSafeNormal();

		}
		FVector Right = FVector::CrossProduct(Tangent.GetSafeNormal(), FVector::UpVector);
		Right.Z = 0.0;
		//FVector Up = FVector::CrossProduct(Right, Tangent).GetSafeNormal();
		Position.Z += RoadHeightOffset;

		FVector RightPosition = Position + Right * HalfWidth;
		FVector LeftPosition = Position - Right * HalfWidth;
		Vertices.Add(RightPosition);
		Vertices.Add(LeftPosition);
		SideCurvePaths.RightCurvePath.Add(RightPosition + GetActorLocation());
		SideCurvePaths.LeftCurvePath.Add(LeftPosition + GetActorLocation());

		if (bGenerateUVs)
		{
			float V = Distance / SplineLength;
			UVs.Add(FVector2f(0.0f, V));
			UVs.Add(FVector2f(1500.0f, V));
		}
	}
	if (SideCurvePaths.RightCurvePath.Num()>1&& SideCurvePaths.LeftCurvePath.Num() > 1)
	{
		JunctionConnections["Start"].ConnectEdge.Add(SideCurvePaths.RightCurvePath[0]);
		JunctionConnections["Start"].ConnectEdge.Add(SideCurvePaths.LeftCurvePath[0]);
		FVector StartDir = InSplineComponent->GetTangentAtSplinePoint(0, ESplineCoordinateSpace::World).GetSafeNormal();
		JunctionConnections["Start"].ConnectDirection.Add(-StartDir);
		JunctionConnections["Start"].ConnectDirection.Add(-StartDir);

		JunctionConnections["End"].ConnectEdge.Add(SideCurvePaths.RightCurvePath.Last());
		JunctionConnections["End"].ConnectEdge.Add(SideCurvePaths.LeftCurvePath.Last());

		int32  SplineCount = InSplineComponent->GetNumberOfSplinePoints();
		FVector EndDir = InSplineComponent->GetTangentAtSplinePoint(SplineCount-1, ESplineCoordinateSpace::World).GetSafeNormal();
		JunctionConnections["End"].ConnectDirection.Add(EndDir);
		JunctionConnections["End"].ConnectDirection.Add(EndDir);
	}



	// 2. 生成三角形
	for (int32 i = 0; i < SplinesCache.Vertices.Num()-1; i++)
	{
		int32 V0 = i * 2;
		int32 V1 = i * 2 + 1;
		int32 V2 = (i + 1) * 2 + 1;
		int32 V3 = (i + 1) * 2;

		// 第一个三角形
		Triangles.Add(V0);
		Triangles.Add(V1);
		Triangles.Add(V2);

		// 第二个三角形
		Triangles.Add(V0);
		Triangles.Add(V2);
		Triangles.Add(V3);
	}

	// 3. 将数据填充到DynamicMesh
	TArray<int32> VertexIDs;
	VertexIDs.SetNumUninitialized(Vertices.Num());

	// 添加顶点
	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		VertexIDs[i] = Mesh->AppendVertex(Vertices[i]);
	}

	// 添加三角形
	for (int32 i = 0; i < Triangles.Num(); i += 3)
	{
		FIndex3i Tri(VertexIDs[Triangles[i]],
		VertexIDs[Triangles[i + 1]],
		VertexIDs[Triangles[i + 2]]);
		Mesh->AppendTriangle(Tri);
	}
	FMeshNormals::InitializeMeshToPerTriangleNormals(Mesh.Get());

	if (bGenerateUVs && UVs.Num() == Vertices.Num())
	{
			FDynamicMeshUVOverlay* UVOverlay = Mesh->HasAttributes() ?
			Mesh->Attributes()->PrimaryUV() : nullptr;

		if (UVOverlay)
		{
			for (int32 i = 0; i < VertexIDs.Num(); i++)
			{
				// 动态网格体 API 需要先 AppendElement 获取 ID，再 SetElement
				int32 NewElementID = UVOverlay->AppendElement(UVs[i]);
				UVOverlay->SetTriangle(Triangles[i / 3], FIndex3i(NewElementID, NewElementID, NewElementID)); // 注意：这里需要根据拓扑结构正确分配 UV ID，最简单的做法是重算 UV
			}
		}
	}
	return Mesh.Release();
}

//void AMoKuEditSplineActor::UpdateTrunkPathCache()
//{
//	if (SplineComponent)
//	{	
//		SplineComponent->ConvertSplineToPolyLine(ESplineCoordinateSpace::Type::Local,1.0, SplinesCache.Vertices);
//		SplinesCache.ComponentTransform = SplineComponent->GetComponentTransform();
//		SplinesCache.bClosed = SplineComponent->IsClosedLoop();		
//	}
//}


void AMoKuEditSplineActor::PostActorCreated()
{
	Super::PostActorCreated();

}
void AMoKuEditSplineActor::RefreshDynamicRoadMesh()
{
	if (SplineComponent)
	{
		//SplinesCache.Vertices = UpdateInterpolateType(1);
		bool bSuccess = SplineComponent->ConvertSplineToPolyLine(ESplineCoordinateSpace::Type::Local,120, SplinesCache.Vertices);
		// 创建动态网格
		if (DynamicMeshComponent)
		{
			UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh();
			DynamicMesh->Reset();
			if (bSuccess)
			{
				JunctionConnections["Start"].Empty();
				JunctionConnections["End"].Empty();
				FDynamicMesh3* ResultMesh = GenerateRoadFromSplineAsDynamicMesh(
					SplineComponent,
					2000.0f,
					10.0f,
					true);
				DynamicMesh->SetMesh(MoveTemp(*ResultMesh));
				
				RightCurve = BuildSideSpline(SideCurvePaths.RightCurvePath);
				LeftCurve =	BuildSideSpline(SideCurvePaths.LeftCurvePath);
				SplineComponent->ParentActor = this;
				RightCurve->ParentActor = this;
				LeftCurve->ParentActor = this;
				RightCurve->Tag = TEXT("Side");
				LeftCurve->Tag = TEXT("Side");

				DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				DynamicMeshComponent->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
				DynamicMeshComponent->bEnableComplexCollision = true; 
				DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
				DynamicMeshComponent->UpdateCollision(false);
				DynamicMeshComponent->NotifyMeshUpdated();
			}
		}
	}
}

//void AMoKuEditSplineActor::UpdateSceneManagementData()
//{
//	UMetaLandRoadEditorMode* CurrentEditMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId);
//
//	if (CurrentEditMode)
//	{
//		if (SplineComponent->GetNumberOfSplinePoints() > 1)
//		{
//			if (!SceneManagement->EditRoadsList.Contains(this))
//			{
//				SceneManagement->EditRoadsList.Add(this);
//			}
//		}
//	}
//}


void AMoKuEditSplineActor::Destroyed()
{
	Super::Destroyed();
	AWorldSceneManagement* Manager = SceneManagement.Get();
	if (Manager)
	{
		Manager->OctreeRemoveRoad(this);
	}
}

void AMoKuEditSplineActor::UpdateOctreeInfo()
{
	AWorldSceneManagement* Manager = SceneManagement.Get();
	if (Manager)
	{
	//	Manager->OctreeRemoveRoad(this);
	//	Manager->OctreeAddElement(LeftCurve);
	//	Manager->OctreeAddElement(RightCurve);
	//	SplineComponent->DistributeSegmentByDist();
	//	Manager->OctreeAddElement(SplineComponent);
	}
	
}

void AMoKuEditSplineActor::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
 {
	 Super::PostEditChangeChainProperty(PropertyChangedEvent);
	 //通过反射获取当前参数信息修改屏幕Gizmo显示
	 FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	 const FEditPropertyChain& PropertyChain = PropertyChangedEvent.PropertyChain;
	 FProperty* MemberGroup = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	 UMetaLandRoadEditorMode* CurrentEditMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId);
	 if (PropertyThatChanged != nullptr)
	 {

		 if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
		 {
			 if (!bIsDragging)
			 {
				 bIsDragging = true;
				 bSkipConstruction = true;
			 }
			 return;
		 }
		 else if (bIsDragging&&PropertyChangedEvent.ChangeType!=EPropertyChangeType::Interactive||PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
		 {
			 FName Category = *MemberGroup->GetMetaData(TEXT("Category"));
			 bIsDragging = false;
			 bSkipConstruction = false;

			 if (Category != TEXT("ExtraMesh"))
			 {
				 if (SplineComponent)
				 {

					 RefreshDynamicRoadMesh();
					 //SplineComponent->UpdateSplineMesh();
				 }

				AddCapAssetToSpline();
				OnInitGizmo().ExecuteIfBound(this);
				if (CurrentEditMode)
				{
					UMetaLandRoadInteractiveTool* CurTool = Cast<UMetaLandRoadInteractiveTool>(CurrentEditMode->GetToolManager()->GetActiveTool(EToolSide::Left));
					TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>> GizmoInfo;
					CurrentEditMode->CreateActiveGizmos(this);
					GizmoInfo.Add(this, GizmoTransformInfos);
				}		 
			 }
			 else
			 {

				 if (!SplineComponent)return;
				 int32 Index = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetName());
				 TArray<UStaticMesh*> StaticMeshArray;
				 TArray<UStaticMeshComponent*> NewExtraLocalMeshComponents;
				 if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
				 {
					 TArray<FMoKuSplineInterpPoint> ExtraAssetPoint = MoKuSplineEditorUtils::SplitSegmentToSpline(SplineComponent, ExtraSplineInfo[Index].ExtraMesh, ExtraSplineInfo[Index]);
					 MoKuSplineEditorUtils::UpdateSplineMesh(NewExtraLocalMeshComponents, ExtraAssetPoint, RootComponent);
					 ExtraLocalMeshComponents.Add(NewExtraLocalMeshComponents);
				 }
				 if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet && Index > -1)
				 {
					 RefreshExtraMeshViaIndex(Index);
				 }
				 else if (Index < 0)
				 {
					 if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(PropertyThatChanged))
					 {
						 if (BoolProperty->GetFName() == TEXT("EnableExtraMesh"))
						 {
							 bool Value = false;

							 PropertyThatChanged->CopyCompleteValue(&Value, PropertyThatChanged->ContainerPtrToValuePtr<bool>(this));
							 if (!Value)
							 {
								 RemoveAllExtraMesh();
							 }
							 else
							 {
								 RefreshExtraMesh();
							 }
						 }

					 }
				 }
			 }

		 }
		 
		// RenderActorBrush();
	 }

	 

 }

void AMoKuEditSplineActor::RefreshExtraMeshViaIndex(int Index)
{
	TArray<UStaticMeshComponent*> IndexOfExtraLocalMeseh =  ExtraLocalMeshComponents[Index];
	
	for (auto MeshComponent : IndexOfExtraLocalMeseh)
	{
		if (MeshComponent != nullptr)
		{
			MeshComponent->DestroyComponent();
		}

	}
	ExtraLocalMeshInterPointList[Index] = MoKuSplineEditorUtils::SplitSegmentToSpline(SplineComponent, ExtraSplineInfo[Index].ExtraMesh, ExtraSplineInfo[Index]);
	MoKuSplineEditorUtils::UpdateSplineMesh(ExtraLocalMeshComponents[Index], ExtraLocalMeshInterPointList[Index], GetRootComponent());
}

void AMoKuEditSplineActor::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)
{
	 Super::PostLoadSubobjects(OuterInstanceGraph);
}

void AMoKuEditSplineActor::InitGizmoSetting(TArray<FSocketGizmoTransformInfo>& OutGizmoTransform)
{
	//AddGizmoInfoToSocket(StartAsset,OutGizmoTransform);
	//AddGizmoInfoToSocket(EndAsset,OutGizmoTransform,true);
}


TArray<FSocketGizmoTransformInfo> AMoKuEditSplineActor::GetGizmoInfo()
{
	GizmoTransformInfos.Empty();
	InitGizmoSetting(GizmoTransformInfos);
	return GizmoTransformInfos;

}


void AMoKuEditSplineActor::PostEditUndo()
{
	Super::PostEditUndo();

}

void AMoKuEditSplineActor::RemoveAllExtraMesh()
{
	for (auto MeshComponents : ExtraLocalMeshComponents)
	{
		for (auto MeshComponent : MeshComponents)
		{
			if (MeshComponent != nullptr)
			{
				MeshComponent->DestroyComponent();
			}

		}
		MeshComponents.Empty();

	}
	ExtraLocalMeshComponents.Empty();
	
}

void AMoKuEditSplineActor::Tick(float DeltaTime)
{


}

FEditSplineIntersectInfo AMoKuEditSplineActor::CheckIntersectionState(AMoKuEditBaseActor* InEditActor)
{
	if (!InEditActor)
	{
		return FEditSplineIntersectInfo();
	}
	RefreshDynamicRoadMesh();
	FEditSplineIntersectInfo Result;

	TArray<FVector> LeftIntersectionPoints;
	TArray<FVector> RightIntersectionPoints;
	FVector LeftIntersectedPoint0;
	int LeftCount = 0;
	int RightCount = 0;
	if (AMoKuEditSplineActor* EditActor = Cast<AMoKuEditSplineActor>(InEditActor))
	{
		bool IntersectedLeft0 = MoKuSplineEditorUtils::DoSplineIntersect(LeftCurve, EditActor->LeftCurve, LeftIntersectedPoint0);
		if (IntersectedLeft0)
		{
			LeftIntersectionPoints.Add(LeftIntersectedPoint0);
			LeftCount++;
		}
		FVector LeftIntersectedPoint1;
		bool IntersectedLeft1 = MoKuSplineEditorUtils::DoSplineIntersect(LeftCurve, EditActor->RightCurve, LeftIntersectedPoint1);
		if (IntersectedLeft1)
		{
			LeftCount++;
			LeftIntersectionPoints.Add(LeftIntersectedPoint1);
		}
		FVector RightIntersectedPoint0;
		bool IntersectedRight0 = MoKuSplineEditorUtils::DoSplineIntersect(RightCurve, EditActor->RightCurve, RightIntersectedPoint0);
		if (IntersectedRight0)
		{
			RightCount++;
			RightIntersectionPoints.Add(RightIntersectedPoint0);
		}
		FVector RightIntersectedPoint1;
		bool IntersectedRight1 = MoKuSplineEditorUtils::DoSplineIntersect(RightCurve, EditActor->LeftCurve, RightIntersectedPoint1);
		if (IntersectedRight1)
		{
			RightCount++;
			RightIntersectionPoints.Add(RightIntersectedPoint1);
		}
		FVector IntersectPosition;
		bool Intersected = MoKuSplineEditorUtils::DoSplineIntersect(SplineComponent, EditActor->GetSplineComponent(), IntersectPosition);
		if (Intersected)
		{
			Result.IntersectPosition = IntersectPosition;
		}

		if (LeftCount > 0)Result.State = EIntersectionState::Left;
		if (RightCount > 0)Result.State = EIntersectionState::Right;

		if (RightCount > 0 && LeftCount > 0)Result.State = EIntersectionState::Both;
		if (RightCount + LeftCount == 0)Result.State = EIntersectionState::None;
		Result.LeftIntersectPositions = LeftIntersectionPoints;
		Result.RightIntersectPositions = RightIntersectionPoints;
		return Result;
	}
	if (AMoKuEditIntersectionActor* IntersectActor = Cast<AMoKuEditIntersectionActor>(InEditActor))
	{
		Result = IntersectActor->CheckIntersectionState(this);
		Result.IntersectPosition = IntersectActor->GetActorLocation();
		return Result;
	}
	return Result;
}


void AMoKuEditSplineActor::UpdateExtraMesh(UStaticMeshComponent*& InLocalMeshComponent, const FMoKuSplineInterpPoint& InSplineInterpPoint, const FSplineExtraMesh& InExtraMeshInfo)
{
	FVector StartTangent;
	FVector EndTangent;
	//FVector StartPos;
	//FVector EndPos;
	if (InLocalMeshComponent->GetStaticMesh())
	{
		if(InSplineInterpPoint.LayoutMethod== ESplineMeshLayoutMethod::Type::SplineMesh)
		{
			USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>();
			FVector2D SplineMeshScale2D(1.0f, 1.0f);
			if (InSplineInterpPoint.FlipModel == true)
			{
				SplineMeshScale2D.X *= -1;
			}
			if (InSplineInterpPoint.AlwaysFaceZ)
			{
				StartTangent.Z = 0;
				EndTangent.Z = 0;
			}

			SplineMesh->SetStaticMesh(InLocalMeshComponent->GetStaticMesh());
			SplineMesh->SetForwardAxis(ESplineMeshAxis::X, true);
			SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
			SplineMesh->SetMobility(EComponentMobility::Movable);
			SplineMesh->SetCollisionEnabled(ECollisionEnabled::Type::QueryAndPhysics);
			SplineMesh->SetStartAndEnd(InSplineInterpPoint.StartPos, InSplineInterpPoint.StartTangent, InSplineInterpPoint.EndPos, InSplineInterpPoint.EndTangent, true);
			SplineMesh->SetStartScale(SplineMeshScale2D);
			SplineMesh->SetEndScale(SplineMeshScale2D);
			SplineMesh->RegisterComponentWithWorld(GetWorld());
			SplineMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			SplineMesh->UpdateMesh();
			SplineMesh->Modify();
			SplineMesh->MarkRenderStateDirty();
			InLocalMeshComponent = Cast<UStaticMeshComponent>(SplineMesh);

		}
		else
		{
			InLocalMeshComponent->RegisterComponentWithWorld(GetWorld());
			InLocalMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			InLocalMeshComponent->MarkRenderStateDirty();

		}
	}
	
}

UMoKuEditSplinesComponent* AMoKuEditSplineActor:: BuildSideSpline(const TArray<FVector>& InPosition)
{
	UMoKuEditSplinesComponent* TmpCurve = NewObject<UMoKuEditSplinesComponent>();
	TmpCurve->ClearSplinePoints();

	// 添加新的样条点
	for (const auto& Point : InPosition)
	{
		TmpCurve->AddSplinePoint(Point, ESplineCoordinateSpace::World);
	}
	TmpCurve->UpdateSpline();
	TmpCurve->DistributeSegmentByDist();
	return TmpCurve;
}

//
//void AMoKuEditSplineActor::RenderActorBrush()
//{
//
//	if (DynamicMeshComponent)
//	{
//		//UKismetRenderingLibrary::ClearRenderTarget2D(this, HeightmapRenderRT, FLinearColor::Black);
//		UMaterialInterface* OriginalMat = DynamicMeshComponent->GetMaterial(0);
//		if (this->BrushMaterialBase)
//		{
//			if (!DrawBrushMID || DrawBrushMID->Parent != BrushMaterialBase)
//			{
//				DrawBrushMID = UMaterialInstanceDynamic::Create(BrushMaterialBase, this, FName("MyStandbyMID"));
//
//				//DrawBrushMID->SetScalarParameterValue(FName("WidthSize"), 1024);
//			}
//		}
//		if (DrawBrushMID)
//		{
//			DynamicMeshComponent->SetMaterial(0, DrawBrushMID);
//			if (this->SceneManagement.IsValid())
//			{
//
//				AWorldSceneManagement* SceneManager = SceneManagement.Get();
//				if (AWorldLandscapeBlueprintBrush* SeceneManagerBrush = Cast<AWorldLandscapeBlueprintBrush>(SceneManager))
//				{
//					if (!HeightmapRenderRT)
//					{
//						SceneManager->SceneCapture->CaptureScene();
//						//SceneManager->WeightmapRenderRT = WeightmapRenderRT;
//						HeightmapRenderRT = UKismetRenderingLibrary::CreateRenderTarget2D(
//							SceneManager->GetOwningLandscape(),
//							2048,
//							2048,
//							ETextureRenderTargetFormat::RTF_R16f
//						);
//					}
//					//if (HeightmapRenderRT)
//					//{
//					//	UKismetRenderingLibrary::ClearRenderTarget2D(this, HeightmapRenderRT, FLinearColor::Black);
//					//}
//					UKismetRenderingLibrary::DrawMaterialToRenderTarget(SceneManager->GetOwningLandscape(), HeightmapRenderRT, DrawBrushMID);
//				}
//
//
//			}
//		}
//	}
//
//
//}
//







#undef LOCTEXT_NAMESPACE