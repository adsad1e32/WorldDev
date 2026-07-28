// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"
#include "Toolkits/IToolkit.h"
#include "MetaLandRoadTypes.h"

class AActor;
class AMoKuEditSplineActor;
class UObject;
class FComponentVisualizer;
class UMetaLandRoadSplineComponent;

/**
 * MetaLandRoad editor module (WaterEditor-style).
 * - Registers FMoKuEditSplinesComponentVisualizer at module startup (PostEngineInit if GUnrealEd not ready).
 * - Alt+E / Alt+W / Alt+T toggle draw branches inside the visualizer.
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

	EMetaLandRoadEditorVisMode GetEditorVisMode() const { return EditorVisMode; }

	bool IsStandardMode() const { return EditorVisMode == EMetaLandRoadEditorVisMode::Standard; }
	bool IsNativeEditMode() const { return EditorVisMode == EMetaLandRoadEditorVisMode::NativeEdit; }
	bool IsSectionMode() const { return EditorVisMode == EMetaLandRoadEditorVisMode::Section; }
	bool IsConnectionMode() const { return EditorVisMode == EMetaLandRoadEditorVisMode::Connection; }
	bool IsLaneEditMode() const { return EditorVisMode == EMetaLandRoadEditorVisMode::Lane; }

	bool IsRoadSectionLineVisMode() const { return IsSectionMode(); }
	bool IsNativeSplineEditMode() const { return IsNativeEditMode(); }
	bool ShouldShowNativeEditOverlay() const { return IsNativeEditMode(); }

	UMetaLandRoadSplineComponent* GetActiveNativeEditSpline() const { return ActiveNativeEditSpline.Get(); }

	void SetEditorVisMode(EMetaLandRoadEditorVisMode NewMode);
	void SetRoadComponentVisMode(bool bShowEditSpline);
	void ToggleRoadComponentVisMode();
	void EnterNativeSplineEditMode();
	void ExitNativeSplineEditMode();
	void ToggleNativeSplineEditMode();
	void EnterLaneEditMode();
	void ExitLaneEditMode();
	void ToggleLaneEditMode();
	void EnsureComponentVisualizersRegistered();

private:
	void OnLevelActorAddedToWorld(AActor* Actor);
	void OnEditorActorPicked(AActor* PickedActor);
	void RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer);
	void TryRegisterComponentVisualizers();
	bool OnDeferredComponentVisRegisterTick(float DeltaTime);
	void RegisterSectionLineEditHotkey();
	void UnregisterSectionLineEditHotkey();
	void OnModulesChangedForHotkey(FName ModuleName, EModuleChangeReason Reason);
	void ApplyEditorVisModeSideEffects(bool bSectionMode);
	void OnEditorSelectionChanged(UObject* NewSelection);
	void SyncSelectedRoadActorToSpline();
	UMetaLandRoadSplineComponent* FindSelectedOrOwnedRoadSpline() const;

	TArray<FName> RegisteredComponentClassNames;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle ActorPickerHandle;
	FDelegateHandle LevelEditorModuleChangedHandle;
	FDelegateHandle EditorSelectionChangedHandle;
	FTSTicker::FDelegateHandle ComponentVisRegisterTickerHandle;
	bool bSyncingRoadActorSelection = false;
	bool bComponentVisualizersRegistered = false;
	bool bSectionLineHotkeyMapped = false;
	EMetaLandRoadEditorVisMode EditorVisMode = EMetaLandRoadEditorVisMode::Standard;
	TWeakObjectPtr<UMetaLandRoadSplineComponent> ActiveNativeEditSpline;
	TOptional<bool> SavedShowSelectionSubcomponents;
};
