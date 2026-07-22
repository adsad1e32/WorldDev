#pragma once

#include "CoreMinimal.h"
#include "LandscapeBlueprintBrush.h"
#include "RoadBrushActorInterface.h"
#include "WorldLandscapeBlueprintBrush.generated.h"

class ALandscape;
class AMoKuEditBaseActor;

UCLASS(Blueprintable, Abstract, hidecategories = (Collision, Replication, Input, LOD, Actor, Cooking, Rendering))
class AWorldLandscapeBlueprintBrush : public ALandscapeBlueprintBrush
{
    GENERATED_BODY()

public:

    AWorldLandscapeBlueprintBrush(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    UFUNCTION(BlueprintCallable, Category = "Road", meta = (DeterminesOutputType = "RoadBodyClass", DynamicOutputParam = "OutWaterBodies"))
    void  GetRoadActors(TSubclassOf<AMoKuEditBaseActor> RoadBodyClass, TArray<AMoKuEditBaseActor*>& OutRoadBodies) const;

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginDestroy() override;
    virtual void SetOwningLandscape(ALandscape* InOwningLandscape) override;
    virtual void PostLoad() override;
    virtual void PostActorCreated() override;
    virtual void Tick(float DeltaTime) override;
    virtual void PostRegisterAllComponents() override;
    void SetTargetLandscape(ALandscape* InTargetLandscape);


    const TArray<TWeakInterfacePtr<IRoadBrushActorInterface>>& GetActorsAffectingLandscape() const { return ActorsAffectingLandscape; }


    UFUNCTION(BlueprintCallable, Category = "Cache", meta = (DeterminesOutputType = "CacheClass"))
    UObject* GetActorCache(AActor* InActor, TSubclassOf<UObject> CacheClass) const;

    UFUNCTION(BlueprintCallable, Category = "Cache")
	void SetActorCache(AActor* InActor, UObject* InCache);

    UFUNCTION(BlueprintNativeEvent)
    void BlueprintRoadBodyChanged(AActor* InActor);

 //   UFUNCTION(BlueprintNativeEvent, meta = (CallInEditor = "true", Category = "Debug"))
	//void BlueprintRoadBodyChanged(AActor* Actor);
	virtual void BlueprintRoadBodyChanged_Native(AActor* Actor) {}

protected:

    virtual void BeginPlay() override;

private:
    TArray<TWeakInterfacePtr<IRoadBrushActorInterface>> ActorsAffectingLandscape;
    FDelegateHandle OnLevelActorAddedHandle;
    FDelegateHandle OnLevelActorDeletedHandle;
    FDelegateHandle OnWorldPostInitHandle;
    FDelegateHandle OnLevelAddedToWorldHandle;
    FDelegateHandle OnLevelRemovedFromWorldHandle;
#if WITH_EDITOR
    FDelegateHandle OnLoadedActorAddedToLevelEventHandle;
    FDelegateHandle OnLoadedActorRemovedFromLevelEventHandle;
#endif

private:

    template<class T>
    friend class FGetActorsOfType;

    void AddActorInternal(AActor* Actor, const UWorld* CurrentWorld, UObject* InCache, bool bTriggerEvent, bool bModify);
    void RemoveActorInternal(AActor* Actor);
    void OnLevelActorAdded(AActor* InActor);
    void OnLevelActorRemoved(AActor* InActor);
    void RegisterDelegates();
    void UpdateActors(bool bTriggerEvents = true);
    void UpdateAffectedWeightmaps();
    void ClearActors();
    bool IsActorAffectingLandscape(AActor* Actor) const;
    void OnRoadBrushActorChanged(const IRoadBrushActorInterface::FRoadBrushActorChangedEventParams& InParams);



    UPROPERTY(Transient, DuplicateTransient, VisibleAnywhere, AdvancedDisplay, meta = (Category = "Debug"))
	TMap<TSoftObjectPtr<AActor>, TObjectPtr<UObject>> Cache;



};