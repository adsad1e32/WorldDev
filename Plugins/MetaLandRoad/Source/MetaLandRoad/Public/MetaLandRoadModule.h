#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "MetaLandRoadEditorHooks.h"

/**
 * Runtime module: shader mapping + Editor hooks.
 * Viewport / Mode / Tool live in MetaLandRoadEditor.
 */
class FMetaLandRoadModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FMetaLandRoadModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMetaLandRoadModule>("MetaLandRoad");
	}

private:
	FDelegateHandle PostEngineInitHandle;
};
