#include "LandscapeExtensionComponent.h"


#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeModule.h"
#include "LandscapeEditLayer.h"

#define LOCTEXT_NAMESPACE "LandscapePatch"

ULandscapeExtensionComponent::ULandscapeExtensionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}


void ULandscapeExtensionComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	SetEditLayer();
}

void ULandscapeExtensionComponent::SetEditLayer()
{

	if (!Landscape.IsValid())
	{
		GetActiveLandscape();
	}

	if (Landscape)
	{
		int32 ExistingLayerIdx = Landscape->GetLayerIndex(FName(EditLayerName));
		if (ExistingLayerIdx != INDEX_NONE)
		{
			return;
		}
		else
		{
			int32 LayerIdx = Landscape->CreateLayer(FName(EditLayerName), ULandscapeLayerInfoObject::StaticClass());
			if (LayerIdx == INDEX_NONE)
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to create layer '%s'!"), *EditLayerName);
			}
		}

	}
	//const ULandscapeEditLayerBase* CurrentEditLayer = Landscape->GetEditLayerConst(FName(EditLayerName));
	//if (CurrentEditLayer)
	//{
	//	SetEditLayerGuid(FGuid());
	//}
}


void ULandscapeExtensionComponent::GetActiveLandscape()
{
	UWorld* World = GetWorld();
	AActor* AttachActor = GetOwner();

	if (!World)
	{		
		World = AttachActor->GetWorld();
	}
	if (AttachActor)
	{
		for (TActorIterator<ALandscape> LandscapeIterator(World); LandscapeIterator; ++LandscapeIterator)
		{
			Landscape = *LandscapeIterator;
			break;
		}
	}
}


void ULandscapeExtensionComponent::SetEditLayerGuid(const FGuid& GuidIn)
{

	//if (!GuidIn.IsValid())
	//{
	//	return;
	//}

	if(!EditLayer.IsValid() || !EditLayer.Get())
	{
		ALandscape* OwningLandscape = Landscape.Get();
		if (OwningLandscape)
		{
			int32 LayerIdx = OwningLandscape->CreateLayer(FName(EditLayerName), ULandscapeEditLayerBase::StaticClass());

			const TArray<ULandscapeEditLayerBase*> EditLayers = Landscape->GetEditLayers();

			EditLayer = EditLayers[LayerIdx];
		}

	}
	#if WITH_EDITOR
		Modify(false);
		//BindToEditLayer(GuidIn);
	#endif
}

#undef LOCTEXT_NAMESPACE