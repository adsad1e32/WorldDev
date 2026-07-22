#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/ClickDragTool.h"
#include "PropertyEditorModule.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "TestModeEditorMode.h"
#include "MoKuEditIntersectionActor.h"
#include "Engine/StaticMeshActor.h"
#include "InteractiveTool.h"
#include "SplineBasicElement.h"
#include "Landscape.h"


#include "TestModeInteractiveTool.generated.h"



class UMoKuEditSplinesComponent;
class FCurvesRoadOp;
struct FInteresectionPointInfo;

UCLASS()
class UInteractiveToolSettings : public  UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "BasicSetting", DisplayName = "Road Mode")
	ERoadType RoadModeType = ERoadType::Surface;

	UPROPERTY(EditAnywhere, Category = BasicSetting, meta = (EditCondition = "RoadModeType != ERoadType::Surface", EditConditionHides))
	double CurveOffset = 0.0;

};

UCLASS()
class TESTMODE_API UTestModeInteractiveToolBuilder : public UInteractiveToolBuilder
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
class TESTMODE_API UTestModeInteractiveTool : public UInteractiveTool, public IHoverBehaviorTarget, public IClickDragBehaviorTarget,public IMouseWheelBehaviorTarget
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
	//≤‚ ‘∫Ø ˝
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
	bool bAdjustControlPoint = false;
	void OnFinishedSplineEdit();
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


private:

	UInteractiveGizmoManager* GizmoManager;
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
	//≤‚ ‘ ¬º˛œÏ”¶
	void Test(AActor* InActor)
	{
		if (InActor->IsA<AMoKuEditBaseActor>())
		{

			AMoKuEditBaseActor* BaseActor = Cast<AMoKuEditBaseActor>(InActor);
			auto RtnValue = GizmoListInfo.Find(BaseActor);
			if (!RtnValue)
			{
				BaseActor->OnInitGizmo().BindUObject(this, &UTestModeInteractiveTool::TEST_AddGizmo);
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
	//≤‚ ‘«¯”Ú
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
	virtual void Initialize(UTestModeInteractiveTool* SplineEditTool);
	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& Input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& Input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;


protected:

	UTestModeInteractiveTool* SplineEditTool;


};
