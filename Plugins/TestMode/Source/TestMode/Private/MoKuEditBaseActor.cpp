#pragma once

#include "MoKuEditBaseActor.h"
#include "LevelEditor.h"
#include "TestModeEditorMode.h"
#include "EditorModeManager.h"
#include  "EditSplineRoadInfo.h"
#include "LandscapeEditLayer.h"

#define LOCTEXT_NAMESPACE "MoKuEditBaseActor"

//AMoKuEditBaseActor::AMoKuEditBaseActor(const FObjectInitializer& ObjectInitializer)
//	:Super(ObjectInitializer)
//{
//	IsInit = true;
//	//UMaterial* BasicMaterial = LoadObject<UMaterial>(nullptr, TEXT("/TestMode/Material/Landscape/LandmassBrush_Width.LandmassBrush_Width"));
//	//if (BasicMaterial)
//	//{
//	//	BrushMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, this);
//	//}
//
//	//LandscapeExtension = CreateDefaultSubobject<UActorComponent>(TEXT("LandscapeExtension"));
//	//RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
//	//SetRootComponent(LandscapeExtension);
//}


void AMoKuEditBaseActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
	UpdateBrushMaterial();
}

void AMoKuEditBaseActor::PostEditUndo()
{
	Super::PostEditUndo();

}
void AMoKuEditBaseActor::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void AMoKuEditBaseActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FOnRoadBodyChangedParams Params(PropertyChangedEvent);
	Params.bUserTriggered = true;
	OnPostEditChangeProperty(Params);

	Super::PostEditChangeProperty(PropertyChangedEvent);


}
void AMoKuEditBaseActor::OnPostEditChangeProperty(FOnRoadBodyChangedParams& InOutOnWaterBodyChangedParams)
{
	const FPropertyChangedEvent& PropertyChangedEvent = InOutOnWaterBodyChangedParams.PropertyChangedEvent;
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AMoKuEditBaseActor, RoadBodyWeightmapSettings))
	{
		InOutOnWaterBodyChangedParams.bWeightmapSettingsChanged = true;
	}
	OnRoadBodyChanged(InOutOnWaterBodyChangedParams);
}

void AMoKuEditBaseActor::InitSceneManagement()
{
	if (!SceneManagement.IsValid() || !SceneManagement.Get())
	{

		UWorld* World = GetWorld();
		if (World)
		{
			// 遍历当前世界中所有 AYourTargetClass 类型的 Actor
			for (TActorIterator<AWorldSceneManagement> It(World); It; ++It)
			{
				AWorldSceneManagement* FoundActorManager = *It;

				if (FoundActorManager)
				{
					SceneManagement = FoundActorManager;
					break;
				}
			}
		}
		return;




		//if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		//{
		//	TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();
		//	if (LevelEditorPtr.IsValid())
		//	{
		//		UTestModeEditorMode* EditMode = Cast<UTestModeEditorMode>(LevelEditorPtr->GetEditorModeManager().GetActiveScriptableMode("EM_TestModeEditorMode"));
		//		if (EditMode)
		//		{
		//			SceneManagement = 
		//			//SceneManagement = EditMode->SceneManagement;
		//			if (!SceneManagement.IsValid())
		//			{
		//				UE_LOG(LogTemp,Warning,TEXT("Scene未初始化"))

		//			}
		//		}
		//	}

		//}
	}
	//if (SceneManagement.Get())
	//{
	//	ULandscapeEditLayerBase* EditLayerBase = SceneManagement->OwningLandscape->GetEditLayer(FName("World_Editor"));
	//	int32 LayerIndex = -1;
	//	if (!EditLayerBase)
	//	{
	//		LayerIndex = SceneManagement->OwningLandscape->CreateLayer(FName("World_Editor"));
	//	}
	//	else
	//	{
	//		LayerIndex = SceneManagement->OwningLandscape->GetLayerIndex(FName("World_Editor"));

	//	}
	//	SceneManagement->OwningLandscape->AddBrushToLayer(LayerIndex, this);
	//}

}

void AMoKuEditBaseActor::Destroyed()
{

	// 目前移除 激发删除Manager管理数据还未处理操作
	//if (SceneManagement.IsValid()|| SceneManagement.Get())
	//{
	//	if (SceneManagement->OwningLandscape)
	//	{
	//		ULandscapeEditLayerBase* EditLayerBase = SceneManagement->OwningLandscape->GetEditLayer(FName("World_Editor"));
	//		if (EditLayerBase)
	//		{			
	//			int32 LayerIndex = SceneManagement->OwningLandscape->GetLayerIndex(FName("World_Editor"));
	//			SceneManagement->OwningLandscape->RemoveBrushFromLayer(LayerIndex, this);
	//		}
	//	}
	//}
}

void AMoKuEditBaseActor::UpdateBrushMaterial()
{
	//const TCHAR* MaterialPath = TEXT("/TestMode/Material/Landscape/CopyRT_WithAlpha.CopyRT_WithAlpha");
	//const TCHAR* MaterialPath = TEXT("/TestMode/Material/Landscape/RenderMeshDepths");
	//BrushMaterialBase = LoadObject<UMaterialInterface>(nullptr, MaterialPath);

	//if (BrushMaterialBase)
	//{
	//	if (DynamicMeshComponent)
	//	{

	//		//DynamicMeshComponent->SetMaterial(0, BrushMaterialBase);
	//	}

	//}
}

void AMoKuEditBaseActor::PostActorCreated()
{
	Super::PostActorCreated();
	InitSceneManagement();
	FOnRoadBodyChangedParams Params;
	Params.PropertyChangedEvent.ChangeType = EPropertyChangeType::ValueSet;
	Params.bShapeOrPositionChanged = true;
	Params.bUserTriggered = false;
	OnRoadBodyChanged(Params);
	//UpdateBrushMaterial();

}

void AMoKuEditBaseActor::OnRoadBodyChanged(const FOnRoadBodyChangedParams& InParams)
{
	IRoadBrushActorInterface::FRoadBrushActorChangedEventParams Params(this, InParams.PropertyChangedEvent);
	Params.bUserTriggered = InParams.bUserTriggered;
	Params.bShapeOrPositionChanged = InParams.bShapeOrPositionChanged;
	Params.bWeightmapSettingsChanged = InParams.bWeightmapSettingsChanged;
	this->BroadcastRoadBrushActorChangedEvent(Params);

}

#undef LOCTEXT_NAMESPACE