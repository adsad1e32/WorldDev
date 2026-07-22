#pragma once

#include "InteractiveGizmo.h"
#include "EditorGizmos/TransformGizmo.h"
#include "BaseGizmos/GizmoActor.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "BaseGizmos/GizmoElementGroup.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditorGizmoBase.generated.h"
class UGizmoViewContext;
class UTransformProxy;
class UGizmoElementHitMultiTarget;
class UGizmoElementRoot;


UENUM(BlueprintType)
namespace ESplineMeshCatergory
{
	enum Type : int
	{
		MainAsset,
		ExtraAsset,
	};
}


//Gizmo 存储信息参数
USTRUCT()
struct FGizmoStoredInfoParams
{
	GENERATED_BODY()
	
	TObjectPtr<AActor>OwnerActor;
	TObjectPtr<UStaticMesh> OwnerMesh;
	TOptional<int32> Index;
	TOptional<TMap<int,int>> ExtraAssetIndex;
};



UCLASS()
class ASocketMarkbleGizmo : public AGizmoActor
{
	GENERATED_BODY()

public:

	ASocketMarkbleGizmo();
	void SetTangentInfo(const FVector& InTangent) { Tangent = InTangent;}
	void SetGizmoInfo(const FSocketGizmoTransformInfo& InGizmoInfo) { GizmoInfo = InGizmoInfo;}
	FVector GetTangentInfo()const { return Tangent; }
	FSocketGizmoTransformInfo GetGizmoInfo()const {return GizmoInfo;}
	TObjectPtr<UGizmoElementGroup> GizmoElementRoot;

protected:

	FVector Tangent;
	FSocketGizmoTransformInfo GizmoInfo; 
};

UCLASS()
class UMoKuEditorGizmoBase : public UInteractiveGizmo,public IHoverBehaviorTarget, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:
	static constexpr FLinearColor AssetMarkColor = FLinearColor(0.0197f, 0.0115f, 0.45f);
	static constexpr FLinearColor LocationMarkColor = FLinearColor(0.0417f, 0.015f, 0.0f);
	static constexpr float ScaleCubeDim = 7.0f;
	static constexpr float PixelHitDistanceThreshold = 7.0;


public:


	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;

	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) {};
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos)override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() {};
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn){};

	virtual void SetupMaterials();
	virtual void SetWorld(UWorld* InWorld);
	virtual void SetGizmoViewContext(UGizmoViewContext* GizmoViewContextIn);
	virtual void SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider = nullptr);
	virtual void ClearActiveTarget();
	virtual UGizmoElementBase* MakeDisplayHandle();
	virtual void SetVisibility(bool InbVisible);
	virtual bool GetVisibility()const { return bVisible; }
	virtual void SetSelectedObject(const FGizmoStoredInfoParams& InParams);

	UPROPERTY()
	TObjectPtr<UTransformProxy> ActiveTarget;

	UPROPERTY()
	TObjectPtr<UGizmoElementHitMultiTarget> HitTarget;

	UPROPERTY()
	TObjectPtr<ASocketMarkbleGizmo> GizmoActor;

	UPROPERTY()
	TScriptInterface<ITransformGizmoSource> TransformGizmoSource;


	UPROPERTY()
	TObjectPtr<UGizmoElementGroup> GizmoElementRoot;

	UPROPERTY()
	TObjectPtr<UGizmoElementBase> MarkableElement;

	UPROPERTY()
	FGizmoStoredInfoParams OwnerGizmoParams;


	UPROPERTY()
	bool bInteraction = false;

	/**
	 * @return current transform of Gizmo
	 */
	FTransform GetGizmoTransform() const;

	void ReinitializeGizmoTransform(const FTransform& NewTransform, bool bKeepGizmoUnscaled = true);

	void CreateGizmoHandle(const FVector& InOrigin, const FQuat& InGizmoQuat);



protected:

	UPROPERTY()
	TObjectPtr<UWorld> World;

	UPROPERTY()
	TObjectPtr<UGizmoViewContext> GizmoViewContext;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CurrentDisplayMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BaseDisplayMaterial;

	UPROPERTY()
	FTransform CurrentTransform = FTransform::Identity;

	UPROPERTY()
	ETransformGizmoPartIdentifier LastHitPart = ETransformGizmoPartIdentifier::Default;

	UPROPERTY()
	bool bVisible = false;


protected:

	virtual FInputRayHit UpdateHoveredPart(const FInputDeviceRay& DevicePos);

	virtual bool VerifyPartIdentifier(uint32 InPartIdentifier) const;

	uint32 GetMaxPartIdentifier() const;

	virtual void UpdateHoverState(bool bInHover, ETransformGizmoPartIdentifier InPartId);

	virtual void SetupBehaviors();




};

UCLASS()
class UMoKuEditRePlaceGizmo : public UMoKuEditorGizmoBase
{

	GENERATED_BODY()
public:
	virtual void SetupMaterials();
	virtual void Setup();
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void SetSelectedObject(const FGizmoStoredInfoParams& InParams)override;

	
};


UCLASS()
class UMoKuEditRePlaceGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
	
};

