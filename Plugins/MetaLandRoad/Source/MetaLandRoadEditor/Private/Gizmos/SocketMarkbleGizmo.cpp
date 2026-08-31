#include "SocketMarkbleGizmo.h"
#include "InteractiveGizmoManager.h"
#include "SceneManagement.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "Components/SphereComponent.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "Engine/CollisionProfile.h"
#include "ContextObjectStore.h"
#include "ToolContextInterfaces.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoElementBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "BaseGizmos/GizmoElementGroup.h"
#include "BaseGizmos/TransformProxy.h"
#include "BaseGizmos/GizmoElementHitTargets.h"
#include "BaseGizmos/AxisSources.h"
#include "EditorGizmos/EditorTransformGizmoSource.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "MetaLandRoadEditorMode.h"
#include "Tools/MetaLandRoadInteractiveTool.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditBaseActor.h"




#define LOCTEXT_NAMESPACE "SocketMarkbleGizmo"


UInteractiveGizmo* USocketMarkbleGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	USPlineMarkGizmo* NewGizmo = NewObject<USPlineMarkGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);
	UGizmoViewContext* GizmoViewContext = SceneState.ToolManager->GetContextObjectStore()->FindContext<UGizmoViewContext>();
	check(GizmoViewContext && GizmoViewContext->IsValidLowLevel());
	NewGizmo->SetGizmoViewContext(GizmoViewContext);
	//NewGizmo->TransformGizmoSource = UEditorTransformGizmoSource::Construct(NewGizmo);
	
	NewGizmo->TransformGizmoSource = UEditorTransformGizmoSource::CreateNew(NewGizmo);
	FActorSpawnParameters SpawnInfo;
	UWorld* World =  NewGizmo->GetWorld();
	NewGizmo->GizmoActor = World->SpawnActor<ASocketMarkbleGizmo>(FVector::ZeroVector,FRotator::ZeroRotator, SpawnInfo);
	return NewGizmo;


}

USPlineMarkGizmo::USPlineMarkGizmo()
{
	GizmoActor = nullptr;
	SplineActor = nullptr;
}

void USPlineMarkGizmo::Setup()
{

	UInteractiveGizmo::Setup();
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	UClickDragInputBehavior* DragBehavior = NewObject<UClickDragInputBehavior>(this);
	HoverBehavior->Initialize(this);
	DragBehavior->Initialize(this);
	HoverBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	DragBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	DragBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	AddInputBehavior(HoverBehavior);
	AddInputBehavior(DragBehavior);
	SetupMaterials();
}

void USPlineMarkGizmo::Shutdown()
{
	ClearActiveTarget();
	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
		SplineActor = nullptr;
	}
}

void USPlineMarkGizmo::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void USPlineMarkGizmo::CreateGizmoHandle(const FVector& InOrigin,const FQuat& InGizmoQuat)
{
	FRotator GizmoRotator = InGizmoQuat.Rotator();
	GizmoActor->SetActorLocation(InOrigin);
	GizmoActor->SetActorRotation(GizmoRotator);
	MarkableElement = MakeDisplayHandle();
	GizmoActor->GizmoElementRoot = NewObject<UGizmoElementGroup>();
	GizmoActor->GizmoElementRoot->SetConstantScale(true);
	GizmoActor->GizmoElementRoot->Add(MarkableElement);
	GizmoActor->GizmoElementRoot->SetHoverMaterial(CurrentDisplayMaterial);
	GizmoActor->GizmoElementRoot->SetInteractMaterial(CurrentDisplayMaterial);
	UTransformProxy* TransformProxy = NewObject<UTransformProxy>();
	if (GizmoViewContext)
	{
		FTransform ActorTransform = GizmoActor->GetActorTransform();
		TransformProxy->SetTransform(ActorTransform);
		SetActiveTarget(TransformProxy);
		HitTarget = UGizmoElementHitMultiTarget::Construct(GizmoActor->GizmoElementRoot, GizmoViewContext);
		HitTarget->GizmoTransformProxy = TransformProxy;

	}

}

void USPlineMarkGizmo::CreateGizmoHandle(const FSocketGizmoTransformInfo& GizmoTransformInfo)
{

	FRotator GizmoRotator = GizmoTransformInfo.Transform.Rotator();
	FVector  GizmoOrigin =  GizmoTransformInfo.Transform.GetLocation();
	FVector  GizmoTangent = GizmoTransformInfo.Tangent;
	GizmoActor->SetActorLocation(GizmoOrigin);
	GizmoActor->SetActorRotation(GizmoRotator);
	GizmoActor->SetTangentInfo(GizmoTangent);
	GizmoActor->SetGizmoInfo(GizmoTransformInfo);
	MarkableElement = MakeDisplayHandle();
	GizmoActor->GizmoElementRoot = NewObject<UGizmoElementGroup>();
	GizmoActor->GizmoElementRoot->SetConstantScale(true);
	GizmoActor->GizmoElementRoot->Add(MarkableElement);
	GizmoActor->GizmoElementRoot->SetHoverMaterial(CurrentDisplayMaterial);
	GizmoActor->GizmoElementRoot->SetInteractMaterial(CurrentDisplayMaterial);
	UTransformProxy* TransformProxy = NewObject<UTransformProxy>();
	if (GizmoViewContext)
	{
		FTransform ActorTransform = GizmoActor->GetActorTransform();
		TransformProxy->SetTransform(ActorTransform);
		SetActiveTarget(TransformProxy);
		HitTarget = UGizmoElementHitMultiTarget::Construct(GizmoActor->GizmoElementRoot, GizmoViewContext);
		HitTarget->GizmoTransformProxy = TransformProxy;

	}
}

FInputRayHit USPlineMarkGizmo::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FInputRayHit RayHit;

	if (HitTarget)
	{
		RayHit = HitTarget->IsHit(PressPos);
		ETransformGizmoPartIdentifier HitPart;
		if (RayHit.bHit && VerifyPartIdentifier(RayHit.HitIdentifier))
		{
			HitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
		}
		else
		{
			HitPart = ETransformGizmoPartIdentifier::Default;
		}
		if (HitPart != ETransformGizmoPartIdentifier::Default)
		{
			LastHitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
		}
	}

	return RayHit;
}

void USPlineMarkGizmo::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == CtrlModifierID)
	{
		bCtrlModifierOn = bIsOn;
	}
	if (ModifierID == ShiftModifierID)
	{
		bShiftModifierOn = bIsOn;
	}
}

void USPlineMarkGizmo::SetSelectedObject(AMoKuEditBaseActor* InSeleGizmo)
{
	SplineActor = InSeleGizmo;
}

void USPlineMarkGizmo::SetGizmoViewContext(UGizmoViewContext* GizmoViewContextIn)
{
	GizmoViewContext = GizmoViewContextIn;
}

UGizmoElementBox* USPlineMarkGizmo::MakeDisplayHandle()
{

	UGizmoElementBox* BoxElement = NewObject<UGizmoElementBox>();
	BoxElement->SetPartIdentifier(static_cast<uint32>(ETransformGizmoPartIdentifier::ScaleUniform));
	BoxElement->SetCenter(FVector::ZeroVector);
	BoxElement->SetUpDirection(FVector::UpVector);
	BoxElement->SetSideDirection(FVector::RightVector);
	BoxElement->SetDimensions(FVector(12.0,12.0f,12.0f));
	BoxElement->SetMaterial(BaseDisplayMaterial);
	return BoxElement;
}

void USPlineMarkGizmo::SetupMaterials()
{
	UMaterial* MaterialBase = GEngine->ArrowMaterial;
	BaseDisplayMaterial = UMaterialInstanceDynamic::Create(MaterialBase, NULL);
	BaseDisplayMaterial->SetVectorParameterValue("GizmoColor", FLinearColor(0.394f, 0.0197f, 0.0f));

	CurrentDisplayMaterial = UMaterialInstanceDynamic::Create(MaterialBase, NULL);
	CurrentDisplayMaterial->SetVectorParameterValue("GizmoColor", FLinearColor(1.0f, 1.0f, 0.0f));

}

void USPlineMarkGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (GizmoActor && RenderAPI)
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		CurrentTransform = ActiveTarget->GetTransform();
		UGizmoElementBase::FRenderTraversalState RenderState;
		RenderState.Initialize(RenderAPI->GetSceneView(), GetGizmoTransform());
		GizmoActor->GizmoElementRoot->Render(RenderAPI, RenderState);
		FVector Tangent = GizmoActor->GetTangentInfo();
	}

}

void USPlineMarkGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{

	IToolsContextQueriesAPI* QueriesAPI = GetGizmoManager()->GetContextQueriesAPI();
	FToolBuilderState StateOut;
	QueriesAPI->GetCurrentSelectionState(StateOut);
	if (StateOut.ToolManager->GetActiveToolName(EToolSide::Left) == SPLINETOOLNAME)
	{
		UMetaLandRoadInteractiveTool* CurTool = (UMetaLandRoadInteractiveTool*)StateOut.ToolManager->GetActiveTool(EToolSide::Left);
		if (CurTool&& StateOut.World)
		{
			if (HitTarget && LastHitPart != ETransformGizmoPartIdentifier::Default)
			{
				if (GizmoActor)
				{
					FTransform HitTransform = ActiveTarget->GetTransform();
					FVector  HitPos = HitTransform.GetLocation();
					FRotator HitRot = HitTransform.GetRotation().Rotator();
					FVector Tangent = GizmoActor->GetTangentInfo();
					FSocketGizmoTransformInfo GizmoInfo = GizmoActor->GetGizmoInfo();
					if (bCtrlModifierOn)
					{
						CurTool->AddSplinePoint(HitPos, HitRot, true, Tangent);
					}
					if (bShiftModifierOn&& GizmoInfo.IndexOfPoint>-1)
					{
						FScopedTransaction Transaction(LOCTEXT("MoKuEdit_Add Point", "Active MoKuSpline Control Point"));
						CurTool->UpdateEditState(true);
						CurTool->SetActiveActor(SplineActor);
						GEditor->SelectNone(true, true);
						GEditor->SelectActor(SplineActor,true,true);
						if (GizmoInfo.IndexOfPoint == 0)
						{
							CurTool->SetStateEditor(EMoKuEditState::Forward);
						}
						else if (GizmoInfo.IndexOfPoint)
						{
							CurTool->SetStateEditor(EMoKuEditState::Defult);
						}
					}
				
				}
			}
		}
	}
}

void USPlineMarkGizmo::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	bIsInteraction = false;
	GEditor->EndTransaction();
}

void USPlineMarkGizmo::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
{
	if (ActiveTarget != nullptr)
	{
		ClearActiveTarget();
	}
	ActiveTarget = Target;
	if (!ActiveTarget)
	{
		return;
	}

	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}

	StateTarget = UGizmoObjectModifyStateTarget::Construct(Target,
		LOCTEXT("SocketTransaction", "Socket"), TransactionProvider, this);

	CameraAxisSource = NewObject<UGizmoConstantFrameAxisSource>(this);

}

void USPlineMarkGizmo::ClearActiveTarget()
{
	ActiveTarget = nullptr;
}

FTransform USPlineMarkGizmo::GetGizmoTransform() const
{
	float Scale = 1.0f;
	if (TransformGizmoSource)
	{
		Scale = TransformGizmoSource->GetGizmoScale();
	}
	FTransform GizmoLocalToWorldTransform = CurrentTransform;
	GizmoLocalToWorldTransform.SetScale3D(FVector(Scale, Scale, Scale));

	return GizmoLocalToWorldTransform;
}

FInputRayHit USPlineMarkGizmo::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
	return UpdateHoveredPart(DevicePos);
}

bool USPlineMarkGizmo::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FInputRayHit RayHit = UpdateHoveredPart(DevicePos);
	return RayHit.bHit;
}

void USPlineMarkGizmo::OnEndHover()
{
	if (HitTarget && LastHitPart != ETransformGizmoPartIdentifier::Default)
	{
		HitTarget->UpdateHoverState(false, static_cast<uint32>(LastHitPart));
	}
}

FInputRayHit USPlineMarkGizmo::UpdateHoveredPart(const FInputDeviceRay& PressPos)
{
	if (!HitTarget)
	{
		bIsHoverd = false;
		return FInputRayHit();
	}

	FInputRayHit RayHit = HitTarget->IsHit(PressPos);

	if (RayHit.bHit)
	{
		bIsHoverd = true;
	}
	ETransformGizmoPartIdentifier HitPart;
	if (RayHit.bHit && VerifyPartIdentifier(RayHit.HitIdentifier))
	{
		HitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
	}
	else
	{
		HitPart = ETransformGizmoPartIdentifier::Default;
		bIsHoverd = false;
	}
	if (HitPart != LastHitPart)
	{
		if (LastHitPart != ETransformGizmoPartIdentifier::Default)
		{
			UpdateHoverState(false, LastHitPart);
		}

		if (HitPart != ETransformGizmoPartIdentifier::Default)
		{
			UpdateHoverState(true, HitPart);
		}

		LastHitPart = HitPart;
	}
	return RayHit;
}

bool USPlineMarkGizmo::VerifyPartIdentifier(uint32 InPartIdentifier) const
{
	if (InPartIdentifier >= GetMaxPartIdentifier())
	{
		return false;
	}
	return true;
}

uint32 USPlineMarkGizmo::GetMaxPartIdentifier() const
{
	return static_cast<uint32>(ETransformGizmoPartIdentifier::Max);
}

void USPlineMarkGizmo::UpdateHoverState(bool bInHover, ETransformGizmoPartIdentifier InHitPartId)
{
	HitTarget->UpdateHoverState(bInHover, static_cast<uint32>(InHitPartId));

}

void USPlineMarkGizmo::Tick(float DeltaTime)
{
	UpdateCameraAxisSource();
}

void USPlineMarkGizmo::UpdateCameraAxisSource()
{
	FViewCameraState CameraState;
	GetGizmoManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
	if (CameraAxisSource != nullptr)
	{
		CameraAxisSource->Origin = ActiveTarget ? ActiveTarget->GetTransform().GetLocation() : FVector::ZeroVector;
		CameraAxisSource->Direction = -CameraState.Forward();
		CameraAxisSource->TangentX = CameraState.Right();
		CameraAxisSource->TangentY = CameraState.Up();
	}



}

#undef LOCTEXT_NAMESPACE