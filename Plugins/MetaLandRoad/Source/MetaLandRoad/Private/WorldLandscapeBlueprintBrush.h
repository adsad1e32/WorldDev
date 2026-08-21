#pragma once

#include "CoreMinimal.h"
#include "LandscapeBlueprintBrush.h"
#include "RoadBrushActorInterface.h"
#include "WorldLandscapeBlueprintBrush.generated.h"

class ALandscape;
class AMoKuEditBaseActor;
class AWorldSceneManagement;

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

    /**
     * Ensures a WorldSceneManagement brush + edit layer "WorldEditor" exist on the landscape.
     * Spawns a new manager when another landscape already owns the only existing one.
     */
    static AWorldSceneManagement* EnsureWorldEditorManagerForLandscape(ALandscape* Landscape);

    /** Picks landscapes under/near the road actor and ensures WorldEditor on each. */
    static AWorldSceneManagement* EnsureWorldEditorManagerForRoadActor(AActor* RoadActor);
    /** Drop a deleted road/junction from the brush list and rebuild landscape once. */
    void HandleRoadActorRemoved(AActor* Actor);

    /** Removes stale null (None) brush slots left by duplicate AddBrushToLayer calls. */
    static int32 CompactNullBlueprintBrushesOnEditLayer(ALandscape* Landscape, FName LayerName = FName(TEXT("WorldEditor")));

    UFUNCTION(CallInEditor, Category = "MetaLandRoad|Debug", meta = (DisplayName = "Compact Stale Landscape Brushes"))
    void DebugCompactStaleLandscapeBrushes();

    const TArray<TWeakInterfacePtr<IRoadBrushActorInterface>>& GetActorsAffectingLandscape() const { return ActorsAffectingLandscape; }

	/** Last road/junction that requested a landscape update (move / edit). */
	AActor* GetLandscapeStampFocusActor() const { return LandscapeStampFocusActor.Get(); }
	void SetLandscapeStampFocusActor(AActor* Actor);

	/** Full-landscape remesh after map open. Saved heightmaps stay flat until this runs. */
	void RequestInitialLandscapeUpdate();

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
    FDelegateHandle OnMapOpenedHandle;
#endif

private:

    template<class T>
    friend class FGetActorsOfType;

    void AddActorInternal(AActor* Actor, const UWorld* CurrentWorld, UObject* InCache, bool bTriggerEvent, bool bModify);
    void RemoveActorInternal(AActor* Actor);
    void OnLevelActorAdded(AActor* InActor);
    void OnLevelActorRemoved(AActor* InActor);
    void RegisterDelegates();
    void ClearActors();
    bool IsActorAffectingLandscape(AActor* Actor) const;
    void OnRoadBrushActorChanged(const IRoadBrushActorInterface::FRoadBrushActorChangedEventParams& InParams);

protected:
    void UpdateActors(bool bTriggerEvents = true);
    void UpdateAffectedWeightmaps();

	/** When true, GetRenderItems does not shrink to a dirty box (map-open remesh). */
	bool bForceFullHeightStamp = false;
	bool bDidInitialLandscapeUpdate = false;



    UPROPERTY(Transient, DuplicateTransient, VisibleAnywhere, AdvancedDisplay, meta = (Category = "Debug"))
	TMap<TSoftObjectPtr<AActor>, TObjectPtr<UObject>> Cache;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LandscapeStampFocusActor;



};