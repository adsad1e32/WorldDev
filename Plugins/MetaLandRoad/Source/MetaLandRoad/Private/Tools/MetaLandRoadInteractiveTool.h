#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/ClickDragTool.h"
#include "PropertyEditorModule.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "MetaLandRoadEditorMode.h"
#include "MoKuEditIntersectionActor.h"
#include "Engine/StaticMeshActor.h"
#include "InteractiveTool.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoManager.h"
#include "CurveOp.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditorUtils.h"
#include "SocketMarkbleGizmo.h"
#include "SplineBasicElement.h"
#include "Landscape.h"


#include "MetaLandRoadInteractiveTool.generated.h"



class UMoKuEditSplinesComponent;
class FPrimitiveDrawInterface;
class FCanvas;
class FSceneView;

UCLASS()
class UInteractiveToolSettings : public  UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "BasicSetting", DisplayName = "Road Mode")
	ERoadType RoadModeType = ERoadType::Surface;

	UPROPERTY(EditAnywhere, Category = "BasicSetting", meta = (EditCondition = "RoadModeType != ERoadType::Surface", EditConditionHides))
	double CurveOffset = 0.0;

	/**
	 * When true: TurnAngle / InsertStep / Snap / lateral Hermite trigger are derived from
	 * the active tip road width (wider → Hermite earlier, larger step/snap).
	 */
	UPROPERTY(EditAnywhere, Category = "Lane Draw")
	bool bAutoLaneDrawFromRoadWidth = true;

	/** Manual parallel lead-out (cm) before Hermite bend. Auto uses ~0.8× tip width. */
	UPROPERTY(EditAnywhere, Category = "Lane Draw", meta = (ClampMin = "0", UIMin = "0", UIMax = "5000", EditCondition = "!bAutoLaneDrawFromRoadWidth"))
	float RampStraightStubCm = 400.0f;

	/** Manual turn-angle threshold (degrees). Ignored when Auto is on. */
	UPROPERTY(EditAnywhere, Category = "Lane Draw", meta = (ClampMin = "5", ClampMax = "89", UIMin = "5", UIMax = "89", EditCondition = "!bAutoLaneDrawFromRoadWidth"))
	float TurnAngleThresholdDeg = 12.0f;

	/** Manual sparse MESH key spacing (cm). Ignored when Auto is on. */
	UPROPERTY(EditAnywhere, Category = "Lane Draw", meta = (ClampMin = "200", UIMin = "200", UIMax = "2000", EditCondition = "!bAutoLaneDrawFromRoadWidth"))
	float SplineInsertStepCm = 800.0f;

	/** Manual connection snap distance (cm). Ignored when Auto is on. */
	UPROPERTY(EditAnywhere, Category = "Lane Draw", meta = (ClampMin = "50", UIMin = "50", UIMax = "2000", EditCondition = "!bAutoLaneDrawFromRoadWidth"))
	float ConnectionSnapDistanceCm = 600.0f;

	/**
	 * When EndConnection snaps to another tip: how far back to mix width toward that lane mouth.
	 * Last 2 / Last 3 = only those tip spans; Entire Road = lerp from start to tip.
	 */
	UPROPERTY(EditAnywhere, Category = "Lane Draw|End Snap")
	EMetaLandRoadEndWidthBlendSpan EndWidthBlendSpan = EMetaLandRoadEndWidthBlendSpan::LastTwoSegments;

	/**
	 * Legacy: End Snap multi-lane mix is chosen with Shift+LMB on the TARGET tip.
	 * Hover always seeds 1 lane; this flag is no longer used for auto-expand.
	 */
	UPROPERTY(EditAnywhere, Category = "Lane Draw|End Snap", meta = (EditCondition = "false", EditConditionHides))
	bool bEndConnectMatchSourceLanes = false;

	/** Updated by Shift+LMB target mix picks (1..N, independent of begin count). */
	UPROPERTY(VisibleAnywhere, Category = "Lane Draw|End Snap")
	int32 EndConnectTargetLaneCount = 1;

	/**
	 * Ctrl+click cross-road finish: scan all road pairs in the level (slow).
	 * Off (default) = local cook — only hubs involving the road being drawn.
	 */
	UPROPERTY(EditAnywhere, Category = "MetaLandRoad|Junction", meta = (DisplayName = "Full World Recook On Draw Finish"))
	bool bFullWorldJunctionRecookOnDrawFinish = false;

	/**
	 * If false (default): A only finishes the spline mesh — no junction cook (A cook is heavy).
	 * Junction cook = Ctrl+press while drawing when XY cross is detected (road hit or terrain under cross).
	 */
	UPROPERTY(EditAnywhere, Category = "MetaLandRoad|Junction", meta = (DisplayName = "Cook Junction On A Finish"))
	bool bCookJunctionOnAFinish = false;

	/**
	 * Overpass gate for XY centerline cross (cm).
	 * 0 = always cook junction (ignore height — default; elevated roads still get hubs).
	 * >0 = skip junction when |Z_a − Z_b| exceeds this (treat as grade separation).
	 */
	UPROPERTY(EditAnywhere, Category = "MetaLandRoad|Junction", meta = (DisplayName = "Junction Max Height Delta (cm)", ClampMin = "0.0", ClampMax = "2000.0"))
	float JunctionMaxHeightDeltaCm = 0.0f;

	/** Draw guidance sample points + L#/R# index labels (off = green rails only). */
	UPROPERTY(EditAnywhere, Category = "Lane Draw|Debug", meta = (DisplayName = "Debug Guidance Points"))
	bool bDebugDrawGuidanceRailPoints = false;

};

UCLASS()
class METALANDROAD_API UMetaLandRoadInteractiveToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()


public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override { return true; }
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


UENUM()
enum class  EMoKuEditState
{
	Defult,
	Forward
};



UCLASS()
class METALANDROAD_API UMetaLandRoadInteractiveTool : public UInteractiveTool, public IHoverBehaviorTarget, public IClickDragBehaviorTarget,public IMouseWheelBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);

	virtual void SetGizmoManager(UInteractiveGizmoManager* InGizmoManager)
	{
		GizmoManager = InGizmoManager;
	}

	/** UInteractiveTool overrides */
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;
	virtual FInputRayHit ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos) override;
	virtual void OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos) override;
	virtual void OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos) override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() {};
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)override;
	virtual void PostEditUndo() override;
	void InvertPreviewMesh();
	//���Ժ���
	void SwitchInsertCrossState();
	void SetUpBehaviors();



	void UpdateEditState(bool InEditing);
	void AddSplinePoint(const FVector& InHitPos, const FRotator& InHitRot,bool InteractiveGizmo, const FVector& InTangent = FVector::ZeroVector,UInteractiveGizmo* InGizmoInfo =  nullptr);
	bool GetEditState()const { return bIsEditing; };
	FInputRayHit FindRayHit(const FRay& WorldRay, FVector& HitPos);
	AMoKuEditBaseActor* GetActiveActor() { return ActiveActor; }
	void SetActiveActor(AMoKuEditBaseActor* InActor)
	{ 
		ActiveActor = InActor;
		if (GetEditState())
		{
			PreviewComponent = NewObject<UMoKuEditSplinesComponent>(ActiveActor, NAME_None, RF_Transactional);
			PreviewComponent->AttachToComponent(ActiveActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			PreviewComponent->SetVisibility(false);
			PreviewComponent->SetHiddenInGame(true);
			PreviewComponent->bDrawDebug = false;
			PreviewComponent->RegisterComponent();
		}
	
	}

	void SetStateEditor(EMoKuEditState InState) {StateOfEditor = InState;}

	void SetHUDState()
	{ 
		bIsDrawHud = bIsDrawHud ? false : true;
	}
	EMoKuEditState GetStateEditor() const { return StateOfEditor; }
	bool bCtrlModifierOn = false;
	bool bShiftModifierOn = false;
	bool bAltModifierOn = false;
	bool bAdjustControlPoint = false;
	void OnFinishedSplineEdit();
	/**
	 * Cook junction + interrupt draw session.
	 * Trigger: Ctrl+press while drawing when current road XY-crosses another (not A).
	 * A finish = end draw + mesh refresh only (see OnFinishedSplineEdit).
	 */
	void CookJunctionForActiveRoad(bool bFinishEditSession = true, bool bForceHubJunctionCook = false);
	/**
	 * After Ctrl add-point: if active road centerline XY-crosses another road, cook + end session.
	 * Needed when raised terrain steals the ray from the crossed road mesh.
	 */
	bool TryCookJunctionOnCtrlPressIfCrossed();

	/** Rebuild green dual-rail draw guidance (cursor follow + rubber-band/snap). */
	void UpdateDrawGuidanceRails();
	/** Selected anchor → cursor preview rails (lane-width green guides before Ctrl draw). */
	void UpdateAnchorPreviewGuidanceRails();
	/** After Shift mouth pick: pin guidance plane + refresh preview rails (EditorMode path). */
	void SyncAnchorPreviewFromEditTarget(const struct FMetaLandRoadEditTarget& Target);
	void ClearDrawGuidanceRails();
	/** Abort spline visualizer tracking, then clear editor selection (lane draw). */
	void ClearSelectionForDrawSession(bool bNoteSelectionChange = false);
	/** Clear guidance + EditTarget + lane-extend flags (no junction cook / no mesh refresh). */
	void EndLaneExtendVisualSession(bool bClearActiveActor = false);
	/** Drop lane-extend / EndSnap / guidance state without cooking roads (mode-exit cleanup). */
	void ResetDrawSessionState();
	/** Restore extend / EndSnap hover after Ctrl+Z without breaking mouth snap. */
	void ResyncLaneExtendSessionAfterUndo();
	void DrawDrawGuidanceRails(FPrimitiveDrawInterface* PDI) const;
	/** Screen labels for guidance sample indices (pairs with DrawDrawGuidanceRails). */
	void DrawDrawGuidanceRailPointHUD(FCanvas* Canvas, const FSceneView* View) const;
	bool HasDrawGuidanceRails() const;
	/** True while Ctrl-drawing from a lane mouth (lane-extend session). */
	bool IsLaneAnchorDrawSession() const { return bLaneAnchorDrawSession; }
	bool ShouldDrawExtendOverlay() const
	{
		return HasDrawGuidanceRails();
	}
	/** Hide all mouth overlay only while Ctrl is placing points. */
	bool ShouldSuppressLanePickOverlay() const;
	/** Hide EditTarget yellow while Ctrl is placing points (EndSnap cyan + hover still draw). */
	bool ShouldSuppressLanePickEditHover() const;
	/** Hide EditTarget yellow mouth bars only while actually placing an extend (keep idle mouth + green rails). */
	bool ShouldSuppressEditTargetMouthBar() const;
	bool TryGetEndSnapMixHoverOverlay(FMetaLandRoadDrawAnchor& OutAnchor) const;
	bool TryGetLaneDrawEndSnapOverlay(FMetaLandRoadDrawAnchor& OutAnchor) const;
	/** Idle Shift on another road: lock target mouth without replacing source EditTarget. */
	void ApplyIdleEndSnapTarget(const FMetaLandRoadDrawAnchor& Anchor);
	/** Release hover/locked EndSnap so the cursor can move freely again. */
	void ClearLaneDrawEndSnap(bool bClearUserLock);
	bool HasLaneDrawEndSnap() const;
	UMetaLandRoadEditorMode* GetCachedRoadEditorMode() const { return CachedRoadEditorMode.Get(); }
	void UpdateGizmoInfo(const TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>>& InGizmoInfos){ UpdateGizmoInfos = InGizmoInfos; }
	TMap <TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>> GetGizmoInfo() { return UpdateGizmoInfos; }
	TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>GizmoListInfo;


protected:

	UPROPERTY()
	TObjectPtr<UInteractiveToolSettings> OutputProperties = nullptr;

	UWorld* TargetWorld = nullptr;
	AMoKuEditBaseActor* ActiveActor = nullptr;
	static const int CtrlModifierID = 1;
	static const int ShiftModifierID = 2;
	static const int AltModifierID = 3;


private:

	UInteractiveGizmoManager* GizmoManager;
	TWeakObjectPtr<UMetaLandRoadEditorMode> CachedRoadEditorMode;
	AWorldSceneManagement* SceneManagement;
	bool bIsEditing = false;
	bool bIsDrawHud = false;
	TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>>UpdateGizmoInfos;
	UMoKuEditSplinesComponent* PreviewComponent = nullptr;
	EMoKuEditState StateOfEditor = EMoKuEditState::Defult;
	void RefreshGizmoInfo();
	void GetSplitIntersectionData(AActor* InSplitActor,const FVector& InWorldPos);

	void UpdateSplineAndJunction(FCurvesRoadOp* InRoadOp,AMoKuEditIntersectionActor*& InJunctionActor, AMoKuEditBaseActor* InHitActor,bool NeedInvert = true);
	void UpdateSplineRoad(USplineComponent* IntersectCurve,USplineComponent* InEditingCurve, const FVector& InIntersectionPos,bool NeedInvert);
	void UpdateRoadJunctionInfo(FCurvesRoadOp* InRoadOp,AMoKuEditIntersectionActor*& InJunctionActor, AMoKuEditBaseActor* IntersectActor, USplineComponent* InEditingCurve, const FVector& InIntersectionPos,EIntersectionState IntersectionState);
	FVector UpdateSplineRoad(AMoKuEditBaseActor* InHitActor, AMoKuEditIntersectionActor* InJunctionActor, bool NeedInvert);
	FCornerInfo UpdateJunctionSideInfo(USplineComponent* IntersectComp,TArray<FVector> IntersectionPosList,EIntersectionState IntersectionState);
	bool bIsDraw = false;

	struct IntersectBaseData
	{
		float TValue = 0;
		float StartValue = 0;
		float EndValue = 0;

		FTransform HitTransform;
		float EntryValue = 100;

		AMoKuEditBaseActor* PreHitActor;
	};

	IntersectBaseData IntersectData;

	IntersectBaseData PreIntersectData;

	FVector3d DrawOutSplinePos;

	FVector OutIntersectionPoint;

	TArray<FVector> DebugCorner;
	TArray<FVector> DebugSideCorner;

	TArray<FInteresectionPointInfo> OutIntersectPoints;

	TArray<FCornerInfo> CornerInfo;

	TArray<FBox> OutDebugBox;

	TArray<FVector> DebugOutIntersect;

private:
	//�����¼���Ӧ
	void Test(AActor* InActor)
	{
		if (InActor->IsA<AMoKuEditBaseActor>())
		{

			AMoKuEditBaseActor* BaseActor = Cast<AMoKuEditBaseActor>(InActor);
			auto RtnValue = GizmoListInfo.Find(BaseActor);
			if (!RtnValue)
			{
				BaseActor->OnInitGizmo().BindUObject(this, &UMetaLandRoadInteractiveTool::TEST_AddGizmo);
			}
		}

		Modify();
	};

	void TEST_AddGizmo(AMoKuEditBaseActor* InActor)
	{
		TArray<USPlineMarkGizmo*> GizmoInfos;
		TArray<FSocketGizmoTransformInfo> GizmoTransformInfos = InActor->GetGizmoInfo();
		//TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>>	InGizmoInfos;
		//InGizmoInfos.Add(InActor, GizmoTransformInfos);

		for (const auto& Info : GizmoTransformInfos)
		{
			USPlineMarkGizmo* SPlineMarkGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<USPlineMarkGizmo>(TEXT("SocketGizmo"), FString(), this);
			SPlineMarkGizmo->CreateGizmoHandle(Info);
			SPlineMarkGizmo->SetSelectedObject(InActor);
			GizmoInfos.Add(SPlineMarkGizmo);
		}
		if (GizmoListInfo.Find(InActor))
		{
			for (auto Item : GizmoListInfo[InActor])
			{
				GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
			}

			GizmoListInfo[InActor] = GizmoInfos;

		}
		else
		{
			GizmoListInfo.Add(InActor, GizmoInfos);
		}
		Modify();


	}
	//��������
	AStaticMeshActor* PreviewActor = nullptr;
	FString DataTablePath = "/Game/AssetTest/Corner_Recommend.Corner_Recommend";
	UDataTable* CornerDataTable = nullptr;
	int ScrollIndex = 0;
	bool bIsInvert = false;
	bool bIsInsertCross = false;
	void GetSelectedAsset(int Index = 0);
	UStaticMesh* CrossRoad = nullptr;

	TUniquePtr<FCurvesRoadOp> RoadOp = nullptr;

	FVector ResultHitPos;
	AActor* HitActor;
	bool bInsertJunction = false;

	/** Live draw guidance: parallel L/R rails (PDI neon green). One polyline pair per lane. */
	TArray<TArray<FVector>> GuidanceLeftPolylines;
	TArray<TArray<FVector>> GuidanceRightPolylines;
	FVector GuidanceLastCursor = FVector::ZeroVector;
	FVector GuidanceMoveDir = FVector::ForwardVector;
	double LastGuidanceHoverRedrawSeconds = 0.0;
	FVector LastGuidanceHoverSamplePos = FVector::ZeroVector;
	float GuidancePlaneZ = 0.0f;
	bool bHasGuidancePlaneZ = false;
	/** Lane-anchor branch draw uses Connect (Direct), not junction split. */
	bool bLaneAnchorDrawSession = false;
	/** Set when lane extend spawns a branch; survives FinalizeLaneDrawEndConnection until exit. */
	bool bRoadSpawnedFromLaneAnchorDraw = false;
	/** Lane-anchor Direct connect: skip hub junction scan on A / mode exit until session reset. */
	bool bLaneAnchorSkipHubJunctionCook = false;
	EMetaLandRoadDrawFinishMode LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	/** MetaRoad FromConnection: source lane mouth used for FitSpline + ConnectTo. */
	FMetaLandRoadDrawAnchor LaneDrawStartAnchor;
	bool bHasLaneDrawStartAnchor = false;

	/** MetaRoad EndConnection: hover/commit snap onto another road tip mouth. */
	FMetaLandRoadDrawAnchor LaneDrawEndSnapAnchor;
	bool bHasLaneDrawEndSnapAnchor = false;
	/** Shift+LMB locked the target mouth bundle (1..N, may be narrower than begin). */
	bool bEndSnapBundleUserLocked = false;
	FVector EndSnapGhostLeft = FVector::ZeroVector;
	FVector EndSnapGhostRight = FVector::ZeroVector;
	/** Travel direction into the yellow mouth (perpendicular to ghost bar). */
	FVector EndSnapGhostTravel = FVector::ForwardVector;
	bool bHasEndSnapGhost = false;
	/** Alt held during extend: hover preview on target tip lane. */
	FMetaLandRoadDrawAnchor EndSnapMixHoverAnchor;
	bool bHasEndSnapMixHover = false;
	void UpdateEndSnapMixHoverPreview(const FVector& CursorWorld);

	/** Restore lane-extend on an existing branch after mode exit / re-enter. */
	bool TryResumeLaneExtendOnBranchRoad(
		const FMetaLandRoadEditTarget& Target,
		AMoKuEditSplineActor* TargetRoad,
		UMetaLandRoadSplineComponent* AnchorSpline,
		const FInputDeviceRay& PressPos);

	/** Shift+LMB: pick/toggle TARGET tip lanes (count independent of begin). A commits EndConnection. */
	bool TryHandleEndSnapTargetLaneMixPick(const FInputDeviceRay& PressPos, bool bAddOnly);
	void FinalizeLaneDrawFromConnection(
		AMoKuEditSplineActor* SpawnedRoad,
		const FMetaLandRoadDrawAnchor& SourceAnchor);

	/** EndConnection: FitSplineToEnd + width blend + ConnectTo, then finish draw session. */
	void FinalizeLaneDrawEndConnection(
		AMoKuEditSplineActor* ActiveRoad,
		const FMetaLandRoadDrawAnchor& TargetAnchor);
	/** Idle two-mouth snap: A spawns the Bezier connector and ConnectTo both ends. */
	bool TryCommitIdleMouthToMouthConnection();


	void SetGuidancePlaneFromMouthCenter(const FVector& MouthCenter);
	void PopulateLaneExtendGuidance(
		const FVector& TipLeft,
		const FVector& TipRight,
		const FVector& TipCenter,
		const FVector& TipTangent,
		const FVector& CursorOnPlane,
		bool bAllowHermiteRamp,
		const FMetaLandRoadDrawAnchor* PerLaneAnchor = nullptr,
		double TipS = 0.0);

	struct FLaneExtendCenterPlan
	{
		FVector StartCenter = FVector::ZeroVector;
		FVector EndCenter = FVector::ZeroVector;
		FVector StartTangent = FVector::ForwardVector;
		FVector EndTangent = FVector::ForwardVector;
		bool bUseHermite = false;
	};

	void ResolveLaneExtendCenterPlan(
		const FVector& TipCenter,
		const FVector& TipTangent,
		const FVector& CursorOnPlane,
		bool bAllowHermiteRamp,
		FLaneExtendCenterPlan& OutPlan) const;

	bool AddLaneDrawSegmentFromCursor(
		const FVector& CursorOnPlane,
		const FRotator& HitRot,
		bool bFirstLaneExtend,
		bool bConnectionSnap,
		const FMetaLandRoadDrawAnchor* CommitEndSnapAnchor = nullptr);

	void AppendLaneDrawKeysToActiveSpline(
		const TArray<FVector>& Keys,
		const FLaneExtendCenterPlan& Plan,
		const FRotator& HitRot,
		bool bPinConnectionEnd = false);

	void SyncLaneDrawCursorFromRay(const FRay& WorldRay);

	void UpdateLaneDrawSnapFromWorldHit(bool bHitWorld, AActor* InHitActor, const FVector& TraceHitPos);

	/** Arrive tangent for lane-anchor edge/mouth snap (not along target centerline). */
	FVector LaneSnapArriveTangent = FVector::ForwardVector;
	bool bHasLaneSnapArriveTangent = false;

	float GetRampStraightStubCm() const;
	float GetTurnAngleThresholdCos() const;
	float GetSplineInsertStepCm() const;
	float GetConnectionSnapDistanceCm() const;
	int32 ResolveEndConnectTargetLaneCount() const;
	/** Snapshot end-snap anchor before hover sync can wipe Alt/Shift bundle. */
	const FMetaLandRoadDrawAnchor* ResolveEndSnapCommitAnchor(FMetaLandRoadDrawAnchor& OutStorage) const;
	bool TryAdjustEndConnectLaneCount(int32 Delta, const FInputDeviceRay& CurrentPos);
	float GetLateralHermiteTriggerCm() const;
	/** Tip mouth full width (L↔R) used by auto lane-draw scaling. */
	float ResolveActiveTipRoadWidthCm() const;

};


UCLASS()
class USplineFinishedBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:

	virtual EInputDevices GetSupportedDevices() override
	{
		return EInputDevices::Keyboard;
	}

	virtual FInputCapturePriority GetPriority()override { return FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY); }
	virtual void Initialize(UMetaLandRoadInteractiveTool* SplineEditTool);
	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& Input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& Input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;


protected:

	UMetaLandRoadInteractiveTool* SplineEditTool;


};
