#pragma once
#include "ActorFactories/ActorFactory.h"
#include "WorldBrushEditorManagerFactory.generated.h"

UCLASS(MinimalAPI, config = Editor)
class UWorldBrushEditorManagerFactory : public UActorFactory
{
	GENERATED_UCLASS_BODY()

	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
};