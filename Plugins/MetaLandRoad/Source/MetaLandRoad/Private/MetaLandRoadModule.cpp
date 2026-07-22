// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaLandRoadModule.h"
#include "MoKuEditorActorPicker.h"
#include "MetaLandRoadEditorModeCommands.h"
#include "PropertyEditorModule.h"
#include "MoKuSplineActorCustomization.h"
#include "MoKuEditSplineActor.h"
#include "MetaLandRoadStyle.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "WorldEditorSettings.h"
#include "WorldBrushEditorManagerFactory.h"
#include "MoKuEditBaseActor.h"
#include "EditSplineRoadInfo.h"

#define LOCTEXT_NAMESPACE "MetaLandRoadModule"

//DEFINE_LOG_CATEGORY(LogRoadEditor);

void FMetaLandRoadModule::StartupModule()
{
	FMetaLandRoadStyle::Initialize();
	FMetaLandRoadEditorModeCommands::Register();
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(AMoKuEditSplineActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FMoKuSplineConnectAssetMeshDetail::MakeInstance));
	GEngine->OnLevelActorAdded().AddRaw(this, &FMetaLandRoadModule::OnLevelActorAddedToWorld);

	if (GEditor)
	{

		GEditor->ActorFactories.Add(NewObject<UWorldBrushEditorManagerFactory>());
	}

	FMoKuEditorActorPicker::Get().Register();
	ActorPickerHandle = FMoKuEditorActorPicker::Get().OnActorPicked().AddRaw(this, &FMetaLandRoadModule::OnEditorActorPicked);
}

void FMetaLandRoadModule::OnEditorActorPicked(AActor* PickedActor)
{
	if (!IsValid(PickedActor))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[MetaLandRoad] OnActorPicked: %s (%s)"),
		*PickedActor->GetActorLabel(),
		*PickedActor->GetClass()->GetName());
}

void FMetaLandRoadModule::ShutdownModule()
{
	if (ActorPickerHandle.IsValid())
	{
		FMoKuEditorActorPicker::Get().OnActorPicked().Remove(ActorPickerHandle);
		ActorPickerHandle.Reset();
	}
	FMoKuEditorActorPicker::Get().Unregister();

	FMetaLandRoadEditorModeCommands::Unregister();
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.UnregisterCustomClassLayout(AMoKuEditSplineActor::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	if (GEngine)
	{
		//GEngine->OnLevelActorAdded().RemoveAll(this);
	}

	FMetaLandRoadStyle::Shutdown();

}


void FMetaLandRoadModule::OnLevelActorAddedToWorld(AActor* Actor)
{
	if (!Actor)return;
	UWorld* ActorWorld = Actor->GetWorld();

	//check(RoadEditorSettings != nullptr);
	IRoadBrushActorInterface* WorldEditBlueprintBrush = Cast<IRoadBrushActorInterface>(Actor);
	if (WorldEditBlueprintBrush)
	{

		if (ActorWorld && ActorWorld->IsEditorWorld()&&WorldEditBlueprintBrush)
		{
			FBox RoadZoneBounds(ForceInit);
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
						RoadZoneBounds += LandscapeBounds;
						bFoundIntersectingLandscape = true;
					}
				}
			}
		
			if (!bFoundIntersectingLandscape && FallbackLandscape != nullptr)
			{
				FoundLandscapes.Add(FallbackLandscape);
				const FBox LandscapeBounds = FallbackLandscape->GetCompleteBounds();
				RoadZoneBounds += LandscapeBounds;
			}
				//WorldEditBlueprintBrush->SetTargetLandscape(Landscape);
			const FBox LandscapeBounds = RoadZoneBounds;
			const UWorldEditorSettings* RoadEditorSettings = GetDefault<UWorldEditorSettings>();

			if ((WorldEditBlueprintBrush != nullptr) && WorldEditBlueprintBrush->AffectsLandscape() && !FoundLandscapes.IsEmpty())
			{

				TSubclassOf<AWorldLandscapeBlueprintBrush> WorldEditBrushClass = RoadEditorSettings->GetWorldEditorManagerClass();
				if (UClass* WorldEditBrushClassPtr = WorldEditBrushClass.Get())
				{
					for (ALandscape* FoundLandscape : FoundLandscapes)
					{
						check(IsValid(FoundLandscape));

						bool bHasEditorManager = false;
						FoundLandscape->ForEachLayerConst([&bHasEditorManager](const FLandscapeLayer& Layer)
						{
								for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
								{
									bHasEditorManager |= Cast<AWorldLandscapeBlueprintBrush>(Brush.GetBrush()) != nullptr;
									if (bHasEditorManager)
									{
										// Stop iterating the moment we've found the brush manager : 
										return false;
									}
								}

								return true;
						});
						if (!bHasEditorManager)
						{
							UActorFactory* WorldEditBrushActorFactory = GEditor->FindActorFactoryForActorClass(WorldEditBrushClassPtr);
							// The manager is added from the BP brush tool only and is being spawned on a valid edit layer
							FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { FoundLandscape->GetActorLabel(), WorldEditBrushClassPtr->GetName() });
							FName BrushActorName = MakeUniqueObjectName(FoundLandscape->GetOuter(), WorldEditBrushClassPtr, FName(BrushActorString));
							FActorSpawnParameters SpawnParams;
							SpawnParams.Name = BrushActorName;
							SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
							AWorldLandscapeBlueprintBrush* NewBrush = (WorldEditBrushActorFactory != nullptr)
								? Cast<AWorldLandscapeBlueprintBrush>(WorldEditBrushActorFactory->CreateActor(WorldEditBrushClassPtr, FoundLandscape->GetLevel(), FTransform(RoadZoneBounds.GetCenter()), SpawnParams))
								: ActorWorld->SpawnActor<AWorldLandscapeBlueprintBrush>(WorldEditBrushClass, SpawnParams);
							//AWorldSceneManagement* NewBrush = ActorWorld->SpawnActor<AWorldSceneManagement>(WorldEditBrushClass, SpawnParams);

							if (NewBrush)
							{
								/*	if (!WaterBrushActorFactory)
									{
										UE_LOG(LogWaterEditor, Warning, TEXT("WaterManager Actor Factory could not be found! The newly spawned %s may have incorrect defaults!"), *NewBrush->GetActorLabel());
									}*/

								bHasEditorManager = true;
								NewBrush->SetActorLabel(BrushActorString);
								NewBrush->SetTargetLandscape(FoundLandscape);

								TObjectPtr<AMoKuEditBaseActor> MoKuEditBaseActor  = Cast<AMoKuEditBaseActor>(Actor);
								MoKuEditBaseActor-> SceneManagement = Cast<AWorldSceneManagement>(NewBrush);

							}
						}

					}
				}

			}
	
		}

	}

}

IMPLEMENT_MODULE(FMetaLandRoadModule, MetaLandRoad)


#undef LOCTEXT_NAMESPACE
