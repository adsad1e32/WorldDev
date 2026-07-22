#include "SplineRoadEditorSubsystem.h"
//#include "ActionableMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Algo/Count.h"
#include "WorldEditorSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SplineRoadEditorSubsystem)

#define LOCTEXT_NAMESPACE "SplineRoadEditorSubsystem"


USplineRoadEditorSubsystem::USplineRoadEditorSubsystem()
{
	//LandscapeMaterialParameterCollection = GetDefault<UWorldEditorSettings>()->LandscapeMaterialParameterCollection.LoadSynchronous();

}

void USplineRoadEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);


	LandscapeMaterialParameterCollection = GetDefault<UWorldEditorSettings>()->LandscapeMaterialParameterCollection.LoadSynchronous();

	//IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
	//WaterModule.SetWaterEditorServices(this);

	UPackage::PackageDirtyStateChangedEvent.AddUObject(this, &USplineRoadEditorSubsystem::OnPackageDirtied);
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnMapChanged().AddUObject(this, &USplineRoadEditorSubsystem::OnMapChanged);
}

void USplineRoadEditorSubsystem::Deinitialize()
{
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnMapChanged().RemoveAll(this);
	UPackage::PackageDirtyStateChangedEvent.RemoveAll(this);

	//IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
	//if (IWaterEditorServices* WaterEditorServices = WaterModule.GetWaterEditorServices())
	//{
	//	if (WaterEditorServices == this)
	//	{
	//		WaterModule.SetWaterEditorServices(nullptr);
	//	}
	//}

	Super::Deinitialize();
}


UWorld* USplineRoadEditorSubsystem::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
}



void USplineRoadEditorSubsystem::OnPackageDirtied(UPackage* Package)
{
	//if (!bSuppressOnDirtyEvents)
	//{
	//	if (Package && Package->IsDirty())
	//	{
	//		// Only update the modified packages messsage if we actually made a change:
	//		if (PackagesNeedingDirtying.Remove(Package) > 0)
	//		{
	//			UpdateModifiedPackagesMessage();
	//		}
	//	}
	//}
}

void USplineRoadEditorSubsystem::OnMapChanged(UWorld* InWorld, EMapChangeType InChangeType)
{
//	// Stop tracking packages for maps that are no longer loaded.
//	if (InChangeType == EMapChangeType::TearDownWorld)
//	{
//		ClearModifiedPackages();
//	}
}


#undef LOCTEXT_NAMESPACE