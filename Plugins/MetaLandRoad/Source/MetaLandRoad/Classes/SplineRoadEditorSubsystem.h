#pragma once

#include "EditorSubsystem.h"
#include "UnrealEdMisc.h"


#include "SplineRoadEditorSubsystem.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class UWorld;
class UMaterialParameterCollection;


UCLASS()
class USplineRoadEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

	USplineRoadEditorSubsystem();
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UMaterialParameterCollection* GetLandscapeMaterialParameterCollection() const { return LandscapeMaterialParameterCollection; }


private:
	UWorld* GetEditorWorld() const;


	void OnPackageDirtied(UPackage* Package);
	void OnMapChanged(UWorld* InWorld, EMapChangeType InChangeType);

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> LandscapeMaterialParameterCollection;



	//UPROPERTY()
	//TMap<TObjectPtr<UClass>, TObjectPtr<UTexture2D>> WaterActorSprites;
	//UPROPERTY()
	//TObjectPtr<UTexture2D> DefaultWaterActorSprite;
	//UPROPERTY()
	//TObjectPtr<UTexture2D> ErrorSprite;

	///** Set of water body packages which have been silently modified but not dirtied. */
	//TSet<TWeakObjectPtr<UPackage>, TWeakObjectPtrSetKeyFuncs<TWeakObjectPtr<UPackage>>> PackagesNeedingDirtying;

	//bool bSuppressOnDirtyEvents = false;
};
