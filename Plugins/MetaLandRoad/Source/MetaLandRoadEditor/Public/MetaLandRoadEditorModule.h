#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "MetaLandRoadTypes.h"

class AActor;
class AMoKuEditSplineActor;
class UObject;
class FComponentVisualizer;
class FMoKuEditSplinesComponentVisualizer;
class UMetaLandRoadSplineComponent;

/**
 * Editor module: visualizer, Mode hotkeys, Landscape overlay, Tool session gates.
 */
class FMetaLandRoadEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FMetaLandRoadEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMetaLandRoadEditorModule>("MetaLandRoadEditor");
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
	void EnterLandscapeAttributeOverlay();
	void HideLandscapeAttributeDisplay();
	void DismissLandscapeOverlayKeepAttribute();
	bool ShouldShowLandscapeDetailsPanel() const;
	void ShowLandscapeOverlayForActor(AActor* Actor);
	class AMoKuEditIntersectionActor* GetLandscapeContextJunction() const;
	void BroadcastLandscapeKeyEditorRefresh();
	FSimpleMulticastDelegate LandscapeKeyEditorRefresh;
	bool ShouldDrawLandscapeAttributeOverlay() const;

	enum class EPropPlaceMode : uint8
	{
		Select,
		AddRail,
		AddSpan,
	};

	bool IsPropSettingSelected() const { return bPropSettingSelected; }
	void SetPropSettingSelected(bool bSelected);
	void EnterPropSettingOverlay();
	void DismissPropSettingOverlay();
	void ShowPropSettingOverlayForActor(AActor* Actor);
	bool ShouldDrawPropSettingOverlay() const;
	bool ShouldShowPropSettingDetailsPanel() const;
	EPropPlaceMode GetPropPlaceMode() const { return PropPlaceMode; }
	void SetPropPlaceMode(EPropPlaceMode Mode);
	FSimpleMulticastDelegate PropSettingEditorRefresh;
	void BroadcastPropSettingEditorRefresh();

	bool IsSidewalkSelected() const { return bSidewalkSelected; }
	void SetSidewalkSelected(bool bSelected);
	void EnterSidewalkOverlay();
	void DismissSidewalkOverlay();
	void ShowSidewalkOverlayForActor(AActor* Actor);
	bool ShouldDrawSidewalkOverlay() const;
	bool ShouldShowSidewalkDetailsPanel() const;
	FSimpleMulticastDelegate SidewalkEditorRefresh;
	void BroadcastSidewalkEditorRefresh();
	int32 GetSidewalkKeyIndex() const { return SidewalkKeyIndex; }
	void SetSidewalkKeyIndex(int32 Index);
	/** +1 right edge, -1 left edge, 0 none. */
	int32 GetSidewalkEdgeSign() const { return SidewalkEdgeSign; }
	void SetSidewalkEdgeSign(int32 Sign);
	int32 GetSidewalkHoverEdgeSign() const { return SidewalkHoverEdgeSign; }
	double GetSidewalkHoverS() const { return SidewalkHoverS; }
	int32 GetSidewalkHoverCornerIndex() const { return SidewalkHoverCornerIndex; }
	int32 GetSidewalkSelectedCornerIndex() const { return SidewalkSelectedCornerIndex; }
	bool IsSidewalkHoverJunction(const class AMoKuEditIntersectionActor* Junction) const
	{
		return SidewalkHoverJunction.Get() == Junction;
	}
	bool IsSidewalkSelectedJunction(const class AMoKuEditIntersectionActor* Junction) const
	{
		return SidewalkSelectedJunction.Get() == Junction;
	}
	UMetaLandRoadSplineComponent* GetSidewalkFocusSpline() const { return SidewalkFocusSpline.Get(); }
	UMetaLandRoadSplineComponent* GetSidewalkHoverSpline() const { return SidewalkHoverSpline.Get(); }
	void SetSidewalkHover(int32 Sign, double S, UMetaLandRoadSplineComponent* Spline = nullptr);
	void SetSidewalkHoverCorner(class AMoKuEditIntersectionActor* Junction, int32 CornerIndex);
	void SetSidewalkFocusSpline(UMetaLandRoadSplineComponent* Spline);
	void SetSidewalkSelectedCorner(class AMoKuEditIntersectionActor* Junction, int32 CornerIndex);

	bool IsRoadSectionLineVisMode() const { return IsSectionMode(); }
	bool IsNativeSplineEditMode() const { return IsNativeEditMode(); }
	bool ShouldShowNativeEditOverlay() const { return IsNativeEditMode(); }

	UMetaLandRoadSplineComponent* GetActiveNativeEditSpline() const { return ActiveNativeEditSpline.Get(); }

	void SetEditorVisMode(EMetaLandRoadEditorVisMode NewMode);
	void SetRoadComponentVisMode(bool bShowEditSpline);
	void ToggleRoadComponentVisMode();
	bool CanUseEditorVisModeHotkeys() const;
	bool IsInteractiveSplineDrawToolActive() const;
	bool IsLiveSplineDrawSession() const;
	void ExitEditorVisModesForInteractiveTool();
	void AbortSplineVisualizerTracking();
	void ClearInteractiveSessionLeftovers();
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
	void BindRuntimeHooks();
	void UnbindRuntimeHooks();
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
	bool bPropSettingSelected = false;
	bool bShowPropSettingOverlay = false;
	bool bSidewalkSelected = false;
	bool bShowSidewalkOverlay = false;
	int32 SidewalkKeyIndex = 0;
	int32 SidewalkEdgeSign = 0;
	int32 SidewalkHoverEdgeSign = 0;
	double SidewalkHoverS = -1.0;
	int32 SidewalkHoverCornerIndex = INDEX_NONE;
	int32 SidewalkSelectedCornerIndex = INDEX_NONE;
	TWeakObjectPtr<UMetaLandRoadSplineComponent> SidewalkHoverSpline;
	TWeakObjectPtr<UMetaLandRoadSplineComponent> SidewalkFocusSpline;
	TWeakObjectPtr<class AMoKuEditIntersectionActor> SidewalkHoverJunction;
	TWeakObjectPtr<class AMoKuEditIntersectionActor> SidewalkSelectedJunction;
	EPropPlaceMode PropPlaceMode = EPropPlaceMode::Select;
	TWeakObjectPtr<class AMoKuEditIntersectionActor> LandscapeOverlayJunction;
	TWeakObjectPtr<UMetaLandRoadSplineComponent> ActiveNativeEditSpline;
	TOptional<bool> SavedShowSelectionSubcomponents;
	TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadComponentVisualizer;
};
