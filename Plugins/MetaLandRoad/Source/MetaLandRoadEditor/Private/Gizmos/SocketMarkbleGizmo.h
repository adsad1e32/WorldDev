#pragma once

#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BaseGizmos/GizmoActor.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "UObject/Package.h"
#include "InteractiveToolChange.h"
#include "EditorGizmos/TransformGizmo.h"
#include "EditorInteractiveGizmoSelectionBuilder.h"
#include "MoKuEditorGizmoBase.h"

#include "SocketMarkbleGizmo.generated.h"
class USphereComponent;
class UGizmoBoxComponent;
class AMoKuEditSplineActor;
class UGizmoViewContext;
class UGizmoElementBox;
class UGizmoElementGroup;
class UTransformProxy;
class UGizmoElementHitMultiTarget;
class UGizmoConstantFrameAxisSource;
class AMoKuEditBaseActor;

UCLASS()
class USocketMarkbleGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;

};

UCLASS()
class USPlineMarkGizmo : public UInteractiveGizmo,public IHoverBehaviorTarget, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:
	// UInteractiveGizmo interface

	USPlineMarkGizmo();


	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;

	// IHoverBehaviorTarget interface
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) {};
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos)override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) {};
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos)override;
	virtual void OnTerminateDragSequence() {};
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	virtual void SetSelectedObject(AMoKuEditBaseActor* InSeleObject);
	AMoKuEditBaseActor* GetSplineActor()const { return SplineActor;};
	virtual void SetWorld(UWorld* InWorld);
	virtual void SetGizmoViewContext(UGizmoViewContext* GizmoViewContextIn);
	virtual void SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider = nullptr);
	virtual void ClearActiveTarget();
	bool GetInteractiveState() const { return bIsInteraction; }
	bool GetHorveState() const { return bIsHoverd; }
	void CreateGizmoHandle(const FVector& InOrigin, const FQuat& GizmoQuat);
	void CreateGizmoHandle(const FSocketGizmoTransformInfo& GizmoTransformInfo);

public:

	UPROPERTY()
	TObjectPtr<UTransformProxy> ActiveTarget;

	UPROPERTY()
	TObjectPtr<UGizmoElementHitMultiTarget> HitTarget;

	UPROPERTY()
	TObjectPtr<UGizmoObjectModifyStateTarget> StateTarget;

	UPROPERTY()
	TObjectPtr<ASocketMarkbleGizmo> GizmoActor;

	UPROPERTY()
	TScriptInterface<ITransformGizmoSource> TransformGizmoSource;

private:

	UPROPERTY()
	TObjectPtr<UWorld> World;
	UPROPERTY()
	TObjectPtr<UGizmoViewContext> GizmoViewContext;

	UPROPERTY()
	TObjectPtr<AMoKuEditBaseActor>  SplineActor;

protected:

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BaseDisplayMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CurrentDisplayMaterial;

	UPROPERTY()
	FTransform CurrentTransform = FTransform::Identity;

	UPROPERTY()
	ETransformGizmoPartIdentifier LastHitPart = ETransformGizmoPartIdentifier::Default;

	UPROPERTY()
	TObjectPtr<UGizmoConstantFrameAxisSource> CameraAxisSource;
	UPROPERTY()
	TObjectPtr<UGizmoElementBox> MarkableElement;

protected:

	virtual void SetupMaterials();
	virtual FInputRayHit UpdateHoveredPart(const FInputDeviceRay& DevicePos);
	bool VerifyPartIdentifier(uint32 InPartIdentifier) const;
	uint32 GetMaxPartIdentifier() const;
	UGizmoElementBox* MakeDisplayHandle();
	FTransform GetGizmoTransform() const;
	void UpdateCameraAxisSource();


protected:

	bool bIsInteraction = false;
	bool bIsHoverd = false;
	int  CtrlModifierID = 1;
	bool bCtrlModifierOn = false;

	int  ShiftModifierID = 2;
	bool bShiftModifierOn = false;

private:

	void UpdateHoverState(bool bInHover, ETransformGizmoPartIdentifier InHitPartId);


};
