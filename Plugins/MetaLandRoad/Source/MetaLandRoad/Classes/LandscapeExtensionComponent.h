#pragma once


#include "Components/SceneComponent.h"
#include "LandscapeBlueprintBrushBase.h"
#include "LandscapeEditLayerRenderer.h"
#include "LandscapeEditTypes.h"
#include "UObject/WeakInterfacePtr.h"

class ALandscape;
class ULandscapeEditLayerBase;

#include "LandscapeExtensionComponent.generated.h"

UCLASS(Blueprintable, BlueprintType, ClassGroup = Landscape, meta = (BlueprintSpawnableComponent))
class ULandscapeExtensionComponent : public USceneComponent , public ILandscapeEditLayerRenderer
{
	GENERATED_BODY()

public:
	ULandscapeExtensionComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());


	virtual UTextureRenderTarget2D* RenderLayer_Native(const FLandscapeBrushParameters& InParameters,
		const FTransform& HeightmapToWorld) {
		return InParameters.CombinedResult;
	}

	virtual bool CanAffectHeightmap() const { return false; }
	virtual bool CanAffectWeightmap() const { return false; }
	virtual bool CanAffectWeightmapLayer(const FName& InLayerName) const { return false; }
	virtual bool CanAffectVisibilityLayer() const { return false; }


	virtual void SetLandscape(ALandscape* NewLandscape) {};
	virtual bool IsEnabled() const { return bIsEnabled; }

	bool CanAffectLandscape() const { return CanAffectHeightmap() || CanAffectWeightmap() || CanAffectVisibilityLayer(); }

	// These compose IsEnabled with the appropriate CanAffect functions
	bool AffectsHeightmap() const { return IsEnabled() && CanAffectHeightmap(); }
	bool AffectsWeightmap() const { return IsEnabled() && CanAffectWeightmap(); }
	bool AffectsWeightmapLayer(const FName& InLayerName) const { return IsEnabled() && CanAffectWeightmapLayer(InLayerName); }
	bool AffectsVisibilityLayer() const { return IsEnabled() && CanAffectVisibilityLayer(); }

	virtual void GetRenderDependencies(TSet<UObject*>& OutDependencies) const {}

	void RequestLandscapeUpdate(bool bInUserTriggeredUpdate = false);

	void SetIsEnabled(bool bEnabledIn);



	//double GetPriority() const { return Priority; }

	//void SetPriority(double PriorityIn);


	FGuid GetEditLayerGuid() const { return EditLayerGuid; }

	void SetEditLayerGuid(const FGuid& GuidIn);



protected:
		UPROPERTY()
		FGuid EditLayerGuid;

#if WITH_EDITOR
	// USceneComponent
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) {};

	// UActorComponent
	virtual void CheckForErrors(){};
	virtual void OnComponentCreated()override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy){Super::OnComponentDestroyed(bDestroyingHierarchy);};
	virtual void OnRegister() { Super::OnRegister(); };
	virtual void OnUnregister() { Super::OnUnregister(); };
	virtual void GetActorDescProperties(FPropertyPairsMap& PropertyPairsMap) const override {};
	//virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;

	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {};
	virtual bool IsPostLoadThreadSafe() const override { return true; }
	virtual void PostLoad() {};
	virtual void PostEditUndo() {};
#endif

	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForClient() const override { return false; }
	virtual bool NeedsLoadForServer() const override { return false; }

	bool bIsEnabled = true;


#if WITH_EDITOR

	//TWeakObjectPtr<ULandscapePatchEditLayer> EditLayer;


	TWeakObjectPtr<ULandscapeEditLayerBase> EditLayer;
	bool BindToEditLayer(const FGuid& Guid);
	void ResetEditLayer();
	bool BindToLandscape(ALandscape* LandscapeIn);
	bool BindToAnyLandscape();
	void UpdateEditLayerFromDetailPanelLayerName();

	void ResetPatchManager();

	// Tells us whether the patch is attached to an actor that is being dragged around as an editor preview
	bool IsPatchPreview();

	// Used to avoid spamming warning messages
	bool bGaveCouldNotBindToEditLayerWarning = false;
	bool bGaveMissingEditLayerGuidWarning = false;
	bool bGaveMismatchedLandscapeWarning = false;
	bool bGaveNotInPatchManagerWarning = false;
	bool bGaveMissingLandscapeWarning = false;
	void ResetWarnings();
	bool bInstanceDataApplied = false;
	bool bDeferUpdateRequestUntilInstanceData = false;

	void ApplyComponentInstanceData(struct FLandscapePatchComponentInstanceData* ComponentInstanceData, ECacheApplyPhase CacheApplyPhase);
#endif

private:

	FString EditLayerName = TEXT("Procedural Extension");
	TSoftObjectPtr<ALandscape> Landscape = nullptr;


private:
	//void SetEditLayerLayerName();
	void GetActiveLandscape();
	void SetEditLayer();
};