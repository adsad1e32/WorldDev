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
class FMoKuEditSplinesComponentVisualizer;
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
	bool IsAttributeMode() const { return EditorVisMode == EMetaLandRoadEditorVisMode::Attribute; }

	bool IsLandscapeAttributeSelected() const { return bLandscapeAttributeSelected; }
	void SetLandscapeAttributeSelected(bool bSelected);
	bool IsLandscapeAttributeOverlayRequested() const { return bShowLandscapeAttributeOverlay; }
	void SetLandscapeAttributeOverlayVisible(bool bVisible);
	/** Exit Alt+W / Alt+E / Alt+T so Landscape overlay can draw on the current main line. */
	void EnterLandscapeAttributeOverlay();
	/** Hide Landscape keys/curve and clear the Landscape selection. */
	void HideLandscapeAttributeDisplay();
	/** Hide keys and the details panel, keep Landscape selected so Show/Hide stay visible. */
	void DismissLandscapeOverlayKeepAttribute();
	/** True after Show — Junction Landscape / Landscape Key panel. */
	bool ShouldShowLandscapeDetailsPanel() const;
	/** Enter Landscape overlay for the selected road, or the first road linked to a junction. */
	void ShowLandscapeOverlayForActor(AActor* Actor);
	class AMoKuEditIntersectionActor* GetLandscapeContextJunction() const;
	void BroadcastLandscapeKeyEditorRefresh();
	FSimpleMulticastDelegate LandscapeKeyEditorRefresh;
	/** Show is on, and not Alt+W / Alt+E / Alt+T / draw tool. */
	bool ShouldDrawLandscapeAttributeOverlay() const;

	bool IsRoadSectionLineVisMode() const { return IsSectionMode(); }
	bool IsNativeSplineEditMode() const { return IsNativeEditMode(); }
	bool ShouldShowNativeEditOverlay() const { return IsNativeEditMode(); }

	UMetaLandRoadSplineComponent* GetActiveNativeEditSpline() const { return ActiveNativeEditSpline.Get(); }

	void SetEditorVisMode(EMetaLandRoadEditorVisMode NewMode);
	void SetRoadComponentVisMode(bool bShowEditSpline);
	void ToggleRoadComponentVisMode();
	/** True when Alt+E / Alt+W / Alt+T may run — false while Interactive Tool is active. */
	bool CanUseEditorVisModeHotkeys() const;
	/** True while MetaLandRoad spline draw / lane-extend interactive tool owns input. */
	bool IsInteractiveSplineDrawToolActive() const;
	/** True only while the draw tool is actively placing points (not idle after A). */
	bool IsLiveSplineDrawSession() const;
	/** Interactive Tool started: force-exit Alt+W/E/T (Mode vs Tool mutually exclusive). */
	void ExitEditorVisModesForInteractiveTool();
	/** Stop viewport drag tracking + end spline visualizer before draw tool clears selection. */
	void AbortSplineVisualizerTracking();
	/** Clear EditTarget + interactive draw leftovers when leaving editor vis modes. */
	void ClearInteractiveSessionLeftovers();
	/** Restore road mesh viewport pick after Alt+W / draw session (bSelectable + visibility). */
	void RestoreAllRoadActorsViewportSelection();
	void EnterNativeSplineEditMode();
	void ExitNativeSplineEditMode();
	void ToggleNativeSplineEditMode();
	void EnterLaneEditMode();
	void ExitLaneEditMode();
	void ToggleLaneEditMode();
	void EnsureComponentVisualizersRegistered();

	TSharedPtr<FMoKuEditSplinesComponentVisualizer> GetRoadComponentVisualizer() const;

private:
	void RegisterEditorFeatures();
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
	void SyncNativeEditTargetToSelection(UMetaLandRoadSplineComponent* SplineComp);
	UMetaLandRoadSplineComponent* FindSelectedOrOwnedRoadSpline() const;

	TArray<FName> RegisteredComponentClassNames;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle EditorFeaturesInitHandle;
	bool bEditorFeaturesRegistered = false;
	FDelegateHandle ActorPickerHandle;
	FDelegateHandle LevelEditorModuleChangedHandle;
	FDelegateHandle EditorSelectionChangedHandle;
	FTSTicker::FDelegateHandle ComponentVisRegisterTickerHandle;
	bool bSyncingRoadActorSelection = false;
	bool bComponentVisualizersRegistered = false;
	bool bSectionLineHotkeyMapped = false;
	EMetaLandRoadEditorVisMode EditorVisMode = EMetaLandRoadEditorVisMode::Standard;
	bool bLandscapeAttributeSelected = false;
	bool bShowLandscapeAttributeOverlay = false;
	TWeakObjectPtr<class AMoKuEditIntersectionActor> LandscapeOverlayJunction;
	TWeakObjectPtr<UMetaLandRoadSplineComponent> ActiveNativeEditSpline;
	TOptional<bool> SavedShowSelectionSubcomponents;
	TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadComponentVisualizer;
};
