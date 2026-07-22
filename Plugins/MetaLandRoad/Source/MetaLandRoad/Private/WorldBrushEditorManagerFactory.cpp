#include "WorldBrushEditorManagerFactory.h"
#include "EditSplineRoadInfo.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldBrushEditorManagerFactory)

#define LOCTEXT_NAMESPACE "WorldBrushEditorManagerFactory"

UWorldBrushEditorManagerFactory::UWorldBrushEditorManagerFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("WorldBrushEditorManagerFactoryDisplayName", "WorldBrush Editor Manager");
	NewActorClass = AWorldSceneManagement::StaticClass();
}

void UWorldBrushEditorManagerFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AWorldSceneManagement* WorldEditorBrushManager = CastChecked<AWorldSceneManagement>(NewActor);
	WorldEditorBrushManager->SetupDefaultMaterials();
}

#undef LOCTEXT_NAMESPACE
