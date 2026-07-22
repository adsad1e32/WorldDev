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
#include "Landscape.h"


#include "ShapesGenerationTool.generated.h"




UCLASS()
class UShapesGenerationSettings : public  UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Test", DisplayName = "Test")
	int32  Shape = 0;

};




USTRUCT()
struct FGridHighLightSelectInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector> EdgePath;
	UPROPERTY()
	bool bIsCreated = false;
	UPROPERTY()
	bool bIsHovered = false;
	UPROPERTY()
	bool bIsSelected = false;
};


//USTRUCT()
//struct FSplineInfoCombination
//{
//	GENERATED_BODY()
//
//	UPROPERTY()
//	USplineComponent* SplineComp;
//	UPROPERTY()
//	AMoKuEditBaseActor* ParentActor;
//};


USTRUCT(BlueprintType)
struct FHighLightMaterialInfo
{
	GENERATED_BODY()

	UPROPERTY()
	UMaterialInterface* HoveredMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("MaterialInstanceConstant'/TestMode/Material/HoveredHighLightMaterial_Inst.HoveredHighLightMaterial_Inst'"));

	UPROPERTY()
	UMaterialInterface* SelectedMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("MaterialInstanceConstant'/TestMode/Material/SelectedHighLightMaterial_Inst.SelectedHighLightMaterial_Inst'"));
};



UCLASS()
class TESTMODE_API UShapesGenerationToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()


public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override { return true; }
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class TESTMODE_API UShapesGenerationTool : public UInteractiveTool, public IHoverBehaviorTarget, public IClickDragBehaviorTarget, public IMouseWheelBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);

	/** UInteractiveTool overrides */
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) {}
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) {};
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos)override;
	virtual void OnTerminateDragSequence() {};
	virtual FInputRayHit ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos) { return FInputRayHit(); };
	virtual void OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos) {};
	virtual void OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos) {};
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos)override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos)override;
	virtual void OnEndHover(){};
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)override;
	virtual void PostEditUndo(){};
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;


	void CreateRegionalShape();

protected:
	UWorld* TargetWorld = nullptr;

	UPROPERTY()
	TObjectPtr<UShapesGenerationSettings> OutputProperties = nullptr;
	const int CtrlModifierID = 1;
	const int ShiftModifierID = 2;

	void SetUpBehaviors();


private:
	void TestVisual();

private:

	bool bCtrlModifierOn = false;
	bool bShiftModifierOn = false;

	bool DetectClosedSpline(const TArray<FVector>&InEdgePath, const FVector2D& MouseViewportPos);

	int32 HoverIndex = -1;
	int32 PrevHoverIndex = -1;

	TArray<FVector> DebugPoints;
	TArray<FGridHighLightSelectInfo> DebugDrawLine;


	//UPROPERTY(Transient)
	AActor* PreviewActor;

	FHighLightMaterialInfo MaterialStateInfo;

	TArray<TObjectPtr<UDynamicMeshComponent>> HighlightMeshCompList;

	TObjectPtr<AWorldSceneManagement> SceneManagement;

	TArray<USplineComponent*> Collection;
};


UCLASS()
class URegionalShapeCreatorBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:

	virtual EInputDevices GetSupportedDevices() override
	{
		return EInputDevices::Keyboard;
	}

	virtual FInputCapturePriority GetPriority()override { return FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY); }
	virtual void Initialize(UShapesGenerationTool* InRegionalShapeTool);
	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& Input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& Input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;
	//virtual bool IsReleased(const FInputDeviceState& input);


protected:

	UShapesGenerationTool* RegionalShapeTool;




};

