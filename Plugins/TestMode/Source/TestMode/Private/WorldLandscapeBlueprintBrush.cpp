#include "WorldLandscapeBlueprintBrush.h"
#include "LandscapeSettings.h"
#include "LandscapeModule.h"
#include "LandscapeEditorServices.h"
#include "Landscape.h"
#include "MoKuEditBaseActor.h"
#include "EngineUtils.h"
#include "Algo/AnyOf.h"

#include "SplineRoadEditorSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldLandscapeBlueprintBrush)

#define LOCTEXT_NAMESPACE "WorldLandscapeBrush"

// Sets default values
AWorldLandscapeBlueprintBrush::AWorldLandscapeBlueprintBrush(const FObjectInitializer& ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	//PrimaryActorTick.bCanEverTick = true;
	SetCanAffectHeightmap(true);
}

void AWorldLandscapeBlueprintBrush::BeginDestroy()
{
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
	if (OwningLandscape != InTargetLandscape)
	{
		if (OwningLandscape)
		{
			OwningLandscape->RemoveBrush(this);
		}
//#if WITH_EDITOR
		if (InTargetLandscape && InTargetLandscape->CanHaveLayersContent())
		{
			static const FName WorldEditLayerName = FName("WorldEditor");

			ILandscapeModule& LandscapeModule = FModuleManager::GetModuleChecked<ILandscapeModule>("Landscape");
			int32 WorldEditLayerIndex = LandscapeModule.GetLandscapeEditorServices()->GetOrCreateEditLayer(WorldEditLayerName, InTargetLandscape);

			InTargetLandscape->AddBrushToLayer(WorldEditLayerIndex, this);
		}
//#endif
	}
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
	if (InActor->GetWorld() == GetWorld())
	{
		AddActorInternal(InActor, GetWorld(), /*InCache = */nullptr, /*bTriggerEvent = */true, /*bModify = */true);
	}
}

void AWorldLandscapeBlueprintBrush::OnLevelActorRemoved(AActor* InActor)
{
	if (InActor->GetWorld() == GetWorld())
	{
		//if (IsActorAffectingLandscape(InActor))
		{
			RemoveActorInternal(InActor);
		}
	}
}

void AWorldLandscapeBlueprintBrush::RegisterDelegates()
{
	if (!IsTemplate())
	{
		OnWorldPostInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* World, const UWorld::InitializationValues IVS)
			{
				if (World == GetWorld())
				{
					const bool bTriggerEvents = false;
					UpdateActors(bTriggerEvents);
				}
			});

		OnLevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddLambda([this](ULevel* Level, UWorld* World)
			{
				if ((World == GetWorld())
					&& (World->IsEditorWorld()
						&& (Level != nullptr)
						&& Algo::AnyOf(Level->Actors, [this](AActor* Actor) { return IsActorAffectingLandscape(Actor); })))
				{
					UpdateActors(!UE::GetIsEditorLoadingPackage());
				}
			});

		OnLevelRemovedFromWorldHandle = FWorldDelegates::LevelRemovedFromWorld.AddLambda([this](ULevel* Level, UWorld* World)
			{
				if ((World == GetWorld())
					&& (World->IsEditorWorld()
						&& (Level != nullptr)
						&& Algo::AnyOf(Level->Actors, [this](AActor* Actor) { return IsActorAffectingLandscape(Actor); })))
				{
					UpdateActors(!UE::GetIsEditorLoadingPackage());
				}
			});


		OnLevelActorAddedHandle = GEngine->OnLevelActorAdded().AddUObject(this, &AWorldLandscapeBlueprintBrush::OnLevelActorAdded);
		OnLevelActorDeletedHandle = GEngine->OnLevelActorDeleted().AddUObject(this, &AWorldLandscapeBlueprintBrush::OnLevelActorRemoved);

		IRoadBrushActorInterface::GetOnRoadBrushActorChangedEvent().AddUObject(this, &AWorldLandscapeBlueprintBrush::OnRoadBrushActorChanged);
	}
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
void AWorldLandscapeBlueprintBrush::RemoveActorInternal(AActor* Actor)
{

	IRoadBrushActorInterface* RoadBrushActor = CastChecked<IRoadBrushActorInterface>(Actor);

	const bool bMarkPackageDirty = false;
	Modify(bMarkPackageDirty);
	int32 Index = ActorsAffectingLandscape.IndexOfByKey(TWeakInterfacePtr<IRoadBrushActorInterface>(RoadBrushActor));
	if (Index != INDEX_NONE)
	{
		ActorsAffectingLandscape.RemoveAt(Index);
		Cache.Remove(TSoftObjectPtr<AActor>(Actor));

		//OnActorsAffectingLandscapeChanged();

		UpdateAffectedWeightmaps();
	}


}

bool AWorldLandscapeBlueprintBrush::IsActorAffectingLandscape(AActor* Actor) const
{
	IRoadBrushActorInterface* RoadBrushActor = Cast<IRoadBrushActorInterface>(Actor);
	return ((RoadBrushActor != nullptr) && RoadBrushActor->AffectsLandscape());
}

void AWorldLandscapeBlueprintBrush::UpdateAffectedWeightmaps()
{

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
				UWorld* World = GetWorld();
				checkf((World != nullptr) && (World->PersistentLevel != nullptr), TEXT("This function should only be called when the world and level are accessible"));
				OnLoadedActorAddedToLevelEventHandle = World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddLambda([this](AActor& InActor) { OnLevelActorAdded(&InActor); });
				OnLoadedActorRemovedFromLevelEventHandle = World->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.AddLambda([this](AActor& InActor) { OnLevelActorRemoved(&InActor); });
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

	if (UWorld* World = GetWorld())
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
			AddActorInternal(Actor, GetWorld(), nullptr, /*bTriggerEvent = */true, /*bModify =*/true);
		}
		else
		{
			RemoveActorInternal(Actor);
		}

		bForceUpdateBrush = true;
	}
	if (InParams.bWeightmapSettingsChanged)
	{
		UpdateAffectedWeightmaps();
	}
	BlueprintRoadBodyChanged(Actor);
	const ULandscapeSettings* LandscapeSettings = GetDefault<ULandscapeSettings>();
	check(LandscapeSettings != nullptr);

	const bool bAllowLandscapeUpdate = (InParams.PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive) || LandscapeSettings->GetShouldUpdateEditLayersDuringInteractiveChanges();
	if (bForceUpdateBrush || (bAffectsLandscape && bAllowLandscapeUpdate))
	{
		RequestLandscapeUpdate(/* bInUserTriggered = */ InParams.bUserTriggered);
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
