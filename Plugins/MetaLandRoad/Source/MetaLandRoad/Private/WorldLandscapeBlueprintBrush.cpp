#include "WorldLandscapeBlueprintBrush.h"
#include "LandscapeSettings.h"
#include "LandscapeModule.h"
#include "LandscapeEditorServices.h"
#include "Landscape.h"
#include "MoKuEditBaseActor.h"
#include "EngineUtils.h"
#include "Algo/AnyOf.h"
#include "EditSplineRoadInfo.h"
#include "WorldEditorSettings.h"
#include "Editor.h"

#include "SplineRoadEditorSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldLandscapeBlueprintBrush)

#define LOCTEXT_NAMESPACE "WorldLandscapeBrush"

static int32 CompactNullBlueprintBrushesOnLayerInternal(ALandscape* Landscape, FName LayerName)
{
#if WITH_EDITOR
	if (!IsValid(Landscape))
	{
		return 0;
	}

	const int32 LayerIndex = Landscape->GetLayerIndex(LayerName);
	if (LayerIndex == INDEX_NONE)
	{
		return 0;
	}

	int32 Removed = 0;
	for (;;)
	{
		TArrayView<const FLandscapeLayer> Layers = Landscape->GetLayersConst();
		if (!Layers.IsValidIndex(LayerIndex))
		{
			break;
		}

		const TArray<FLandscapeLayerBrush>& Brushes = Layers[LayerIndex].Brushes;
		int32 NullBrushIndex = INDEX_NONE;
		for (int32 BrushIndex = Brushes.Num() - 1; BrushIndex >= 0; --BrushIndex)
		{
			if (!IsValid(Brushes[BrushIndex].GetBrush()))
			{
				NullBrushIndex = BrushIndex;
				break;
			}
		}

		if (NullBrushIndex == INDEX_NONE)
		{
			break;
		}

		Landscape->RemoveBrushFromLayer(LayerIndex, NullBrushIndex);
		++Removed;
	}

	if (Removed > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] CompactNullBlueprintBrushes removed %d stale None slot(s) on layer '%s'"),
			Removed, *LayerName.ToString());
	}

	return Removed;
#else
	return 0;
#endif
}

static UWorld* GetBrushActorWorldSafe(const AActor* Actor)
{
	if (!IsValid(Actor) || Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return nullptr;
	}
	if (Actor->GetOuter() == nullptr)
	{
		return nullptr;
	}
	return Actor->GetWorld();
}

static bool ShouldBrushRespondToWorldEvent(const AWorldLandscapeBlueprintBrush* Brush, const UWorld* EventWorld)
{
	if (!IsValid(Brush) || EventWorld == nullptr || EventWorld->IsPreviewWorld())
	{
		return false;
	}
	const UWorld* BrushWorld = GetBrushActorWorldSafe(Brush);
	return BrushWorld != nullptr && BrushWorld == EventWorld;
}

// Sets default values
AWorldLandscapeBlueprintBrush::AWorldLandscapeBlueprintBrush(const FObjectInitializer& ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	//PrimaryActorTick.bCanEverTick = true;
	SetCanAffectHeightmap(true);
	SetCanAffectWeightmap(true);
}

void AWorldLandscapeBlueprintBrush::BeginDestroy()
{
	if (!IsTemplate())
	{
		ClearActors();

		FWorldDelegates::OnPostWorldInitialization.Remove(OnWorldPostInitHandle);
		OnWorldPostInitHandle.Reset();

		FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldHandle);
		OnLevelAddedToWorldHandle.Reset();

		FWorldDelegates::LevelRemovedFromWorld.Remove(OnLevelRemovedFromWorldHandle);
		OnLevelRemovedFromWorldHandle.Reset();

		if (GEngine)
		{
			GEngine->OnLevelActorAdded().Remove(OnLevelActorAddedHandle);
			GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedHandle);
		}
		OnLevelActorAddedHandle.Reset();
		OnLevelActorDeletedHandle.Reset();

		IRoadBrushActorInterface::GetOnRoadBrushActorChangedEvent().RemoveAll(this);
	}

	Super::BeginDestroy();
}
// Called every frame
void AWorldLandscapeBlueprintBrush::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AWorldLandscapeBlueprintBrush::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);


}

void AWorldLandscapeBlueprintBrush::SetTargetLandscape(ALandscape* InTargetLandscape)
{
#if WITH_EDITOR
	if (!InTargetLandscape)
	{
		if (OwningLandscape)
		{
			OwningLandscape->RemoveBrush(this);
		}
		return;
	}

	const bool bLandscapeChanged = (OwningLandscape != InTargetLandscape);
	if (bLandscapeChanged && OwningLandscape)
	{
		OwningLandscape->RemoveBrush(this);
	}

	if (!InTargetLandscape->CanHaveLayersContent())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] SetTargetLandscape: '%s' has Edit Layers disabled — cannot create WorldEditor"),
			*InTargetLandscape->GetActorLabel());
		return;
	}

	static const FName WorldEditLayerName = FName("WorldEditor");
	CompactNullBlueprintBrushesOnLayerInternal(InTargetLandscape, WorldEditLayerName);

	ILandscapeModule& LandscapeModule = FModuleManager::GetModuleChecked<ILandscapeModule>("Landscape");
	const int32 WorldEditLayerIndex = LandscapeModule.GetLandscapeEditorServices()->GetOrCreateEditLayer(WorldEditLayerName, InTargetLandscape);

	if (InTargetLandscape->GetBrushLayer(this) == INDEX_NONE)
	{
		InTargetLandscape->AddBrushToLayer(WorldEditLayerIndex, this);
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] SetTargetLandscape: bound '%s' → layer WorldEditor (idx=%d) on '%s'"),
			*GetActorLabel(), WorldEditLayerIndex, *InTargetLandscape->GetActorLabel());
	}
	else if (bLandscapeChanged)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] SetTargetLandscape: '%s' already on WorldEditor of '%s'"),
			*GetActorLabel(), *InTargetLandscape->GetActorLabel());
	}
#else
	(void)InTargetLandscape;
#endif
}

static void CollectLandscapesForRoadActor(AActor* RoadActor, TArray<ALandscape*>& OutLandscapes)
{
	OutLandscapes.Reset();
	if (!IsValid(RoadActor))
	{
		return;
	}

	UWorld* World = RoadActor->GetWorld();
	if (!World)
	{
		return;
	}

	const FVector ActorLocation = RoadActor->GetActorLocation();
	FBox ActorBounds = RoadActor->GetComponentsBoundingBox(/*bNonColliding=*/true, /*bIncludeFromChildActors=*/false);
	if (!ActorBounds.IsValid || ActorBounds.GetExtent().SizeSquared() < 1.0)
	{
		const FVector Extent(5000.0, 5000.0, 100000.0);
		ActorBounds = FBox(ActorLocation - Extent, ActorLocation + Extent);
	}
	else
	{
		ActorBounds.Min.Z -= 100000.0;
		ActorBounds.Max.Z += 100000.0;
	}

	ALandscape* FallbackLandscape = nullptr;
	ALandscape* BestContaining = nullptr;
	double BestContainingDistSq = TNumericLimits<double>::Max();

	for (ALandscape* Landscape : TActorRange<ALandscape>(World))
	{
		if (!IsValid(Landscape) || !Landscape->GetLandscapeGuid().IsValid())
		{
			continue;
		}
		if (!Landscape->CanHaveLayersContent())
		{
			continue;
		}
		if (!FallbackLandscape)
		{
			FallbackLandscape = Landscape;
		}

		FBox LandscapeBounds = Landscape->GetCompleteBounds();
		LandscapeBounds.Min.Z -= 100000.0;
		LandscapeBounds.Max.Z += 100000.0;

		const bool bXYInside =
			ActorLocation.X >= LandscapeBounds.Min.X && ActorLocation.X <= LandscapeBounds.Max.X &&
			ActorLocation.Y >= LandscapeBounds.Min.Y && ActorLocation.Y <= LandscapeBounds.Max.Y;

		if (LandscapeBounds.Intersect(ActorBounds) || bXYInside)
		{
			OutLandscapes.AddUnique(Landscape);
			if (bXYInside)
			{
				const double DistSq = FVector::DistSquared2D(ActorLocation, LandscapeBounds.GetCenter());
				if (DistSq < BestContainingDistSq)
				{
					BestContainingDistSq = DistSq;
					BestContaining = Landscape;
				}
			}
		}
	}

	if (OutLandscapes.IsEmpty() && FallbackLandscape)
	{
		OutLandscapes.Add(FallbackLandscape);
	}

	if (BestContaining && OutLandscapes.Num() > 1)
	{
		OutLandscapes.RemoveSingle(BestContaining);
		OutLandscapes.Insert(BestContaining, 0);
	}
}

AWorldSceneManagement* AWorldLandscapeBlueprintBrush::EnsureWorldEditorManagerForLandscape(ALandscape* Landscape)
{
#if WITH_EDITOR
	if (!IsValid(Landscape))
	{
		return nullptr;
	}
	if (!Landscape->CanHaveLayersContent())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] EnsureWorldEditor: '%s' Edit Layers off — enable Enable Edit Layers then redraw"),
			*Landscape->GetActorLabel());
		return nullptr;
	}

	CompactNullBlueprintBrushesOnEditLayer(Landscape);

	AWorldSceneManagement* ManagerOnLandscape = nullptr;
	Landscape->ForEachLayerConst([&ManagerOnLandscape](const FLandscapeLayer& Layer)
	{
		for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
		{
			if (AWorldSceneManagement* Manager = Cast<AWorldSceneManagement>(Brush.GetBrush()))
			{
				if (IsValid(Manager))
				{
					ManagerOnLandscape = Manager;
					return false;
				}
			}
		}
		return true;
	});

	if (ManagerOnLandscape)
	{
		ManagerOnLandscape->SetTargetLandscape(Landscape);
		return ManagerOnLandscape;
	}

	UWorld* World = Landscape->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AWorldSceneManagement* UnboundManager = nullptr;
	for (TActorIterator<AWorldSceneManagement> It(World); It; ++It)
	{
		AWorldSceneManagement* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}
		ALandscape* Owned = Candidate->GetOwningLandscape();
		if (!IsValid(Owned) || Owned->GetBrushLayer(Candidate) == INDEX_NONE)
		{
			UnboundManager = Candidate;
			break;
		}
	}

	if (UnboundManager)
	{
		UnboundManager->SetTargetLandscape(Landscape);
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] EnsureWorldEditor: rebound unbound manager → '%s'"),
			*Landscape->GetActorLabel());
		return UnboundManager;
	}

	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	TSubclassOf<AWorldLandscapeBlueprintBrush> ManagerClass = Settings ? Settings->GetWorldEditorManagerClass() : nullptr;
	UClass* ManagerClassPtr = ManagerClass.Get();
	if (!ManagerClassPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] EnsureWorldEditor: WorldEditorManagerClassPath failed to load"));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = Landscape->GetLevel();
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transactional;
	SpawnParams.bAllowDuringConstructionScript = true;
	const FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { Landscape->GetActorLabel(), ManagerClassPtr->GetName() });
	SpawnParams.Name = MakeUniqueObjectName(Landscape->GetOuter(), ManagerClassPtr, FName(*BrushActorString));

	AWorldSceneManagement* NewManager = World->SpawnActor<AWorldSceneManagement>(ManagerClassPtr, FTransform(Landscape->GetCompleteBounds().GetCenter()), SpawnParams);
	if (!NewManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] EnsureWorldEditor: SpawnActor failed for '%s'"),
			*Landscape->GetActorLabel());
		return nullptr;
	}

	NewManager->SetActorLabel(BrushActorString);
	NewManager->SetFolderPath(TEXT("Procedural_Scene"));
	NewManager->SetTargetLandscape(Landscape);
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] EnsureWorldEditor: spawned '%s' for landscape '%s'"),
		*NewManager->GetActorLabel(), *Landscape->GetActorLabel());
	return NewManager;
#else
	(void)Landscape;
	return nullptr;
#endif
}

AWorldSceneManagement* AWorldLandscapeBlueprintBrush::EnsureWorldEditorManagerForRoadActor(AActor* RoadActor)
{
#if WITH_EDITOR
	if (!IsValid(RoadActor)
		|| RoadActor->bIsEditorPreviewActor
		|| RoadActor->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| RoadActor->IsTemplate()
		|| RoadActor->IsActorBeingDestroyed())
	{
		return nullptr;
	}

	if (AMoKuEditBaseActor* RoadBody = Cast<AMoKuEditBaseActor>(RoadActor))
	{
		RoadBody->EnsureDefaultTerrainInfluenceSettings();
	}

	TArray<ALandscape*> Landscapes;
	CollectLandscapesForRoadActor(RoadActor, Landscapes);
	if (Landscapes.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] EnsureWorldEditor: no Landscape with Edit Layers near '%s'"),
			*RoadActor->GetActorLabel());
		return nullptr;
	}

	AWorldSceneManagement* PrimaryManager = nullptr;
	for (ALandscape* Landscape : Landscapes)
	{
		if (AWorldSceneManagement* Manager = EnsureWorldEditorManagerForLandscape(Landscape))
		{
			if (!PrimaryManager)
			{
				PrimaryManager = Manager;
			}
			if (AMoKuEditBaseActor* RoadBody = Cast<AMoKuEditBaseActor>(RoadActor))
			{
				RoadBody->SceneManagement = Manager;
			}
		}
	}

	if (AMoKuEditBaseActor* RoadBody = Cast<AMoKuEditBaseActor>(RoadActor))
	{
		FOnRoadBodyChangedParams Params;
		Params.PropertyChangedEvent.ChangeType = EPropertyChangeType::ValueSet;
		Params.bShapeOrPositionChanged = true;
		Params.bWeightmapSettingsChanged = true;
		Params.bUserTriggered = false;
		RoadBody->OnRoadBodyChanged(Params);
	}

	return PrimaryManager;
#else
	(void)RoadActor;
	return nullptr;
#endif
}

int32 AWorldLandscapeBlueprintBrush::CompactNullBlueprintBrushesOnEditLayer(ALandscape* Landscape, FName LayerName)
{
	return CompactNullBlueprintBrushesOnLayerInternal(Landscape, LayerName);
}

void AWorldLandscapeBlueprintBrush::DebugCompactStaleLandscapeBrushes()
{
#if WITH_EDITOR
	if (ALandscape* Landscape = GetOwningLandscape())
	{
		const int32 Removed = CompactNullBlueprintBrushesOnEditLayer(Landscape);
		if (Removed == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] CompactStaleLandscapeBrushes: no None slots on layer 'WorldEditor'"));
		}
		RequestLandscapeUpdate(/* bInUserTriggered = */ true);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] CompactStaleLandscapeBrushes: brush has no OwningLandscape"));
	}
#endif
}

void AWorldLandscapeBlueprintBrush::SetOwningLandscape(ALandscape * InOwningLandscape)
{
	if (OwningLandscape != nullptr)
	{
		OwningLandscape->OnEditLayersMerged().RemoveAll(this);
	}

	Super::SetOwningLandscape(InOwningLandscape);

	if (OwningLandscape != nullptr)
	{
	//	OwningLandscape->OnEditLayersMerged().AddUObject(this, &AWorldLandscapeBrush::OnEditLayersMerged);
	}


}

void AWorldLandscapeBlueprintBrush::PostLoad()
{
	Super::PostLoad();
	RegisterDelegates();

	constexpr bool bInTriggerEvents = true;
	UpdateActors(bInTriggerEvents);
}


void AWorldLandscapeBlueprintBrush::PostActorCreated()
{
	Super::PostActorCreated();
	RegisterDelegates();

	constexpr bool bInTriggerEvents = true;
	UpdateActors(bInTriggerEvents);
}
void AWorldLandscapeBlueprintBrush::BeginPlay()
{
	Super::BeginPlay();
}

void AWorldLandscapeBlueprintBrush::OnLevelActorAdded(AActor* InActor)
{
	UWorld* BrushWorld = GetBrushActorWorldSafe(this);
	if (BrushWorld && InActor && InActor->GetWorld() == BrushWorld)
	{
		AddActorInternal(InActor, BrushWorld, /*InCache = */nullptr, /*bTriggerEvent = */true, /*bModify = */true);
	}
}

void AWorldLandscapeBlueprintBrush::OnLevelActorRemoved(AActor* InActor)
{
	UWorld* BrushWorld = GetBrushActorWorldSafe(this);
	if (!InActor || InActor->GetWorld() != BrushWorld)
	{
		return;
	}

	// Only road-brush actors are tracked; deleting Landscape / SceneManagement must not CastChecked.
	if (IsActorAffectingLandscape(InActor))
	{
		RemoveActorInternal(InActor);
	}
}

void AWorldLandscapeBlueprintBrush::RegisterDelegates()
{
	if (IsTemplate() || OnWorldPostInitHandle.IsValid())
	{
		return;
	}

	OnWorldPostInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* World, const UWorld::InitializationValues IVS)
		{
			if (ShouldBrushRespondToWorldEvent(this, World))
			{
				const bool bTriggerEvents = false;
				UpdateActors(bTriggerEvents);
			}
		});

	OnLevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddLambda([this](ULevel* Level, UWorld* World)
		{
			if (ShouldBrushRespondToWorldEvent(this, World)
				&& World->IsEditorWorld()
				&& (Level != nullptr)
				&& Algo::AnyOf(Level->Actors, [this](AActor* Actor) { return IsActorAffectingLandscape(Actor); }))
			{
				UpdateActors(!UE::GetIsEditorLoadingPackage());
			}
		});

	OnLevelRemovedFromWorldHandle = FWorldDelegates::LevelRemovedFromWorld.AddLambda([this](ULevel* Level, UWorld* World)
		{
			if (ShouldBrushRespondToWorldEvent(this, World)
				&& World->IsEditorWorld()
				&& (Level != nullptr)
				&& Algo::AnyOf(Level->Actors, [this](AActor* Actor) { return IsActorAffectingLandscape(Actor); }))
			{
				UpdateActors(!UE::GetIsEditorLoadingPackage());
			}
		});


	OnLevelActorAddedHandle = GEngine->OnLevelActorAdded().AddUObject(this, &AWorldLandscapeBlueprintBrush::OnLevelActorAdded);
	OnLevelActorDeletedHandle = GEngine->OnLevelActorDeleted().AddUObject(this, &AWorldLandscapeBlueprintBrush::OnLevelActorRemoved);

	IRoadBrushActorInterface::GetOnRoadBrushActorChangedEvent().AddUObject(this, &AWorldLandscapeBlueprintBrush::OnRoadBrushActorChanged);
}

void AWorldLandscapeBlueprintBrush::AddActorInternal(AActor* Actor, const UWorld* ThisWorld, UObject* InCache, bool bTriggerEvent, bool bModify)
{
	if (!Actor->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject) &&
		IsValidChecked(Actor) &&
		!Actor->IsUnreachable() &&
		Actor->GetLevel() != nullptr &&
		!Actor->GetLevel()->bIsBeingRemoved &&
		ThisWorld == Actor->GetWorld())
	{
		if (bModify)
		{
			const bool bMarkPackageDirty = false;
			Modify(bMarkPackageDirty);
		}

		IRoadBrushActorInterface* RoadBrushActor = Cast<IRoadBrushActorInterface>(Actor);
		if (RoadBrushActor)
		{
			ActorsAffectingLandscape.Add(TWeakInterfacePtr<IRoadBrushActorInterface>(RoadBrushActor));

			if (InCache)
			{
				Cache.Add(TSoftObjectPtr<AActor>(Actor), InCache);
			}

			if (bTriggerEvent)
			{
				UpdateAffectedWeightmaps();
				//OnActorsAffectingLandscapeChanged();
			}
		}
	}
}
void AWorldLandscapeBlueprintBrush::HandleRoadActorRemoved(AActor* Actor)
{
	RemoveActorInternal(Actor);
}

void AWorldLandscapeBlueprintBrush::RemoveActorInternal(AActor* Actor)
{
	IRoadBrushActorInterface* RoadBrushActor = Cast<IRoadBrushActorInterface>(Actor);
	if (!RoadBrushActor)
	{
		return;
	}

	const bool bMarkPackageDirty = false;
	Modify(bMarkPackageDirty);
	const int32 Index = ActorsAffectingLandscape.IndexOfByKey(TWeakInterfacePtr<IRoadBrushActorInterface>(RoadBrushActor));
	if (Index != INDEX_NONE)
	{
		ActorsAffectingLandscape.RemoveAt(Index);
		Cache.Remove(TSoftObjectPtr<AActor>(Actor));
		UpdateAffectedWeightmaps();
		RequestLandscapeUpdate(/* bInUserTriggered = */ true);
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Removed %s — RequestLandscapeUpdate"), *GetNameSafe(Actor));
	}
}

bool AWorldLandscapeBlueprintBrush::IsActorAffectingLandscape(AActor* Actor) const
{
	IRoadBrushActorInterface* RoadBrushActor = Cast<IRoadBrushActorInterface>(Actor);
	return ((RoadBrushActor != nullptr) && RoadBrushActor->AffectsLandscape());
}

void AWorldLandscapeBlueprintBrush::UpdateAffectedWeightmaps()
{
	AffectedWeightmapLayers.Empty();
	for (const TWeakInterfacePtr<IRoadBrushActorInterface>& RoadBrushActor : ActorsAffectingLandscape)
	{
		if (RoadBrushActor.IsValid())
		{
			for (const TPair<FName, FRoadBodyWeightmapSettings>& Pair : RoadBrushActor->GetLayerWeightmapSettings())
			{
				AffectedWeightmapLayers.AddUnique(Pair.Key);
			}
		}
	}

	// Landscape only calls weight RenderLayer for names in AffectedWeightmapLayers.
	// If the actor list is empty/stale, an empty list sets CanAffectWeightmap=false → weight settings appear to do nothing.
	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName DefaultRoadLayer = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	const FName DefaultShoulderLayer = Settings ? Settings->GetDefaultShoulderWeightmapLayerName() : FName(TEXT("Dirty"));
	if (DefaultRoadLayer != NAME_None)
	{
		AffectedWeightmapLayers.AddUnique(DefaultRoadLayer);
	}
	// Side slopes (embankment) — separate from pavement Road layer.
	if (DefaultShoulderLayer != NAME_None)
	{
		AffectedWeightmapLayers.AddUnique(DefaultShoulderLayer);
	}

	SetCanAffectWeightmap(AffectedWeightmapLayers.Num() > 0);

	FString LayerNames;
	for (const FName& LayerName : AffectedWeightmapLayers)
	{
		if (!LayerNames.IsEmpty())
		{
			LayerNames += TEXT(",");
		}
		LayerNames += LayerName.ToString();
	}
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] UpdateAffectedWeightmaps layers=%d (%s) actors=%d"),
		AffectedWeightmapLayers.Num(),
		LayerNames.IsEmpty() ? TEXT("none") : *LayerNames,
		ActorsAffectingLandscape.Num());
}

void AWorldLandscapeBlueprintBrush::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	#if WITH_EDITOR
		if (!IsTemplate())
		{
			if (!OnLoadedActorRemovedFromLevelEventHandle.IsValid())
			{
				check(!OnLoadedActorRemovedFromLevelEventHandle.IsValid());

				// In world partition, actors don't belong to levels and the on loaded/removed callbacks are different : 
				// Since these are world events, we register/unregister to it in AWaterLandscapeBrush::RegisterAllComponents() / AWaterLandscapeBrush::UnregisterAllComponents() to make sure that the world is in a valid state when it's called :
				UWorld* World = GetBrushActorWorldSafe(this);
				if (World && World->PersistentLevel)
				{
					OnLoadedActorAddedToLevelEventHandle = World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddLambda([this](AActor& InActor) { OnLevelActorAdded(&InActor); });
					OnLoadedActorRemovedFromLevelEventHandle = World->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.AddLambda([this](AActor& InActor) { OnLevelActorRemoved(&InActor); });
				}
			}

			if (ALandscape* Landscape = GetOwningLandscape())
			{
				CompactNullBlueprintBrushesOnLayerInternal(Landscape, FName(TEXT("WorldEditor")));
			}

			UpdateActors();
		}
#endif // WITH_EDITOR
}

void AWorldLandscapeBlueprintBrush::UpdateActors(bool bInTriggerEvents)
{
	if (IsTemplate())
	{
		return;
	}

	const bool bMarkPackageDirty = false;
	Modify(bMarkPackageDirty);

	ClearActors();

	// Backup Cache
	TMap<TSoftObjectPtr<AActor>, TObjectPtr<UObject>> PreviousCache;
	Swap(Cache, PreviousCache);

	if (UWorld* World = GetBrushActorWorldSafe(this))
	{
		for (FActorIterator It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (IRoadBrushActorInterface* RoadBrushActor = Cast<IRoadBrushActorInterface>(Actor))
			{
				const TObjectPtr<UObject>* FoundCache = PreviousCache.Find(TSoftObjectPtr<AActor>(Actor));
				const bool bTriggerEvent = false;
				const bool bModify = false;
				AddActorInternal(Actor, World, FoundCache != nullptr ? *FoundCache : nullptr, bTriggerEvent, bModify);
			}
		}
	}

	UpdateAffectedWeightmaps();

	if (bInTriggerEvents)
	{
		//OnActorsAffectingLandscapeChanged();
	}
}


void AWorldLandscapeBlueprintBrush::OnRoadBrushActorChanged(const IRoadBrushActorInterface::FRoadBrushActorChangedEventParams& InParams)
{
	AActor* Actor = CastChecked<AActor>(InParams.RoadBrushActor);
	const bool bAffectsLandscape = InParams.RoadBrushActor->AffectsLandscape();

	const bool bActorAlreadyAffectingLandscape = ActorsAffectingLandscape.Contains(InParams.RoadBrushActor);
	bool bForceUpdateBrush = false;

	if (bAffectsLandscape != bActorAlreadyAffectingLandscape)
	{
		if (bAffectsLandscape)
		{
			UWorld* BrushWorld = GetBrushActorWorldSafe(this);
			if (BrushWorld)
			{
				AddActorInternal(Actor, BrushWorld, nullptr, /*bTriggerEvent = */true, /*bModify =*/true);
			}
		}
		else
		{
			RemoveActorInternal(Actor);
		}

		bForceUpdateBrush = true;
	}
	if (InParams.bWeightmapSettingsChanged || InParams.bShapeOrPositionChanged)
	{
		if (InParams.bWeightmapSettingsChanged)
		{
			UpdateAffectedWeightmaps();
		}
		// Do NOT global-kill DF cache here — that forced CaptureRoadDepth+JumpFlood on every
		// road (41×) when one junction moved. Per-actor invalidate is in BlueprintRoadBodyChanged_Native.
		if (bAffectsLandscape && !bActorAlreadyAffectingLandscape)
		{
			UWorld* BrushWorld = GetBrushActorWorldSafe(this);
			if (BrushWorld)
			{
				AddActorInternal(Actor, BrushWorld, nullptr, /*bTriggerEvent = */true, /*bModify =*/true);
			}
		}
	}
	BlueprintRoadBodyChanged(Actor);
	const ULandscapeSettings* LandscapeSettings = GetDefault<ULandscapeSettings>();
	check(LandscapeSettings != nullptr);

	const bool bAllowLandscapeUpdate = (InParams.PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive) || LandscapeSettings->GetShouldUpdateEditLayersDuringInteractiveChanges();
	if (bForceUpdateBrush || InParams.bWeightmapSettingsChanged || (bAffectsLandscape && bAllowLandscapeUpdate))
	{
		RequestLandscapeUpdate(/* bInUserTriggered = */ InParams.bUserTriggered || InParams.bWeightmapSettingsChanged);
	}

}

void AWorldLandscapeBlueprintBrush::ClearActors()
{
	ActorsAffectingLandscape.Empty();
}


template<class T>
class FGetActorsOfType
{
public:
	void operator()(const AWorldLandscapeBlueprintBrush* Brush, TSubclassOf<T> ActorClass, TArray<T*>& OutActors)
	{
		OutActors.Empty();
		for (const TWeakInterfacePtr<IRoadBrushActorInterface>& RoadBrushActor : Brush->ActorsAffectingLandscape)
		{
			T* Actor = Cast<T>(RoadBrushActor.GetObject());
			if (Actor && Actor->IsA(ActorClass))
			{
				OutActors.Add(Actor);
			}
		}
	}
};

void AWorldLandscapeBlueprintBrush::GetRoadActors(TSubclassOf<AMoKuEditBaseActor> RoadBodyClass, TArray<AMoKuEditBaseActor*>& OutRoadBodies) const
{
	FGetActorsOfType<AMoKuEditBaseActor>()(this, RoadBodyClass, OutRoadBodies);
}


UObject* AWorldLandscapeBlueprintBrush::GetActorCache(AActor* InActor, TSubclassOf<UObject> CacheClass) const
{
	TObjectPtr<UObject> const* ValuePtr = Cache.Find(TSoftObjectPtr<AActor>(InActor));
	if (ValuePtr && (*ValuePtr) && (*ValuePtr)->IsA(*CacheClass))
	{
		return *ValuePtr;
	}
	return nullptr;
}

void AWorldLandscapeBlueprintBrush::SetActorCache(AActor* InActor, UObject* InCache)
{
	if (!InCache)
	{
		return;
	}

	TObjectPtr<UObject>& Value = Cache.FindOrAdd(TSoftObjectPtr<AActor>(InActor));
	Value = InCache;
}
void AWorldLandscapeBlueprintBrush::BlueprintRoadBodyChanged_Implementation(AActor* Actor)
{
	BlueprintRoadBodyChanged_Native(Actor);
}



#undef LOCTEXT_NAMESPACE
