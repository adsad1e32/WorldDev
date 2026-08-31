#include "MoKuEditorGizmoBase.h"

#include "InteractiveGizmoManager.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "BaseGizmos/GizmoElementBox.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "ContextObjectStore.h"
#include "LibraryAssetWindow.h"
#include "Components/SphereComponent.h"
#include "MoKuEditSplineActor.h"


ASocketMarkbleGizmo::ASocketMarkbleGizmo()
{
	USphereComponent* SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SocketGizmo"));
	RootComponent = SphereComponent;
	SphereComponent->InitSphereRadius(1);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}



void UMoKuEditorGizmoBase::Setup()
{

	UInteractiveGizmo::Setup();
	SetupBehaviors();
	SetupMaterials();
}


void UMoKuEditorGizmoBase::SetupMaterials()
{

	UMaterial* MaterialBase = GEngine->ArrowMaterial;


	BaseDisplayMaterial = UMaterialInstanceDynamic::Create(MaterialBase, NULL);
	BaseDisplayMaterial->SetVectorParameterValue("GizmoColor", FLinearColor(0.394f, 0.0197f, 0.0f));

	CurrentDisplayMaterial = UMaterialInstanceDynamic::Create(MaterialBase, NULL);
	CurrentDisplayMaterial->SetVectorParameterValue("GizmoColor", FLinearColor(1.0f, 1.0f, 0.0f));

}

void UMoKuEditorGizmoBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (GizmoActor && RenderAPI && bVisible)
	{
		CurrentTransform = ActiveTarget->GetTransform();
		UGizmoElementBase::FRenderTraversalState RenderState;
		RenderState.Initialize(RenderAPI->GetSceneView(), GetGizmoTransform());
		GizmoActor->GizmoElementRoot->Render(RenderAPI, RenderState);
	}

}


void UMoKuEditorGizmoBase::Shutdown()
{
	ClearActiveTarget();
}

void UMoKuEditorGizmoBase::Tick(float DeltaTime)
{
}



void UMoKuEditorGizmoBase::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void UMoKuEditorGizmoBase::SetGizmoViewContext(UGizmoViewContext* GizmoViewContextIn)
{

	GizmoViewContext = GizmoViewContextIn;

}


void UMoKuEditorGizmoBase::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
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

}

void UMoKuEditorGizmoBase::ClearActiveTarget()
{
	ActiveTarget = nullptr;
}

FTransform UMoKuEditorGizmoBase::GetGizmoTransform() const
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



void UMoKuEditorGizmoBase::SetSelectedObject(const FGizmoStoredInfoParams& InParams)
{
	OwnerGizmoParams = InParams;
}




UGizmoElementBase* UMoKuEditorGizmoBase::MakeDisplayHandle()
{

	UGizmoElementBox* BoxElement = NewObject<UGizmoElementBox>();
	BoxElement->SetPartIdentifier(static_cast<uint32>(ETransformGizmoPartIdentifier::ScaleUniform));
	BoxElement->SetCenter(FVector::ZeroVector);
	BoxElement->SetUpDirection(FVector::UpVector);
	BoxElement->SetSideDirection(FVector::RightVector);
	BoxElement->SetDimensions(FVector(ScaleCubeDim, ScaleCubeDim,ScaleCubeDim));
	BoxElement->SetMaterial(BaseDisplayMaterial);
	BoxElement->SetPixelHitDistanceThreshold(PixelHitDistanceThreshold);
	return BoxElement;

}

void UMoKuEditorGizmoBase::SetVisibility(bool bVisibleIn)
{
	bVisible = bVisibleIn;
}

void UMoKuEditorGizmoBase::ReinitializeGizmoTransform(const FTransform& NewTransform, bool bKeepGizmoUnscaled)
{

	bool bSavedSetPivotMode = ActiveTarget->bSetPivotMode;
	ActiveTarget->bSetPivotMode = true;
	ActiveTarget->SetTransform(NewTransform);
	ActiveTarget->bSetPivotMode = bSavedSetPivotMode;

}

FInputRayHit UMoKuEditorGizmoBase::UpdateHoveredPart(const FInputDeviceRay& PressPos)
{
	if (!HitTarget)
	{
		return FInputRayHit();
	}

	FInputRayHit RayHit = HitTarget->IsHit(PressPos);
	ETransformGizmoPartIdentifier HitPart;
	if (RayHit.bHit && VerifyPartIdentifier(RayHit.HitIdentifier))
	{
		HitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
	}
	else
	{
		HitPart = ETransformGizmoPartIdentifier::Default;
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

bool UMoKuEditorGizmoBase::VerifyPartIdentifier(uint32 InPartIdentifier) const
{
	if (InPartIdentifier >= GetMaxPartIdentifier())
	{
		return false;
	}

	return true;
}

uint32 UMoKuEditorGizmoBase::GetMaxPartIdentifier() const
{
	return static_cast<uint32>(ETransformGizmoPartIdentifier::Max);
}

void UMoKuEditorGizmoBase::UpdateHoverState(bool bInHover, ETransformGizmoPartIdentifier InHitPartId)
{
	HitTarget->UpdateHoverState(bInHover, static_cast<uint32>(InHitPartId));
}

FInputRayHit UMoKuEditorGizmoBase::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
	return UpdateHoveredPart(DevicePos);
}

bool UMoKuEditorGizmoBase::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FInputRayHit RayHit = UpdateHoveredPart(DevicePos);
	return RayHit.bHit;
}

void UMoKuEditorGizmoBase::OnEndHover()
{
	if (HitTarget && LastHitPart != ETransformGizmoPartIdentifier::Default)
	{
		HitTarget->UpdateHoverState(false, static_cast<uint32>(LastHitPart));
	}
}

void UMoKuEditorGizmoBase::SetupBehaviors()
{
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	HoverBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	AddInputBehavior(HoverBehavior);

	UClickDragInputBehavior* MouseBehavior = NewObject<UClickDragInputBehavior>();
	MouseBehavior->Initialize(this);
	MouseBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	AddInputBehavior(MouseBehavior);

}

FInputRayHit UMoKuEditorGizmoBase::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
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

void UMoKuEditorGizmoBase::OnClickPress(const FInputDeviceRay& PressPos)
{

}

void UMoKuEditorGizmoBase::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!bInteraction)
	{
		return;
	}

}

void UMoKuEditorGizmoBase::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if (!bInteraction)
	{
		return;
	}
	bInteraction = false;

}

void UMoKuEditorGizmoBase::CreateGizmoHandle(const FVector& InOrigin, const FQuat& InGizmoQuat)
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





void UMoKuEditRePlaceGizmo::SetupMaterials()
{

	UMaterial* MaterialBase = GEngine->ArrowMaterial;
	BaseDisplayMaterial = UMaterialInstanceDynamic::Create(MaterialBase, NULL);
	BaseDisplayMaterial->SetVectorParameterValue("GizmoColor", AssetMarkColor);

	CurrentDisplayMaterial = UMaterialInstanceDynamic::Create(MaterialBase, NULL);
	CurrentDisplayMaterial->SetVectorParameterValue("GizmoColor", FLinearColor(1.0f, 1.0f, 0.0f));

}

void UMoKuEditRePlaceGizmo::Setup()
{
	UMoKuEditorGizmoBase::Setup();
	SetupMaterials();
}

void UMoKuEditRePlaceGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{
	TWeakObjectPtr<UMoKuEditRePlaceGizmo> WeakGizmo = this;
	if (GetVisibility())
	{
		FLibraryAssetWindow::Get().CreateLibAssetWindow(WeakGizmo);
	}

}



void UMoKuEditRePlaceGizmo::SetSelectedObject(const FGizmoStoredInfoParams& InParams)
{
	OwnerGizmoParams = InParams;
}


UInteractiveGizmo* UMoKuEditRePlaceGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UMoKuEditRePlaceGizmo* NewGizmo = NewObject<UMoKuEditRePlaceGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);
	UGizmoViewContext* GizmoViewContext = SceneState.ToolManager->GetContextObjectStore()->FindContext<UGizmoViewContext>();
	check(GizmoViewContext && GizmoViewContext->IsValidLowLevel());
	NewGizmo->SetGizmoViewContext(GizmoViewContext);
	FActorSpawnParameters SpawnInfo;
	UWorld* World = NewGizmo->GetWorld();
	NewGizmo->GizmoActor = Cast<ASocketMarkbleGizmo>(World->SpawnActor<ASocketMarkbleGizmo>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo));
	return NewGizmo;

}


