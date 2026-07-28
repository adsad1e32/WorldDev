#pragma once

#include "SplineComponentVisualizer.h"
#include "MetaLandRoadVisProxy.h"
#include "MetaLandRoadSplineComponent.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Containers/Ticker.h"
#include "MetaLandRoadSectionComponentVisualizer.generated.h"

class UToolMenu;

UENUM()
enum class EMetaLandRoadSectionSelectionState : uint8
{
	None = 0,
	Component,
	Section,
	Lane,
	Key,
	KeyTangent,
};

/** Selection state captured by scoped transactions (MetaRoad-compatible). */
UCLASS(Transient)
class UMetaLandRoadSectionComponentVisualizerSelectionState : public UObject
{
	GENERATED_BODY()

public:
	void ResetSelection(bool bSaveSplineSelection);
	void SetSelectedSpline(FComponentPropertyPath& SplinePropertyPath);
	void SetSelectedSection(int32 SectionIndex);
	void SetSelectedLane(int32 LaneIndex);
	void SetSelectedKeyIndex(int32 KeyIndex);

	const FComponentPropertyPath GetSplinePropertyPath() const;
	UMetaLandRoadSplineComponent* GetSelectedSpline() const;
	int32 GetSelectedSectionIndex() const;
	int32 GetSelectedLaneIndex() const;
	int32 GetSelectedKeyIndex() const;
	bool IsSectionBoundarySelected() const;
	void SetSectionBoundarySelected(bool bInSelected);

	void SetCachedData(const FVector& Position, const FQuat& Rotation, float SplineKey);
	void SetCachedDataAtSplineInputKey(float Key);
	void SetCachedDataAtSectionStart(int32 SectionIndex);
	void SetCachedDataAtLane(int32 SectionIndex, int32 LaneIndex, double SOffset, double Alpha);
	void RecordPickSplineKey(float Key);
	void ResetCachedData();

	FVector GetCachedPosition() const;
	FQuat GetCachedRotation() const;
	float GetCachedSplineKey() const;
	float GetLastPickSplineKey() const;

	EMetaLandRoadSectionSelectionState GetState() const;
	EMetaLandRoadSectionSelectionState GetStateVerified() const;

	bool IsSelected(const UMetaLandRoadSplineComponent* Spline, int SectionIndex) const;
	bool IsSelected(const UMetaLandRoadSplineComponent* Spline, int SectionIndex, int LaneIndex) const;
	bool IsSelected(const UMetaLandRoadSplineComponent* Spline, int SectionIndex, int LaneIndex, int WidthIndex) const;

	void FixState();

protected:
	UPROPERTY()
	FComponentPropertyPath SplinePropertyPath;

	UPROPERTY()
	int32 SelectedSectionIndex = INDEX_NONE;

	UPROPERTY()
	int32 SelectedLaneIndex = LANE_INDEX_NONE;

	UPROPERTY()
	int32 SelectedKeyIndex = INDEX_NONE;

	UPROPERTY()
	FVector CachedPosition;

	UPROPERTY()
	FQuat CachedRotation;

	UPROPERTY()
	float CachedSplineKey = 0.0f;

	UPROPERTY()
	float LastPickSplineKey = -1.0f;

	/** True only when the section START cross is selected (SOffset gizmo). Span pick keeps this false. */
	UPROPERTY()
	bool bSectionBoundarySelected = false;

	UPROPERTY()
	EMetaLandRoadSectionSelectionState State = EMetaLandRoadSectionSelectionState::None;
};

/**
 * MoKu / MetaLandRoad spline component visualizer (Water-style: one class on UMoKuEditSplinesComponent).
 * Extends FSplineComponentVisualizer so FSplinePointDetails keeps a valid ISplineDetailsProvider.
 */
class FMoKuEditSplinesComponentVisualizer : public FSplineComponentVisualizer
{
public:
	FMoKuEditSplinesComponentVisualizer();
	virtual ~FMoKuEditSplinesComponentVisualizer() override;

	virtual void OnRegister() override;
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual void EndEditing() override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual bool HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool HasFocusOnSelectionBoundingBox(FBox& OutBoundingBox) override;
	virtual bool HandleSnapTo(const bool bInAlign, const bool bInUseLineTrace, const bool bInUseBounds, const bool bInUsePivot, AActor* InDestination) override;
	virtual void TrackingStarted(FEditorViewportClient* InViewportClient) override;
	virtual void TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove) override;
	virtual UActorComponent* GetEditedComponent() const override;
	virtual TSharedPtr<SWidget> GenerateContextMenu() const override;
	virtual bool IsVisualizingArchetype() const override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual bool ShouldShowForSelectedSubcomponents(const UActorComponent* Component) override;
	virtual FString GetReferencerName() const override;

	UMetaLandRoadSectionComponentVisualizerSelectionState* GetSelectionState() const;
	UMetaLandRoadSplineComponent* GetEditedMetaLandRoadSpline() const;

	/** Clear section/boundary selection + highlight (click empty). */
	void ClearSectionHighlight();

	/** Alt+W hover key — updated each draw pass from the viewport under the mouse. */
	mutable int32 CachedNativeHoveredKey = INDEX_NONE;

	/** Alt+W hover segment (when not hovering a key). */
	mutable int32 CachedNativeHoveredSegment = INDEX_NONE;

	/** Alt+T hover lane — updated from viewport hit proxy each tick. */
	mutable int32 CachedLaneHoveredSectionIndex = INDEX_NONE;
	mutable int32 CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	mutable int32 CachedLaneHoveredWidthIndex = INDEX_NONE;

	/**
	 * Section-line mode is owned by FMetaLandRoadModule (Alt+E).
	 * One visualizer handles Alt+E section edit and Alt+W native control-point edit.
	 */
	bool IsSectionLineEditMode() const;

	/** Viewport tick: section hover (Alt+E) or native control-point hover (Alt+W). */
	static void UpdateEditorViewportTick();
	static void StartEditorViewportTick();
	static void StopEditorViewportTick();

	void SetActiveForSpline(UMetaLandRoadSplineComponent* SplineComp);

	void SyncEngineSelectionStateForSpline(const UMetaLandRoadSplineComponent* SplineComp);

	void ClearNativeKeySelection();

	/** Alt+W native spline helpers (formerly MoKu visualizer). */
	static float GetNativeEditHeightOffset(const UMetaLandRoadSplineComponent* RoadSpline);
	static float GetNativeEditRingWorldRadius(
		const FSceneView* View,
		const FVector& CenterWorld,
		const FVector& RingAxisX,
		const FVector& RingAxisY,
		float DesiredPixelRadius = 18.0f);
	static int32 PickHoveredControlPointKey(
		const UMetaLandRoadSplineComponent* RoadSpline,
		const FSceneView* View,
		const FIntPoint& MousePos,
		float DesiredHoverPixelRadius = 22.0f);
	static int32 PickHoveredSplineSegment(
		const UMetaLandRoadSplineComponent* RoadSpline,
		const FSceneView* View,
		const FIntPoint& MousePos,
		float MaxPixelDistance = 20.0f);
	static void EnsureNativeEditTranslateWidget(FEditorViewportClient* ViewportClient = nullptr);
	static void DrawNativeEditRingOverlay(
		const FSceneView* View,
		int32 HoveredKey,
		const TSet<int32>& SelectedKeys,
		int32 SelectedSegment,
		const UMetaLandRoadSplineComponent* RoadSpline,
		FPrimitiveDrawInterface* PDI);
	static void DrawLaneWidthKeyRingOverlay(
		const FSceneView* View,
		const UMetaLandRoadSplineComponent* RoadSpline,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight,
		int32 HoveredSectionIndex,
		int32 HoveredLaneIndex,
		int32 HoveredWidthIndex);
	static void DrawNativeEditSplineOverlay(
		const UMetaLandRoadSplineComponent* RoadSpline,
		FPrimitiveDrawInterface* PDI,
		int32 HoveredKey,
		int32 HoveredSegment,
		int32 SelectedSegment,
		const TSet<int32>& SelectedKeys);

	/** Alt+E: true when main center line is hovered or selected (not boundary-only). */
	bool HasMainLineSectionMenuContext() const;

	/** Alt+E section pick: optionally push the section context menu. */
	bool FinishPickContextMenu(FEditorViewportClient* ViewportClient, bool bShowContextMenu) const;

	/** Sync flag after module swaps registration (do not call to toggle — use module). */
	void SyncSectionLineEditModeFlag(bool bEnabled);

	void SetSectionLineEditMode(bool bEnabled);
	void ToggleSectionLineEditMode();

	/** Pick section center line near cursor; optionally show the section context menu. */
	bool TryHandleRoadSplinePickClick(
		FEditorViewportClient* InViewportClient,
		HHitProxy* HitProxy,
		const FViewportClick& Click,
		bool bShowContextMenu);

	void ActivateRoadVisualization();

	/**
	 * Select a movable section boundary (SectionIndex >= 1) and show the translate gizmo.
	 * Used by LMB click / hover-ticker because LineBatch hover has no hit proxies.
	 */
	bool SelectSectionBoundaryForDrag(int32 SectionIndex);

	/**
	 * Alt+E: click Section main (longitudinal) line → select nearest spline key + gizmo.
	 * Enables path drag without switching to Alt+W.
	 */
	bool SelectMainLineForPathDrag(int32 SectionIndex, float SplineKey);

	/** Move selected/dragged section boundary to world point projected onto the center spline. */
	bool ApplySectionBoundaryWorldDrag(int32 SectionIndex, const FVector& WorldPoint);

	/**
	 * Main split entry point (set breakpoint here for menu/console/debug).
	 * @param OverrideSplitKey Spline input key; < 0 resolves from pick/hover/selection.
	 * @return New section index, or INDEX_NONE on failure (see Output Log).
	 */
	int32 TrySplitSectionSide(EMetaLandRoadLaneSectionSide Side, float OverrideSplitKey = -1.0f);

	static FString GetReferencerNameStatic();

protected:
	virtual void GenerateContextMenuSections(FMenuBuilder& InMenuBuilder) const;
	const UMetaLandRoadSplineComponent* UpdateSelectedComponentAndSectionAndLane(HComponentVisProxy* VisProxy);
	bool VisProxyHandleNativeEditClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click);
	bool VisProxyHandleEngineSplineEditClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click);
	bool VisProxyHandleMetaLandRoadProxyClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click);
	bool VisProxyHandleLaneWidthClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click);
	bool TryCompleteNativeEditPointSelection(
		HComponentVisProxy* VisProxy,
		FEditorViewportClient* InViewportClient,
		const USplineComponent* SplineComp);
	void RefreshRoadMeshForSpline(UMetaLandRoadSplineComponent* SplineComp) const;
	void RefreshRoadMeshAndRedraw(UMetaLandRoadSplineComponent* SplineComp, bool bForceRedraw = true) const;
	void EnsureDefaultProfileIfNeeded(UMetaLandRoadSplineComponent* SplineComp) const;
	void OnSplitSectionSide(EMetaLandRoadLaneSectionSide Side);
	void OnDeleteSection();
	void OnAddLane(bool bLeft);
	void OnDeleteLane();
	void OnAddWidthKey();
	void OnDeleteWidthKey();

	void OnSetRoadSplinePointType(EMetaLandRoadSplinePointType Mode);
	bool IsRoadSplinePointTypeSet(EMetaLandRoadSplinePointType Mode) const;
	void GenerateRoadSplinePointTypeSubMenu(FMenuBuilder& MenuBuilder) const;
	void GenerateLaneWidthContextMenuSections(FMenuBuilder& InMenuBuilder) const;

protected:
	TSharedPtr<FUICommandList> SectionComponentVisualizerActions;
	TObjectPtr<UMetaLandRoadSectionComponentVisualizerSelectionState> RoadSectionSelectionState;

	/** Alt+E toggle: draw/pick section center lines (independent of native spline edit). */
	bool bSectionLineEditMode = false;

	/** Updated each draw pass from the viewport under the mouse. */
	mutable int32 HoveredSectionIndex = INDEX_NONE;
	mutable int32 HoveredBoundarySectionIndex = INDEX_NONE;
	mutable float HoveredSplineKey = -1.0f;

	static FTSTicker::FDelegateHandle EditorViewportTickerHandle;
	bool TickEditorViewport(float DeltaTime);
	void TickNativeEditHover();
	void TickLaneEditHover();
	void UpdateNativeHoverCacheFromViewport();
};

extern TSharedPtr<FMoKuEditSplinesComponentVisualizer> GMoKuEditSplinesComponentVisualizer;
