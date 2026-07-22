#include "ShapesGenerationTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "MoKuEditorUtils.h"
#include "Async/ParallelFor.h"
#include "GeomTools.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "RegionalPlanningActor.h"
#include "WorldEditorSettings.h"

#define LOCTEXT_NAMESPACE "UShapesGenerationTool"



UInteractiveTool* UShapesGenerationToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UShapesGenerationTool* NewTool = NewObject<UShapesGenerationTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	return NewTool;
}
void UShapesGenerationTool::SetWorld(UWorld* World)
{
	check(World);
	this->TargetWorld = World;
}


void UShapesGenerationTool::Setup()
{
	Super::Setup();
	if (!TargetWorld)return;
	UEdMode* ParentMode = GLevelEditorModeTools().GetActiveScriptableMode(UTestModeEditorMode::EM_TestModeEditorModeId);
	if (UTestModeEditorMode* ParentEdMode = Cast<UTestModeEditorMode>(ParentMode))
	{
		const UWorldEditorSettings* RoadEditorSettings = GetDefault<UWorldEditorSettings>();

		if (RoadEditorSettings)
		{

			TSubclassOf<AWorldLandscapeBlueprintBrush> WorldEditBrushClass = RoadEditorSettings->GetWorldEditorManagerClass();
			SceneManagement = Cast<AWorldSceneManagement>(WorldEditBrushClass->GetDefaultObject());
		}
	}
	//TArray<AActor*> JunctionsActorsList = MoKuSplineEditorUtils::GetActorListByFolderPath(FString(TEXT("Procedural_Scene/Scene_Junction")));
	//TArray<AActor*> RoadActorsList = MoKuSplineEditorUtils::GetActorListByFolderPath(FString(TEXT("Procedural_Scene/Scene_Road")));

	//TArray<USplineComponent*> CollectionSplines;
	//TArray<bool> StateList;

	//if (JunctionsActorsList.Num() > 0)
	//{
	//	if (RoadActorsList.Num() > 0)
	//	{
	//		for (auto& Actor : RoadActorsList)
	//		{
	//			AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Actor);

	//			if (RoadActor)
	//			{

	//				if (!RoadActor->StartOfJunction.IsValid() || !RoadActor->EndOfJunction.IsValid())
	//				{
	//					//FSideRoadCurvePaths SideCurvePaths = RoadActor->GetSideCurvePaths();
	//					////RoadActor->BuildSideSpline(SideCurvePaths.LeftCurvePath);
	//					////RoadActor->BuildSideSpline(SideCurvePaths.RightCurvePath);

	//					continue;

	//				}
	//				//FSideRoadCurvePaths SideCurvePaths = RoadActor->GetSideCurvePaths();
	//				//RoadActor->LeftCurve = RoadActor->BuildSideSpline(SideCurvePaths.LeftCurvePath);
	//				//RoadActor->RightCurve = RoadActor->BuildSideSpline(SideCurvePaths.RightCurvePath);

	//				RoadActor->RefreshDynamicRoadMesh();
	//				CollectionSplines.Add(RoadActor->LeftCurve);
	//				CollectionSplines.Add(RoadActor->RightCurve);

	//				StateList.Add(false);
	//				StateList.Add(false);
	//			}
	//		}
	//	}
	//	//TArray<TArray<FVector>> OutConnectionPoints;
	//	for (auto& Actor : JunctionsActorsList)
	//	{
	//		AMoKuEditIntersectionActor* JunctionActor = Cast<AMoKuEditIntersectionActor>(Actor);
	//		if (JunctionActor)
	//		{
	//			TArray<TObjectPtr<AMoKuEditSplineActor>>ConnectionActors = JunctionActor->ConnectionActors;
	//			TArray<FCornerInfo>CornerInfos = JunctionActor->CornerInfo;
	//			for (FCornerInfo Info : CornerInfos)
	//			{
	//				//if (Info.Tag != ECornerTag::Corner) continue;
	//				TArray<FVector> CornerPoints = Info.CornerPoints;
	//				//DebugPoints.Append(CornerPoints);
	//				USplineComponent* SplineComp = NewObject<USplineComponent>();
	//				SplineComp->ClearSplinePoints();
	//				for (int idx = 0;idx<CornerPoints.Num();idx++)
	//				{
	//					SplineComp->AddSplinePointAtIndex(CornerPoints[idx], idx,ESplineCoordinateSpace::World);
	//				}
	//				SplineComp->UpdateSpline();
	//				CollectionSplines.Add(SplineComp);
	//				StateList.Add(false);
	//			}
	//			//JunctionActor->FindClosedLotsByConnector(OutConnectionPoints);
	//		}

	//	}
	//	TArray<TArray<USplineComponent*>> MarkLists;
	//	FVector ReserchPos;
	//	for (int32 i = 0; i < CollectionSplines.Num(); i++)
	//	{
	//		if (i < 10)
	//		{

	//			for (int Idx = 0; Idx < CollectionSplines[i]->GetNumberOfSplinePoints(); Idx++)
	//			{
	//				FVector Position = CollectionSplines[i]->GetLocationAtSplinePoint(Idx, ESplineCoordinateSpace::World);
	//				//Segment.Add(Position);
	//				DebugPoints.Add(Position);
	//			}
	//			//DebugPoints.Append(SideCurvePaths.LeftCurvePath);
	//			//DebugPoints.Append(SideCurvePaths.RightCurvePath);
	//			
	//		}

	//		if (StateList[i])continue;
	//		bool FoundState = true;
	//		TArray<USplineComponent*> MarkList;
	//		int Count = CollectionSplines[i]->GetNumberOfSplinePoints();
	//		FVector StartOfPosition = CollectionSplines[i]->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	//		FVector EndOfPosition = CollectionSplines[i]->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World);
	//		ReserchPos = StartOfPosition;
	//		StateList[i] = true;
	//		int64 Iteration = 0;
	//		while(FoundState)
	//		{ 
	//			int64 Index = Iteration % (CollectionSplines.Num());
	//			Iteration++;
	//			if (Iteration > CollectionSplines.Num() - 1)
	//			{
	//				FoundState = false;
	//				break;
	//			}
	//			if (StateList[Index])
	//			{
	//				continue;
	//			}

	//			int Num = CollectionSplines[Index]->GetNumberOfSplinePoints();
	//			FVector RefStartOfPosition = CollectionSplines[Index]->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	//			FVector RefEndOfPosition = CollectionSplines[Index]->GetLocationAtSplinePoint(Num - 1, ESplineCoordinateSpace::World);
	//			//DebugPoints.Add(RefStartOfPosition);
	//			//DebugPoints.Add(RefEndOfPosition);
	//			float DistA = FVector::Distance(ReserchPos, RefStartOfPosition);
	//			float DistB = FVector::Distance(ReserchPos, RefEndOfPosition);
	//			if (DistA < 50.0f)
	//			{
	//				ReserchPos = RefEndOfPosition;
	//				StateList[Index] = true;
	//				FoundState = true;
	//				MarkList.Add(CollectionSplines[Index]);
	//				Iteration = 0;
	//				float DistC = FVector::Distance(ReserchPos, EndOfPosition);
	//				if(DistC<50.0f)
	//				{
	//					MarkList.Add(CollectionSplines[i]);
	//					break;
	//				}
	//			}
	//			else if (DistB < 50.0f)
	//			{
	//				ReserchPos = RefStartOfPosition;
	//				StateList[Index] = true;
	//				int LineCount  = CollectionSplines[Index]->GetNumberOfSplinePoints();
	//				MarkList.Add(CollectionSplines[Index]);
	//				FoundState = true;
	//				Iteration=0;
	//				float DistD = FVector::Distance(ReserchPos, EndOfPosition);
	//				if (DistD < 50.0f)
	//				{
	//					MarkList.Add(CollectionSplines[i]);
	//					break;
	//				}
	//			}
	//		}
	//		if (FoundState)
	//		{
	//			MarkLists.Add(MarkList);
	//		}
	//	}


	//	//目前还差多线程优化
	//	//TArray<TArray<TArray<FVector>>> AllSections;

	//	//ParallelFor(MarkLists.Num(), [&](int32 ListIndex)
	//	//{
	//	//	const auto& Lists = MarkLists[ListIndex];
	//	//	TArray<TArray<FVector>> Sections;
	//	//	for (const auto& Item : Lists)
	//	//	{
	//	//		TArray<FVector> Segment;
	//	//		const int32 NumPoints = Item->GetNumberOfSplinePoints();
	//	//		Segment.Reserve(NumPoints);

	//	//		for (int idx = 0; idx < NumPoints; idx++)
	//	//		{
	//	//			Segment.Add(Item->GetLocationAtSplinePoint(idx, ESplineCoordinateSpace::World));
	//	//		}
	//	//		Sections.Add(Segment);
	//	//	}

	//	//});

	//	for (const auto& Lists : MarkLists)
	//	{
	//		TArray<TArray<FVector>> Sections;
	//		for (const auto& Item : Lists)
	//		{
	//			TArray<FVector> Segment;
	//			for (int Idx = 0; Idx < Item->GetNumberOfSplinePoints(); Idx++)
	//			{
	//				FVector Position = Item->GetLocationAtSplinePoint(Idx, ESplineCoordinateSpace::World);
	//				Segment.Add(Position);
	//				//DebugPoints.Add(Position);
	//			}
	//			Sections.Add(Segment);
	//		}		
	//		TArray<FVector> VisualPart = MoKuSplineEditorUtils::SortEdgePathByList(Sections);
	//		FGridHighLightSelectInfo NewGridInfo;
	//		NewGridInfo.EdgePath = VisualPart;
	//		DebugDrawLine.Add(NewGridInfo);
	//	}
	//	HighlightMeshCompList.SetNumUninitialized(DebugDrawLine.Num());
	//	HighlightMeshCompList.Init(nullptr,DebugDrawLine.Num());
	//}

	SetUpBehaviors();
	OutputProperties = NewObject<UShapesGenerationSettings>(this);
	AddToolPropertySource(OutputProperties);
	TestVisual();
	OutputProperties->WatchProperty(OutputProperties->Shape,
		[this](auto){ TestVisual(); }
	);
}


void UShapesGenerationTool::TestVisual()
{
	DebugPoints.Empty();
	TArray<AActor*> JunctionsActorsList = MoKuSplineEditorUtils::GetActorListByFolderPath(FString(TEXT("Procedural_Scene/Scene_Junction")));
	TArray<AActor*> RoadActorsList = MoKuSplineEditorUtils::GetActorListByFolderPath(FString(TEXT("Procedural_Scene/Scene_Road")));

	TArray<USplineComponent*> CollectionSplines;
	TArray<bool> StateList;

	if (JunctionsActorsList.Num() > 0)
	{
		if (RoadActorsList.Num() > 0)
		{
			for (auto& Actor : RoadActorsList)
			{
				AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Actor);

				if (RoadActor)
				{

					if (!RoadActor->StartOfJunction.IsValid() || !RoadActor->EndOfJunction.IsValid())
					{
						continue;
					}
					RoadActor->RefreshDynamicRoadMesh();
					CollectionSplines.Add(RoadActor->RightCurve);
					CollectionSplines.Add(RoadActor->LeftCurve);		
					StateList.Add(false);
					StateList.Add(false);
				}
			}
		}
		for (auto& Actor : JunctionsActorsList)
		{
			AMoKuEditIntersectionActor* JunctionActor = Cast<AMoKuEditIntersectionActor>(Actor);
			if (JunctionActor)
			{
				TArray<TObjectPtr<AMoKuEditSplineActor>>ConnectionActors = JunctionActor->ConnectionActors;
				TArray<FCornerInfo>CornerInfos = JunctionActor->CornerInfo;
				for (FCornerInfo Info : CornerInfos)
				{
					if (Info.Tag != ECornerTag::Corner)
					{
						
						continue;
					}
					TArray<FVector> CornerPoints = Info.CornerPoints;
					///DebugPoints.Append(Info.CornerPoints);
					USplineComponent* SplineComp = NewObject<USplineComponent>();
					SplineComp->ClearSplinePoints();
					for (int idx = 0; idx < CornerPoints.Num(); idx++)
					{
						SplineComp->AddSplinePointAtIndex(CornerPoints[idx], idx, ESplineCoordinateSpace::World);
					}
					SplineComp->UpdateSpline();
					CollectionSplines.Add(SplineComp);
					StateList.Add(false);
				}
			}

		}
		TArray<TArray<USplineComponent*>> MarkLists;
		FVector ReserchPos;
		Collection = CollectionSplines;
		for (int32 i =0; i < CollectionSplines.Num(); i++)
		{
			if (StateList[i])continue;
			bool FoundState = true;
			TArray<USplineComponent*> MarkList;
			TMap<USplineComponent*, int32> MarkableList;
			int Count = CollectionSplines[i]->GetNumberOfSplinePoints();
			FVector StartOfPosition = CollectionSplines[i]->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
			FVector EndOfPosition = CollectionSplines[i]->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World);
			ReserchPos = StartOfPosition;
			StateList[i] = true;
			int64 Iteration = 0;
			int32 StepCount = 0;
 			while (FoundState)
			{
				int64 Index = Iteration % (CollectionSplines.Num());
				Iteration++;
				if (Iteration > CollectionSplines.Num())
				{
					
					FoundState = false;
					break;
				}
				if (StateList[Index])
				{
					continue;
				}

				int Num = CollectionSplines[Index]->GetNumberOfSplinePoints();
				FVector RefStartOfPosition = CollectionSplines[Index]->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
				FVector RefEndOfPosition = CollectionSplines[Index]->GetLocationAtSplinePoint(Num - 1, ESplineCoordinateSpace::World);
				float DistA = FVector::Distance(ReserchPos, RefStartOfPosition);
				float DistB = FVector::Distance(ReserchPos, RefEndOfPosition);


				if (DistA < 30.0f)
				{
					ReserchPos = RefEndOfPosition;
					StateList[Index] = true;
					FoundState = true;
					StepCount++;
					MarkList.Add(CollectionSplines[Index]);
					Iteration = 0;
					float DistC0 = FVector::Distance(ReserchPos, EndOfPosition);
					float DistC1 = FVector::Distance(ReserchPos, StartOfPosition);
					if (DistC0 < 30.0f|| DistC1 < 30.0f)
					{
						MarkList.Add(CollectionSplines[i]);
						break;
					}
				}
				else if (DistB < 30.0f)
				{
					ReserchPos = RefStartOfPosition;
					StateList[Index] = true;
					MarkList.Add(CollectionSplines[Index]);
					FoundState = true;
					Iteration = 0;
					float DistD0 = FVector::Distance(ReserchPos, StartOfPosition);
					float DistD1 = FVector::Distance(ReserchPos, EndOfPosition);

					if (DistD0 < 30.0f||DistD1 < 30.0f)
					{
						MarkList.Add(CollectionSplines[i]);
						break;
					}
				}
			}
			if (FoundState)
			{
				MarkLists.Add(MarkList);
			}
		}

		for (const auto& Lists : MarkLists)
		{
			TArray<TArray<FVector>> Sections;
			for (const auto& Item : Lists)
			{
				TArray<FVector> Segment;
				for (int Idx = 0; Idx < Item->GetNumberOfSplinePoints(); Idx++)
				{
					FVector Position = Item->GetLocationAtSplinePoint(Idx, ESplineCoordinateSpace::World);
					Segment.Add(Position);
					
				}
				Sections.Add(Segment);
			}
			TArray<FVector> VisualPart = MoKuSplineEditorUtils::SortEdgePathByList(Sections);
			FGridHighLightSelectInfo NewGridInfo;
			NewGridInfo.EdgePath = VisualPart;
			DebugDrawLine.Add(NewGridInfo);
		}
		HighlightMeshCompList.SetNumUninitialized(DebugDrawLine.Num());
		HighlightMeshCompList.Init(nullptr, DebugDrawLine.Num());
	}





}

void UShapesGenerationTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (SceneManagement)
	{
		for (auto& HighlightMeshComp : HighlightMeshCompList)
		{
			if (HighlightMeshComp)
			{
				if (HighlightMeshComp->GetOwner() == SceneManagement)
				{
					HighlightMeshComp->UnregisterComponent();
					HighlightMeshComp->DestroyComponent();
				}
			}
		}

	}


}
void UShapesGenerationTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

	//USplineComponent* Spline = Collection[OutputProperties->Shape];

	//for (int idx = 0; idx < Spline->GetNumberOfSplinePoints(); idx++)
	//{
	//	FVector Location = Spline->GetLocationAtSplinePoint(idx, ESplineCoordinateSpace::World);
	//	PDI->DrawPoint(
	//		Location,
	//		FLinearColor(0.0f, 1.0f, 0.0f, 1.0f),
	//		5.0f,
	//		SDPG_Foreground);


	//}
	for (auto Vertex : DebugPoints)
	{
			PDI->DrawPoint(
				Vertex,
				FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
				5.0f,
				SDPG_Foreground);
	}
	for (auto& Section : DebugDrawLine)
	{
		for (int32 i = 0; i < Section.EdgePath.Num() -1; ++i)
		{
			//FVector Location = Comp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
			//FVector Tangent = Comp->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
			FVector S = Section.EdgePath[i];
			FVector F = Section.EdgePath[i + 1];
			PDI->DrawLine(S, F, FLinearColor(0.0f, 0.0f, 1.0f, 0.5f), SDPG_Foreground, 35.0f);
			//PDI->DrawPoint(
			//	Section[i],
			//	FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
			//	5.0f,
			//	SDPG_Foreground);
		}
	}
}

void UShapesGenerationTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{

}


bool UShapesGenerationTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{

	int Index = -1;
	HoverIndex = -1;
	for (auto& Section : DebugDrawLine)
	{
		Index++;
		if (DetectClosedSpline(Section.EdgePath, DevicePos.ScreenPosition))
		{
			HoverIndex = Index;
			if (PrevHoverIndex == HoverIndex)continue;
			if (!Section.bIsHovered&&!Section.bIsSelected)
			{
				UDynamicMeshComponent* HighlightMeshComp = MoKuSplineEditorUtils::SplineMeshBuilder(Section.EdgePath);
				if (!HighlightMeshComp)
				{
					return false;
				}
				FDynamicMesh3* DynamicMeshPtr = HighlightMeshComp->GetDynamicMesh()->GetMeshPtr();
				FDynamicMesh3 MeshCopy = *DynamicMeshPtr;
				HighlightMeshComp->Rename(nullptr, SceneManagement);
				HighlightMeshComp->CreationMethod = EComponentCreationMethod::Instance;
				HighlightMeshComp->AttachToComponent(SceneManagement->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
				HighlightMeshComp->RegisterComponent();
				HighlightMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				HighlightMeshComp->SetMaterial(0, MaterialStateInfo.HoveredMaterial);
				HighlightMeshComp->SetVisibility(true);
				Section.bIsHovered = true;

				HighlightMeshComp->NotifyMeshUpdated();
				HighlightMeshCompList[HoverIndex] = HighlightMeshComp;
				SceneManagement->AddInstanceComponent(HighlightMeshComp);
			}
			else
			{
				HighlightMeshCompList[Index]->SetMaterial(0, MaterialStateInfo.HoveredMaterial);
				HighlightMeshCompList[Index]->NotifyMeshUpdated();
			}

		}
		else
		{

			if (PrevHoverIndex == -1) continue;
			if (HighlightMeshCompList[PrevHoverIndex])
			{
				if (Section.bIsSelected)
				{
					Section.bIsHovered = false;
					HighlightMeshCompList[Index]->SetMaterial(0, MaterialStateInfo.SelectedMaterial);
					HighlightMeshCompList[Index]->NotifyMeshUpdated();
				}
			}
			else
			{
				HighlightMeshCompList[Index]->SetMaterial(0, MaterialStateInfo.SelectedMaterial);
				HighlightMeshCompList[Index]->NotifyMeshUpdated();
			}

		}
	}
	PrevHoverIndex = HoverIndex;
	if (SceneManagement)
	{
		for (int i = 0; i < HighlightMeshCompList.Num(); i++)
		{
			if (HighlightMeshCompList[i])
			{
				if (i != HoverIndex&&!DebugDrawLine[i].bIsSelected&&!DebugDrawLine[i].bIsCreated)
				{				
					HighlightMeshCompList[i]->UnregisterComponent();
					HighlightMeshCompList[i]->DestroyComponent();
					HighlightMeshCompList[i]->SetVisibility(false);

					HighlightMeshCompList[i] = nullptr;
					DebugDrawLine[i].bIsHovered = false;
					DebugDrawLine[i].bIsSelected = false;
				}
				else if(HoverIndex == -1)
				{
					if (!DebugDrawLine[i].bIsSelected&&!DebugDrawLine[i].bIsCreated)
					{
						HighlightMeshCompList[i]->SetVisibility(false);

						HighlightMeshCompList[i]->UnregisterComponent();
						HighlightMeshCompList[i]->DestroyComponent();
						HighlightMeshCompList[i] = nullptr;
						//HighlightMeshCompList.RemoveAt(i);

						//CleanRegionalState();
						DebugDrawLine[i].bIsHovered = false;
						DebugDrawLine[i].bIsSelected = false;
					}
				}
			}
		}
		SceneManagement->Modify();
	//	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("SceneManagementCompCount==%i"), SceneManagement->GetInstanceComponents().Num()));
	}

	return true;
}
void UShapesGenerationTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);
}
FInputRayHit UShapesGenerationTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, PressPos.WorldRay.Origin, PressPos.WorldRay.PointAt(HALF_WORLD_MAX), QueryParams);
	if (bHitWorld)
	{
		return FInputRayHit(Result.Distance);
	}
	return FInputRayHit();

}
FInputRayHit UShapesGenerationTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	return BeginHoverSequenceHitTest(PressPos);
}

bool UShapesGenerationTool::DetectClosedSpline(const TArray<FVector>&InEdgePath, const FVector2D& MouseViewportPos)
{
	FViewportClient* Viewport = GetToolManager()->GetContextQueriesAPI()->GetHoveredViewport()->GetClient();
	if (!Viewport)return false;
	FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(Viewport);
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags)
	);
	FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
	if (!SceneView)return false;
	TArray<FVector2D> ScreenPoints;
	for (const auto& WorldPos : InEdgePath)
	{
		FVector2D ScreenPos;
		if (SceneView->WorldToPixel(WorldPos, ScreenPos))
		{
			ScreenPoints.Add(ScreenPos);
		}
	}
	bool bInPolygon = FGeomTools2D::IsPointInPolygon(MouseViewportPos, ScreenPoints);

	return bInPolygon && ScreenPoints.Num() >= 3;

}
void UShapesGenerationTool::SetUpBehaviors()
{
	UClickDragInputBehavior* MouseBehavior = NewObject<UClickDragInputBehavior>();
	UMouseHoverBehavior* HoverMouseBehavior = NewObject<UMouseHoverBehavior>();
	URegionalShapeCreatorBehavior* RegionalShapeBehavior = NewObject<URegionalShapeCreatorBehavior>();
	MouseBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	MouseBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	MouseBehavior->Initialize(this);
	HoverMouseBehavior->Initialize(this);
	RegionalShapeBehavior->Initialize(this);
	AddInputBehavior(RegionalShapeBehavior);
	AddInputBehavior(MouseBehavior);
	AddInputBehavior(HoverMouseBehavior);
}

void UShapesGenerationTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == CtrlModifierID)
	{
		bCtrlModifierOn = bIsOn;
	}
	if (ModifierID == ShiftModifierID)
	{
		bShiftModifierOn = bIsOn;
	}
}
void UShapesGenerationTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	FScopedTransaction Transaction(LOCTEXT("Selelct Area To Lots", "Selelct Area To Lots"));
	if (bShiftModifierOn)
	{
		if (SceneManagement)
		{ 
			if (HoverIndex > -1)
			{		
				if (DebugDrawLine[HoverIndex].bIsHovered&&!DebugDrawLine[HoverIndex].bIsSelected)
				{
					HighlightMeshCompList[HoverIndex]->SetMaterial(0, MaterialStateInfo.SelectedMaterial);
					DebugDrawLine[HoverIndex].bIsSelected = true;
				}
				else if(DebugDrawLine[HoverIndex].bIsSelected)
				{
					HighlightMeshCompList[HoverIndex]->SetMaterial(0, MaterialStateInfo.HoveredMaterial);
					DebugDrawLine[HoverIndex].bIsSelected = false;
					DebugDrawLine[HoverIndex].bIsHovered = true;
				}	
			}
			else
			{
				int32 Index = -1;
				for (auto& Section : DebugDrawLine)
				{
					Index++;
					Section.bIsSelected = false;
					Section.bIsHovered = false;
				}
			}
		}
	}
	Modify();
}
void UShapesGenerationTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{

	GEditor->EndTransaction();

}

void UShapesGenerationTool::CreateRegionalShape()
{
	int32 Index = -1;
	for (auto& Section : DebugDrawLine)
	{
		Index++;
		if (Section.bIsSelected)
		{
			if (HighlightMeshCompList[Index])
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = this->TargetWorld->PersistentLevel;
				SpawnParams.bNoFail = true;
				SpawnParams.ObjectFlags |= RF_Transactional;
				FTransform CompTransform = HighlightMeshCompList[Index]->GetComponentTransform();
				
				ARegionalPlanningActor* RegionalPlanningActor = this->TargetWorld->SpawnActor<ARegionalPlanningActor>(CompTransform.GetLocation(),CompTransform.GetRotation().Rotator(), SpawnParams);
				if (RegionalPlanningActor)
				{
					RegionalPlanningActor->SetFolderPath(SCENE_LOT_PATH);
					UDynamicMesh* DynamicMeshData = HighlightMeshCompList[Index]->GetDynamicMesh();

					RegionalPlanningActor->RegionalGrid->SetDynamicMesh(MoveTemp(DynamicMeshData));
					Section.bIsCreated = true;
					Section.bIsSelected = false;
					Section.bIsHovered = false;
					HighlightMeshCompList[Index]->UnregisterComponent();
					HighlightMeshCompList[Index]->DestroyComponent();
					HighlightMeshCompList[Index] = nullptr;
					RegionalPlanningActor->RegionalGrid->SetMaterial(0, nullptr);
				}
			}

		}

	}
}

void URegionalShapeCreatorBehavior::Initialize(UShapesGenerationTool* InRegionalShapeTool)
{
	check(InRegionalShapeTool != nullptr);
	this->RegionalShapeTool = InRegionalShapeTool;
}
FInputCaptureRequest URegionalShapeCreatorBehavior::WantsCapture(const FInputDeviceState& Input)
{
	FDeviceButtonState ActiveKey = Input.Keyboard.ActiveKey;

	if (ActiveKey.Button == EKeys::B && ActiveKey.bPressed == true)
	{
		RegionalShapeTool->CreateRegionalShape();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);

	}
	return FInputCaptureRequest::Ignore();
}
FInputCaptureUpdate URegionalShapeCreatorBehavior::UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& data)
{
	
	return FInputCaptureUpdate::Ignore();
}
FInputCaptureUpdate URegionalShapeCreatorBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	return FInputCaptureUpdate::Ignore();
}
void URegionalShapeCreatorBehavior::ForceEndCapture(const FInputCaptureData& data)
{

}




#undef LOCTEXT_NAMESPACE