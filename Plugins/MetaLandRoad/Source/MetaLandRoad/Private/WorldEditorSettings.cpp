
#include "WorldEditorSettings.h"
#include "Materials/MaterialInterface.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldEditorSettings)



UWorldEditorSettings::UWorldEditorSettings()
	: TextureGroupForGeneratedTextures(TEXTUREGROUP_World)
	, HeightTextureSize(2048)
	, LandscapeMaterialParameterCollection(FSoftObjectPath(TEXT("/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape")))
	, WorldEditorManagerClassPath("/Script/MetaLandRoad.WorldLandscapeBlueprintBrush")
	, DefaultBrushAngleFalloffMaterial(FSoftObjectPath(TEXT("/MetaLandRoad/Material/Landscape/LandmassBrush_Blender.LandmassBrush_Blender")))
	, DefaultBrushWidthFalloffMaterial(FSoftObjectPath(TEXT("/Water/Materials/Brushes/MeshBrush_Width.MeshBrush_Width")))
	, DefaultBrushWeightmapMaterial(FSoftObjectPath(TEXT("/MetaLandRoad/Material/Landscape/Brush/RoadMeshBrush_Weightmap.RoadMeshBrush_Weightmap")))
	, DefaultCacheDistanceFieldCacheMaterial(FSoftObjectPath(TEXT("MetaLandRoad/Material/Landscape/CacheDistanceField.CacheDistanceField")))
	, DefaultCompositeRoadBodyTextureMaterial(FSoftObjectPath(TEXT("MetaLandRoad/Material/Landscape/Brush/CompositeMeshBodyTexture.CompositeMeshBodyTexture")))
	, DefaultJumpFloodStepMaterial(FSoftObjectPath(TEXT("/Landmass/DistanceFields/Materials/JumpFloodStep.JumpFloodStep")))
	, DefaultBlurEdgesMaterial(FSoftObjectPath(TEXT("/Landmass/DistanceFields/Materials/BlurEdges.BlurEdges")))
	, DefaultFindEdgesMaterial(FSoftObjectPath(TEXT("/Landmass/DistanceFields/Materials/DetectEdges.DetectEdges")))
	, DefaultDrawCanvasMaterial(FSoftObjectPath(TEXT("/Water/Materials/Brushes/CanvasDrawing.CanvasDrawing")))
	, DefaultRenderRoadSplineDepthsMaterial(FSoftObjectPath(TEXT("/MetaLandRoad/Material/Landscape/RenderMeshDepths.RenderMeshDepths")))
	//, DefaultRenderRoadSplineDepthsMaterial(FSoftObjectPath(TEXT("/MetaLandRoad/Material/Landscape/RenderMeshDepths.RenderMeshDepths")))
{
	
}

TSubclassOf<AWorldLandscapeBlueprintBrush> UWorldEditorSettings::GetWorldEditorManagerClass() const
{

	return WorldEditorManagerClassPath.TryLoadClass<AWorldLandscapeBlueprintBrush>();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBrushAngleFalloffMaterial() const
{
	return DefaultBrushAngleFalloffMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBrushWidthFalloffMaterial() const
{
	return DefaultBrushWidthFalloffMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBrushWeightmapMaterial() const
{
	return DefaultBrushWeightmapMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultCacheDistanceFieldCacheMaterial() const
{
	return DefaultCacheDistanceFieldCacheMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultCompositeRoadBodyTextureMaterial() const
{
	return DefaultCompositeRoadBodyTextureMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultJumpFloodStepMaterial() const
{
	return DefaultJumpFloodStepMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBlurEdgesMaterial() const
{
	return DefaultBlurEdgesMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultFindEdgesMaterial() const
{
	return DefaultFindEdgesMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultDrawCanvasMaterial() const
{
	return DefaultDrawCanvasMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultRenderRoadSplineDepthsMaterial() const
{
	return DefaultRenderRoadSplineDepthsMaterial.LoadSynchronous();
}
