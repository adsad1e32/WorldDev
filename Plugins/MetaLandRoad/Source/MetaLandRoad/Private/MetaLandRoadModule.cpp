#include "MetaLandRoadModule.h"
#include "MetaLandRoadLaneAttribute.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "ShaderCore.h"

#if WITH_EDITOR
TFunction<void(UMetaLandRoadSplineComponent*)> FMetaLandRoadEditorHooks::RefreshLaneVisualization;
TFunction<void(AMoKuEditIntersectionActor*, bool)> FMetaLandRoadEditorHooks::RecookJunction;
TFunction<bool(const UMetaLandRoadSplineComponent*, int32&, int32&)> FMetaLandRoadEditorHooks::TryGetSelectedLandscapeKey;
TFunction<void(AMoKuEditSplineActor*)> FMetaLandRoadEditorHooks::NotifySplineComponentSet;
#endif

void FMetaLandRoadModule::StartupModule()
{
	FString ShaderDir;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MetaLandRoad")))
	{
		ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	}
	else
	{
		ShaderDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("MetaLandRoad/Shaders"));
	}
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/MetaLandRoad"), ShaderDir);

	// PostConfigInit is too early for StaticStruct / UHT package construct.
	auto RegisterTypes = []()
	{
		FMetaLandRoadAttributeRegistry::RegisterBuiltInTypes();
	};
	if (GEngine)
	{
		RegisterTypes();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda(RegisterTypes);
	}
}

void FMetaLandRoadModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
}

IMPLEMENT_MODULE(FMetaLandRoadModule, MetaLandRoad)
