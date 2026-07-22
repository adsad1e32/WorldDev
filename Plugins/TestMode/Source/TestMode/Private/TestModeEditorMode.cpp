// Copyright Epic Games, Inc. All Rights Reserved.
#include "TestModeEditorMode.h"
#include "TestModeEditorModeToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "TestModeEditorModeCommands.h"
#include "EngineUtils.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "UnrealWidget.h"
#include "Tools/TestModeInteractiveTool.h"
#include "InputCoreTypes.h"
#include "SocketMarkbleGizmo.h"
#include "MoKuEditorGizmoBase.h"
#include "Selection.h"
#include "MoKuEditBaseActor.h"
#include "Tools/ShapesGenerationTool.h"
#include "MoKuEditorUtils.h"
#include "EditSplineRoadInfo.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "WorldEditorSettings.h"
#include "WorldBrushEditorManagerFactory.h"


#define LOCTEXT_NAMESPACE "TestModeEditorMode"

const FEditorModeID UTestModeEditorMode::EM_TestModeEditorModeId = TEXT("EM_TestModeEditorMode");
FString UTestModeEditorMode::InteractiveToolName = SPLINETOOLNAME;
FString UTestModeEditorMode::ShapesGenerationName = SHAPESTOOLNAME;

UTestModeEditorMode::UTestModeEditorMode()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	// appearance and icon in the editing mode ribbon can be customized here
	Info = FEditorModeInfo(UTestModeEditorMode::EM_TestModeEditorModeId,
		LOCTEXT("ModeName", "World Builder"),
		FSlateIcon(),
		true);
}


UTestModeEditorMode::~UTestModeEditorMode()
{
}

UTestModeEditorMode::UTestModeEditorMode(FVTableHelper& Helper)
	: UBaseLegacyWidgetEdMode(Helper)
{
	
}


void UTestModeEditorMode::ActorSelectionChangeNotify()
{
	UpdateAssetGizmoImpl();
}

void UTestModeEditorMode::Enter()
{
	Super::Enter();
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(TEXT("SocketGizmo"), NewObject<USocketMarkbleGizmoBuilder>());
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(TEXT("AssetMark"), NewObject<UMoKuEditRePlaceGizmoBuilder>());
	const FTestModeEditorModeCommands& SampleToolCommands = FTestModeEditorModeCommands::Get();
	RegisterTool(SampleToolCommands.InteractiveTool, InteractiveToolName, NewObject<UTestModeInteractiveToolBuilder>(this));
	RegisterTool(SampleToolCommands.ShapesTool, ShapesGenerationName, NewObject<UShapesGenerationToolBuilder>(this));


	if (GEditor)
	{
		GEditor->SelectNone(true, true);
		GEditor->OnLevelActorDeleted().AddUObject(this, &UTestModeEditorMode::RefreshGizmoVisual);
		//GEditor->OnLevelActorAdded().AddUObject(this, &UTestModeEditorMode::OnLevelActorAddedToWorld);
	}
	return;

	if (MoKuSplineEditorUtils::CreateSceneStructure())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			TArray<AActor*>ActorList = MoKuSplineEditorUtils::GetActorListByFolderPath("Procedural_Scene");
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = World->PersistentLevel;
			SpawnParams.bNoFail = true;
			SpawnParams.ObjectFlags |= RF_Transactional;
			if (ActorList.Num() == 0)
			{
				SceneManagement = World->SpawnActor<AWorldSceneManagement>(FVector(0, 0, 0), FRotator(0.0f, 0.0f, 0.0f), SpawnParams);
			}
			else
			{
				int Index = 0;
				for (auto& ChildActor : ActorList)
				{
					if (SceneManagement = Cast<AWorldSceneManagement>(ChildActor))
					{
						break;
					}

				}
			}
			if (SceneManagement)
			{

				const UWorldEditorSettings* WorldEditorSettings = GetDefault<UWorldEditorSettings>();
				check(WorldEditorSettings != nullptr);
				AWorldLandscapeBlueprintBrush* WorldEditBlueprintBrush = Cast<AWorldLandscapeBlueprintBrush>(SceneManagement);
				if (WorldEditBlueprintBrush)
				{
					UWorld* ActorWorld = WorldEditBlueprintBrush->GetWorld();

					if (ActorWorld)
					{
						FBox WaterZoneBounds(ForceInit);
						TArray<ALandscape*> FoundLandscapes;
						bool bFoundIntersectingLandscape = false;

						const bool bNonColliding = true;
						const bool bIncludeChildActors = false;
						const FBox ActorBounds = SceneManagement->GetComponentsBoundingBox(bNonColliding, bIncludeChildActors);

						ALandscape* FallbackLandscape = nullptr;
						for (ALandscape* Landscape : TActorRange<ALandscape>(ActorWorld))
						{
							check(Landscape);
							if (Landscape->GetLandscapeGuid().IsValid())
							{
								if (FallbackLandscape == nullptr)
								{
									FallbackLandscape = Landscape;
								}
								
								const FBox LandscapeBounds = Landscape->GetCompleteBounds();
								if (LandscapeBounds.Intersect(ActorBounds))
								{
									FoundLandscapes.Add(Landscape);
									// Make sure the water zone's bounds is large enough to fit all landscapes that intersect with this water body :
									WaterZoneBounds += LandscapeBounds;
									bFoundIntersectingLandscape = true;
								}
							}
						}

						if (!bFoundIntersectingLandscape && FallbackLandscape != nullptr)
						{
							FoundLandscapes.Add(FallbackLandscape);
							const FBox LandscapeBounds = FallbackLandscape->GetCompleteBounds();
							WaterZoneBounds += LandscapeBounds;
						}
						

						TSubclassOf<AWorldLandscapeBlueprintBrush> WorldEditBrushClass = WorldEditorSettings->GetWorldEditorManagerClass();
						if (UClass* WorldEditBrushClassPtr = WorldEditBrushClass.Get())
						{
							for (ALandscape* FoundLandscape : FoundLandscapes)
							{
								check(IsValid(FoundLandscape));

								bool bHasWaterManager = false;
								FoundLandscape->ForEachLayerConst([&bHasWaterManager](const FLandscapeLayer& Layer)
									{
										for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
										{
											bHasWaterManager |= Cast<AWorldLandscapeBlueprintBrush>(Brush.GetBrush()) != nullptr;
											if (bHasWaterManager)
											{
												// Stop iterating the moment we've found the brush manager : 
												return false;
											}
										}

										return true;
									});


								if (!bHasWaterManager)
								{
									//UActorFactory* WorldEditBrushActorFactory = GEditor->FindActorFactoryForActorClass(WorldEditBrushClassPtr);

									// The manager is added from the BP brush tool only and is being spawned on a valid edit layer
								//	FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { FoundLandscape->GetActorLabel(), WorldEditBrushClassPtr->GetName() });
								//	FName BrushActorName = MakeUniqueObjectName(FoundLandscape->GetOuter(), WorldEditBrushClassPtr, FName(BrushActorString));
								//	FActorSpawnParameters SpawnParams;
								//	SpawnParams.Name = BrushActorName;
								//	SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
								//	/*						AWorldLandscapeBlueprintBrush* NewBrush = (WorldEditBrushActorFactory != nullptr)
								//								? Cast<AWorldLandscapeBlueprintBrush>(WorldEditBrushActorFactory->CreateActor(WorldEditBrushClassPtr, FoundLandscape->GetLevel(), FTransform(WaterZoneBounds.GetCenter()), SpawnParams))
								//: */AWorldSceneManagement* NewBrush = ActorWorld->SpawnActor<AWorldSceneManagement>(WorldEditBrushClass, SpawnParams);

								//if (NewBrush)
								{
									/*	if (!WaterBrushActorFactory)
										{
											UE_LOG(LogWaterEditor, Warning, TEXT("WaterManager Actor Factory could not be found! The newly spawned %s may have incorrect defaults!"), *NewBrush->GetActorLabel());
										}*/

									bHasWaterManager = true;
									//NewBrush->SetActorLabel(BrushActorString);
									//WorldEditBlueprintBrush->SetOwningLandscape(FoundLandscape);
									WorldEditBlueprintBrush->SetTargetLandscape(FoundLandscape);

								}
								}
							}

						}

					}

				}

				SceneManagement->SetFolderPath("Procedural_Scene");
				ALandscape* OwningLandscape = SceneManagement->GetOwningLandscape();
				if (OwningLandscape)
				{
					int32 Width = OwningLandscape->GetBoundingRect().Width();
					//FIntRect RectLandscape = OwningLandscape->GetBoundingRect();
					//UE_LOG(LogTemp,Warning,TEXT("Width==%i"), Width)
				}
			}


		}
	}


}

void UTestModeEditorMode::Exit()
{
	UTestModeInteractiveTool* CurTool = (UTestModeInteractiveTool*)GetToolManager()->GetActiveTool(EToolSide::Left);

	if (CurTool)
	{
		for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair : CurTool->GizmoListInfo)
		{

			for (auto Item : KeyValuePair.Value)
			{
				GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
			}
		}
	}
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(TEXT("SocketGizmo"));
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(TEXT("AssetMark"));
	GEditor->OnLevelActorDeleted().RemoveAll(this);
	GEditor->OnLevelActorAdded().RemoveAll(this);
	ActiveGizmos.Empty();
	UEdMode::Exit();
}

void UTestModeEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FTestModeEditorModeToolkit);
}
void UTestModeEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{

	const FString* ToolName = FTextInspector::GetSourceString(Tool->GetToolInfo().ToolDisplayName);
	//UE_LOG(LogTemp,Warning,TEXT("ToolName=%s"),**ToolName)

}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UTestModeEditorMode::GetModeCommands() const
{
	return FTestModeEditorModeCommands::Get().GetCommands();
}



bool UTestModeEditorMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	return false;
}



bool UTestModeEditorMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	if((InEvent == IE_Pressed) && (InKey == EKeys::Escape))
	{
		GetToolManager()->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);
		ActorSelectionChangeNotify();
		return true;
	}


	if ((InEvent == IE_Pressed) && (InKey == EKeys::P))
	{
		bool bIsVisibility = false;
		if (DisplayMode == GizmoMode)
		{
			DisplayMode = MoKuEditDisplayMode::EditMode;
			bIsVisibility = false;
		}
		else
		{
			DisplayMode = MoKuEditDisplayMode::GizmoMode;
			bIsVisibility = true;
		}
		for (auto& It : ActiveGizmos)
		{
			It->SetVisibility(bIsVisibility);
		}
	}

	return false;
}

void UTestModeEditorMode::ModeTick(float DeltaTime)
{

	//后续可以考虑改成委托能够更方便简洁
	UTestModeInteractiveTool* CurTool = (UTestModeInteractiveTool*)GetToolManager()->GetActiveTool(EToolSide::Left);
	if (CurTool)
	{
		DisplayMode = MoKuEditDisplayMode::EditMode;
		for (auto& It : ActiveGizmos)
		{
			It->SetVisibility(false);
		}
	}


}

bool UTestModeEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	return false;
}

void UTestModeEditorMode::RefreshGizmoVisual(AActor* InActor)
{
	if (InActor->IsA<AMoKuEditSplineActor>())
	{
		AMoKuEditSplineActor* DelActor = Cast<AMoKuEditSplineActor>(InActor);
		UTestModeInteractiveTool* CurTool = (UTestModeInteractiveTool*)GetToolManager()->GetActiveTool(EToolSide::Left);
		if (CurTool)
		{
			auto RtnVal = CurTool->GizmoListInfo.Find(DelActor);
			if (RtnVal)
			{
				for (auto Item : *RtnVal)
				{
					GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
				}

			}
			CurTool->GizmoListInfo.Remove(DelActor);		
			for (auto& ActiveItem : ActiveGizmos)
			{
				if (ActiveItem->OwnerGizmoParams.OwnerActor)
				{
					GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(ActiveItem);
				}
			}
			ActiveGizmos.Empty();
		}
	}
}

bool UTestModeEditorMode::ShouldDrawWidget() const
{
	// hide standard xform gizmo if we have an active tool
	if (GetInteractiveToolsContext() != nullptr && GetToolManager()->HasAnyActiveTool())
	{
		return false;
	}
	return UBaseLegacyWidgetEdMode::ShouldDrawWidget();
}
void UTestModeEditorMode::UpdateAssetGizmoImpl()
{

	if (GetInteractiveToolsContext() != nullptr && !GetToolManager()->HasAnyActiveTool())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World != nullptr)
		{
			USelection* SelectedActors = GEditor->GetSelectedActors();
			if (SelectedActors)
			{
				for (FSelectionIterator It(*SelectedActors); It; ++It)
				{
					UObject* SelectedObj = *It;
					if (SelectedObj->IsA<AMoKuEditSplineActor>())
					{
						AMoKuEditSplineActor* ActiveActor = Cast<AMoKuEditSplineActor>(SelectedObj);
						if (ActiveActor)
						{
							CreateActiveGizmos(ActiveActor);
						}

					}
				}
			}

		}

	}
}
void UTestModeEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	//UWorld* World = GEditor->GetEditorWorldContext().World();
	//if (World != nullptr)
	//{
	//	USelection* SelectedActors = GEditor->GetSelectedActors();
	//	if (SelectedActors)
	//	{
	//		for (FSelectionIterator It(*SelectedActors); It; ++It)
	//		{
	//			UObject* SelectedObj = *It;
	//			if (SelectedObj->IsA<AMoKuEditSplineActor>())
	//			{
	//				AMoKuEditSplineActor* ActiveActor = Cast<AMoKuEditSplineActor>(SelectedObj);
	//				if (ActiveActor)
	//				{
	//					//for (auto Vertex : ActiveActor->EndConnection.ConnectEdge)
	//					//{
	//					//	PDI->DrawPoint(
	//					//		Vertex,
	//					//		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
	//					//		5.0f,
	//					//		SDPG_Foreground);

	//					//}
	//					//for (auto Vertex : ActiveActor->StartConnection.ConnectEdge)
	//					//{
	//					//	PDI->DrawPoint(
	//					//		Vertex,
	//					//		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
	//					//		5.0f,
	//					//		SDPG_Foreground);

	//					//}
	//				}

	//			}
	//		}
	//	}
	//}
}
void UTestModeEditorMode::BindCommands()
{
	const FTestModeEditorModeCommands& Commands = FTestModeEditorModeCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
}

void UTestModeEditorMode::CreateActiveGizmos(AActor* InActor)
{
	for (auto& It : ActiveGizmos)
	{
		GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(It);
	}
	ActiveGizmos.Empty();
	if (InActor)
	{
		FGizmoStoredInfoParams GizmoInfoParams;
		if (AMoKuEditSplineActor* ActiveActor = Cast<AMoKuEditSplineActor>(InActor))
		{
			TArray<UStaticMeshComponent*> LocalMeshes = ActiveActor->GetSplineComponent()->LocalMeshComponents;
			int32 Index = 0;
			for (const auto& Mesh : LocalMeshes)
			{
				if (Mesh)
				{
					GizmoInfoParams.OwnerActor = ActiveActor;
					GizmoInfoParams.OwnerMesh = Mesh->GetStaticMesh();
					GizmoInfoParams.Index = Index;
					CreateGizmoHandle(Mesh, GizmoInfoParams);
					Index++;
				}
			}
			//针对于ExtraMesh
			int ExtraNum = ActiveActor->ExtraLocalMeshComponents.Num();
			for (int i = 0; i < ActiveActor->ExtraLocalMeshComponents.Num(); i++)
			{
				int SubIndex = 0;
				for (const auto& ExtraItem : ActiveActor->ExtraLocalMeshComponents[i])
				{
					if (ExtraItem)
					{
						TMap<int, int> ExtraMeshInfo;
						ExtraMeshInfo.Add(i, SubIndex);

						GizmoInfoParams.OwnerActor = ActiveActor;
						GizmoInfoParams.OwnerMesh = ExtraItem->GetStaticMesh();
						GizmoInfoParams.ExtraAssetIndex = ExtraMeshInfo;
						CreateGizmoHandle(ExtraItem, GizmoInfoParams);
						SubIndex++;
					}
				}
			}
		}
	}


}
void UTestModeEditorMode::CreateGizmoHandle(UStaticMeshComponent* InMesh,const FGizmoStoredInfoParams& InGizmoInfoParams)
{
	AActor* ParentActor = InMesh->GetOwner();
	FVector Location = InMesh->GetComponentTransform().GetLocation();
	if (ParentActor)
	{
		FVector ActorLocation = ParentActor->GetActorLocation();
		FQuat ActorQuat = FQuat(ParentActor->GetActorRotation());
		if (USplineMeshComponent* SplineMesh = Cast<USplineMeshComponent>(InMesh))
		{
			Location = (SplineMesh->GetStartPosition() + SplineMesh->GetEndPosition()) / 2;
			Location = ActorQuat.RotateVector(Location);
			Location += ActorLocation;

		}
	}
	UMoKuEditRePlaceGizmo* AssetGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UMoKuEditRePlaceGizmo>(TEXT("AssetMark"), FString(), this);
	AssetGizmo->CreateGizmoHandle(Location,InMesh->GetComponentTransform().GetRotation());
	AssetGizmo->SetSelectedObject(InGizmoInfoParams);
	if (DisplayMode == MoKuEditDisplayMode::GizmoMode)
	{
		AssetGizmo->SetVisibility(true);
	}
	ActiveGizmos.Add(AssetGizmo);

}



void UTestModeEditorMode::OnLevelActorAddedToWorld(AActor* Actor)
{
	const UWorldEditorSettings* WorldEditorSettings = GetDefault<UWorldEditorSettings>();
	check(WorldEditorSettings != nullptr);
	AWorldLandscapeBlueprintBrush* WorldEditBlueprintBrush = Cast<AWorldLandscapeBlueprintBrush>(Actor);
	if (WorldEditBlueprintBrush)
	{
		UWorld* ActorWorld = WorldEditBlueprintBrush->GetWorld();

		if (ActorWorld)
		{
			FBox WaterZoneBounds(ForceInit);
			TArray<ALandscape*> FoundLandscapes;
			bool bFoundIntersectingLandscape = false;

			const bool bNonColliding = true;
			const bool bIncludeChildActors = false;
			const FBox ActorBounds = Actor->GetComponentsBoundingBox(bNonColliding, bIncludeChildActors);

			ALandscape* FallbackLandscape = nullptr;
			for (ALandscape* Landscape : TActorRange<ALandscape>(ActorWorld))
			{
				check(Landscape);
				if (Landscape->GetLandscapeGuid().IsValid())
				{
					if (FallbackLandscape == nullptr)
					{
						FallbackLandscape = Landscape;
					}

					const FBox LandscapeBounds = Landscape->GetCompleteBounds();
					if (LandscapeBounds.Intersect(ActorBounds))
					{
						FoundLandscapes.Add(Landscape);
						// Make sure the water zone's bounds is large enough to fit all landscapes that intersect with this water body :
						WaterZoneBounds += LandscapeBounds;
						bFoundIntersectingLandscape = true;
					}
				}
			}

			if (!bFoundIntersectingLandscape && FallbackLandscape != nullptr)
			{
				FoundLandscapes.Add(FallbackLandscape);
				const FBox LandscapeBounds = FallbackLandscape->GetCompleteBounds();
				WaterZoneBounds += LandscapeBounds;
			}
			//WorldEditBlueprintBrush->SetTargetLandscape(Landscape);

			TSubclassOf<AWorldLandscapeBlueprintBrush> WorldEditBrushClass = WorldEditorSettings->GetWorldEditorManagerClass();
			if (UClass* WorldEditBrushClassPtr = WorldEditBrushClass.Get())
			{
				for (ALandscape* FoundLandscape : FoundLandscapes)
				{
					check(IsValid(FoundLandscape));

					bool bHasWaterManager = false;
					FoundLandscape->ForEachLayerConst([&bHasWaterManager](const FLandscapeLayer& Layer)
						{
							for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
							{
								bHasWaterManager |= Cast<AWorldLandscapeBlueprintBrush>(Brush.GetBrush()) != nullptr;
								if (bHasWaterManager)
								{
									// Stop iterating the moment we've found the brush manager : 
									return false;
								}
							}

							return true;
						});


					if (!bHasWaterManager)
					{
						//UActorFactory* WorldEditBrushActorFactory = GEditor->FindActorFactoryForActorClass(WorldEditBrushClassPtr);

						// The manager is added from the BP brush tool only and is being spawned on a valid edit layer
						FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { FoundLandscape->GetActorLabel(), WorldEditBrushClassPtr->GetName() });
						FName BrushActorName = MakeUniqueObjectName(FoundLandscape->GetOuter(), WorldEditBrushClassPtr, FName(BrushActorString));
						FActorSpawnParameters SpawnParams;
						SpawnParams.Name = BrushActorName;
						SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
						/*						AWorldLandscapeBlueprintBrush* NewBrush = (WorldEditBrushActorFactory != nullptr)
													? Cast<AWorldLandscapeBlueprintBrush>(WorldEditBrushActorFactory->CreateActor(WorldEditBrushClassPtr, FoundLandscape->GetLevel(), FTransform(WaterZoneBounds.GetCenter()), SpawnParams))
					: */AWorldSceneManagement* NewBrush = ActorWorld->SpawnActor<AWorldSceneManagement>(WorldEditBrushClass, SpawnParams);

					if (NewBrush)
					{
						/*	if (!WaterBrushActorFactory)
							{
								UE_LOG(LogWaterEditor, Warning, TEXT("WaterManager Actor Factory could not be found! The newly spawned %s may have incorrect defaults!"), *NewBrush->GetActorLabel());
							}*/

						bHasWaterManager = true;
						//NewBrush->SetActorLabel(BrushActorString);
						NewBrush->SetTargetLandscape(FoundLandscape);
					}
					}
				}

			}

		}

	}

}


#undef LOCTEXT_NAMESPACE
