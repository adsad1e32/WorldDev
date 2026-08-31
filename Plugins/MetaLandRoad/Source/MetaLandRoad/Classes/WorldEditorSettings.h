#pragma once

#include "Engine/DeveloperSettings.h"
#include "Materials/MaterialParameterCollection.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "WorldEditorSettings.generated.h"

class UMaterialParameterCollection;


UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "World Editor"))
class METALANDROAD_API UWorldEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWorldEditorSettings();

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/** Fill a null / broken Landscape MPC soft path with the official Landmass asset. */
	void RepairMissingBrushMaterialPaths();

	bool GetShouldUpdateWaterMeshDuringInteractiveChanges() const { return bUpdateWaterMeshDuringInteractiveChanges; }

	FName GetDefaultRoadWeightmapLayerName() const { return DefaultRoadWeightmapLayerName; }
	FName GetDefaultShoulderWeightmapLayerName() const { return DefaultShoulderWeightmapLayerName; }

public:
	/** The texture group to use for generated textures such as the combined velocity and height texture */
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TEnumAsByte<TextureGroup> TextureGroupForGeneratedTextures;

	UPROPERTY(EditAnywhere, config, Category = Rendering, meta = (ClampMin = 1, ClampMax = 2048))
	int32 HeightTextureSize;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialParameterCollection> LandscapeMaterialParameterCollection;

	TSubclassOf<AWorldLandscapeBlueprintBrush> GetWorldEditorManagerClass() const;
	FSoftClassPath GetWorldEditorManagerClassPath() const { return WorldEditorManagerClassPath; }

private:
	
	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	bool bUpdateWaterMeshDuringInteractiveChanges = false;

	/** Class of the landscape brush used by WorldEditor */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (MetaClass = "/Script/MetaLandRoad.WorldLandscapeBlueprintBrush"), AdvancedDisplay)
	FSoftClassPath WorldEditorManagerClassPath;

	/** Pavement paint layer (road bed only). Must match Landscape Material / LayerInfo name. */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (DisplayName = "Default Road Weightmap Layer"))
	FName DefaultRoadWeightmapLayerName = FName(TEXT("Road"));

	/** Legacy Dirty layer name. Profile stamp no longer paints this layer. */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (DisplayName = "Default Shoulder Weightmap Layer"))
	FName DefaultShoulderWeightmapLayerName = FName(TEXT("Dirty"));

};
