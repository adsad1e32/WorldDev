#pragma once

#include "Engine/DeveloperSettings.h"
#include "Materials/MaterialParameterCollection.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "WorldEditorSettings.generated.h"


class UMaterialInterface;
class UMaterialParameterCollection;


UCLASS(MinimalAPI, config = Engine, defaultconfig, meta = (DisplayName = "World Editor"))
class UWorldEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWorldEditorSettings();

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/** Fill null / broken MetaLandRoad fork soft paths with official Water/Landmass assets. */
	void RepairMissingBrushMaterialPaths();

	UMaterialInterface* GetDefaultBrushAngleFalloffMaterial() const;
	FSoftObjectPath GetDefaultBrushAngleFalloffMaterialPath() const { return DefaultBrushAngleFalloffMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBrushWidthFalloffMaterial() const;
	FSoftObjectPath GetDefaultBrushWidthFalloffMaterialPath() const { return DefaultBrushWidthFalloffMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBrushWeightmapMaterial() const;
	FSoftObjectPath GetDefaultBrushWeightmapMaterialPath() const { return DefaultBrushWeightmapMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultCacheDistanceFieldCacheMaterial() const;
	FSoftObjectPath GetDefaultCacheDistanceFieldCacheMaterialPath() const { return DefaultCacheDistanceFieldCacheMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultCompositeRoadBodyTextureMaterial() const;
	FSoftObjectPath GetDefaultCompositeRoadBodyTextureMaterialPath() const { return DefaultCompositeRoadBodyTextureMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultJumpFloodStepMaterial() const;
	FSoftObjectPath GetDefaultJumpFloodStepMaterialPath() const { return DefaultJumpFloodStepMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultBlurEdgesMaterial() const;
	FSoftObjectPath GetDefaultBlurEdgesMaterialPath() const { return DefaultBlurEdgesMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultFindEdgesMaterial() const;
	FSoftObjectPath GetDefaultFindEdgesMaterialPath() const { return DefaultFindEdgesMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultDrawCanvasMaterial() const;
	FSoftObjectPath GetDefaultDrawCanvasMaterialPath() const { return DefaultDrawCanvasMaterial.ToSoftObjectPath(); }

	UMaterialInterface* GetDefaultRenderRoadSplineDepthsMaterial() const;
	FSoftObjectPath GetDefaultRenderRiverSplineDepthsMaterialPath() const { return DefaultRenderRoadSplineDepthsMaterial.ToSoftObjectPath(); }

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
	
	UPROPERTY(EditAnywhere, config, Category = Brush)
	bool bUpdateWaterMeshDuringInteractiveChanges = false;

	///** Class of the water zone to be used*/
	//UPROPERTY(EditAnywhere, config, Category = Water, meta = (MetaClass = "/Script/Water.WaterZone"))
	//FSoftClassPath WaterZoneClassPath;

	/** Class of the water brush to be used in landscape */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (MetaClass = "/Script/MetaLandRoad.WorldLandscapeBlueprintBrush"), AdvancedDisplay)
	FSoftClassPath WorldEditorManagerClassPath;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushAngleFalloffMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushWidthFalloffMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultBrushWeightmapMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultCacheDistanceFieldCacheMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultCompositeRoadBodyTextureMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultJumpFloodStepMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultBlurEdgesMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultFindEdgesMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultDrawCanvasMaterial;

	UPROPERTY(EditAnywhere, config, Category = Brush, AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> DefaultRenderRoadSplineDepthsMaterial;

	/** Pavement paint layer (road bed only). Must match Landscape Material / LayerInfo name. */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (DisplayName = "Default Road Weightmap Layer"))
	FName DefaultRoadWeightmapLayerName = FName(TEXT("Road"));

	/** Embankment / side-slope paint layer (Dirty). Must match Landscape Material / LayerInfo name. */
	UPROPERTY(EditAnywhere, config, Category = Brush, meta = (DisplayName = "Default Shoulder Weightmap Layer"))
	FName DefaultShoulderWeightmapLayerName = FName(TEXT("Dirty"));

};