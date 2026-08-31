
#include "WorldEditorSettings.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldEditorSettings)

namespace MetaLandRoadSettingsPaths
{
	static const TCHAR* MPC = TEXT("/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape");

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
}

UWorldEditorSettings::UWorldEditorSettings()
	: TextureGroupForGeneratedTextures(TEXTUREGROUP_World)
	, HeightTextureSize(2048)
	, LandscapeMaterialParameterCollection(FSoftObjectPath(MetaLandRoadSettingsPaths::MPC))
	, WorldEditorManagerClassPath("/Script/MetaLandRoad.WorldSceneManagement")
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
}

TSubclassOf<AWorldLandscapeBlueprintBrush> UWorldEditorSettings::GetWorldEditorManagerClass() const
{
	return WorldEditorManagerClassPath.TryLoadClass<AWorldLandscapeBlueprintBrush>();
}
