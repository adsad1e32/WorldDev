
#include "WorldEditorSettings.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldEditorSettings)

namespace MetaLandRoadSettingsPaths
{
	static const TCHAR* MPC = TEXT("/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape");
	static const TCHAR* BrushAngle = TEXT("/Water/Materials/Brushes/MeshBrush_Angle.MeshBrush_Angle");
	static const TCHAR* BrushWidth = TEXT("/Water/Materials/Brushes/MeshBrush_Width.MeshBrush_Width");
	static const TCHAR* BrushWeightmap = TEXT("/Water/Materials/Brushes/MeshBrush_Weightmap.MeshBrush_Weightmap");
	static const TCHAR* CacheDistanceField = TEXT("/Landmass/Landscape/BlueprintBrushes/Materials/CacheDistanceField.CacheDistanceField");
	static const TCHAR* CompositeBody = TEXT("/Water/Materials/Brushes/CompositeWaterBodyTexture.CompositeWaterBodyTexture");
	static const TCHAR* JumpFloodStep = TEXT("/Landmass/DistanceFields/Materials/JumpFloodStep.JumpFloodStep");
	static const TCHAR* BlurEdges = TEXT("/Landmass/DistanceFields/Materials/BlurEdges.BlurEdges");
	static const TCHAR* FindEdges = TEXT("/Landmass/DistanceFields/Materials/DetectEdges.DetectEdges");
	static const TCHAR* RenderSplineDepths = TEXT("/Landmass/Landscape/BlueprintBrushes/Materials/RenderSplineDepths.RenderSplineDepths");

	static bool NeedsRepairMaterial(const TSoftObjectPtr<UMaterialInterface>& SoftPtr)
	{
		if (SoftPtr.IsNull())
		{
			return true;
		}

		const FString Path = SoftPtr.ToSoftObjectPath().ToString();
		// Broken TestMode-era forks under MetaLandRoad Landscape content.
		if (Path.Contains(TEXT("/MetaLandRoad/Material/Landscape"))
			|| Path.Contains(TEXT("/MetaLandRoad/Landscape/BlueprintBrushes")))
		{
			return true;
		}

		// Soft path set but asset missing → Project Settings shows None and Save clears it.
		if (SoftPtr.LoadSynchronous() == nullptr)
		{
			return true;
		}

		return false;
	}

	static bool NeedsRepairMPC(const TSoftObjectPtr<UMaterialParameterCollection>& SoftPtr)
	{
		if (SoftPtr.IsNull())
		{
			return true;
		}

		const FString Path = SoftPtr.ToSoftObjectPath().ToString();
		if (Path.Contains(TEXT("/MetaLandRoad/")))
		{
			return true;
		}

		return SoftPtr.LoadSynchronous() == nullptr;
	}

	static void AssignMaterial(TSoftObjectPtr<UMaterialInterface>& SoftPtr, const TCHAR* OfficialPath)
	{
		SoftPtr = FSoftObjectPath(OfficialPath);
	}
}

UWorldEditorSettings::UWorldEditorSettings()
	: TextureGroupForGeneratedTextures(TEXTUREGROUP_World)
	, HeightTextureSize(2048)
	, LandscapeMaterialParameterCollection(FSoftObjectPath(MetaLandRoadSettingsPaths::MPC))
	, WorldEditorManagerClassPath("/Script/MetaLandRoad.WorldSceneManagement")
	, DefaultBrushAngleFalloffMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::BrushAngle))
	, DefaultBrushWidthFalloffMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::BrushWidth))
	, DefaultBrushWeightmapMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::BrushWeightmap))
	, DefaultCacheDistanceFieldCacheMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::CacheDistanceField))
	, DefaultCompositeRoadBodyTextureMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::CompositeBody))
	, DefaultJumpFloodStepMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::JumpFloodStep))
	, DefaultBlurEdgesMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::BlurEdges))
	, DefaultFindEdgesMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::FindEdges))
	, DefaultDrawCanvasMaterial(FSoftObjectPath())
	, DefaultRenderRoadSplineDepthsMaterial(FSoftObjectPath(MetaLandRoadSettingsPaths::RenderSplineDepths))
{
}

void UWorldEditorSettings::PostInitProperties()
{
	Super::PostInitProperties();
	RepairMissingBrushMaterialPaths();
}

#if WITH_EDITOR
void UWorldEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RepairMissingBrushMaterialPaths();
}
#endif

void UWorldEditorSettings::RepairMissingBrushMaterialPaths()
{
	using namespace MetaLandRoadSettingsPaths;

	if (NeedsRepairMPC(LandscapeMaterialParameterCollection))
	{
		LandscapeMaterialParameterCollection = FSoftObjectPath(MPC);
	}

	if (NeedsRepairMaterial(DefaultBrushAngleFalloffMaterial))
	{
		AssignMaterial(DefaultBrushAngleFalloffMaterial, BrushAngle);
	}
	if (NeedsRepairMaterial(DefaultBrushWidthFalloffMaterial))
	{
		AssignMaterial(DefaultBrushWidthFalloffMaterial, BrushWidth);
	}
	if (NeedsRepairMaterial(DefaultBrushWeightmapMaterial))
	{
		AssignMaterial(DefaultBrushWeightmapMaterial, BrushWeightmap);
	}
	if (NeedsRepairMaterial(DefaultCacheDistanceFieldCacheMaterial))
	{
		AssignMaterial(DefaultCacheDistanceFieldCacheMaterial, CacheDistanceField);
	}
	if (NeedsRepairMaterial(DefaultCompositeRoadBodyTextureMaterial))
	{
		AssignMaterial(DefaultCompositeRoadBodyTextureMaterial, CompositeBody);
	}
	if (NeedsRepairMaterial(DefaultJumpFloodStepMaterial))
	{
		AssignMaterial(DefaultJumpFloodStepMaterial, JumpFloodStep);
	}
	if (NeedsRepairMaterial(DefaultBlurEdgesMaterial))
	{
		AssignMaterial(DefaultBlurEdgesMaterial, BlurEdges);
	}
	if (NeedsRepairMaterial(DefaultFindEdgesMaterial))
	{
		AssignMaterial(DefaultFindEdgesMaterial, FindEdges);
	}
	if (NeedsRepairMaterial(DefaultRenderRoadSplineDepthsMaterial))
	{
		AssignMaterial(DefaultRenderRoadSplineDepthsMaterial, RenderSplineDepths);
	}
}

TSubclassOf<AWorldLandscapeBlueprintBrush> UWorldEditorSettings::GetWorldEditorManagerClass() const
{
	return WorldEditorManagerClassPath.TryLoadClass<AWorldLandscapeBlueprintBrush>();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBrushAngleFalloffMaterial() const
{
	if (UMaterialInterface* Material = DefaultBrushAngleFalloffMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::BrushAngle).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBrushWidthFalloffMaterial() const
{
	if (UMaterialInterface* Material = DefaultBrushWidthFalloffMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::BrushWidth).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBrushWeightmapMaterial() const
{
	if (UMaterialInterface* Material = DefaultBrushWeightmapMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::BrushWeightmap).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultCacheDistanceFieldCacheMaterial() const
{
	if (UMaterialInterface* Material = DefaultCacheDistanceFieldCacheMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::CacheDistanceField).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultCompositeRoadBodyTextureMaterial() const
{
	if (UMaterialInterface* Material = DefaultCompositeRoadBodyTextureMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::CompositeBody).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultJumpFloodStepMaterial() const
{
	if (UMaterialInterface* Material = DefaultJumpFloodStepMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::JumpFloodStep).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultBlurEdgesMaterial() const
{
	if (UMaterialInterface* Material = DefaultBlurEdgesMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::BlurEdges).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultFindEdgesMaterial() const
{
	if (UMaterialInterface* Material = DefaultFindEdgesMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::FindEdges).TryLoad());
}

UMaterialInterface* UWorldEditorSettings::GetDefaultDrawCanvasMaterial() const
{
	return DefaultDrawCanvasMaterial.LoadSynchronous();
}

UMaterialInterface* UWorldEditorSettings::GetDefaultRenderRoadSplineDepthsMaterial() const
{
	if (UMaterialInterface* Material = DefaultRenderRoadSplineDepthsMaterial.LoadSynchronous())
	{
		return Material;
	}
	return Cast<UMaterialInterface>(FSoftObjectPath(MetaLandRoadSettingsPaths::RenderSplineDepths).TryLoad());
}
