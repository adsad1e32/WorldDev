#pragma once

#include "ComponentVisualizer.h"
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
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadPropSetting.h"
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

USTRUCT()
struct FMetaLandRoadSelectedLaneRefU
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SectionIndex = INDEX_NONE;

	UPROPERTY()
	int32 LaneIndex = LANE_INDEX_NONE;
};

/** Selection state captured by scoped transactions (MetaRoad-compatible). */
UCLASS(Transient)
class UMetaLandRoadSectionComponentVisualizerSelectionState : public UObject
{
	GENERATED_BODY()

public:
	void ResetSelection(bool bSaveSplineSelection);
	void SetSelectedSpline(FComponentPropertyPath& SplinePropertyPath);
	/** Update spline path only — preserves lane / width selection (unlike SetSelectedSpline). */
	void AssignSelectedSplinePath(const FComponentPropertyPath& InSplinePropertyPath);
	void SetSelectedSection(int32 SectionIndex);
	void SetSelectedLane(int32 LaneIndex);
	void SetPrimaryLaneSelection(int32 SectionIndex, int32 LaneIndex, bool bAddToMultiSelection);
	void ToggleLaneInMultiSelection(int32 SectionIndex, int32 LaneIndex);
	void ClearLaneMultiSelection();
	bool IsLaneInMultiSelection(int32 SectionIndex, int32 LaneIndex) const;
	const TArray<FMetaLandRoadSelectedLaneRefU>& GetMultiSelectedLanes() const;
	void SetSelectedKeyIndex(int32 KeyIndex);
	/** Width-key only — keeps lane context for gizmo but clears lane-face multi-select. */
	void SetWidthKeySelection(int32 SectionIndex, int32 LaneIndex, int32 KeyIndex);
	void ClearWidthKeySelection();
	void SetAttributeKeySelection(int32 SectionIndex, int32 KeyIndex);
	void ClearAttributeKeySelection();
	bool HasAttributeKeySelection() const;

	/** Ctrl+LMB lane face fill — not set for boundary/width RMB context. */
	bool IsLaneFacePrimSelected() const { return bLaneFacePrimSelected; }
	void SetLaneFacePrimSelected(bool bInSelected) { bLaneFacePrimSelected = bInSelected; }

	/** Boundary / width-key context — enables Width items in Alt+T RMB menu. */
	bool HasLaneWidthMenuContext() const;
	void SetLaneWidthMenuContext(bool bInContext) { bLaneWidthMenuContext = bInContext; }

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
	void SetLastPickSplineKey(float Key);
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
	TArray<FMetaLandRoadSelectedLaneRefU> MultiSelectedLanes;

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

	/** True after Ctrl+LMB lane face pick (yellow fill). */
	UPROPERTY()
	bool bLaneFacePrimSelected = false;

	/** True after boundary / width-segment / width-key pick (Width RMB menu). */
	UPROPERTY()
	bool bLaneWidthMenuContext = false;

	UPROPERTY()
	bool bAttributeKeySelected = false;

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

	/** Landscape overlay hover — reused native screen pick. */
	mutable int32 CachedLandscapeHoveredSectionIndex = INDEX_NONE;
	mutable int32 CachedLandscapeHoveredKeyIndex = INDEX_NONE;
	mutable double CachedLandscapeHoveredWorldS = -1.0;
	mutable bool bCachedLandscapeHoveredLine = false;

	/** Alt+T hover lane — updated from viewport screen pick each tick. */
	mutable int32 CachedLaneHoveredSectionIndex = INDEX_NONE;
	mutable int32 CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	mutable int32 CachedLaneHoveredWidthIndex = INDEX_NONE;
	mutable bool CachedLaneHoveredWidthIsKey = false;
	mutable bool CachedLaneHoveredBoundaryLine = false;
	mutable int32 CachedLaneHoveredSectionBoundaryIndex = INDEX_NONE;
	/** Spline input key at the current Alt+T hover (mouse S on that span). */
	mutable float CachedLaneHoveredSplineKey = -1.0f;

	/**
	 * Section-line mode is owned by FMetaLandRoadEditorModule (Alt+E).
	 * One visualizer handles Alt+E section edit and Alt+W native control-point edit.
	 */
	bool IsSectionLineEditMode() const;

	/** Viewport tick: section hover (Alt+E) or native control-point hover (Alt+W). */
	static void UpdateEditorViewportTick();
	static void StartEditorViewportTick();
	static void StopEditorViewportTick();

	void SetActiveForSpline(UMetaLandRoadSplineComponent* SplineComp);
	bool EnsureLaneEditMenuSelection();
	bool CanAddLaneInCurrentMode() const;
	bool CanDeleteLaneInCurrentMode() const;

	void SyncEngineSelectionStateForSpline(const UMetaLandRoadSplineComponent* SplineComp);

	void ClearNativeKeySelection();
	void SelectNativeEditKey(int32 KeyIndex, FEditorViewportClient* ViewportClient = nullptr);
	bool TryHandleNativeEditMousePress(FEditorViewportClient* ViewportClient, FViewport* Viewport);
	int32 GetSelectedSectionIndex() const;

	static void EnsureNativeEditTranslateWidget(FEditorViewportClient* ViewportClient = nullptr);
	bool HasSelectedLaneWidthKey() const;
	bool HasSelectedLandscapeAttributeKey() const;
	void DrawLandscapeAttributeOverlay(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const FSceneView* View = nullptr) const;
	bool HandleLandscapeAttributeClick(
		FEditorViewportClient* InViewportClient,
		HHitProxy* HitProxy,
		const FViewportClick& Click);
	void ClearLandscapeAttributeKeySelection();
	bool GetSelectedLandscapeKeyValue(FMetaLandRoadLandscapeAttributeValue& OutValue, double& OutSOffset) const;
	bool SetSelectedLandscapeKeyValue(const FMetaLandRoadLandscapeAttributeValue& InValue);
	bool SetSelectedLandscapeKeySOffset(double InSOffset);
	int32 GetSelectedLandscapeKeyDisplayIndex() const;
	FText GetSelectedLandscapeKeyTitle() const;
	bool SelectLandscapeAttributeKey(int32 SectionIndex, int32 KeyIndex);

	void DrawPropSettingOverlay(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const FSceneView* View = nullptr) const;
	bool HandlePropSettingClick(
		FEditorViewportClient* InViewportClient,
		HHitProxy* HitProxy,
		const FViewportClick& Click);
	void ClearPropSettingSelection();
	bool HasSelectedPropRail() const;
	bool HasSelectedPropKey() const;
	bool GetSelectedPropSettingDetails(FMetaLandRoadPropSettingDetails& OutDetails) const;
	bool SetSelectedPropSettingDetails(const FMetaLandRoadPropSettingDetails& InDetails);
	FText GetSelectedPropSettingTitle() const;
	int32 ResolvePropSpanKeyIndex(const FMetaLandRoadPropRail& Rail, double SectionS0) const;

	/** Alt+E: true when main center line or selected lane strips allow Add Section. */
	bool HasMainLineSectionMenuContext() const;

	/** Face to split from selected lanes (Both if none / both faces). */
	EMetaLandRoadLaneSectionSide ResolveAddSectionSideFromLaneSelection() const;

	/** Add Section using selected-lane faces (or Both). */
	int32 TryAddSectionFromLaneSelection(float OverrideSplitKey = -1.0f);

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

	/** Alt+T RMB: screen-pick selection then lane/width context menu. */
	bool TryHandleLaneEditPickClick(
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

	/** Alt+T: screen-pick lane face selection without stack hit proxy. */
	bool SelectLaneFaceFromScreenPick(
		UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		int32 LaneIndex,
		const FViewportClick& Click,
		bool bLaneFaceFill = true);

	/** Alt+T: clear width-key + lane face selection (empty-space click). */
	bool ClearLaneEditSelection();

	bool TryHandleLaneEditMousePress(FEditorViewportClient* ViewportClient, FViewport* Viewport);

	/** Move selected/dragged section boundary to world point projected onto the center spline. */
	bool ApplySectionBoundaryWorldDrag(int32 SectionIndex, const FVector& WorldPoint);
	/** After a section-boundary drag: layout + mesh (not per mouse tick). */
	void FinishSectionBoundaryDrag();

	/**
	 * Main split entry point (set breakpoint here for menu/console/debug).
	 * @param OverrideSplitKey Spline input key; < 0 resolves from pick/hover/selection.
	 * @return New section index, or INDEX_NONE on failure (see Output Log).
	 */
	int32 TrySplitSectionSide(EMetaLandRoadLaneSectionSide Side, float OverrideSplitKey = -1.0f);

	static FString GetReferencerNameStatic();

protected:
	virtual void GenerateContextMenuSections(FMenuBuilder& InMenuBuilder) const override;
	void AppendMetaLandRoadCurveMenu(FMenuBuilder& MenuBuilder) const;
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
	void OnDeleteSectionSide(EMetaLandRoadLaneSectionSide Side);
	bool CanDeleteSectionSide(EMetaLandRoadLaneSectionSide Side) const;
	void OnAddLane(bool bLeft);
	void OnDeleteLane();
	void OnAddWidthKey();
	void OnDeleteWidthKey();
	void OnAddLandscapeKey();
	void OnDeleteLandscapeKey();
	void OnAddPropKey();
	void OnDeletePropKey();
	void OnDeletePropRail();
	bool CommitPropLockedRail(
		UMetaLandRoadSplineComponent* SplineComp,
		FEditorViewportClient* ViewportClient,
		int32 SectionIndex,
		EMetaLandRoadPropEdgeRef EdgeRef,
		double WorldS);
	bool CommitPropFreeRail(
		UMetaLandRoadSplineComponent* SplineComp,
		FEditorViewportClient* ViewportClient,
		int32 SectionIndex,
		float Alpha,
		double WorldS);
	void GeneratePropSettingContextMenuSections(FMenuBuilder& InMenuBuilder) const;
	bool HandlePropSettingInputDelta(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		const FVector& DeltaTranslate);
	void RefreshPropCachedWidget();
	void PrepareLandscapeKeys(UMetaLandRoadSplineComponent* SplineComp) const;
	bool VisProxyHandleAttributeKeyClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click);
	bool TryHandleLandscapeAttributePickClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click);
	/** Landscape overlay click dispatch (VisProxyHandleClick). */
	bool TryHandleLandscapeOverlayVisProxyClick(
		FEditorViewportClient* InViewportClient,
		HComponentVisProxy* VisProxy,
		const FViewportClick& Click);
	/** Landscape overlay gizmo drag (HandleInputDelta). */
	bool HandleLandscapeAttributeInputDelta(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		const FVector& DeltaTranslate);
	bool TrySelectHoveredLandscapeAttributeKey(FEditorViewportClient* InViewportClient);
	bool TrySelectLandscapeAttributeKeyAtScreen(
		FEditorViewportClient* InViewportClient,
		UMetaLandRoadSplineComponent* SplineComp,
		const FIntPoint& ScreenPos);
	void NotifyLandscapeAttributeChanged(UMetaLandRoadSplineComponent* SplineComp) const;
	void ApplyLandscapeAttributeKeySelection(
		UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		int32 KeyIndex);
	bool SelectLandscapeKeyAtDisplayS(UMetaLandRoadSplineComponent* SplineComp, double WorldS);
	void RefreshLandscapeKeyCachedWidget(
		UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		int32 KeyIndex) const;
	double GetLandscapeSectionLocalLength(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex) const;
	void GenerateLandscapeAttributeContextMenuSections(FMenuBuilder& InMenuBuilder) const;
	/** Fill segment + click position for add-point. Does not select a key. */
	bool EnsureKeySelectionForSegmentSplit();
	void OnSafeAddKeyToSegment();
	bool CanAddSplinePointOnCurve() const;
	void OnDeleteSelectedSplinePoints();
	bool CanDeleteSelectedSplinePoints() const;
	USplineComponent* ResolveSplineForAddPoint() const;
	/** Insert on the existing cubic (De Casteljau) so lane-extend Bezier shape is kept. */
	bool TryInsertSplinePointPreservingCurve();

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

	int32 PropSectionIndex = INDEX_NONE;
	FGuid PropRailId;
	int32 PropKeyIndex = INDEX_NONE;
	double PropFocusWorldS = -1.0;
	FVector PropCachedWidgetWorld = FVector::ZeroVector;

	mutable bool bCachedPropHoveredRoad = false;
	mutable double CachedPropHoveredWorldS = -1.0;
	mutable float CachedPropHoveredAlpha = 0.5f;
	mutable int32 CachedPropHoveredSectionIndex = INDEX_NONE;
	mutable double CachedPropHoveredSpanS0 = 0.0;
	mutable double CachedPropHoveredSpanS1 = 0.0;
	mutable int32 CachedPropHoveredKeyIndex = INDEX_NONE;
	mutable FGuid CachedPropHoveredRailId;
	mutable EMetaLandRoadPropEdgeRef CachedPropHoveredEdgeRef = EMetaLandRoadPropEdgeRef::Free;
	mutable bool bCachedPropHoveredCenter = false;

	static FTSTicker::FDelegateHandle EditorViewportTickerHandle;
	bool TickEditorViewport(float DeltaTime);
	void TickNativeEditHover();
	void TickLaneEditHover();
	void TickLandscapeAttributeHover();
	void TickPropSettingHover();
	void UpdateNativeHoverCacheFromViewport();
};
