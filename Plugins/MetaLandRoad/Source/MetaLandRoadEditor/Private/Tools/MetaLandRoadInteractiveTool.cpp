#include "MetaLandRoadInteractiveTool.h"
#include "MetaLandRoadInteractiveToolDraw.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "MoKuEditSplineActor.h"
#include "UnrealEdGlobals.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "SceneManagement.h"
#include "Components/SplineComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Editor.h"
#include "Algo/Copy.h"
#include "MoKuEditSplinesComponent.h"
#include "MetaLandRoadProfile.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "SocketMarkbleGizmo.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshSocket.h"
#include "MoKuEditorUtils.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditBaseActor.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "EditorModeManager.h"
#include "Tools/UEdMode.h"
#include "EditSplineRoadInfo.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "PrimitiveDrawingUtils.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadEditorModule.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "MetaLandRoadEditorMode.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadLaneEditPick.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Selection.h"
#include "SceneView.h"


#define LOCTEXT_NAMESPACE "UMetaLandRoadInteractiveTool"


UInteractiveTool* UMetaLandRoadInteractiveToolBuilder::BuildTool(const FToolBuilderState & SceneState) const
{
	UMetaLandRoadInteractiveTool* NewTool = NewObject<UMetaLandRoadInteractiveTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetGizmoManager(SceneState.GizmoManager);
	return NewTool;
}
void UMetaLandRoadInteractiveTool::SetWorld(UWorld* World)
{
	check(World);
	this->TargetWorld = World;
}
void UMetaLandRoadInteractiveTool::Setup()
{
	UInteractiveTool::Setup();
	if (!TargetWorld)return;
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		FMetaLandRoadEditorModule::Get().SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
	}
	UEdMode* ParentMode = GLevelEditorModeTools().GetActiveScriptableMode(UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId);
	if (UMetaLandRoadEditorMode* ParentEdMode = Cast<UMetaLandRoadEditorMode>(ParentMode))
	{
		CachedRoadEditorMode = ParentEdMode;
		SceneManagement = ParentEdMode->SceneManagement;
	}

	SetUpBehaviors();
	OutputProperties = NewObject<UInteractiveToolSettings>(this);
	AddToolPropertySource(OutputProperties);
}
void UMetaLandRoadInteractiveTool::SetUpBehaviors()
{
	UClickDragInputBehavior* MouseBehavior = NewObject<UClickDragInputBehavior>();
	UMouseHoverBehavior* HoverMouseBehavior = NewObject<UMouseHoverBehavior>();
	UMouseWheelInputBehavior* WheelMouseBehavior = NewObject<UMouseWheelInputBehavior>();
	USplineFinishedBehavior* SplineFinishedBehavior = NewObject<USplineFinishedBehavior>();
	MouseBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	MouseBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	MouseBehavior->Modifiers.RegisterModifier(AltModifierID, FInputDeviceState::IsAltKeyDown);
	MouseBehavior->Initialize(this);
	HoverMouseBehavior->Initialize(this);
	WheelMouseBehavior->Initialize(this);
	SplineFinishedBehavior->Initialize(this);
	AddInputBehavior(SplineFinishedBehavior);
	AddInputBehavior(MouseBehavior);
	AddInputBehavior(HoverMouseBehavior);
	AddInputBehavior(WheelMouseBehavior);

}
void UMetaLandRoadInteractiveTool::Shutdown(EToolShutdownType ShutdownType)
{
	ClearDrawGuidanceRails();
	OnFinishedSplineEdit();
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		FMetaLandRoadEditorModule::Get().SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
	}
	GEditor->OnLevelActorAdded().RemoveAll(this);
}
FInputRayHit UMetaLandRoadInteractiveTool::ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos)
{
	FInputRayHit ToReturn;
	ToReturn.bHit = true;
	if (!bSplineSnapActive && !bHasEndSnapGhost)
	{
		ToReturn.bHit = false;
	}
	return ToReturn;
}
void UMetaLandRoadInteractiveTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == CtrlModifierID)
	{
		bCtrlModifierOn = bIsOn;
	}
	if (ModifierID == ShiftModifierID)
	{
		bShiftModifierOn = bIsOn;
	}
	if (ModifierID == AltModifierID)
	{
		bAltModifierOn = bIsOn;
	}
}
namespace
{
FInputRayHit AcceptInputHitIfNeeded(const FInputRayHit& RayHit)
{
	if (RayHit.bHit)
	{
		return RayHit;
	}
	// Mouth arrows are overlays  - world trace often misses. Still capture so mix/guidance run.
	FInputRayHit Accept;
	Accept.bHit = true;
	Accept.HitDepth = 1.0f;
	return Accept;
}
}

FInputRayHit UMetaLandRoadInteractiveTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bPendingAnchor = RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor;
	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	const bool bCtrlDown = ModKeys.IsControlDown();
	const bool bShiftDown = ModKeys.IsShiftDown();

	FVector Temp;
	const FInputRayHit RayHit = FindRayHit(PressPos.WorldRay, Temp);
	const bool bLaneSession = IsDrawSessionActive();

	const bool bShouldCapture =
		bLaneSession
		|| bContinueDrawMouthArmed
		|| (bPendingAnchor && bShiftDown && !bCtrlDown)
		|| (bHasEndSnapGhost && bLaneSession)
		|| (bPendingAnchor && bCtrlDown && !bShiftDown)
		|| (bCtrlDown && !bShiftDown && RayHit.bHit);

	if (!bShouldCapture)
	{
		return FInputRayHit();
	}
	return AcceptInputHitIfNeeded(RayHit);
}
FInputRayHit UMetaLandRoadInteractiveTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bPendingAnchor = RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor;
	// Once a mouth is selected, hover must drive green rails even if the ray misses mesh.
	if (!IsDrawSessionActive() && !bHasEndSnapGhost && !bPendingAnchor)
	{
		return FInputRayHit();
	}
	FVector Temp;
	return AcceptInputHitIfNeeded(FindRayHit(PressPos.WorldRay, Temp));
}
// OnClickPress lives in MetaLandRoadInteractiveToolClick.cpp (Fig1 dispatcher).

void UMetaLandRoadInteractiveTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	Super::DrawHUD(Canvas, RenderAPI);
	if (Canvas == nullptr)
	{
		return;
	}

	// Engine may not expose RenderAPI->GetSceneView(); use active editor viewport.
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			GCurrentLevelEditingViewportClient->Viewport,
			GCurrentLevelEditingViewportClient->GetScene(),
			GCurrentLevelEditingViewportClient->EngineShowFlags));
		if (const FSceneView* View = GCurrentLevelEditingViewportClient->CalcSceneView(&ViewFamily))
		{
			DrawDrawGuidanceRailPointHUD(Canvas, View);
		}
	}

	if (GetEditState() || bContinueDrawSession)
	{
		const FString Judge = bHasDrawCrossing
			? TEXT("交叉：将形成路口（点下去吸到交点）")
			: TEXT("未交叉");
		const FLinearColor JudgeColor = bHasDrawCrossing
			? FLinearColor(0.2f, 1.0f, 0.45f, 1.0f)
			: FLinearColor(0.85f, 0.85f, 0.85f, 1.0f);
		UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
		if (Font)
		{
			FCanvasTextItem JudgeItem(
				FVector2D(24.0f, 22.0f),
				FText::FromString(Judge),
				Font,
				JudgeColor);
			JudgeItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(JudgeItem);
		}
	}

	if (!bIsDrawHud)
	{
		return;
	}

	FString HelpText = TEXT("LMB / Shift+LMB: Select lane anchor\n")
		TEXT("Hover: Mouth highlight\n")
		TEXT("Selected + move mouse: Lane-width guide rails\n")
		TEXT("Selected + Ctrl+LMB: Extend road from lane\n")
		TEXT("Extend + Shift+LMB on TARGET tip: toggle mix lane (use THIS  - Alt+LMB is camera)\n")
		TEXT("Extend + Alt+Shift+LMB: add-only mix lane\n")
		TEXT("Extend + Alt/Shift hover: green preview on target tip lane\n")
		TEXT("Selected mix: cyan mouth bars  - screen shows EndSnap mix lanes [..]\n")
		TEXT("Ctrl+LMB (no selection): Normal road draw\n")
		TEXT("Selected road + C: highlight nearer mouth; LMB starts continue-draw\n")
		TEXT("Ctrl+Z: Undo\n")
		TEXT("Shift+F1: Toggle Help");
	MoKuSplineEditorUtils::DrawHelpCanvasTileItem(Canvas, HelpText);
}





void UMetaLandRoadInteractiveTool::AddSplinePoint(const FVector& InHitPos, const FRotator& InHitRot, bool InteractiveGizmo,const FVector& InTangent,UInteractiveGizmo* InGizmoInfo)
{
	FScopedTransaction Transaction(LOCTEXT("MoKuEdit_Add Point", "Add MoKu Spline Control Point"));
	// Drawing clicks refresh road mesh  - must not Recook junctions until A finish.
	TGuardValue<bool> SuppressJunctionRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
	UMoKuEditSplinesComponent* SplineComponent = nullptr;
	FVector Tangent = InTangent;
	// Commit points stick to landscape  - never keep elevated road/junction mesh Z (walls).
	FVector SnapHitPos = InHitPos;
	ProjectPointOntoLandscapeSurface(
		TargetWorld ? TargetWorld : GetWorld(),
		SnapHitPos,
		Cast<AMoKuEditSplineActor>(ActiveActor));
	if (bContinueDrawSession)
	{
		if (AMoKuEditSplineActor* ContinueRoad = ContinueDrawRoad.Get())
		{
			ActiveActor = ContinueRoad;
		}
		if (ActiveActor && !GetEditState())
		{
			UpdateEditState(true);
		}
	}
	if (bContinueDrawSession && !ActiveActor)
	{
		return;
	}
	if (!ActiveActor|| !GetEditState())
	{
		ClearSelectionForDrawSession(true);
		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = this->TargetWorld->PersistentLevel;
		SpawnParams.bNoFail = true;
		SpawnParams.ObjectFlags |= RF_Transactional;
		ActiveActor = this->TargetWorld->SpawnActor<AMoKuEditSplineActor>(SnapHitPos, InHitRot, SpawnParams);
		if (ActiveActor)
		{
			ActiveActor->SetFolderPath(SCENE_ROAD_PATH);
			if (AWorldSceneManagement* Manager = AWorldLandscapeBlueprintBrush::EnsureWorldEditorManagerForRoadActor(ActiveActor))
			{
				SceneManagement = Manager;
				if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
				{
					RoadMode->SceneManagement = Manager;
				}
			}
		}
		AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
		SplineActor->RoadModeType = OutputProperties->RoadModeType;
		SplineComponent = SplineActor->GetSplineComponent();
		HideEditorSplineRibbon(SplineComponent);
		if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
		{
			const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
			if (Target.bHasPendingAnchor && Target.Spline.IsValid())
			{
				MetaLandRoadProfile::ApplyRoadProfileFromDrawAnchor(
					SplineComponent,
					Target.Spline.Get(),
					Target.PendingAnchor);
			}
			else
			{
				MetaLandRoadProfile::EnsureDefaultRoadProfile(SplineComponent);
			}
		}
		else
		{
			MetaLandRoadProfile::EnsureDefaultRoadProfile(SplineComponent);
		}
		UpdateEditState(true);
		PreviewComponent = NewObject<UMoKuEditSplinesComponent>(ActiveActor, NAME_None, RF_Transactional);
		PreviewComponent->AttachToComponent(SplineActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		// Hide editor spline visualizer ribbon  - guidance is PDI green dual-rail only.
		HideEditorSplineRibbon(PreviewComponent);
		PreviewComponent->RegisterComponent();
		if (!InteractiveGizmo)
		{
			SplineComponent->RemoveSplinePoint(1);
		}
	}
	else
	{
		if (ActiveActor)
		{
			AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			SplineComponent = SplineActor->GetSplineComponent();
			if (!InteractiveGizmo)
			{	
				if (StateOfEditor == EMoKuEditState::Forward)
				{
					SplineComponent->AddSplinePointLocation(SnapHitPos, 0);
				}
				if (StateOfEditor == EMoKuEditState::Defult)
				{
					SplineComponent->AddSplinePointLocation(SnapHitPos, SplineComponent->GetNumberOfSplinePoints());
				}
				HideEditorSplineRibbon(SplineComponent);
				if (!bContinueDrawSession)
				{
					SplineActor->SetSplineComponent(SplineComponent);
				}
				const int32 NumPts = SplineComponent->GetNumberOfSplinePoints();
				if (NumPts >= 3)
				{
					const int32 TipIndex = (StateOfEditor == EMoKuEditState::Forward) ? 0 : (NumPts - 1);
					const int32 JointIndex = (StateOfEditor == EMoKuEditState::Forward) ? 1 : (NumPts - 2);
					ApplyWidthAwareNewSpanTangents(SplineComponent, JointIndex, TipIndex);
					if (UMetaLandRoadSplineComponent* RoadSpline =
						Cast<UMetaLandRoadSplineComponent>(SplineComponent))
					{
						RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
					}
				}
			}
			else
			{
				FVector InterPos = SnapHitPos + Tangent.GetSafeNormal()*50;
				ProjectPointOntoLandscapeSurface(
					TargetWorld ? TargetWorld : GetWorld(), InterPos, SplineActor);
				SplineComponent->AddSplinePointLocation(InterPos, SplineComponent->GetNumberOfSplinePoints());
				SplineComponent->AddSplinePointLocation(SnapHitPos, SplineComponent->GetNumberOfSplinePoints());
				SplineActor->SetSplineComponent(SplineComponent);

			}
		}
		ActiveActor->IsInit = false;
		
	}

	if (bContinueDrawSession)
	{
		HideEditorSplineRibbon(SplineComponent);
		if (PreviewComponent)
		{
			HideEditorSplineRibbon(PreviewComponent);
		}
		ActiveActor->Modify();
		Modify();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	RefreshGizmoInfo();
	GEditor->SelectActor(ActiveActor, true, true, false, true);
	// Keep spline ribbon hidden after selection (mesh + green guidance only).
	HideEditorSplineRibbon(SplineComponent);
	if (PreviewComponent)
	{
		HideEditorSplineRibbon(PreviewComponent);
	}
	ActiveActor->Modify();
	Modify();

	if (SplineComponent)
	{
		FProperty* SplineCurvesProperty = FindFProperty<FProperty>(
			UMoKuEditSplinesComponent::StaticClass(),
			GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));
		if (SplineCurvesProperty)
		{
			FPropertyChangedEvent PropertyChangedEvent(SplineCurvesProperty, EPropertyChangeType::ValueSet);
			SplineComponent->PostEditChangeProperty(PropertyChangedEvent);
			this->PostEditChangeProperty(PropertyChangedEvent);
		}
		HideEditorSplineRibbon(SplineComponent);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}
void UMetaLandRoadInteractiveTool::OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos)
{
	TryAdjustEndConnectLaneCount(+1, CurrentPos);
}
void UMetaLandRoadInteractiveTool::OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos)
{
	TryAdjustEndConnectLaneCount(-1, CurrentPos);
}
void UMetaLandRoadInteractiveTool::GetSplitIntersectionData(AActor* InSplitActor,const FVector& InWorldPos)
{
	if (InSplitActor)
	{
		AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(InSplitActor);
		if (SplineActor)
		{
			const UMoKuEditSplinesComponent* SplineComp = SplineActor->GetSplineComponent();

			check(SplineComp != nullptr);
			const float SplineLength = SplineComp->GetSplineLength();
			float OutputKey = SplineComp->FindInputKeyClosestToWorldLocation(InWorldPos);
			float SampleDist = SplineComp->GetDistanceAlongSplineAtSplineInputKey(OutputKey);
			IntersectData.StartValue = SampleDist- IntersectData.EntryValue;
			IntersectData. EndValue = SampleDist + IntersectData.EntryValue;	
			IntersectData.StartValue = FMath::Clamp(IntersectData.StartValue,0, SplineLength);
			IntersectData.EndValue = FMath::Clamp(IntersectData.EndValue, 0, SplineLength);

			DrawOutSplinePos = SplineComp->GetLocationAtDistanceAlongSpline(SampleDist, ESplineCoordinateSpace::World);
			const float SnapDistCm = GetConnectionSnapDistanceCm();
			const float DistToSplineCm = FVector::Dist2D(DrawOutSplinePos, InWorldPos);
			if (DistToSplineCm < SnapDistCm)
			{
				bSplineSnapActive = true;
				IntersectData.TValue = SplineComp->GetDistanceAlongSplineAtLocation(DrawOutSplinePos, ESplineCoordinateSpace::World);
			}
			else
			{
				bSplineSnapActive = false;
				IntersectData.TValue = 0;
				IntersectData.HitTransform.SetLocation(InWorldPos);
			}
			GEditor->RedrawLevelEditingViewports(true);
		}
		else
		{
			bSplineSnapActive = false;
			IntersectData.TValue = 0;
			IntersectData.HitTransform.SetLocation(InWorldPos);
		}
	}
}
void UMetaLandRoadInteractiveTool::SetToolSession(EMetaLandRoadToolSession NewSession)
{
	ToolSession = NewSession;
}

void UMetaLandRoadInteractiveTool::UpdateEditState(bool InEditing)
{
	if (InEditing)
	{
		if (ToolSession == EMetaLandRoadToolSession::Idle)
		{
			ToolSession = EMetaLandRoadToolSession::DrawPath;
		}
	}
	else
	{
		ToolSession = EMetaLandRoadToolSession::Idle;
	}
}
void UMetaLandRoadInteractiveTool::OnClickDrag(const FInputDeviceRay& DragPos)
{	
}
void UMetaLandRoadInteractiveTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{

	GEditor->EndTransaction();

}
void UMetaLandRoadInteractiveTool::OnTerminateDragSequence()
{

}
void UMetaLandRoadInteractiveTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	(void)PropertySet;
	(void)Property;
}
FInputRayHit UMetaLandRoadInteractiveTool::FindRayHit(const FRay& WorldRay, FVector& HitPos)
{
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, WorldRay.Origin, WorldRay.PointAt(HALF_WORLD_MAX), QueryParams);
	if (bHitWorld)
	{
		HitPos = Result.ImpactPoint;
		return FInputRayHit(Result.Distance);
	}
	return FInputRayHit();
}
void UMetaLandRoadInteractiveTool::ClearDrawGuidanceRails()
{
	GuidanceLeftPolylines.Reset();
	GuidanceRightPolylines.Reset();
	GuidanceCenterline.Reset();
	bDrawGuidanceInvalid = false;
	bHasDrawCrossing = false;
	DrawCrossingWorld = FVector::ZeroVector;
	DrawCrossingFrom = FVector::ZeroVector;
	LastMixHoverMousePos = FIntPoint(MIN_int32, MIN_int32);
	bHasEndSnapGhost = false;
	bHasLaneDrawEndSnapAnchor = false;
	bEndSnapBundleUserLocked = false;
	bHasEndSnapMixHover = false;
	EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();
	LaneDrawEndSnapAnchor = FMetaLandRoadDrawAnchor();
}

void UMetaLandRoadInteractiveTool::UpdateDrawCrossingJudge()
{
	bHasDrawCrossing = false;
	DrawCrossingWorld = FVector::ZeroVector;
	DrawCrossingFrom = FVector::ZeroVector;

	if (!GetEditState() && !bContinueDrawSession)
	{
		return;
	}

	AMoKuEditSplineActor* Drawing = ContinueDrawRoad.Get();
	if (!Drawing)
	{
		Drawing = Cast<AMoKuEditSplineActor>(ActiveActor);
	}
	UMoKuEditSplinesComponent* Spline = Drawing ? Drawing->GetSplineComponent() : nullptr;
	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (!Drawing || !Spline || !World || Spline->GetNumberOfSplinePoints() < 1)
	{
		return;
	}

	FVector From = FVector::ZeroVector;
	if (bContinueDrawSession)
	{
		From = ContinueDrawMouthCenter;
		if (From.IsNearlyZero())
		{
			if (ContinueDrawMouthS >= 0.0)
			{
				From = Spline->GetLocationAtDistanceAlongSpline(
					ContinueDrawMouthS, ESplineCoordinateSpace::World);
			}
			else
			{
				const bool bInsertAtStart = (ContinueDrawState == EMoKuEditState::Forward);
				const int32 Tip = bInsertAtStart ? 0 : (Spline->GetNumberOfSplinePoints() - 1);
				From = Spline->GetLocationAtSplinePoint(Tip, ESplineCoordinateSpace::World);
			}
		}
	}
	else
	{
		const bool bFwd = (StateOfEditor == EMoKuEditState::Forward);
		const int32 Tip = bFwd ? 0 : (Spline->GetNumberOfSplinePoints() - 1);
		From = Spline->GetLocationAtSplinePoint(Tip, ESplineCoordinateSpace::World);
	}

	FVector Cross = FVector::ZeroVector;
	if (!TrySnapDrawToRoadCrossing(
			Drawing,
			From,
			ResultHitPos,
			World,
			GetConnectionSnapDistanceCm(),
			Cross))
	{
		return;
	}

	bHasDrawCrossing = true;
	DrawCrossingFrom = From;
	DrawCrossingWorld = Cross;
	OutIntersectionPoint = Cross;
}

bool UMetaLandRoadInteractiveTool::TryApplyJudgedDrawCrossing(FVector& InOutPlace) const
{
	if (!bHasDrawCrossing)
	{
		return false;
	}
	InOutPlace = DrawCrossingWorld;
	return true;
}

void UMetaLandRoadInteractiveTool::ClearSelectionForDrawSession(bool bNoteSelectionChange)
{
	FMetaLandRoadEditorModule::Get().AbortSplineVisualizerTracking();
	if (GEditor)
	{
		GEditor->SelectNone(true, bNoteSelectionChange);
	}
}

void UMetaLandRoadInteractiveTool::EndLaneExtendVisualSession(bool bClearActiveActor)
{
	ClearDrawGuidanceRails();
	UpdateEditState(false);
	bSplineSnapActive = false;
	bInsertJunction = false;
	bHasGuidancePlaneZ = false;
	SetToolSession(EMetaLandRoadToolSession::Idle);
	bRoadSpawnedFromLaneAnchorDraw = false;
	bLaneAnchorSkipHubJunctionCook = false;
	bHasLaneDrawStartAnchor = false;
	LaneDrawStartAnchor = FMetaLandRoadDrawAnchor();
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	bHasLaneSnapArriveTangent = false;
	bHasEndSnapMixHover = false;
	EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();
	GuidanceLastCursor = FVector::ZeroVector;
	LastGuidanceHoverSamplePos = FVector::ZeroVector;
	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(ActiveActor))
	{
		if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
		{
			Spline->bDeferRoadMeshRefresh = false;
		}
	}
	bContinueDrawSession = false;
	ClearContinueDrawMouthArm();

	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		RoadMode->ClearEditTarget();
	}

	if (bClearActiveActor)
	{
		ActiveActor = nullptr;
	}
	else if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActiveActor))
	{
		if (UMoKuEditSplinesComponent* Spline = RoadActor->GetSplineComponent())
		{
			Spline->bIsRenderPreviewMesh = false;
			Spline->bIsAddPreviewMeshPoint = false;
		}
	}

	if (PreviewComponent && PreviewComponent->IsRegistered())
	{
		PreviewComponent->DestroyComponent();
	}
	PreviewComponent = nullptr;

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

bool UMetaLandRoadInteractiveTool::ShouldSuppressLanePickOverlay() const
{
	return GetEditState() && bCtrlModifierOn && bSplineSnapActive;
}

bool UMetaLandRoadInteractiveTool::ShouldSuppressLanePickEditHover() const
{
	return GetEditState() && bCtrlModifierOn && bSplineSnapActive;
}

bool UMetaLandRoadInteractiveTool::ShouldSuppressEditTargetMouthBar() const
{
	return IsLaneAnchorDrawSession() || (GetEditState() && bSplineSnapActive);
}

bool UMetaLandRoadInteractiveTool::TryGetLaneDrawEndSnapOverlay(FMetaLandRoadDrawAnchor& OutAnchor) const
{
	if (bEndSnapBundleUserLocked && bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid())
	{
		OutAnchor = LaneDrawEndSnapAnchor;
		return true;
	}
	return false;
}

bool UMetaLandRoadInteractiveTool::TryGetEndSnapMixHoverOverlay(FMetaLandRoadDrawAnchor& OutAnchor) const
{
	if (bHasEndSnapMixHover && EndSnapMixHoverAnchor.IsValid())
	{
		OutAnchor = EndSnapMixHoverAnchor;
		return true;
	}
	if (!bEndSnapBundleUserLocked && bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid())
	{
		OutAnchor = LaneDrawEndSnapAnchor;
		return true;
	}
	return false;
}

void UMetaLandRoadInteractiveTool::ApplyIdleEndSnapTarget(const FMetaLandRoadDrawAnchor& Anchor)
{
	if (!Anchor.IsValid() || !Anchor.SourceSpline.IsValid())
	{
		return;
	}

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	if (RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor)
	{
		if (AActor* SourceOwner = RoadMode->GetEditTarget().RoadActor.Get())
		{
			if (Anchor.SourceSpline->GetOwner() == SourceOwner)
			{
				return;
			}
		}
	}

	LaneDrawEndSnapAnchor = Anchor;
	bHasLaneDrawEndSnapAnchor = true;
	bEndSnapBundleUserLocked = true;

	FVector TipLeft, TipRight, TipCenter, Outward;
	if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		Anchor.SourceSpline.Get(), Anchor, TipLeft, TipRight, TipCenter, Outward))
	{
		EndSnapGhostLeft = TipLeft;
		EndSnapGhostRight = TipRight;
		ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
		ApplyGuidanceSurfaceZ(EndSnapGhostRight);
		bHasEndSnapGhost = true;
		DrawOutSplinePos = TipCenter;
		HitActor = Anchor.SourceSpline->GetOwner();
		LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
		bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
	}

	if (OutputProperties)
	{
		OutputProperties->bEndConnectMatchSourceLanes = false;
		OutputProperties->EndConnectTargetLaneCount = FMath::Clamp(
			CountDrawAnchorBundleLanes(LaneDrawEndSnapAnchor), 1, 8);
	}

	UpdateAnchorPreviewGuidanceRails();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991132,
			4.0f,
			FColor::Green,
			TEXT("[MetaLandRoad] Two mouths ready  - press A to generate the connection"));
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void UMetaLandRoadInteractiveTool::ClearLaneDrawEndSnap(bool bClearUserLock)
{
	if (!bClearUserLock && bEndSnapBundleUserLocked)
	{
		return;
	}

	bHasEndSnapGhost = false;
	bHasLaneDrawEndSnapAnchor = false;
	bHasEndSnapMixHover = false;
	bHasLaneSnapArriveTangent = false;
	bEndSnapBundleUserLocked = false;
	LaneDrawEndSnapAnchor = FMetaLandRoadDrawAnchor();
	EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();
	LaneSnapArriveTangent = FVector::ForwardVector;
	EndSnapGhostLeft = FVector::ZeroVector;
	EndSnapGhostRight = FVector::ZeroVector;
}

bool UMetaLandRoadInteractiveTool::HasLaneDrawEndSnap() const
{
	return bEndSnapBundleUserLocked || bHasEndSnapGhost || bHasLaneDrawEndSnapAnchor;
}

void UMetaLandRoadInteractiveTool::ResetDrawSessionState()
{
	ClearDrawGuidanceRails();
	UpdateEditState(false);
	StateOfEditor = EMoKuEditState::Defult;
	bSplineSnapActive = false;
	bInsertJunction = false;
	bHasGuidancePlaneZ = false;
	SetToolSession(EMetaLandRoadToolSession::Idle);
	bRoadSpawnedFromLaneAnchorDraw = false;
	bLaneAnchorSkipHubJunctionCook = false;
	bHasLaneDrawStartAnchor = false;
	LaneDrawStartAnchor = FMetaLandRoadDrawAnchor();
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	bHasLaneSnapArriveTangent = false;
	LaneSnapArriveTangent = FVector::ForwardVector;
	HitActor = nullptr;
	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(ActiveActor))
	{
		if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
		{
			Spline->bDeferRoadMeshRefresh = false;
		}
	}
	ActiveActor = nullptr;
	GuidanceLastCursor = FVector::ZeroVector;
	LastGuidanceHoverSamplePos = FVector::ZeroVector;
	IntersectData = IntersectBaseData();

	if (PreviewComponent && PreviewComponent->IsRegistered())
	{
		PreviewComponent->DestroyComponent();
	}
	PreviewComponent = nullptr;
	bContinueDrawSession = false;
	ClearContinueDrawMouthArm();

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void UMetaLandRoadInteractiveTool::ClearContinueDrawMouthArm()
{
	bContinueDrawMouthArmed = false;
	if (!bContinueDrawSession)
	{
		ContinueDrawRoad.Reset();
		ContinueDrawOriginalNumPoints = 0;
		ContinueDrawState = EMoKuEditState::Defult;
		ContinueDrawMouthLeft = FVector::ZeroVector;
		ContinueDrawMouthRight = FVector::ZeroVector;
		ContinueDrawMouthCenter = FVector::ZeroVector;
		ContinueDrawMouthS = -1.0;
		ContinueDrawMouthSpanIndex = INDEX_NONE;
		bContinueDrawMouthIsSpanEnd = true;
	}
}

static AMoKuEditSplineActor* FindSingleSelectedContinueDrawRoad()
{
	if (!GEditor)
	{
		return nullptr;
	}

	AMoKuEditSplineActor* Found = nullptr;
	int32 Count = 0;
	if (USelection* Selection = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				Found = Road;
				++Count;
				if (Count > 1)
				{
					return nullptr;
				}
			}
		}
	}
	return Count == 1 ? Found : nullptr;
}

bool UMetaLandRoadInteractiveTool::HasLiveContinueDrawTarget() const
{
	if (!bContinueDrawSession)
	{
		return false;
	}
	return ContinueDrawRoad.Get() != nullptr
		|| Cast<AMoKuEditSplineActor>(ActiveActor) != nullptr;
}

void UMetaLandRoadInteractiveTool::AbandonStaleContinueDraw()
{
	if (HasLiveContinueDrawTarget())
	{
		return;
	}
	bContinueDrawSession = false;
	ClearContinueDrawMouthArm();
}

struct FContinueDrawMouthCache
{
	TWeakObjectPtr<AMoKuEditSplineActor> Road;
	int32 NumPts = INDEX_NONE;
	FVector BeginL = FVector::ZeroVector;
	FVector BeginR = FVector::ZeroVector;
	FVector BeginC = FVector::ZeroVector;
	FVector EndL = FVector::ZeroVector;
	FVector EndR = FVector::ZeroVector;
	FVector EndC = FVector::ZeroVector;
	double BeginS = 0.0;
	double EndS = 0.0;
};

static FContinueDrawMouthCache GContinueDrawMouthCache;
static FMetaLandRoadDrawAnchor GContinueDrawOverlay;
static bool GContinueDrawOverlayValid = false;
static TWeakObjectPtr<AMoKuEditSplineActor> GContinueDrawOverlayRoad;
static EMoKuEditState GContinueDrawOverlayState = EMoKuEditState::Defult;

static bool ResolveContinueDrawMouthPair(
	AMoKuEditSplineActor* Road,
	FVector& OutBeginLeft,
	FVector& OutBeginRight,
	FVector& OutBeginCenter,
	FVector& OutEndLeft,
	FVector& OutEndRight,
	FVector& OutEndCenter)
{
	UMetaLandRoadSplineComponent* Spline = Road ? Road->GetSplineComponent() : nullptr;
	if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	const int32 LiveNumPts = Spline->GetNumberOfSplinePoints();
	if (GContinueDrawMouthCache.Road.Get() == Road
		&& GContinueDrawMouthCache.NumPts == LiveNumPts)
	{
		OutBeginLeft = GContinueDrawMouthCache.BeginL;
		OutBeginRight = GContinueDrawMouthCache.BeginR;
		OutBeginCenter = GContinueDrawMouthCache.BeginC;
		OutEndLeft = GContinueDrawMouthCache.EndL;
		OutEndRight = GContinueDrawMouthCache.EndR;
		OutEndCenter = GContinueDrawMouthCache.EndC;
		return true;
	}

	double BeginS = 0.0;
	double EndS = Spline->GetSplineLength();
	TArray<TPair<double, double>> Spans;
	Road->GetJunctionMeshGenerationSpans(Spans);
	if (Spans.Num() > 0)
	{
		BeginS = Spans[0].Key;
		EndS = Spans.Last().Value;
	}
	const bool bBegin = Spline->GetRoadEdgePointsAtS(
		BeginS, OutBeginLeft, OutBeginRight, OutBeginCenter, ESplineCoordinateSpace::World);
	const bool bEnd = Spline->GetRoadEdgePointsAtS(
		EndS, OutEndLeft, OutEndRight, OutEndCenter, ESplineCoordinateSpace::World);
	if (!bBegin || !bEnd)
	{
		OutBeginCenter = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		OutEndCenter = Spline->GetLocationAtSplinePoint(LiveNumPts - 1, ESplineCoordinateSpace::World);
		const FVector BeginRight = Spline->GetRightVectorAtSplinePoint(0, ESplineCoordinateSpace::World).GetSafeNormal2D();
		const FVector EndRight = Spline->GetRightVectorAtSplinePoint(LiveNumPts - 1, ESplineCoordinateSpace::World).GetSafeNormal2D();
		const double HalfW = FMath::Max(Spline->GetRoadHalfWidthAtS(BeginS), 200.0);
		OutBeginLeft = OutBeginCenter - BeginRight * HalfW;
		OutBeginRight = OutBeginCenter + BeginRight * HalfW;
		OutEndLeft = OutEndCenter - EndRight * HalfW;
		OutEndRight = OutEndCenter + EndRight * HalfW;
	}

	GContinueDrawMouthCache.Road = Road;
	GContinueDrawMouthCache.NumPts = LiveNumPts;
	GContinueDrawMouthCache.BeginL = OutBeginLeft;
	GContinueDrawMouthCache.BeginR = OutBeginRight;
	GContinueDrawMouthCache.BeginC = OutBeginCenter;
	GContinueDrawMouthCache.EndL = OutEndLeft;
	GContinueDrawMouthCache.EndR = OutEndRight;
	GContinueDrawMouthCache.EndC = OutEndCenter;
	GContinueDrawMouthCache.BeginS = BeginS;
	GContinueDrawMouthCache.EndS = EndS;
	return true;
}

static double DistPointToMouthBar2D(
	const FVector& Pos,
	const FVector& Left,
	const FVector& Right,
	const FVector& Center)
{
	const FVector MouthDir = Right - Left;
	const double MouthLen = MouthDir.Size2D();
	if (MouthLen <= 1.0)
	{
		return FVector::Dist2D(Pos, Center);
	}

	const FVector2D A(Left.X, Left.Y);
	const FVector2D B(Right.X, Right.Y);
	const FVector2D P(Pos.X, Pos.Y);
	const FVector2D AB = B - A;
	const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared(), 0.0f, 1.0f);
	return FVector2D::Distance(P, A + AB * T);
}

static double ContinueDrawMouthAcceptCm(double MouthLen)
{
	return FMath::Max(MouthLen * 0.28, 140.0);
}

static bool IsContinueDrawHotkeyHeld(const FEditorViewportClient* ViewportClient)
{
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return false;
	}
	if (ViewportClient->IsAltPressed()
		|| ViewportClient->IsCtrlPressed()
		|| ViewportClient->IsShiftPressed())
	{
		return false;
	}
	return ViewportClient->Viewport->KeyState(EKeys::C);
}

static bool GetContinueDrawCursorWorld(FVector& OutCursor, float PlaneZ = 0.0f)
{
	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	const FVector Origin = Cursor.GetOrigin();
	const FVector Direction = Cursor.GetDirection();
	if (Direction.IsNearlyZero())
	{
		return false;
	}
	OutCursor = FMath::RayPlaneIntersection(
		Origin, Direction, FPlane(FVector(0.0, 0.0, PlaneZ), FVector::UpVector));
	return FMath::IsFinite(OutCursor.X);
}

static bool BuildFullMouthAnchorAtTip(
	UMetaLandRoadSplineComponent* Spline,
	bool bNearEnd,
	FMetaLandRoadDrawAnchor& OutAnchor)
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	if (!Spline)
	{
		return false;
	}

	double MouthS = bNearEnd ? Spline->GetSplineLength() : 0.0;
	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
	{
		MouthS = ResolveEditableTipS(Road, Spline, /*bForward*/ !bNearEnd);
	}
	const EMetaLandRoadMouthSide MouthSide =
		bNearEnd ? EMetaLandRoadMouthSide::End : EMetaLandRoadMouthSide::Start;
	int32 SectionIndex = Spline->GetRoadLayout().FindSectionAtSOffset(MouthS);
	if (SectionIndex == INDEX_NONE || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
	{
		Spline->GetRoadLayoutMutable().UpdateLayout(Spline);
		SectionIndex = Spline->GetRoadLayout().FindSectionAtSOffset(MouthS);
	}
	if (SectionIndex == INDEX_NONE || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	int32 SeedLane = METALANDROAD_LANE_INDEX_NONE;
	const int32 RightSectionIndex = Spline->GetRoadLayout().FindSideSection(
		SectionIndex, EMetaLandRoadLaneSectionSide::Right);
	const int32 LeftSectionIndex = Spline->GetRoadLayout().FindSideSection(
		SectionIndex, EMetaLandRoadLaneSectionSide::Left);
	if (RightSectionIndex != INDEX_NONE && Spline->GetLaneSection(RightSectionIndex).Right.Num() > 0)
	{
		SeedLane = 1;
	}
	else if (LeftSectionIndex != INDEX_NONE && Spline->GetLaneSection(LeftSectionIndex).Left.Num() > 0)
	{
		SeedLane = -1;
	}
	else
	{
		return false;
	}

	if (!Spline->BuildDrawAnchorForLaneMouth(SectionIndex, SeedLane, MouthS, MouthSide, OutAnchor))
	{
		return false;
	}

	MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(Spline, SectionIndex, OutAnchor);
	return OutAnchor.IsValid();
}

bool UMetaLandRoadInteractiveTool::TryGetContinueDrawMouthOverlay(FMetaLandRoadDrawAnchor& OutAnchor) const
{
	if (!bContinueDrawMouthArmed
		|| !IsContinueDrawHotkeyHeld(GCurrentLevelEditingViewportClient))
	{
		return false;
	}

	if (GContinueDrawOverlayValid
		&& GContinueDrawOverlayRoad.Get() == ContinueDrawRoad.Get()
		&& GContinueDrawOverlayState == ContinueDrawState)
	{
		OutAnchor = GContinueDrawOverlay;
		return OutAnchor.IsValid();
	}

	AMoKuEditSplineActor* Road = ContinueDrawRoad.Get();
	UMetaLandRoadSplineComponent* Spline = Road ? Road->GetSplineComponent() : nullptr;
	const bool bNearEnd = ContinueDrawState != EMoKuEditState::Forward;
	if (!BuildFullMouthAnchorAtTip(Spline, bNearEnd, OutAnchor))
	{
		return false;
	}
	if (ContinueDrawMouthS >= 0.0)
	{
		OutAnchor.AnchorS = ContinueDrawMouthS;
	}
	GContinueDrawOverlay = OutAnchor;
	GContinueDrawOverlayValid = true;
	GContinueDrawOverlayRoad = Road;
	GContinueDrawOverlayState = ContinueDrawState;
	return true;
}

bool UMetaLandRoadInteractiveTool::TryArmContinueDrawMouth()
{
	if (IsLaneAnchorDrawSession() || bContinueDrawSession)
	{
		return false;
	}

	AMoKuEditSplineActor* Road = FindSingleSelectedContinueDrawRoad();
	if (!Road)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent();
	if (!Spline)
	{
		return false;
	}

	TArray<TPair<double, EMetaLandRoadMouthSide>> Stations;
	MetaLandRoad::CollectVisibleMouthStations(Spline, Stations);
	if (Stations.Num() < 1)
	{
		return false;
	}

	TArray<TPair<double, double>> Spans;
	Road->GetJunctionMeshGenerationSpans(Spans);
	const float PlaneZ = !ResultHitPos.IsNearlyZero()
		? static_cast<float>(ResultHitPos.Z)
		: static_cast<float>(Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Z);
	FVector Cursor = FVector::ZeroVector;
	if (!GetContinueDrawCursorWorld(Cursor, PlaneZ))
	{
		if (ResultHitPos.IsNearlyZero())
		{
			return false;
		}
		Cursor = ResultHitPos;
	}
	ResultHitPos = Cursor;

	int32 BestStation = INDEX_NONE;
	double BestDist = TNumericLimits<double>::Max();
	FVector BestL = FVector::ZeroVector;
	FVector BestR = FVector::ZeroVector;
	FVector BestC = FVector::ZeroVector;
	for (int32 i = 0; i < Stations.Num(); ++i)
	{
		FVector L, R, C;
		if (!Spline->GetRoadEdgePointsAtS(Stations[i].Key, L, R, C, ESplineCoordinateSpace::World))
		{
			continue;
		}
		const double Dist = DistPointToMouthBar2D(Cursor, L, R, C);
		const double Accept = ContinueDrawMouthAcceptCm(FVector::Dist2D(L, R));
		if (Dist <= Accept && Dist < BestDist)
		{
			BestDist = Dist;
			BestStation = i;
			BestL = L;
			BestR = R;
			BestC = C;
		}
	}
	if (BestStation == INDEX_NONE)
	{
		return false;
	}

	const double MouthS = Stations[BestStation].Key;
	const bool bSpanEnd = Stations[BestStation].Value == EMetaLandRoadMouthSide::End;
	int32 SpanIndex = INDEX_NONE;
	for (int32 i = 0; i < Spans.Num(); ++i)
	{
		if (FMath::IsNearlyEqual(MouthS, bSpanEnd ? Spans[i].Value : Spans[i].Key, 1.0))
		{
			SpanIndex = i;
			break;
		}
	}
	const bool bGlobalStart = !bSpanEnd
		&& (MouthS <= 80.0 || (Spans.Num() > 0 && FMath::IsNearlyEqual(MouthS, Spans[0].Key, 1.0)));
	const FVector NewMouthCenter = BestC;
	const bool bNewArm = !bContinueDrawMouthArmed
		|| ContinueDrawRoad.Get() != Road
		|| !ContinueDrawMouthCenter.Equals(NewMouthCenter, 1.0);
	ContinueDrawRoad = Road;
	ContinueDrawState = bGlobalStart ? EMoKuEditState::Forward : EMoKuEditState::Defult;
	ContinueDrawMouthLeft = BestL;
	ContinueDrawMouthRight = BestR;
	ContinueDrawMouthCenter = NewMouthCenter;
	ContinueDrawMouthS = MouthS;
	ContinueDrawMouthSpanIndex = SpanIndex;
	bContinueDrawMouthIsSpanEnd = bSpanEnd;
	bContinueDrawMouthArmed = true;
	if (bNewArm && !bContinueDrawSession)
	{
		ClearDrawGuidanceRails();
	}
	if (!GContinueDrawOverlayValid
		|| GContinueDrawOverlayRoad.Get() != Road
		|| GContinueDrawOverlayState != ContinueDrawState)
	{
		GContinueDrawOverlayValid = BuildFullMouthAnchorAtTip(Spline, bSpanEnd, GContinueDrawOverlay);
		if (GContinueDrawOverlayValid)
		{
			GContinueDrawOverlay.AnchorS = MouthS;
		}
		GContinueDrawOverlayRoad = Road;
		GContinueDrawOverlayState = ContinueDrawState;
	}
	return true;
}

void UMetaLandRoadInteractiveTool::RefreshContinueDrawMouthArm()
{
	if (IsLaneAnchorDrawSession() || bContinueDrawSession)
	{
		return;
	}

	const bool bWasArmed = bContinueDrawMouthArmed;
	const FVector PrevCenter = ContinueDrawMouthCenter;
	const bool bCHeld = IsContinueDrawHotkeyHeld(GCurrentLevelEditingViewportClient);
	if (!bCHeld || !TryArmContinueDrawMouth())
	{
		const bool bHadArm = bContinueDrawMouthArmed;
		ClearContinueDrawMouthArm();
		if (bHadArm && !bContinueDrawSession)
		{
			ClearDrawGuidanceRails();
		}
	}
	if (bWasArmed != bContinueDrawMouthArmed
		|| (bContinueDrawMouthArmed && !PrevCenter.Equals(ContinueDrawMouthCenter, 1.0)))
	{
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
	}
}

bool UMetaLandRoadInteractiveTool::TryCommitContinueDrawMouth(
	const FInputDeviceRay& PressPos,
	bool bRequireClickOnBar)
{
	if (!bContinueDrawMouthArmed)
	{
		return false;
	}

	AMoKuEditSplineActor* Road = ContinueDrawRoad.Get();
	if (!Road)
	{
		ClearContinueDrawMouthArm();
		return false;
	}

	FVector ClickPos = ResultHitPos;
	if (PressPos.WorldRay.Direction.SizeSquared() > KINDA_SMALL_NUMBER
		&& FindRayHit(PressPos.WorldRay, ClickPos).bHit)
	{
		ResultHitPos = ClickPos;
	}
	else if (ClickPos.IsNearlyZero())
	{
		GetContinueDrawCursorWorld(ClickPos);
	}

	if (bRequireClickOnBar)
	{
		const double DistToBar = DistPointToMouthBar2D(
			ClickPos, ContinueDrawMouthLeft, ContinueDrawMouthRight, ContinueDrawMouthCenter);
		const double Accept = ContinueDrawMouthAcceptCm(
			FVector::Dist2D(ContinueDrawMouthLeft, ContinueDrawMouthRight));
		if (DistToBar > Accept)
		{
			return false;
		}
	}

	FScopedTransaction Transaction(LOCTEXT("ContinueDrawRoad", "Continue Draw Road"));
	ActiveActor = Road;
	StateOfEditor = ContinueDrawState;
	UpdateEditState(true);
	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		RoadMode->ClearEditTarget();
	}
	bHasLaneDrawStartAnchor = false;
	LaneDrawStartAnchor = FMetaLandRoadDrawAnchor();
	if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
	{
		ContinueDrawOriginalNumPoints = Spline->GetNumberOfSplinePoints();
		HideEditorSplineRibbon(Spline);
		Spline->bDeferRoadMeshRefresh = true;
	}
	GuidancePlaneZ = static_cast<float>(ContinueDrawMouthCenter.Z);
	bHasGuidancePlaneZ = true;
	GuidanceLastCursor = ContinueDrawMouthCenter;
	LastGuidanceHoverSamplePos = FVector::ZeroVector;
	LastGuidanceHoverRedrawSeconds = 0.0;
	FVector CommitCursor = ContinueDrawMouthCenter;
	if (GetContinueDrawCursorWorld(CommitCursor, GuidancePlaneZ))
	{
		ResultHitPos = CommitCursor;
	}
	bContinueDrawSession = true;
	ClearContinueDrawMouthArm();
	UpdateDrawGuidanceRails();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
	return true;
}

bool UMetaLandRoadInteractiveTool::TryHandleContinueDrawClick(const FInputDeviceRay& PressPos)
{
	if (IsLaneAnchorDrawSession())
	{
		return false;
	}
	if (IsDrawSessionActive() && !bContinueDrawSession)
	{
		return false;
	}
	// Only commit a C-arm. Do not auto-arm on click — that steals Shift lane-extend picks.
	if (!bContinueDrawMouthArmed)
	{
		return false;
	}
	return TryCommitContinueDrawMouth(PressPos);
}

void UMetaLandRoadInteractiveTool::UpdateEndSnapMixHoverPreview(const FVector& CursorWorld)
{
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		FIntPoint MousePos;
		GCurrentLevelEditingViewportClient->Viewport->GetMousePos(MousePos);
		if (LastMixHoverMousePos.X != MIN_int32
			&& (MousePos - LastMixHoverMousePos).SizeSquared() < 36)
		{
			return;
		}
		LastMixHoverMousePos = MousePos;
	}

	bHasEndSnapMixHover = false;
	EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();

	UMetaLandRoadEditorMode* HoverRoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bHasSourceMouth =
		AllowsEndSnapConnectionHover()
		|| bContinueDrawMouthArmed
		|| (HoverRoadMode && HoverRoadMode->GetEditTarget().bHasPendingAnchor);
	if (!bHasSourceMouth)
	{
		return;
	}

	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	if (ModKeys.IsControlDown())
	{
		return;
	}

	AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!ActiveRoad && HoverRoadMode)
	{
		ActiveRoad = HoverRoadMode->GetEditTarget().RoadActor.Get();
	}
	if (!ActiveRoad)
	{
		ActiveRoad = ContinueDrawRoad.Get();
	}
	if (!ActiveRoad || !TargetWorld)
	{
		return;
	}

	auto ApplyHoverGhost = [this](const FMetaLandRoadDrawAnchor& Anchor)
	{
		UMetaLandRoadSplineComponent* HoverSpline = Anchor.SourceSpline.Get();
		if (!HoverSpline)
		{
			return;
		}
		FVector TipLeft, TipRight, TipCenter, Outward;
		// Yellow overlay draws GetRoadEdgePointsAtS(AnchorS). Ghost must use the same bar.
		const bool bFullRoad = HoverSpline->GetRoadEdgePointsAtS(
			Anchor.AnchorS, TipLeft, TipRight, TipCenter, ESplineCoordinateSpace::World);
		if (!bFullRoad
			&& !MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				HoverSpline, Anchor, TipLeft, TipRight, TipCenter, Outward))
		{
			return;
		}
		if (Outward.IsNearlyZero())
		{
			MetaLandRoadLaneEditPick::ResolveMouthOutwardTangent(HoverSpline, Anchor, Outward);
		}
		if (!bEndSnapBundleUserLocked)
		{
			LaneDrawEndSnapAnchor = Anchor;
			bHasLaneDrawEndSnapAnchor = true;
			EndSnapGhostLeft = TipLeft;
			EndSnapGhostRight = TipRight;
			ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
			ApplyGuidanceSurfaceZ(EndSnapGhostRight);
			bHasEndSnapGhost = true;
			DrawOutSplinePos = TipCenter;
		}
		else
		{
			DrawOutSplinePos = (EndSnapGhostLeft + EndSnapGhostRight) * 0.5;
		}
		bSplineSnapActive = true;
		if (AActor* Owner = HoverSpline->GetOwner())
		{
			HitActor = Owner;
		}
		LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
		bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
	};

	FEndSnapTargetLanePick Pick;
	if (!FindEndSnapTargetLanePick(
		TargetWorld,
		ActiveRoad,
		CursorWorld,
		GetConnectionSnapDistanceCm(),
		Pick))
	{
		if (!bEndSnapBundleUserLocked)
		{
			ClearLaneDrawEndSnap(false);
		}
		return;
	}

	if (!Pick.Spline->BuildDrawAnchorForLaneMouth(
		Pick.Section,
		Pick.Lane,
		Pick.MouthS,
		Pick.Mouth,
		EndSnapMixHoverAnchor))
	{
		EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();
		EndSnapMixHoverAnchor.SourceSpline = Pick.Spline;
		EndSnapMixHoverAnchor.SectionIndex = Pick.Section;
		EndSnapMixHoverAnchor.LaneIndex = Pick.Lane;
		EndSnapMixHoverAnchor.AnchorS = Pick.MouthS;
		EndSnapMixHoverAnchor.MouthSide = Pick.Mouth;
		EndSnapMixHoverAnchor.bFromSuccessor = Pick.Mouth == EMetaLandRoadMouthSide::End;
	}
	MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
		Pick.Spline, EndSnapMixHoverAnchor.SectionIndex, EndSnapMixHoverAnchor);
	EndSnapMixHoverAnchor.AnchorS = Pick.MouthS;

	bHasEndSnapMixHover = EndSnapMixHoverAnchor.IsValid();
	if (bHasEndSnapMixHover)
	{
		ApplyHoverGhost(EndSnapMixHoverAnchor);
	}
	(void)CursorWorld;
}

bool UMetaLandRoadInteractiveTool::TryHandleEndSnapTargetLaneMixPick(
	const FInputDeviceRay& PressPos,
	const bool bAddOnly)
{
	// During extend: Alt/Shift+LMB only edits target EndSnap mix lanes (never source EditTarget).
	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	const bool bMixModifier = bAddOnly
		? (ModKeys.IsAltDown() && !ModKeys.IsControlDown())
		: (ModKeys.IsShiftDown() && !ModKeys.IsControlDown());
	UMetaLandRoadEditorMode* MixRoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bHasSourceMouth =
		IsLaneAnchorDrawSession()
		|| bContinueDrawSession
		|| (MixRoadMode && MixRoadMode->GetEditTarget().bHasPendingAnchor);
	if (!bHasSourceMouth || !bMixModifier || ModKeys.IsControlDown())
	{
		return false;
	}

	AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!ActiveRoad && MixRoadMode)
	{
		ActiveRoad = MixRoadMode->GetEditTarget().RoadActor.Get();
	}
	if (!TargetWorld)
	{
		return true;
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return true;
	}

	FIntPoint MousePos;
	ViewportClient->Viewport->GetMousePos(MousePos);

	const FMetaLandRoadDrawAnchor* ExistingAnchor =
		bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid()
		? &LaneDrawEndSnapAnchor
		: nullptr;

	FMetaLandRoadDrawAnchor PickedAnchor;
	UMetaLandRoadSplineComponent* PickedSpline = nullptr;
	const bool bPicked = MetaLandRoadLaneEditPick::PickLaneAnchorAtMouse(
		TargetWorld,
		ViewportClient,
		ViewportClient->Viewport,
		MousePos,
		/*bShiftDown*/ true,
		/*bSelectAllLanesOnSide*/ false,
		ExistingAnchor,
		PickedAnchor,
		PickedSpline)
		&& PickedAnchor.IsValid()
		&& PickedSpline;

	if (!bPicked)
	{
		const FMetaLandRoadDrawAnchor* HoverAnchor =
			bHasEndSnapMixHover && EndSnapMixHoverAnchor.IsValid()
			? &EndSnapMixHoverAnchor
			: nullptr;
		UMetaLandRoadSplineComponent* HoverSpline = HoverAnchor
			? HoverAnchor->SourceSpline.Get()
			: nullptr;
		if (HoverSpline
			&& HoverAnchor->IsValid()
			&& Cast<AMoKuEditSplineActor>(HoverSpline->GetOwner()) != ActiveRoad)
		{
			if (ExistingAnchor
				&& MetaLandRoadLaneEditPick::IsSamePhysicalMouth(
					*ExistingAnchor,
					HoverSpline,
					HoverAnchor->MouthSide,
					HoverAnchor->AnchorS))
			{
				PickedAnchor = *ExistingAnchor;
				if (HoverAnchor->LaneIndex != PickedAnchor.LaneIndex
					&& !PickedAnchor.BundledLaneIndices.Contains(HoverAnchor->LaneIndex))
				{
					MetaLandRoadLaneEditPick::TryExpandAnchorBundle(
						PickedAnchor, HoverAnchor->LaneIndex);
				}
				else if (HoverAnchor->LaneIndex != PickedAnchor.LaneIndex)
				{
					PickedAnchor.BundledLaneIndices.Remove(HoverAnchor->LaneIndex);
				}
				else if (PickedAnchor.BundledLaneIndices.Num() > 0)
				{
					PickedAnchor.LaneIndex = PickedAnchor.BundledLaneIndices[0];
					PickedAnchor.BundledLaneIndices.RemoveAt(0);
				}
			}
			else
			{
				PickedAnchor = *HoverAnchor;
			}
			PickedSpline = HoverSpline;
		}
		else
		{
			return false;
		}
	}

	UMetaLandRoadSplineComponent* BestSpline = PickedSpline;

	if (ActiveRoad && Cast<AMoKuEditSplineActor>(BestSpline->GetOwner()) == ActiveRoad)
	{
		return false;
	}

	if (bAddOnly && ExistingAnchor)
	{
		const bool bSameMouth = MetaLandRoadLaneEditPick::IsSamePhysicalMouth(
			*ExistingAnchor,
			BestSpline,
			PickedAnchor.MouthSide,
			PickedAnchor.AnchorS);
		if (bSameMouth)
		{
			LaneDrawEndSnapAnchor = *ExistingAnchor;
			if (PickedAnchor.LaneIndex != LaneDrawEndSnapAnchor.LaneIndex
				&& !LaneDrawEndSnapAnchor.BundledLaneIndices.Contains(PickedAnchor.LaneIndex))
			{
				MetaLandRoadLaneEditPick::TryExpandAnchorBundle(
					LaneDrawEndSnapAnchor, PickedAnchor.LaneIndex);
			}
		}
		else
		{
			LaneDrawEndSnapAnchor = PickedAnchor;
			LaneDrawEndSnapAnchor.BundledLaneIndices.Reset();
		}
	}
	else
	{
		LaneDrawEndSnapAnchor = PickedAnchor;
	}

	bHasLaneDrawEndSnapAnchor = LaneDrawEndSnapAnchor.IsValid();
	bEndSnapBundleUserLocked = true;

	FVector TipLeft, TipRight, TipCenter, Outward;
	if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		BestSpline, LaneDrawEndSnapAnchor, TipLeft, TipRight, TipCenter, Outward))
	{
		return true;
	}

	EndSnapGhostLeft = TipLeft;
	EndSnapGhostRight = TipRight;
	ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
	ApplyGuidanceSurfaceZ(EndSnapGhostRight);
	bHasEndSnapGhost = true;
	DrawOutSplinePos = TipCenter;
	bSplineSnapActive = true;
	HitActor = BestSpline->GetOwner();
	LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
	if (LaneSnapArriveTangent.IsNearlyZero())
	{
		const FVector Across = (EndSnapGhostRight - EndSnapGhostLeft).GetSafeNormal2D();
		LaneSnapArriveTangent = FVector::CrossProduct(FVector::UpVector, Across).GetSafeNormal2D();
	}
	bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();

	int32 Count = CountDrawAnchorBundleLanes(LaneDrawEndSnapAnchor);
	if (OutputProperties)
	{
		OutputProperties->bEndConnectMatchSourceLanes = false;
		OutputProperties->EndConnectTargetLaneCount = FMath::Clamp(Count, 1, 8);
	}

	MetaLandRoadLogDrawAnchor(TEXT("EndSnapMix"), LaneDrawEndSnapAnchor);
	if (UMetaLandRoadSplineComponent* MixSpline = BestSpline)
	{
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(MixSpline);
	}
	UpdateDrawGuidanceRails();
	if (GEngine)
	{
		FString LaneList = FString::FromInt(LaneDrawEndSnapAnchor.LaneIndex);
		for (const int32 BundledLaneIndex : LaneDrawEndSnapAnchor.BundledLaneIndices)
		{
			LaneList += FString::Printf(TEXT(",%d"), BundledLaneIndex);
		}
		GEngine->AddOnScreenDebugMessage(
			991123,
			2.5f,
			FColor::Cyan,
			FString::Printf(
				TEXT("[MetaLandRoad] End mix [%s] count=%d  - Shift+/- | A connects (Ctrl does not confirm)"),
				*LaneList,
				Count));
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
	(void)PressPos;
	return true;
}


void UMetaLandRoadInteractiveTool::SetGuidancePlaneFromMouthCenter(const FVector& MouthCenter)
{
	GuidancePlaneZ = static_cast<float>(MouthCenter.Z);
	bHasGuidancePlaneZ = true;
}

bool UMetaLandRoadInteractiveTool::TryResumeLaneExtendOnBranchRoad(
	const FMetaLandRoadEditTarget& Target,
	AMoKuEditSplineActor* TargetRoad,
	UMetaLandRoadSplineComponent* AnchorSpline,
	const FInputDeviceRay& PressPos)
{
	if (!TargetRoad || !AnchorSpline || !Target.bHasPendingAnchor)
	{
		return false;
	}
	if (!TargetRoad->Tags.Contains(MetaLandRoadLaneBranchTag))
	{
		return false;
	}
	if (AnchorSpline->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	const double TipS = AnchorSpline->GetSplineLength();
	const bool bAtExtendTip = Target.PendingAnchor.MouthSide == EMetaLandRoadMouthSide::End
		&& FMath::IsNearlyEqual(Target.PendingAnchor.AnchorS, TipS, 200.0);
	if (!bAtExtendTip)
	{
		return false;
	}

	ActiveActor = TargetRoad;
	SetToolSession(EMetaLandRoadToolSession::LaneAnchor);
	bRoadSpawnedFromLaneAnchorDraw = true;
	bLaneAnchorSkipHubJunctionCook = true;
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	bSplineSnapActive = false;
	bInsertJunction = false;
	StateOfEditor = EMoKuEditState::Defult;

	FMetaLandRoadDrawAnchor RestoredStart;
	if (RestoreLaneDrawStartAnchorFromBranch(
		Cast<UMetaLandRoadSplineComponent>(AnchorSpline),
		Target.PendingAnchor,
		RestoredStart))
	{
		LaneDrawStartAnchor = RestoredStart;
		bHasLaneDrawStartAnchor = true;
	}
	else
	{
		bHasLaneDrawStartAnchor = false;
		LaneDrawStartAnchor = FMetaLandRoadDrawAnchor();
	}

	FVector TipLeft, TipRight, TipCenter, TipTangent;
	if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		AnchorSpline, Target.PendingAnchor, TipLeft, TipRight, TipCenter, TipTangent))
	{
		SetGuidancePlaneFromMouthCenter(TipCenter);
		GuidanceLastCursor = TipCenter;
		if (!TipTangent.IsNearlyZero())
		{
			GuidanceMoveDir = TipTangent.GetSafeNormal2D();
		}
	}

	UpdateEditState(true);
	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		TransferEditTargetToSpawnedBranch(RoadMode, Target, TargetRoad);
	}
	SyncLaneDrawCursorFromRay(PressPos.WorldRay);
	UpdateDrawGuidanceRails();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
	return true;
}

void UMetaLandRoadInteractiveTool::FinalizeLaneDrawFromConnection(
	AMoKuEditSplineActor* SpawnedRoad,
	const FMetaLandRoadDrawAnchor& SourceAnchor)
{
	if (!SpawnedRoad || !SourceAnchor.IsValid())
	{
		return;
	}

	UMoKuEditSplinesComponent* SpawnedSpline = SpawnedRoad->GetSplineComponent();
	if (!SpawnedSpline || SpawnedSpline->GetNumberOfSplinePoints() < 1)
	{
		return;
	}

	HideEditorSplineRibbon(SpawnedSpline);

	// FromConnection spawn must not Recook nearby junctions; cook only on A.
	TGuardValue<bool> SuppressJunctionRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);

	// Keep N-lane profile aligned with source bundle on every spawn.
	if (UMetaLandRoadSplineComponent* SourceSpline = SourceAnchor.SourceSpline.Get())
	{
		MetaLandRoadProfile::ApplyRoadProfileFromDrawAnchor(
			SpawnedSpline, SourceSpline, SourceAnchor);
	}

	// Spawn always places the mouth at point 0 (ctrl extend FromConnection).
	// MetaRoad: Road Predecessor ->source Lane Successor (or Pred if pick Start mouth).
	constexpr bool bAttachAtStart = true;
	SpawnedSpline->FitSplineToAnchor(SourceAnchor, bAttachAtStart);
	const bool bConnected = SpawnedSpline->ConnectRoadToDrawAnchor(SourceAnchor, bAttachAtStart);

	SpawnedRoad->Tags.AddUnique(MetaLandRoadLaneBranchTag);

	// Re-pin mouth+stub leave after Fit (ClearNonMouth must keep stub CustomTangent).
	FVector TipLeft, TipRight, TipCenter, Outward;
	if (UMetaLandRoadSplineComponent* SourceSpline = SourceAnchor.SourceSpline.Get())
	{
		if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			SourceSpline, SourceAnchor, TipLeft, TipRight, TipCenter, Outward))
		{
			const int32 StubIndex = SpawnedSpline->GetNumberOfSplinePoints() >= 2 ? 1 : INDEX_NONE;
			const double MouthWidthCm = FVector::Dist2D(TipLeft, TipRight);
			StabilizeLaneExtendMouthPoint(SpawnedSpline, 0, TipCenter, Outward, MouthWidthCm);
			ClearNonMouthCustomTangents(SpawnedSpline, 0, StubIndex);
		}
		else
		{
			ClearNonMouthCustomTangents(SpawnedSpline, 0, 1);
		}
	}
	else
	{
		ClearNonMouthCustomTangents(SpawnedSpline, 0, 1);
	}

	SpawnedSpline->UpdateSpline();
	SpawnedRoad->SetSplineComponent(SpawnedSpline);
	RefreshRoadMeshSuppressJunction(SpawnedRoad);
	if (UMetaLandRoadSplineComponent* SourceSpline = SourceAnchor.SourceSpline.Get())
	{
		RefreshRoadMeshSuppressJunction(Cast<AMoKuEditSplineActor>(SourceSpline->GetOwner()));
	}

	(void)bConnected;
}


bool UMetaLandRoadInteractiveTool::TryCommitIdleMouthToMouthConnection()
{
	if (IsDrawSessionActive())
	{
		return false;
	}

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	if (!RoadMode)
	{
		return false;
	}

	const FMetaLandRoadEditTarget& SourceTarget = RoadMode->GetEditTarget();
	if (!SourceTarget.bHasPendingAnchor || !SourceTarget.Spline.IsValid() || !SourceTarget.RoadActor.Get())
	{
		return false;
	}

	FMetaLandRoadDrawAnchor TargetStorage;
	const FMetaLandRoadDrawAnchor* TargetAnchor = ResolveEndSnapCommitAnchor(TargetStorage);
	if (!TargetAnchor || !TargetAnchor->IsValid() || !TargetAnchor->SourceSpline.IsValid())
	{
		if (FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient)
		{
			if (ViewportClient->Viewport && TargetWorld)
			{
				FIntPoint MousePos;
				ViewportClient->Viewport->GetMousePos(MousePos);
				FMetaLandRoadDrawAnchor PickedAnchor;
				UMetaLandRoadSplineComponent* PickedSpline = nullptr;
				if (MetaLandRoadLaneEditPick::PickLaneAnchorAtMouse(
					TargetWorld,
					ViewportClient,
					ViewportClient->Viewport,
					MousePos,
					/*bShiftDown*/ true,
					/*bSelectAllLanesOnSide*/ false,
					nullptr,
					PickedAnchor,
					PickedSpline)
					&& PickedAnchor.IsValid()
					&& PickedSpline
					&& PickedSpline->GetOwner() != SourceTarget.RoadActor.Get())
				{
					TargetStorage = PickedAnchor;
					TargetAnchor = &TargetStorage;
				}
			}
		}
	}
	if (!TargetAnchor || !TargetAnchor->IsValid() || !TargetAnchor->SourceSpline.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991133,
				2.5f,
				FColor::Orange,
				TEXT("[MetaLandRoad] A: Shift-select the TARGET mouth first"));
		}
		return false;
	}
	if (TargetAnchor->SourceSpline->GetOwner() == SourceTarget.RoadActor.Get())
	{
		return false;
	}

	FVector SrcL, SrcR, SrcC, SrcOut;
	FVector TgtL, TgtR, TgtC, TgtOut;
	if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			SourceTarget.Spline.Get(), SourceTarget.PendingAnchor, SrcL, SrcR, SrcC, SrcOut)
		|| !MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			TargetAnchor->SourceSpline.Get(), *TargetAnchor, TgtL, TgtR, TgtC, TgtOut))
	{
		return false;
	}

	const FVector Arrive = (-TgtOut).GetSafeNormal2D().IsNearlyZero()
		? (TgtC - SrcC).GetSafeNormal2D()
		: (-TgtOut).GetSafeNormal2D();
	const FVector Leave = SrcOut.GetSafeNormal2D().IsNearlyZero()
		? (TgtC - SrcC).GetSafeNormal2D()
		: SrcOut.GetSafeNormal2D();

	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (!World)
	{
		return false;
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("Connect Two Lane Mouths")));

	LaneDrawStartAnchor = SourceTarget.PendingAnchor;
	bHasLaneDrawStartAnchor = true;
	LaneDrawEndSnapAnchor = *TargetAnchor;
	bHasLaneDrawEndSnapAnchor = true;
	bEndSnapBundleUserLocked = true;

	const FRotator SpawnRot = Leave.Rotation();
	AddSplinePoint(SrcC, SpawnRot, false);
	AMoKuEditSplineActor* SpawnedRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMoKuEditSplinesComponent* SpawnedSpline = SpawnedRoad ? SpawnedRoad->GetSplineComponent() : nullptr;
	if (!SpawnedRoad || !SpawnedSpline)
	{
		GEditor->EndTransaction();
		return false;
	}

	while (SpawnedSpline->GetNumberOfSplinePoints() > 1)
	{
		SpawnedSpline->RemoveSplinePoint(SpawnedSpline->GetNumberOfSplinePoints() - 1, false);
	}
	if (UMetaLandRoadSplineComponent* SourceSpline = SourceTarget.Spline.Get())
	{
		MetaLandRoadProfile::ApplyRoadProfileFromDrawAnchor(
			SpawnedSpline, SourceSpline, SourceTarget.PendingAnchor);
	}
	SpawnedSpline->SetLocationAtSplinePoint(0, SrcC, ESplineCoordinateSpace::World, false);
	SpawnedSpline->AddSplinePointLocation(TgtC, SpawnedSpline->GetNumberOfSplinePoints());
	ApplyAsymmetricTangentBezierConnection(SpawnedSpline, 0, TgtC, Leave, Arrive);
	SpawnedSpline->SetLocationAtSplinePoint(0, SrcC, ESplineCoordinateSpace::World, false);
	if (SpawnedSpline->GetNumberOfSplinePoints() >= 2)
	{
		SpawnedSpline->SetLocationAtSplinePoint(
			SpawnedSpline->GetNumberOfSplinePoints() - 1, TgtC, ESplineCoordinateSpace::World, false);
	}
	ApplyAsymmetricTangentBezierConnection(SpawnedSpline, 0, TgtC, Leave, Arrive);

	SpawnedRoad->Tags.AddUnique(MetaLandRoadLaneBranchTag);
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SpawnedSpline))
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		RoadSpline->ConnectRoadToDrawAnchor(LaneDrawStartAnchor, /*bAttachAtSplineStart*/ true);
		RoadSpline->BlendEndWidthToTargetMouth(*TargetAnchor, EMetaLandRoadEndWidthBlendSpan::EntireRoad);
		RoadSpline->ConnectRoadToDrawAnchor(*TargetAnchor, /*bAttachAtSplineStart*/ false);
		// Connect/blend must not keep a Fit-nudged stub  - re-pin both mouths last.
		RoadSpline->SetLocationAtSplinePoint(0, SrcC, ESplineCoordinateSpace::World, false);
		if (RoadSpline->GetNumberOfSplinePoints() >= 2)
		{
			RoadSpline->SetLocationAtSplinePoint(
				RoadSpline->GetNumberOfSplinePoints() - 1, TgtC, ESplineCoordinateSpace::World, false);
		}
		ApplyAsymmetricTangentBezierConnection(SpawnedSpline, 0, TgtC, Leave, Arrive);
		RoadSpline->UpdateSpline();
		SpawnedRoad->SetSplineComponent(SpawnedSpline);
		RefreshRoadMeshSuppressJunction(SpawnedRoad);
		if (UMetaLandRoadSplineComponent* TargetSpline = TargetAnchor->SourceSpline.Get())
		{
			RefreshRoadMeshSuppressJunction(Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()));
		}
		if (AMoKuEditSplineActor* SourceRoad = SourceTarget.RoadActor.Get())
		{
			RefreshRoadMeshSuppressJunction(SourceRoad);
		}
	}

	if (UMetaLandRoadEditorMode* ClearMode = ResolveMetaLandRoadEditorMode(this))
	{
		ClearMode->ClearEditTarget();
	}
	ClearDrawGuidanceRails();
	UpdateEditState(false);
	bSplineSnapActive = false;
	SetToolSession(EMetaLandRoadToolSession::Idle);
	bHasLaneDrawStartAnchor = false;
	bHasLaneDrawEndSnapAnchor = false;
	bEndSnapBundleUserLocked = false;
	bHasEndSnapGhost = false;
	ActiveActor = nullptr;
	FMetaLandRoadEditorModule::Get().RestoreAllRoadActorsViewportSelection();
	if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
	{
		RoadVis->ClearNativeKeySelection();
		RoadVis->EndEditing();
	}
	if (GEditor)
	{
		GEditor->SelectNone(false, true);
	}

	GEditor->EndTransaction();
	return true;
}

void UMetaLandRoadInteractiveTool::FinalizeLaneDrawEndConnection(
	AMoKuEditSplineActor* ActiveRoad,
	const FMetaLandRoadDrawAnchor& TargetAnchor,
	bool bAttachAtSplineStart,
	bool bContinueDrawFinish,
	const TArray<FVector>* PreviewCenterline,
	double PreviewMouthS,
	const FVector& PreviewMouthCenter)
{
	if (!ActiveRoad || !TargetAnchor.IsValid())
	{
		return;
	}

	FMetaLandRoadDrawAnchor WorkingTarget = TargetAnchor;
	const FMetaLandRoadDrawAnchor SourceAnchorCopy = LaneDrawStartAnchor;

	UMoKuEditSplinesComponent* ActiveSpline = ActiveRoad->GetSplineComponent();
	if (!ActiveSpline || ActiveSpline->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	HideEditorSplineRibbon(ActiveSpline);
	TGuardValue<bool> SuppressJunctionRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);

	UMetaLandRoadSplineComponent* TargetSpline = WorkingTarget.SourceSpline.Get();
	FVector TargetLeft, TargetRight, TargetCenter, TargetOutward;
	bool bHaveTargetFrame = false;
	if (TargetSpline)
	{
		bHaveTargetFrame = TargetSpline->GetRoadEdgePointsAtS(
			WorkingTarget.AnchorS,
			TargetLeft,
			TargetRight,
			TargetCenter,
			ESplineCoordinateSpace::World);
		if (bHaveTargetFrame)
		{
			if (!MetaLandRoadLaneEditPick::ResolveMouthOutwardTangent(
				TargetSpline, WorkingTarget, TargetOutward))
			{
				FVector UnusedL, UnusedR, UnusedC;
				if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
					TargetSpline, WorkingTarget, UnusedL, UnusedR, UnusedC, TargetOutward))
				{
					TargetOutward = FVector::ZeroVector;
				}
			}
		}
		else
		{
			bHaveTargetFrame = MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				TargetSpline, WorkingTarget, TargetLeft, TargetRight, TargetCenter, TargetOutward);
		}
	}

	const bool bPreserveExistingRoad =
		bContinueDrawFinish || !IsLaneBranchRoad(ActiveRoad);
	if (bPreserveExistingRoad)
	{
		// Existing / C / free-draw: pin the new tip only.
		// EnsureLaneCount / FitSpline / Bezier-from-0 / BlendEndWidth rewrite native sections.
		if (bHaveTargetFrame)
		{
			bool bAttach = bAttachAtSplineStart;
			if (!PinContinueDrawTipToTargetMouth(
				ActiveRoad,
				ActiveSpline,
				bAttach,
				ContinueDrawOriginalNumPoints,
				TargetCenter,
				TargetOutward,
				PreviewCenterline,
				PreviewMouthS,
				PreviewMouthCenter))
			{
				return;
			}
			ActiveSpline->BlendTipSectionWidthToTargetMouth(WorkingTarget, bAttach);
		}
	}
	else
	{

	// Profile policy:
	// - LaneBranch (N-lane extend): NEVER grow to full junction / target road width.
	//   Hub cover fills the intersection; one-arm asphalt flare breaks 比例审美 (图一/ -.
	// - Other roads: keep START count when source wider; else match TARGET mouth.
	const int32 SourceLaneCount = (bHasLaneDrawStartAnchor && SourceAnchorCopy.IsValid())
		? CountDrawAnchorBundleLanes(SourceAnchorCopy)
		: 0;
	const int32 TargetBundleCount = CountDrawAnchorBundleLanes(WorkingTarget);
	UMetaLandRoadSplineComponent* SourceSpline = SourceAnchorCopy.SourceSpline.Get();
	const bool bLaneBranch = IsLaneBranchRoad(ActiveRoad);

	bool bExpandActiveToTargetMix = false;
	if (bLaneBranch && SourceLaneCount > 0 && SourceSpline)
	{
		MetaLandRoadProfile::EnsureLaneCountFromDrawAnchor(
			ActiveSpline, SourceSpline, SourceAnchorCopy);
	}
	else
	{
		bExpandActiveToTargetMix =
			TargetSpline
			&& TargetBundleCount > SourceLaneCount
			&& TargetBundleCount > 0;

		if (bExpandActiveToTargetMix)
		{
			MetaLandRoadProfile::EnsureLaneCountFromDrawAnchor(
				ActiveSpline, TargetSpline, WorkingTarget);
		}
		else if (SourceLaneCount > 0 && SourceSpline)
		{
			MetaLandRoadProfile::EnsureLaneCountFromDrawAnchor(
				ActiveSpline, SourceSpline, SourceAnchorCopy);
		}
		else if (TargetBundleCount > 0 && TargetSpline)
		{
			MetaLandRoadProfile::EnsureLaneCountFromDrawAnchor(
				ActiveSpline, TargetSpline, WorkingTarget);
		}
	}

	if (!ActiveSpline || ActiveSpline->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	FVector LeaveDir = FVector::ZeroVector;
	if (bHasLaneDrawStartAnchor && SourceAnchorCopy.IsValid() && SourceSpline)
	{
		FVector SrcL, SrcR, SrcC, SrcOut;
		if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			SourceSpline, SourceAnchorCopy, SrcL, SrcR, SrcC, SrcOut))
		{
			LeaveDir = SrcOut.GetSafeNormal2D();
		}
	}
	if (LeaveDir.IsNearlyZero())
	{
		LeaveDir = ActiveSpline->GetLeaveTangentAtSplinePoint(0, ESplineCoordinateSpace::World).GetSafeNormal2D();
	}
	if (LeaveDir.IsNearlyZero() && ActiveSpline->GetNumberOfSplinePoints() >= 2)
	{
		LeaveDir = (ActiveSpline->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World)
			- ActiveSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World)).GetSafeNormal2D();
	}

	FVector ArriveDir = bHaveTargetFrame
		? (-TargetOutward).GetSafeNormal2D()
		: FVector::ZeroVector;
	if (ArriveDir.IsNearlyZero() && bHaveTargetFrame)
	{
		ArriveDir = (TargetCenter
			- ActiveSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World)).GetSafeNormal2D();
	}

	if (bAttachAtSplineStart)
	{
		// Forward continue-draw: new tip is index 0. Do not rebuild from 0 (wipes the body).
		if (bHaveTargetFrame)
		{
			ActiveSpline->SetLocationAtSplinePoint(0, TargetCenter, ESplineCoordinateSpace::World, false);
		}
		ActiveSpline->FitSplineToAnchor(WorkingTarget, /*bAtSplineStart*/ true);
	}
	else if (bHaveTargetFrame && !LeaveDir.IsNearlyZero() && !ArriveDir.IsNearlyZero())
	{
		ApplyAsymmetricTangentBezierConnection(
			ActiveSpline,
			/*StartIndex*/ 0,
			TargetCenter,
			LeaveDir,
			ArriveDir);
	}
	else
	{
		ActiveSpline->FitSplineToAnchor(WorkingTarget, /*bAtSplineStart*/ false);
	}
	const EMetaLandRoadEndWidthBlendSpan BlendSpan = OutputProperties
		? OutputProperties->EndWidthBlendSpan
		: EMetaLandRoadEndWidthBlendSpan::LastTwoSegments;

	// LaneBranch: taper down to a narrower target (2-> mix) is allowed.
	// Skip only when the target mouth is substantially wider (hub cover owns flare).
	bool bDoEndWidthBlend = true;
	if (bLaneBranch && SourceSpline && SourceAnchorCopy.IsValid() && TargetSpline)
	{
		TArray<int32> SrcLanes, TgtLanes;
		TArray<float> SrcWidths, TgtWidths;
		float SrcSum = 0.0f;
		float TgtSum = 0.0f;
		if (MetaLandRoadProfile::GetDrawAnchorBundleLaneWidths(
				SourceSpline, SourceAnchorCopy, SrcLanes, SrcWidths)
			&& MetaLandRoadProfile::GetDrawAnchorBundleLaneWidths(
				TargetSpline, WorkingTarget, TgtLanes, TgtWidths))
		{
			for (float W : SrcWidths)
			{
				SrcSum += FMath::Max(W, 1.0f);
			}
			for (float W : TgtWidths)
			{
				TgtSum += FMath::Max(W, 1.0f);
			}
			bDoEndWidthBlend = TgtSum <= SrcSum * 1.12f;
		}
	}
	if (bDoEndWidthBlend && !bAttachAtSplineStart)
	{
		ActiveSpline->BlendEndWidthToTargetMouth(WorkingTarget, BlendSpan);
	}
	}

	UMetaLandRoadSplineComponent* SourceSpline = SourceAnchorCopy.SourceSpline.Get();
	const bool bConnected = ActiveSpline->ConnectRoadToDrawAnchor(
		WorkingTarget, bAttachAtSplineStart, /*bRebuildLayouts*/ !bPreserveExistingRoad);

	if (!bPreserveExistingRoad)
	{
		ActiveSpline->UpdateSpline();
		ActiveRoad->SetSplineComponent(ActiveSpline);
	}
	RefreshRoadMeshSuppressJunction(ActiveRoad);
	if (!bPreserveExistingRoad)
	{
		if (TargetSpline)
		{
			RefreshRoadMeshSuppressJunction(Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()));
		}
		if (bHasLaneDrawStartAnchor && SourceAnchorCopy.IsValid() && SourceSpline)
		{
			RefreshRoadMeshSuppressJunction(Cast<AMoKuEditSplineActor>(SourceSpline->GetOwner()));
		}
	}

	(void)bConnected;

	UpdateEditState(false);
	StateOfEditor = EMoKuEditState::Defult;
	bSplineSnapActive = false;
	bInsertJunction = false;
	SetToolSession(EMetaLandRoadToolSession::Idle);
	bHasLaneDrawStartAnchor = false;
	LaneDrawStartAnchor = FMetaLandRoadDrawAnchor();
	bHasLaneDrawEndSnapAnchor = false;
	bEndSnapBundleUserLocked = false;
	LaneDrawEndSnapAnchor = FMetaLandRoadDrawAnchor();
	bHasEndSnapGhost = false;
	bHasGuidancePlaneZ = false;
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	// Keep bLaneAnchorSkipHubJunctionCook until ResetDrawSessionState on A / mode exit.
	ClearDrawGuidanceRails();
	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		RoadMode->ClearEditTarget();
	}

	FMetaLandRoadEditorModule::Get().RestoreAllRoadActorsViewportSelection();
	if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
	{
		RoadVis->ClearNativeKeySelection();
		RoadVis->EndEditing();
	}
	if (GEditor)
	{
		GEditor->SelectNone(false, true);
		GEditor->RedrawLevelEditingViewports(true);
	}
}

float UMetaLandRoadInteractiveTool::ResolveActiveTipRoadWidthCm() const
{
	const float Fallback = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const FMetaLandRoadEditTarget* EditTarget = RoadMode ? &RoadMode->GetEditTarget() : nullptr;

	AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!RoadActor && EditTarget)
	{
		RoadActor = EditTarget->RoadActor.Get();
	}

	UMetaLandRoadSplineComponent* Spline = nullptr;
	if (RoadActor)
	{
		Spline = Cast<UMetaLandRoadSplineComponent>(RoadActor->GetSplineComponent());
	}
	if (!Spline && EditTarget)
	{
		Spline = EditTarget->Spline.Get();
	}
	if (!Spline)
	{
		return Fallback;
	}

	const bool bForward = (StateOfEditor == EMoKuEditState::Forward);
	const bool bWholeMouth = bContinueDrawSession || !IsLaneAnchorDrawSession();
	const double TipS = (bContinueDrawSession
		&& ContinueDrawOriginalNumPoints > 0
		&& Spline->GetNumberOfSplinePoints() <= ContinueDrawOriginalNumPoints)
		? ResolveEditableTipS(RoadActor, Cast<UMoKuEditSplinesComponent>(Spline), bForward)
		: ResolveDrawTipSForGuidance(Spline, bForward);
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	if (bContinueDrawSession
		&& FVector::Dist2D(ContinueDrawMouthLeft, ContinueDrawMouthRight) > 1.0)
	{
		const float MouthWidth = static_cast<float>(
			FVector::Dist2D(ContinueDrawMouthLeft, ContinueDrawMouthRight));
		if (MouthWidth > 50.0f)
		{
			return MouthWidth;
		}
	}
	if (ResolveDrawTipLaneEdges(
		RoadActor, Spline, TipS, bForward, EditTarget, TipLeft, TipRight, TipCenter, bWholeMouth))
	{
		const float MouthWidth = static_cast<float>(FVector::Dist2D(TipLeft, TipRight));
		if (MouthWidth > 50.0f)
		{
			return MouthWidth;
		}
	}

	const float HalfWidth = static_cast<float>(Spline->GetRoadHalfWidthAtS(TipS));
	return FMath::Max(Fallback, HalfWidth * 2.0f);
}

float UMetaLandRoadInteractiveTool::GetRampStraightStubCm() const
{
	if (OutputProperties && !OutputProperties->bAutoLaneDrawFromRoadWidth)
	{
		return FMath::Max(0.0f, OutputProperties->RampStraightStubCm);
	}
	// Auto: 贴边直行 length !=0.8× tip width, then Hermite bend (red-line shape).
	const float W = FMath::Max(ResolveActiveTipRoadWidthCm(), 50.0f);
	return FMath::Clamp(W * 0.8f, 200.0f, 700.0f);
}

float UMetaLandRoadInteractiveTool::GetTurnAngleThresholdCos() const
{
	float Degrees = 12.0f;
	if (OutputProperties && !OutputProperties->bAutoLaneDrawFromRoadWidth)
	{
		Degrees = FMath::Clamp(OutputProperties->TurnAngleThresholdDeg, 5.0f, 89.0f);
	}
	else
	{
		// Wider tip ->Hermite sooner (smaller angle). Narrow ->tolerate more straight.
		// θ = θ_ref * sqrt(W0 / W), clamped.
		const float W0 = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);
		const float W = FMath::Max(ResolveActiveTipRoadWidthCm(), 50.0f);
		// Trigger Hermite earlier  - delayed turns become tip→end chords (折角).
		constexpr float ThetaRefDeg = 10.0f;
		Degrees = FMath::Clamp(ThetaRefDeg * FMath::Sqrt(W0 / W), 5.0f, 22.0f);
	}
	return FMath::Cos(FMath::DegreesToRadians(Degrees));
}

float UMetaLandRoadInteractiveTool::GetSplineInsertStepCm() const
{
	if (OutputProperties && !OutputProperties->bAutoLaneDrawFromRoadWidth)
	{
		return FMath::Max(200.0f, OutputProperties->SplineInsertStepCm);
	}
	// ~1.4× tip width: wider roads keep fewer mesh keys per meter.
	const float W = FMath::Max(ResolveActiveTipRoadWidthCm(), 50.0f);
	return FMath::Clamp(W * 1.4f, 300.0f, 1600.0f);
}

float UMetaLandRoadInteractiveTool::GetConnectionSnapDistanceCm() const
{
	if (OutputProperties && !OutputProperties->bAutoLaneDrawFromRoadWidth)
	{
		return FMath::Max(50.0f, OutputProperties->ConnectionSnapDistanceCm);
	}
	const float W = FMath::Max(ResolveActiveTipRoadWidthCm(), 50.0f);
	return FMath::Clamp(W * 0.85f, 280.0f, 900.0f);
}

int32 UMetaLandRoadInteractiveTool::ResolveEndConnectTargetLaneCount() const
{
	auto CountBundle = [](const FMetaLandRoadDrawAnchor& Anchor) -> int32
	{
		if (!Anchor.IsValid())
		{
			return 0;
		}
		int32 Count = 1;
		for (const int32 BundledLaneIndex : Anchor.BundledLaneIndices)
		{
			if (BundledLaneIndex != Anchor.LaneIndex)
			{
				++Count;
			}
		}
		return FMath::Clamp(Count, 1, 8);
	};

	const int32 LockedEndCount =
		(bEndSnapBundleUserLocked && bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid())
			? CountBundle(LaneDrawEndSnapAnchor)
			: 0;

	// Locked mix is authoritative (2-lane begin ->1-lane end is allowed).
	// Hover seeds 1 lane; Shift+/- grows/shrinks the target bundle.
	if (LockedEndCount > 0)
	{
		return LockedEndCount;
	}
	if (bContinueDrawSession)
	{
		return 16;
	}
	if (OutputProperties && !OutputProperties->bEndConnectMatchSourceLanes)
	{
		return FMath::Clamp(OutputProperties->EndConnectTargetLaneCount, 1, 8);
	}
	return 1;
}

const FMetaLandRoadDrawAnchor* UMetaLandRoadInteractiveTool::ResolveEndSnapCommitAnchor(
	FMetaLandRoadDrawAnchor& OutStorage) const
{
	if (bEndSnapBundleUserLocked && LaneDrawEndSnapAnchor.IsValid())
	{
		OutStorage = LaneDrawEndSnapAnchor;
		return &OutStorage;
	}
	if (bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid())
	{
		OutStorage = LaneDrawEndSnapAnchor;
		return &OutStorage;
	}
	if (bHasEndSnapMixHover && EndSnapMixHoverAnchor.IsValid())
	{
		OutStorage = EndSnapMixHoverAnchor;
		return &OutStorage;
	}
	return nullptr;
}

bool UMetaLandRoadInteractiveTool::TryAdjustEndConnectLaneCount(
	int32 Delta,
	const FInputDeviceRay& CurrentPos)
{
	if (!IsLaneAnchorDrawSession() || !GetEditState() || !bHasEndSnapGhost)
	{
		return false;
	}
	if (!OutputProperties)
	{
		return false;
	}

	const int32 Current = ResolveEndConnectTargetLaneCount();
	const int32 Next = FMath::Clamp(Current + Delta, 1, 8);
	OutputProperties->bEndConnectMatchSourceLanes = false;
	OutputProperties->EndConnectTargetLaneCount = Next;

	SyncLaneDrawCursorFromRay(CurrentPos.WorldRay);
	UpdateDrawGuidanceRails();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
	return true;
}


float UMetaLandRoadInteractiveTool::GetLateralHermiteTriggerCm() const
{
	if (OutputProperties && !OutputProperties->bAutoLaneDrawFromRoadWidth)
	{
		// Manual mode: ~0.25× insert step as a stable lateral proxy.
		return FMath::Clamp(GetSplineInsertStepCm() * 0.2f, 80.0f, 400.0f);
	}
	// Lateral !=0.35× tip width ->big side moves trigger Hermite with road scale.
	const float W = FMath::Max(ResolveActiveTipRoadWidthCm(), 50.0f);
	return FMath::Clamp(W * 0.35f, 100.0f, 500.0f);
}

void UMetaLandRoadInteractiveTool::UpdateLaneDrawSnapFromWorldHit(
	bool bHitWorld,
	AActor* InHitActor,
	const FVector& TraceHitPos)
{
	const bool bKeepUserBundle = bEndSnapBundleUserLocked && LaneDrawEndSnapAnchor.IsValid();
	const FMetaLandRoadDrawAnchor LockedAnchor = LaneDrawEndSnapAnchor;

	// Idle two-mouth: hover must not wipe a locked / hovered target mouth.
	if (!IsDrawSessionActive())
	{
		if (bKeepUserBundle && LockedAnchor.IsValid() && LockedAnchor.SourceSpline.IsValid())
		{
			FVector TipLeft, TipRight, TipCenter, Outward;
			if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				LockedAnchor.SourceSpline.Get(), LockedAnchor, TipLeft, TipRight, TipCenter, Outward))
			{
				EndSnapGhostLeft = TipLeft;
				EndSnapGhostRight = TipRight;
				ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
				ApplyGuidanceSurfaceZ(EndSnapGhostRight);
				bHasEndSnapGhost = true;
				DrawOutSplinePos = TipCenter;
				LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
				bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
				if (AActor* Owner = LockedAnchor.SourceSpline->GetOwner())
				{
					HitActor = Owner;
				}
			}
		}
		return;
	}

	bHasLaneSnapArriveTangent = false;
	LaneSnapArriveTangent = FVector::ForwardVector;
	if (!bKeepUserBundle)
	{
		bHasLaneDrawEndSnapAnchor = false;
		LaneDrawEndSnapAnchor = FMetaLandRoadDrawAnchor();
		if (LaneDrawFinishMode == EMetaLandRoadDrawFinishMode::Blend)
		{
			LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
		}
	}
	bHasEndSnapGhost = false;

	auto ApplyLockedEndSnapGhost = [this](const FMetaLandRoadDrawAnchor& Anchor) -> bool
	{
		UMetaLandRoadSplineComponent* LockedSpline = Anchor.SourceSpline.Get();
		if (!LockedSpline || !Anchor.IsValid())
		{
			return false;
		}
		FVector TipLeft, TipRight, TipCenter, Outward;
		if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			LockedSpline, Anchor, TipLeft, TipRight, TipCenter, Outward))
		{
			return false;
		}
		LaneDrawEndSnapAnchor = Anchor;
		bHasLaneDrawEndSnapAnchor = true;
		bEndSnapBundleUserLocked = true;
		EndSnapGhostLeft = TipLeft;
		EndSnapGhostRight = TipRight;
		ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
		ApplyGuidanceSurfaceZ(EndSnapGhostRight);
		bHasEndSnapGhost = true;
		DrawOutSplinePos = TipCenter;
		bSplineSnapActive = true;
		LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
		bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
		if (AActor* Owner = LockedSpline->GetOwner())
		{
			HitActor = Owner;
		}
		return true;
	};

	if (!bHitWorld)
	{
		if (bKeepUserBundle && ApplyLockedEndSnapGhost(LockedAnchor))
		{
			return;
		}
		HitActor = nullptr;
	}
	else
	{
		HitActor = InHitActor;
	}
	const bool bAllowConnectionSnap = AllowsEndSnapConnectionHover();
	AMoKuEditSplineActor* SnapRoad = Cast<AMoKuEditSplineActor>(InHitActor);
	AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!bAllowConnectionSnap)
	{
		if (bKeepUserBundle && ApplyLockedEndSnapGhost(LockedAnchor))
		{
			return;
		}
		bSplineSnapActive = false;
		return;
	}

	if (bKeepUserBundle && ApplyLockedEndSnapGhost(LockedAnchor))
	{
		return;
	}

	auto ApplyHoverEndSnap = [this](
		const FMetaLandRoadDrawAnchor& SnapAnchor,
		const FVector& SnapPos,
		const FVector& ArriveTangent,
		const FVector& GhostL,
		const FVector& GhostR,
		float SampleDist) -> void
	{
		bEndSnapBundleUserLocked = false;
		bSplineSnapActive = true;
		DrawOutSplinePos = SnapPos;
		IntersectData.TValue = SampleDist;
		LaneSnapArriveTangent = ArriveTangent;
		bHasLaneSnapArriveTangent = !ArriveTangent.IsNearlyZero();
		if (SnapAnchor.IsValid())
		{
			LaneDrawEndSnapAnchor = SnapAnchor;
			bHasLaneDrawEndSnapAnchor = true;
		}
		EndSnapGhostLeft = GhostL;
		EndSnapGhostRight = GhostR;
		ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
		ApplyGuidanceSurfaceZ(EndSnapGhostRight);
		bHasEndSnapGhost = true;
	};

	// Landscape / road-body hover: snap to the nearer mouth before the ray
	// hits a mesh corner (图二).
	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	FEndSnapTargetLanePick Pick;
	if (FindEndSnapTargetLanePick(
		World,
		ActiveRoad,
		TraceHitPos,
		GetConnectionSnapDistanceCm(),
		Pick))
	{
		FMetaLandRoadDrawAnchor SnapAnchor;
		FVector GhostL, GhostR, SnapPos, ArriveTangent;
		if (BuildEndSnapFromLanePick(
			Pick,
			ResolveEndConnectTargetLaneCount(),
			bContinueDrawSession,
			SnapAnchor,
			GhostL,
			GhostR,
			SnapPos,
			ArriveTangent))
		{
			ApplyHoverEndSnap(
				SnapAnchor,
				SnapPos,
				ArriveTangent,
				GhostL,
				GhostR,
				static_cast<float>(Pick.MouthS));
			if (AActor* Owner = Pick.Spline ? Pick.Spline->GetOwner() : nullptr)
			{
				HitActor = Owner;
			}
			return;
		}
	}

	if (SnapRoad && SnapRoad != ActiveRoad)
	{
		FVector TipCenter = TraceHitPos;
		if (ActiveRoad)
		{
			if (UMoKuEditSplinesComponent* ActiveSpline = ActiveRoad->GetSplineComponent())
			{
				const bool bForward = (StateOfEditor == EMoKuEditState::Forward);
				const int32 TipIndex = bForward ? 0 : (ActiveSpline->GetNumberOfSplinePoints() - 1);
				if (ActiveSpline->GetNumberOfSplinePoints() > 0 && TipIndex >= 0)
				{
					TipCenter = ActiveSpline->GetLocationAtSplinePoint(
						TipIndex, ESplineCoordinateSpace::World);
				}
			}
		}

		FVector SnapPos;
		FVector ArriveTangent;
		float SampleDist = 0.0f;
		FMetaLandRoadDrawAnchor SnapAnchor;
		FVector GhostL, GhostR;
		if (ResolveLaneEdgeConnectionSnap(
			SnapRoad,
			TipCenter,
			TraceHitPos,
			GetConnectionSnapDistanceCm(),
			ResolveEndConnectTargetLaneCount(),
			SnapPos,
			ArriveTangent,
			SampleDist,
			&SnapAnchor,
			&GhostL,
			&GhostR))
		{
			if (bContinueDrawSession)
			{
				if (UMetaLandRoadSplineComponent* SnapSpline =
					Cast<UMetaLandRoadSplineComponent>(SnapRoad->GetSplineComponent()))
				{
					MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
						SnapSpline, SnapAnchor.SectionIndex, SnapAnchor);
					FVector TipL, TipR, TipC, Outward;
					if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
						SnapSpline, SnapAnchor, TipL, TipR, TipC, Outward))
					{
						GhostL = TipL;
						GhostR = TipR;
						SnapPos = TipC;
						ArriveTangent = (-Outward).GetSafeNormal2D();
					}
				}
			}
			ApplyHoverEndSnap(SnapAnchor, SnapPos, ArriveTangent, GhostL, GhostR, SampleDist);
			return;
		}
	}

	bSplineSnapActive = false;
}

void UMetaLandRoadInteractiveTool::SyncLaneDrawCursorFromRay(const FRay& WorldRay)
{
	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (!World)
	{
		return;
	}

	const FName TraceTagName = FName("WorldCollision");
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
	const bool bHitWorld = World->LineTraceSingleByObjectType(
		Result,
		WorldRay.Origin,
		WorldRay.PointAt(HALF_WORLD_MAX),
		QueryParams,
		QueryCollisionParams);

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	AMoKuEditSplineActor* HoverActor = Cast<AMoKuEditSplineActor>(GetActiveActor());
	const bool bLaneGuidanceActive =
		IsLaneAnchorDrawSession()
		|| GetEditState()
		|| bContinueDrawSession
		|| (RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor);

	if (bLaneGuidanceActive)
	{
		const float FallbackZ = bHasGuidancePlaneZ
			? GuidancePlaneZ
			: (GuidanceLastCursor.IsNearlyZero() ? 0.0f : static_cast<float>(GuidanceLastCursor.Z));
		ResultHitPos = ResolveGuidanceCursorForRay(
			WorldRay,
			RoadMode,
			HoverActor,
			StateOfEditor,
			GuidancePlaneZ,
			bHasGuidancePlaneZ,
			FallbackZ,
			bHitWorld ? Result.ImpactPoint : ResultHitPos,
			bHitWorld,
			World);
	}
	else if (bHitWorld)
	{
		ResultHitPos = Result.ImpactPoint;
		// Free draw: if we already have a guidance plane, keep MetaRoad planar Z.
		if (bHasGuidancePlaneZ)
		{
			ResultHitPos.Z = GuidancePlaneZ;
		}
		else if (Cast<AMoKuEditSplineActor>(Result.GetActor())
			|| Cast<AMoKuEditIntersectionActor>(Result.GetActor()))
		{
			// Hit road top  - drop XY onto landscape so first point is not elevated on mesh.
			ProjectPointOntoLandscapeSurface(World, ResultHitPos, HoverActor);
		}
	}

	AActor* TraceHitActor = bHitWorld ? Result.GetActor() : nullptr;
	UpdateLaneDrawSnapFromWorldHit(bHitWorld, TraceHitActor, ResultHitPos);
}

void UMetaLandRoadInteractiveTool::ResolveLaneExtendCenterPlan(
	const FVector& TipCenter,
	const FVector& TipTangent,
	const FVector& CursorOnPlane,
	bool bAllowHermiteRamp,
	FLaneExtendCenterPlan& OutPlan) const
{
	OutPlan.StartCenter = TipCenter;
	OutPlan.EndCenter = CursorOnPlane;
	OutPlan.StartTangent = TipTangent.GetSafeNormal2D();
	OutPlan.EndTangent = OutPlan.StartTangent;
	OutPlan.bUseHermite = false;

	if (OutPlan.StartTangent.IsNearlyZero())
	{
		OutPlan.StartTangent = GuidanceMoveDir.GetSafeNormal2D();
	}
	if (OutPlan.StartTangent.IsNearlyZero())
	{
		OutPlan.StartTangent = FVector::ForwardVector;
	}

	if (!bAllowHermiteRamp)
	{
		// Initial dual-rail (NumPts < 3): free tip ->mouse. Do NOT project onto tip leave  - 
		// that "Ctrl locks" the rubber-band onto one axis and makes L/R cross when mouse goes aside.
		OutPlan.EndCenter = CursorOnPlane;
		OutPlan.EndTangent = (OutPlan.EndCenter - TipCenter).GetSafeNormal2D();
		if (OutPlan.EndTangent.IsNearlyZero())
		{
			OutPlan.EndTangent = OutPlan.StartTangent;
		}
		return;
	}

	if (bHasEndSnapGhost)
	{
		OutPlan.EndCenter = (EndSnapGhostLeft + EndSnapGhostRight) * 0.5;
		OutPlan.EndTangent = bHasLaneSnapArriveTangent
			? LaneSnapArriveTangent.GetSafeNormal2D()
			: (OutPlan.EndCenter - TipCenter).GetSafeNormal2D();
		OutPlan.bUseHermite = true;
		if (OutPlan.EndTangent.IsNearlyZero())
		{
			OutPlan.EndTangent = OutPlan.StartTangent;
		}
		return;
	}

	if (bSplineSnapActive && HitActor && HitActor->IsA<AMoKuEditSplineActor>())
	{
		AMoKuEditSplineActor* SnapRoad = Cast<AMoKuEditSplineActor>(HitActor);
		AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
		const float SnapDistCm = GetConnectionSnapDistanceCm();
		if (SnapRoad && SnapRoad != ActiveRoad)
		{
			FVector SnapPos = FVector(DrawOutSplinePos);
			FVector ArriveTangent = bHasLaneSnapArriveTangent
				? LaneSnapArriveTangent.GetSafeNormal2D()
				: FVector::ZeroVector;
			float SampleDist = IntersectData.TValue;
			if (!ResolveLaneEdgeConnectionSnap(
				SnapRoad,
				TipCenter,
				CursorOnPlane,
				SnapDistCm,
				ResolveEndConnectTargetLaneCount(),
				SnapPos,
				ArriveTangent,
				SampleDist))
			{
				// Stale hover snap  - ignore rather than twist onto centerline.
			}
			else if (FVector::Dist2D(TipCenter, SnapPos) < static_cast<double>(SnapDistCm) * 1.15)
			{
				OutPlan.EndCenter = SnapPos;
				OutPlan.EndTangent = ArriveTangent.IsNearlyZero()
					? (SnapPos - TipCenter).GetSafeNormal2D()
					: ArriveTangent;
				OutPlan.bUseHermite = true;
			}
		}
	}

	if (!OutPlan.bUseHermite)
	{
		const FVector CursorWorld = OutPlan.EndCenter;
		const FVector ToCursor = (CursorWorld - TipCenter).GetSafeNormal2D();
		const double DotAlign = FVector::DotProduct(OutPlan.StartTangent, ToCursor);
		const FVector ParallelEnd = ProjectOntoParallelLaneExtend(
			TipCenter, OutPlan.StartTangent, CursorWorld);
		const double LateralCm = FVector::Dist2D(CursorWorld, ParallelEnd);
		const double LateralHermiteCm = static_cast<double>(GetLateralHermiteTriggerCm());
		const bool bTurnByAngle =
			!ToCursor.IsNearlyZero()
			&& DotAlign < static_cast<double>(GetTurnAngleThresholdCos());
		const bool bTurnByLateral = LateralCm >= LateralHermiteCm;
		if (bTurnByAngle || bTurnByLateral)
		{
			OutPlan.bUseHermite = true;
			OutPlan.EndCenter = CursorWorld;
			// Free-end arrive = ToCursor (rotated StartTangent caused outward V on green rails).
			OutPlan.EndTangent = ToCursor.IsNearlyZero() ? OutPlan.StartTangent : ToCursor;
		}
		else
		{
			OutPlan.EndCenter = ParallelEnd;
		}
	}

	if (OutPlan.EndTangent.IsNearlyZero())
	{
		OutPlan.EndTangent = OutPlan.StartTangent;
	}
}

void UMetaLandRoadInteractiveTool::AppendLaneDrawKeysToActiveSpline(
	const TArray<FVector>& Keys,
	const FLaneExtendCenterPlan& Plan,
	const FRotator& HitRot,
	bool bPinConnectionEnd)
{
	(void)HitRot;
	if (!GetEditState() || !ActiveActor || Keys.IsEmpty())
	{
		return;
	}

	AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMoKuEditSplinesComponent* SplineComponent = SplineActor ? SplineActor->GetSplineComponent() : nullptr;
	if (!SplineComponent)
	{
		return;
	}

	const bool bForward = (StateOfEditor == EMoKuEditState::Forward);
	const int32 NumBefore = SplineComponent->GetNumberOfSplinePoints();
	// Freeze already-committed points BEFORE insert so click N never reshapes click 1..N-1.
	TArray<FLaneExtendPointFreeze> CommittedFreezes;
	if (!bForward && NumBefore >= 2)
	{
		CaptureLaneExtendPointFreezes(SplineComponent, 0, NumBefore, CommittedFreezes);
	}

	if (Plan.bUseHermite && !bForward)
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		SplineComponent->Modify();
		SplineActor->Modify();
		Modify();
		const int32 SegStartIndex = (NumBefore <= 2) ? 0 : FMath::Max(NumBefore - 1, 0);
		ApplyAsymmetricTangentBezierConnection(
			SplineComponent,
			SegStartIndex,
			Plan.EndCenter,
			Plan.StartTangent,
			Plan.EndTangent);
		if (bPinConnectionEnd && SplineComponent->GetNumberOfSplinePoints() >= 2)
		{
			const int32 EndIndex = SplineComponent->GetNumberOfSplinePoints() - 1;
			SplineComponent->SetLocationAtSplinePoint(
				EndIndex, Plan.EndCenter, ESplineCoordinateSpace::World, false);
		}
		if (CommittedFreezes.Num() > 0 && SegStartIndex > 0)
		{
			RestoreLaneExtendPointFreezes(
				SplineComponent,
				/*IndexBegin*/ 0,
				CommittedFreezes,
				/*TipIndexKeepNewLeave*/ SegStartIndex);
		}
		HideEditorSplineRibbon(SplineComponent);
		SplineComponent->UpdateSpline();
		SplineActor->SetSplineComponent(SplineComponent);
		ActiveActor->IsInit = false;
		RefreshGizmoInfo();
		RefreshRoadMeshSuppressJunction(SplineActor);
		GuidanceLastCursor = Plan.EndCenter;
		GuidanceMoveDir = (Plan.EndCenter - Plan.StartCenter).GetSafeNormal2D();
		return;
	}
	// Lane-extend key inserts refresh mesh  - junction cook waits for A.
	TGuardValue<bool> SuppressJunctionRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
	SplineComponent->Modify();
	SplineActor->Modify();
	Modify();

	for (const FVector& Key : Keys)
	{
		if (bForward)
		{
			SplineComponent->AddSplinePointLocation(Key, 0);
		}
		else
		{
			SplineComponent->AddSplinePointLocation(Key, SplineComponent->GetNumberOfSplinePoints());
		}
	}

	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SplineComponent))
	{
		RoadSpline->EnsureAllCurvePointTypesForSmoothCorners();
	}

	// True mouth is always index 0 for Default (append at end). Never treat mid tips as mouth.
	const int32 MouthIndex = bForward ? (SplineComponent->GetNumberOfSplinePoints() - 1) : 0;
	const int32 StubNeighbor = (!bForward && SplineComponent->GetNumberOfSplinePoints() >= 2)
		? 1
		: (bForward && SplineComponent->GetNumberOfSplinePoints() >= 2
			? (SplineComponent->GetNumberOfSplinePoints() - 2)
			: INDEX_NONE);
	// Joint where the new segment attaches (old tip). Soften this  - it is the big 折角.
	const int32 SoftJointIndex = (!bForward && NumBefore >= 2)
		? (NumBefore - 1)
		: (bForward && NumBefore >= 2 ? Keys.Num() : INDEX_NONE);
	// Continue clicks must not demote prior CurveUser handles (even if this click is straight).
	if (!Plan.bUseHermite && NumBefore < 2)
	{
		ClearNonMouthCustomTangents(SplineComponent, MouthIndex, StubNeighbor, SoftJointIndex);
	}

	// Mouth Fit/Stabilize ONLY on first leave. Later clicks must not touch committed geometry.
	if (NumBefore <= 1)
	{
		if (!bHasLaneDrawStartAnchor
			&& SplineComponent->GetNumberOfSplinePoints() >= 1)
		{
			StabilizeLaneExtendMouthPoint(
				SplineComponent, MouthIndex, Plan.StartCenter, Plan.StartTangent);
		}

		if (!bForward && bHasLaneDrawStartAnchor && LaneDrawStartAnchor.IsValid())
		{
			if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SplineComponent))
			{
				RoadSpline->FitSplineToAnchor(LaneDrawStartAnchor, true);
				FVector MouthLeft, MouthRight, MouthCenter, MouthOutward;
				if (UMetaLandRoadSplineComponent* SourceSpline = LaneDrawStartAnchor.SourceSpline.Get();
					SourceSpline
					&& MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
						SourceSpline,
						LaneDrawStartAnchor,
						MouthLeft,
						MouthRight,
						MouthCenter,
						MouthOutward))
				{
					const double MouthWidthCm = FVector::Dist2D(MouthLeft, MouthRight);
					StabilizeLaneExtendMouthPoint(
						SplineComponent, 0, MouthCenter, MouthOutward, MouthWidthCm);
				}
				if (!Plan.bUseHermite)
				{
					ClearNonMouthCustomTangents(SplineComponent, 0, StubNeighbor, SoftJointIndex);
				}
			}
		}
	}

	// Hermite turns: NEVER Soften tip to Curve (auto bisector = pink 折角).
	// Apply tip→end CurveUser handles last so FitSpline/ClearCustom cannot wipe them.
	if (Plan.bUseHermite && SplineComponent->GetNumberOfSplinePoints() >= 2)
	{
		const int32 EndIndex = bForward ? 0 : (SplineComponent->GetNumberOfSplinePoints() - 1);
		const int32 SegStartIndex = bForward ? Keys.Num() : FMath::Max(NumBefore - 1, 0);
		ApplyLaneExtendHermiteSegmentHandles(
			SplineComponent,
			SegStartIndex,
			EndIndex,
			Plan.StartTangent,
			Plan.EndTangent,
			/*bPreserveStartArrive*/ NumBefore >= 2);
	}
	else if (SoftJointIndex != INDEX_NONE
		&& SoftJointIndex != MouthIndex
		&& SoftJointIndex != StubNeighbor)
	{
		SoftenLaneExtendCornerJoint(SplineComponent, SoftJointIndex);
	}

	if (bPinConnectionEnd && SplineComponent->GetNumberOfSplinePoints() >= 2)
	{
		const int32 EndIndex = bForward ? 0 : (SplineComponent->GetNumberOfSplinePoints() - 1);
		SplineComponent->SetLocationAtSplinePoint(
			EndIndex, Plan.EndCenter, ESplineCoordinateSpace::World, false);
		if (Plan.bUseHermite)
		{
			// Keep Hermite tip→end handles after snap relocate (do not crush to 0.28 CurveUser).
			const int32 SegStartIndex = bForward ? Keys.Num() : FMath::Max(NumBefore - 1, 0);
			ApplyLaneExtendHermiteSegmentHandles(
				SplineComponent,
				SegStartIndex,
				EndIndex,
				Plan.StartTangent,
				Plan.EndTangent,
				/*bPreserveStartArrive*/ NumBefore >= 2);
		}
		else
		{
			SplineComponent->SetSplinePointType(EndIndex, ESplinePointType::Curve, false);
			const FVector EndTangent = Plan.EndTangent.GetSafeNormal2D();
			if (!EndTangent.IsNearlyZero())
			{
				const int32 PrevIndex = bForward ? 1 : (EndIndex - 1);
				const FVector PrevLoc = SplineComponent->GetLocationAtSplinePoint(
					PrevIndex, ESplineCoordinateSpace::World);
				const double SegLen = FMath::Max(FVector::Dist2D(PrevLoc, Plan.EndCenter), 80.0);
				const double TangentSize = FMath::Clamp(SegLen * 0.28, 80.0, 280.0);
				SplineComponent->SetSplinePointType(EndIndex, ESplinePointType::CurveCustomTangent, false);
				SplineComponent->SetTangentAtSplinePoint(
					EndIndex,
					EndTangent * TangentSize,
					ESplineCoordinateSpace::World,
					false);
			}
		}
	}

	// Re-apply committed freeze so Fit/Hermite cannot reshape prior segments (含第一段→第二 -.
	if (!bForward && CommittedFreezes.Num() > 0)
	{
		RestoreLaneExtendPointFreezes(
			SplineComponent,
			/*IndexBegin*/ 0,
			CommittedFreezes,
			/*TipIndexKeepNewLeave*/ NumBefore - 1);
	}

	HideEditorSplineRibbon(SplineComponent);
	SplineComponent->UpdateSpline();
	SplineActor->SetSplineComponent(SplineComponent);
	ActiveActor->IsInit = false;
	RefreshGizmoInfo();

	FProperty* SplineCurvesProperty = FindFProperty<FProperty>(
		UMoKuEditSplinesComponent::StaticClass(),
		GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));
	if (SplineCurvesProperty)
	{
		FPropertyChangedEvent PropertyChangedEvent(SplineCurvesProperty, EPropertyChangeType::ValueSet);
		SplineComponent->PostEditChangeProperty(PropertyChangedEvent);
		PostEditChangeProperty(PropertyChangedEvent);
	}

	RefreshRoadMeshSuppressJunction(SplineActor);

	GuidanceLastCursor = Plan.EndCenter;
	GuidanceMoveDir = (Plan.EndCenter - Plan.StartCenter).GetSafeNormal2D();
}

bool UMetaLandRoadInteractiveTool::AddLaneDrawSegmentFromCursor(
	const FVector& CursorOnPlane,
	const FRotator& HitRot,
	bool bFirstLaneExtend,
	bool bConnectionSnap,
	const FMetaLandRoadDrawAnchor* CommitEndSnapAnchor)
{
	if (!GetEditState() || !ActiveActor)
	{
		return false;
	}

	AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(
		RoadActor ? RoadActor->GetSplineComponent() : nullptr);
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return false;
	}

	FVector CursorSnap = CursorOnPlane;
	ProjectPointOntoLandscapeSurface(
		TargetWorld ? TargetWorld : GetWorld(), CursorSnap, RoadActor);

	const bool bForward = (StateOfEditor == EMoKuEditState::Forward);
	const int32 NumPts = Spline->GetNumberOfSplinePoints();
	const int32 TipIndex = bForward ? 0 : NumPts - 1;
	const double TipS = ResolveDrawTipSForGuidance(Spline, bForward);
	FVector TipCenter = Spline->GetLocationAtSplinePoint(TipIndex, ESplineCoordinateSpace::World);
	// Match guidance: tip Leave handle (not blended DirectionAtDistance).
	FVector TipTangent = bForward
		? (-Spline->GetArriveTangentAtSplinePoint(TipIndex, ESplineCoordinateSpace::World).GetSafeNormal2D())
		: Spline->GetLeaveTangentAtSplinePoint(TipIndex, ESplineCoordinateSpace::World).GetSafeNormal2D();
	if (TipTangent.IsNearlyZero())
	{
		TipTangent = Spline->GetDirectionAtDistanceAlongSpline(TipS, ESplineCoordinateSpace::World).GetSafeNormal2D();
		if (bForward)
		{
			TipTangent = -TipTangent;
		}
	}

	// Only true mouth leave. NumPts==2 is already segment-1 complete  - must NOT re-apply mouth outward.
	const bool bFromMouth = bFirstLaneExtend || NumPts <= 1;
	if (bFromMouth)
	{
		if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
		{
			const FMetaLandRoadEditTarget& EditTarget = RoadMode->GetEditTarget();
			if (EditTarget.bHasPendingAnchor && EditTarget.RoadActor.Get() == RoadActor)
			{
				FMetaLandRoadDrawAnchor TipAnchor = EditTarget.PendingAnchor;
				TipAnchor.AnchorS = TipS;
				if (!MetaLandRoadLaneEditPick::ResolveMouthOutwardTangent(Spline, TipAnchor, TipTangent))
				{
					FVector TipLeft;
					FVector TipRight;
					FVector FrameCenter;
					MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
						Spline, TipAnchor, TipLeft, TipRight, FrameCenter, TipTangent);
				}
			}
		}
		if (bHasLaneDrawStartAnchor && LaneDrawStartAnchor.IsValid())
		{
			FVector MouthLeft, MouthRight, MouthCenter, MouthOutward;
			if (UMetaLandRoadSplineComponent* SourceSpline = LaneDrawStartAnchor.SourceSpline.Get();
				SourceSpline
				&& MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
					SourceSpline,
					LaneDrawStartAnchor,
					MouthLeft,
					MouthRight,
					MouthCenter,
					MouthOutward)
				&& !MouthOutward.IsNearlyZero())
			{
				TipTangent = MouthOutward.GetSafeNormal2D();
			}
		}
	}

	// Match green-rail PathCenter = (TipLeft+TipRight)/2 so mesh bake shares the same start.
	{
		FVector EdgeL, EdgeR, EdgeC;
		const FMetaLandRoadEditTarget* EditTarget = nullptr;
		if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
		{
			EditTarget = &RoadMode->GetEditTarget();
		}
		if (ResolveDrawTipLaneEdges(RoadActor, Spline, TipS, bForward, EditTarget, EdgeL, EdgeR, EdgeC))
		{
			TipCenter = (EdgeL + EdgeR) * 0.5;
		}
	}

	FLaneExtendCenterPlan Plan;
	ResolveLaneExtendCenterPlan(TipCenter, TipTangent, CursorSnap, true, Plan);
	Plan.StartCenter = TipCenter;
	Plan.StartTangent = TipTangent.GetSafeNormal2D().IsNearlyZero()
		? Plan.StartTangent
		: TipTangent.GetSafeNormal2D();
	Plan.EndCenter.Z = CursorSnap.Z;

	// Hermite: one new end key + CurveUser handles (same math as green rails).
	// Mid bake keys created pink control-point 折角 and folded mesh rings.
	TArray<FVector> MeshKeys;
	MeshKeys.Add(Plan.EndCenter);

	if (Plan.bUseHermite || MeshKeys.Num() > 1)
	{
		AppendLaneDrawKeysToActiveSpline(MeshKeys, Plan, HitRot, bConnectionSnap);
	}
	else
	{
		AddSplinePoint(Plan.EndCenter, HitRot, false);
		if (UMoKuEditSplinesComponent* Comp = RoadActor->GetSplineComponent())
		{
			const int32 MouthIndex = bForward ? (Comp->GetNumberOfSplinePoints() - 1) : 0;
			const int32 StubNeighbor = (!bForward && Comp->GetNumberOfSplinePoints() >= 2) ? 1 : INDEX_NONE;
			const int32 SoftJointIndex = (!bForward && NumPts >= 2)
				? (NumPts - 1)
				: INDEX_NONE;
			ClearNonMouthCustomTangents(Comp, MouthIndex, StubNeighbor, SoftJointIndex);
			if (SoftJointIndex != INDEX_NONE)
			{
				SoftenLaneExtendCornerJoint(Comp, SoftJointIndex);
			}
			if (NumPts <= 2 && bHasLaneDrawStartAnchor && LaneDrawStartAnchor.IsValid())
			{
				if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Comp))
				{
					RoadSpline->FitSplineToAnchor(LaneDrawStartAnchor, true);
					FVector MouthLeft, MouthRight, MouthCenter, MouthOutward;
					if (UMetaLandRoadSplineComponent* SourceSpline = LaneDrawStartAnchor.SourceSpline.Get();
						SourceSpline
						&& MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
							SourceSpline,
							LaneDrawStartAnchor,
							MouthLeft,
							MouthRight,
							MouthCenter,
							MouthOutward))
					{
						const double MouthWidthCm = FVector::Dist2D(MouthLeft, MouthRight);
						StabilizeLaneExtendMouthPoint(
							Comp, 0, MouthCenter, MouthOutward, MouthWidthCm);
					}
				}
			}
			else if (NumPts <= 1)
			{
				StabilizeLaneExtendMouthPoint(Comp, MouthIndex, Plan.StartCenter, Plan.StartTangent);
			}
			TGuardValue<bool> SuppressJunctionRecook(
				AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
			Comp->UpdateSpline();
			RoadActor->SetSplineComponent(Comp);
			RefreshRoadMeshSuppressJunction(RoadActor);
		}
		GuidanceLastCursor = Plan.EndCenter;
	}

	// MetaRoad EndConnection: seal tip to target mouth after this commit.
	const FMetaLandRoadDrawAnchor* EndSnapForCommit = CommitEndSnapAnchor;
	if (!EndSnapForCommit && bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid())
	{
		EndSnapForCommit = &LaneDrawEndSnapAnchor;
	}
	if (bConnectionSnap && EndSnapForCommit && EndSnapForCommit->IsValid())
	{
		FinalizeLaneDrawEndConnection(RoadActor, *EndSnapForCommit);
	}

	return true;
}

void UMetaLandRoadInteractiveTool::PopulateLaneExtendGuidance(
	const FVector& TipLeft,
	const FVector& TipRight,
	const FVector& TipCenter,
	const FVector& TipTangent,
	const FVector& CursorOnPlane,
	bool bAllowHermiteRamp,
	const FMetaLandRoadDrawAnchor* PerLaneAnchor,
	double TipS)
{
	FVector TipDir = TipTangent.GetSafeNormal2D();
	if (TipDir.IsNearlyZero())
	{
		TipDir = GuidanceMoveDir.GetSafeNormal2D();
	}
	if (TipDir.IsNearlyZero())
	{
		TipDir = FVector::ForwardVector;
	}

	FLaneExtendCenterPlan Plan;
	ResolveLaneExtendCenterPlan(TipCenter, TipDir, CursorOnPlane, bAllowHermiteRamp, Plan);

	const FVector* EndLeftPtr = nullptr;
	const FVector* EndRightPtr = nullptr;
	const bool bUseGhostForPlan = bHasEndSnapGhost
		&& (!PerLaneAnchor || !PerLaneAnchor->IsValid() || bEndSnapBundleUserLocked);
	if (bUseGhostForPlan)
	{
		EndLeftPtr = &EndSnapGhostLeft;
		EndRightPtr = &EndSnapGhostRight;
		Plan.EndCenter = (EndSnapGhostLeft + EndSnapGhostRight) * 0.5;
		Plan.StartTangent = TipDir;
		if (bHasLaneSnapArriveTangent && !LaneSnapArriveTangent.IsNearlyZero())
		{
			Plan.EndTangent = LaneSnapArriveTangent.GetSafeNormal2D();
		}
		else
		{
			const FVector Across = (EndSnapGhostRight - EndSnapGhostLeft).GetSafeNormal2D();
			FVector EndN(-Across.Y, Across.X, 0.0);
			const FVector Guess = (Plan.EndCenter - TipCenter).GetSafeNormal2D();
			if (EndN.Normalize())
			{
				Plan.EndTangent = (!Guess.IsNearlyZero() && FVector::DotProduct(Guess, EndN) < 0.0)
					? -EndN
					: EndN;
			}
			else if (!Guess.IsNearlyZero())
			{
				Plan.EndTangent = Guess;
			}
		}
		Plan.bUseHermite = true;
	}

	// One corridor (L/R rails). Per-lane multi-rails cross on turns and hide width lerp.
	// EndSnap ghost / locked mix ->taper TipMouth ->EndMouth; else constant start mouth width.
	if (bHasEndSnapGhost)
	{
		TArray<FVector> SingleLeftRail;
		TArray<FVector> SingleRightRail;
		FCornerCurveDebug CurveDebug;
		BuildLaneWidthGuidanceRails(
			TipLeft,
			TipRight,
			TipCenter,
			Plan.StartTangent,
			Plan.EndCenter,
			Plan.bUseHermite,
			Plan.EndTangent,
			GetRampStraightStubCm(),
			static_cast<double>(GuidanceSampleStepCm),
			SingleLeftRail,
			SingleRightRail,
			&EndSnapGhostLeft,
			&EndSnapGhostRight,
			&GuidanceCenterline,
			&CurveDebug);
		bDrawGuidanceInvalid = CurveDebug.bInvalid;
		GuidanceLeftPolylines.Add(MoveTemp(SingleLeftRail));
		GuidanceRightPolylines.Add(MoveTemp(SingleRightRail));
		return;
	}
	(void)PerLaneAnchor;
	(void)TipS;

	TArray<FVector> SingleLeftRail;
	TArray<FVector> SingleRightRail;
	FCornerCurveDebug CurveDebug;
	BuildLaneWidthGuidanceRails(
		TipLeft,
		TipRight,
		TipCenter,
		Plan.StartTangent,
		Plan.EndCenter,
		Plan.bUseHermite,
		Plan.EndTangent,
		GetRampStraightStubCm(),
		static_cast<double>(GuidanceSampleStepCm),
		SingleLeftRail,
		SingleRightRail,
		EndLeftPtr,
		EndRightPtr,
		&GuidanceCenterline,
		&CurveDebug);
	bDrawGuidanceInvalid = CurveDebug.bInvalid;
	GuidanceLeftPolylines.Add(MoveTemp(SingleLeftRail));
	GuidanceRightPolylines.Add(MoveTemp(SingleRightRail));
}

bool UMetaLandRoadInteractiveTool::HasDrawGuidanceRails() const
{
	for (const TArray<FVector>& Rail : GuidanceLeftPolylines)
	{
		if (Rail.Num() >= 2)
		{
			return true;
		}
	}
	return false;
}

void UMetaLandRoadInteractiveTool::UpdateAnchorPreviewGuidanceRails()
{
	GuidanceLeftPolylines.Reset();
	GuidanceRightPolylines.Reset();
	GuidanceCenterline.Reset();
	bDrawGuidanceInvalid = false;

	// Allow Shift multi-select preview while idle; only skip during Ctrl place.
	if (GetEditState() && bSplineSnapActive)
	{
		return;
	}

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	if (!RoadMode)
	{
		return;
	}

	const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
	if (!Target.bHasPendingAnchor || !Target.Spline.IsValid())
	{
		return;
	}

	// Re-enter on tagged branch tip: same frame as extend, only while session is active.
	if (AMoKuEditSplineActor* BranchActor = Target.RoadActor.Get())
	{
		if (BranchActor->Tags.Contains(MetaLandRoadLaneBranchTag)
			&& Target.PendingAnchor.MouthSide == EMetaLandRoadMouthSide::End
			&& (IsLaneAnchorDrawSession() || bRoadSpawnedFromLaneAnchorDraw || GetEditState()))
		{
			AMoKuEditBaseActor* const SavedActive = ActiveActor;
			const EMetaLandRoadToolSession SavedSession = ToolSession;
			ActiveActor = BranchActor;
			SetToolSession(EMetaLandRoadToolSession::LaneAnchor);
			UpdateDrawGuidanceRails();
			ActiveActor = SavedActive;
			SetToolSession(SavedSession);
			return;
		}
	}

	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector OutTangent;
	if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		Target.Spline.Get(),
		Target.PendingAnchor,
		TipLeft,
		TipRight,
		TipCenter,
		OutTangent))
	{
		return;
	}

	const float PlaneZ = ResolveGuidancePlaneZOnSurface(
		TargetWorld ? TargetWorld : GetWorld(),
		TipCenter,
		Target.RoadActor.Get());
	TipLeft.Z = PlaneZ;
	TipRight.Z = PlaneZ;
	TipCenter.Z = PlaneZ;
	GuidancePlaneZ = PlaneZ;
	bHasGuidancePlaneZ = true;

	FVector Cursor = ResultHitPos;
	if (bHasEndSnapGhost)
	{
		Cursor = (EndSnapGhostLeft + EndSnapGhostRight) * 0.5;
	}
	else if (bHasLaneDrawEndSnapAnchor && LaneDrawEndSnapAnchor.IsValid()
		&& LaneDrawEndSnapAnchor.SourceSpline.IsValid())
	{
		FVector SnapL, SnapR, SnapC, SnapOut;
		if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			LaneDrawEndSnapAnchor.SourceSpline.Get(),
			LaneDrawEndSnapAnchor,
			SnapL,
			SnapR,
			SnapC,
			SnapOut))
		{
			Cursor = SnapC;
		}
	}
	Cursor.Z = PlaneZ;
	GuidanceLastCursor = Cursor;
	GuidanceMoveDir = (Cursor - TipCenter).GetSafeNormal2D();

	PopulateLaneExtendGuidance(TipLeft, TipRight, TipCenter, OutTangent, Cursor, true);
}

void UMetaLandRoadInteractiveTool::SyncAnchorPreviewFromEditTarget(const FMetaLandRoadEditTarget& Target)
{
	if (!Target.bHasPendingAnchor || !Target.Spline.IsValid())
	{
		ClearDrawGuidanceRails();
		return;
	}

	// Shift-picked a source mouth: do not let a leftover C-arm steal the next Ctrl.
	if (!bContinueDrawSession && !IsLaneAnchorDrawSession())
	{
		ClearContinueDrawMouthArm();
	}

	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector OutTangent;
	if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		Target.Spline.Get(),
		Target.PendingAnchor,
		TipLeft,
		TipRight,
		TipCenter,
		OutTangent))
	{
		SetGuidancePlaneFromMouthCenter(TipCenter);
		GuidanceLastCursor = TipCenter;
		if (!OutTangent.IsNearlyZero())
		{
			GuidanceMoveDir = OutTangent.GetSafeNormal2D();
		}
	}

	UpdateAnchorPreviewGuidanceRails();
}

void UMetaLandRoadInteractiveTool::UpdateDrawGuidanceRails()
{
	GuidanceLeftPolylines.Reset();
	GuidanceRightPolylines.Reset();
	GuidanceCenterline.Reset();
	bDrawGuidanceInvalid = false;

	AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!RoadActor || !IsDrawSessionActive())
	{
		return;
	}
	UMoKuEditSplinesComponent* Spline = RoadActor ? RoadActor->GetSplineComponent() : nullptr;
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return;
	}

	const bool bConnectionGuidance = AllowsEndSnapConnectionHover()
		&& GetEditState()
		&& bSplineSnapActive
		&& HitActor
		&& HitActor->IsA<AMoKuEditSplineActor>()
		&& HitActor != ActiveActor;
	const bool bAimAtEndSnapMouth = bHasEndSnapGhost;
	FVector Cursor = bAimAtEndSnapMouth
		? ((EndSnapGhostLeft + EndSnapGhostRight) * 0.5)
		: (bConnectionGuidance ? FVector(DrawOutSplinePos) : ResultHitPos);
	const FVector Delta2D = (Cursor - GuidanceLastCursor).GetSafeNormal2D();
	if (!Delta2D.IsNearlyZero())
	{
		GuidanceMoveDir = Delta2D;
	}
	GuidanceLastCursor = Cursor;

	const int32 NumPts = Spline->GetNumberOfSplinePoints();
	const bool bForward = (StateOfEditor == EMoKuEditState::Forward);
	FVector RubberEnd = Cursor;

	// Whole-mouth C / free-draw: visible CutS bar. Lane-extend keeps per-lane frame.
	const bool bWholeMouthGuidance = bContinueDrawSession || !IsLaneAnchorDrawSession();
	const bool bStillOnArmedMouth =
		FVector::Dist2D(ContinueDrawMouthLeft, ContinueDrawMouthRight) > 1.0
		&& (bContinueDrawMouthArmed
			|| (bContinueDrawSession
				&& ContinueDrawOriginalNumPoints > 0
				&& NumPts <= ContinueDrawOriginalNumPoints));
	const double TipS = (bStillOnArmedMouth && ContinueDrawMouthS >= 0.0)
		? ContinueDrawMouthS
		: (bStillOnArmedMouth
			? ResolveEditableTipS(RoadActor, Spline, bForward)
			: ResolveDrawTipSForGuidance(Spline, bForward));
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	const FMetaLandRoadEditTarget* EditTarget = nullptr;
	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		EditTarget = &RoadMode->GetEditTarget();
	}
	if (bStillOnArmedMouth)
	{
		TipLeft = ContinueDrawMouthLeft;
		TipRight = ContinueDrawMouthRight;
		TipCenter = ContinueDrawMouthCenter;
	}
	else if (!ResolveDrawTipLaneEdges(
		RoadActor, Spline, TipS, bForward, EditTarget, TipLeft, TipRight, TipCenter, bWholeMouthGuidance))
	{
		TipCenter = bForward
			? Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World)
			: Spline->GetLocationAtSplinePoint(NumPts - 1, ESplineCoordinateSpace::World);
		const double HalfWidth = FMath::Max(
			Spline->GetRoadHalfWidthAtS(TipS),
			MetaLandRoad::DefaultRoadLaneWidth);
		const FVector TipRightVec = Spline->GetRightVectorAtDistanceAlongSpline(TipS, ESplineCoordinateSpace::World);
		TipLeft = TipCenter - TipRightVec * HalfWidth;
		TipRight = TipCenter + TipRightVec * HalfWidth;
	}

	// Leave at the remaining-road tip (CutS / free end), not last leftover keys.
	// MouthOutwardFromSplinePoints uses PLast-PPrev and folds when leftover sits after CutS.
	FVector ExtendTangent = Spline->GetDirectionAtDistanceAlongSpline(
		TipS, ESplineCoordinateSpace::World).GetSafeNormal2D();
	if (bForward)
	{
		ExtendTangent = -ExtendTangent;
	}
	if (ExtendTangent.IsNearlyZero())
	{
		ExtendTangent = MouthOutwardFromSplinePoints(Spline, bForward);
	}

	// Always path from mouth mid  - must match spawn MouthLoc / mesh centerline.
	// Keep raw tip Z here; BuildLaneWidthGuidanceRails flattens + applies one PDI lift (MetaRoad).
	TipCenter = (TipLeft + TipRight) * 0.5;

	FVector TipTangent = ExtendTangent;
	if (TipTangent.IsNearlyZero())
	{
		TipTangent = (RubberEnd - TipCenter).GetSafeNormal2D();
	}
	if (TipTangent.IsNearlyZero())
	{
		TipTangent = GuidanceMoveDir.GetSafeNormal2D();
	}
	if (TipTangent.IsNearlyZero())
	{
		TipTangent = FVector::ForwardVector;
	}

	// Live tip mouth width from start bundle (or EditTarget on branch). Always refresh.
	const bool bEditTargetOnActiveRoad = EditTarget
		&& EditTarget->bHasPendingAnchor
		&& EditTarget->RoadActor.Get() == RoadActor
		&& EditTarget->Spline.Get() == Spline;
	FMetaLandRoadDrawAnchor LiveStartBundle;
	bool bHaveLiveStart = false;
	if (bEditTargetOnActiveRoad)
	{
		LiveStartBundle = EditTarget->PendingAnchor;
		bHaveLiveStart = LiveStartBundle.IsValid();
	}
	else if (bHasLaneDrawStartAnchor && LaneDrawStartAnchor.IsValid())
	{
		if (LaneDrawStartAnchor.SourceSpline.Get() == Spline)
		{
			LiveStartBundle = LaneDrawStartAnchor;
			bHaveLiveStart = LiveStartBundle.IsValid();
		}
		else
		{
			TArray<int32> SourceLanes;
			CollectSortedBundleLaneIndices(LaneDrawStartAnchor, SourceLanes);
			LiveStartBundle.SourceSpline = Spline;
			LiveStartBundle.SectionIndex = 0;
			LiveStartBundle.AnchorS = TipS;
			LiveStartBundle.MouthSide = bForward
				? EMetaLandRoadMouthSide::Start
				: EMetaLandRoadMouthSide::End;
			LiveStartBundle.bFromSuccessor = !bForward;
			LiveStartBundle.LaneIndex = MapSpawnedBranchLaneIndex(LaneDrawStartAnchor.LaneIndex, SourceLanes);
			LiveStartBundle.BundledLaneIndices.Reset();
			for (const int32 SourceLane : SourceLanes)
			{
				if (SourceLane == LaneDrawStartAnchor.LaneIndex)
				{
					continue;
				}
				const int32 Mapped = MapSpawnedBranchLaneIndex(SourceLane, SourceLanes);
				if (Mapped != LANE_INDEX_NONE && Mapped != LiveStartBundle.LaneIndex)
				{
					LiveStartBundle.BundledLaneIndices.AddUnique(Mapped);
				}
			}
			bHaveLiveStart = LiveStartBundle.LaneIndex != LANE_INDEX_NONE;
		}
	}
	if (bHaveLiveStart && !bWholeMouthGuidance)
	{
		LiveStartBundle.SourceSpline = Spline;
		LiveStartBundle.AnchorS = TipS;
		LiveStartBundle.MouthSide = bForward
			? EMetaLandRoadMouthSide::Start
			: EMetaLandRoadMouthSide::End;
		LiveStartBundle.bFromSuccessor = !bForward;
		FVector BundleCenter;
		FVector BundleTangent;
		if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			Spline, LiveStartBundle, TipLeft, TipRight, BundleCenter, BundleTangent))
		{
			TipCenter = BundleCenter;
		}
	}

	{
		const FVector MouthOut = ExtendTangent;
		const FVector BarAcross = (TipRight - TipLeft).GetSafeNormal2D();
		FVector BarOut = FVector::CrossProduct(FVector::UpVector, BarAcross).GetSafeNormal2D();
		if (!MouthOut.IsNearlyZero() && FVector::DotProduct(BarOut, MouthOut) < 0.0)
		{
			BarOut = -BarOut;
		}
		if (!BarOut.IsNearlyZero())
		{
			TipTangent = BarOut;
		}
		else if (!MouthOut.IsNearlyZero())
		{
			TipTangent = MouthOut;
		}
	}

	// Plane = landscape under tip (not elevated mesh Z) so green rails hug ground like MetaRoad.
	const float PlaneZ = (bContinueDrawSession && bHasGuidancePlaneZ)
		? GuidancePlaneZ
		: ResolveGuidancePlaneZOnSurface(TargetWorld, TipCenter, RoadActor);
	TipLeft.Z = PlaneZ;
	TipRight.Z = PlaneZ;
	TipCenter.Z = PlaneZ;
	RubberEnd.Z = PlaneZ;
	GuidancePlaneZ = PlaneZ;
	bHasGuidancePlaneZ = true;

	// Hermite rubber-band when spline already has a segment (NumPts >= 2).
	// NumPts < 2 (刚落第一 -: tip→cursor stays parallel dual straight.
	const bool bAllowCurveGuidance = NumPts >= 2;
	PopulateLaneExtendGuidance(
		TipLeft, TipRight, TipCenter, TipTangent, RubberEnd, bAllowCurveGuidance, nullptr, TipS);
	(void)bConnectionGuidance;
}

void UMetaLandRoadInteractiveTool::DrawDrawGuidanceRails(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI)
	{
		return;
	}
	auto DrawRail = [PDI](const TArray<FVector>& Rail, const FLinearColor& Color)
	{
		for (int32 i = 0; i + 1 < Rail.Num(); ++i)
		{
			PDI->DrawLine(Rail[i], Rail[i + 1], Color, SDPG_Foreground, GuidanceLineWidth);
		}
	};
	const FLinearColor RailColor = bDrawGuidanceInvalid
		? GuidanceErrorRailColor
		: GuidanceLeftRailColor;
	for (const TArray<FVector>& Rail : GuidanceLeftPolylines)
	{
		DrawRail(Rail, RailColor);
	}
	for (const TArray<FVector>& Rail : GuidanceRightPolylines)
	{
		DrawRail(Rail, RailColor);
	}

	// Default off  - green dual-rails only; enable via Tool Details ->Debug Guidance Points.
	if (!OutputProperties || !OutputProperties->bDebugDrawGuidanceRailPoints)
	{
		return;
	}

	auto DrawRailPoints = [PDI](const TArray<FVector>& Rail, const FLinearColor& Color, bool bLeft)
	{
		const float PointSize = bLeft ? 12.0f : 10.0f;
		for (int32 i = 0; i < Rail.Num(); ++i)
		{
			const FLinearColor PtColor = (i == 0)
				? FLinearColor(1.0f, 1.0f, 0.15f, 1.0f)   // tip = yellow
				: ((i == Rail.Num() - 1)
					? FLinearColor(1.0f, 0.35f, 0.1f, 1.0f) // free end = orange
					: Color);
			PDI->DrawPoint(Rail[i], PtColor, PointSize, SDPG_Foreground);
			// Tiny axis so a lone point is obvious in Perspective.
			const FVector Nudge = FVector::UpVector * 25.0;
			PDI->DrawLine(Rail[i], Rail[i] + Nudge, PtColor, SDPG_Foreground, 2.0f);
		}
	};
	for (const TArray<FVector>& Rail : GuidanceLeftPolylines)
	{
		DrawRailPoints(Rail, GuidanceLeftRailColor, true);
	}
	for (const TArray<FVector>& Rail : GuidanceRightPolylines)
	{
		DrawRailPoints(Rail, GuidanceRightRailColor, false);
	}
}

void UMetaLandRoadInteractiveTool::DrawDrawGuidanceRailPointHUD(
	FCanvas* Canvas,
	const FSceneView* View) const
{
	if (!Canvas || !View)
	{
		return;
	}

	if (!OutputProperties || !OutputProperties->bDebugDrawGuidanceRailPoints)
	{
		return;
	}
	if (!HasDrawGuidanceRails())
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	auto DrawRailLabels = [Canvas, View, Font](
		const TArray<TArray<FVector>>& Rails,
		const TCHAR* Prefix,
		const FLinearColor& TextColor)
	{
		for (int32 RailIdx = 0; RailIdx < Rails.Num(); ++RailIdx)
		{
			const TArray<FVector>& Rail = Rails[RailIdx];
			for (int32 i = 0; i < Rail.Num(); ++i)
			{
				FVector2D Screen;
				if (!View->WorldToPixel(Rail[i], Screen))
				{
					continue;
				}
				const FString Label = (Rails.Num() > 1)
					? FString::Printf(TEXT("%s%d.%d"), Prefix, RailIdx, i)
					: FString::Printf(TEXT("%s%d"), Prefix, i);
				const FVector2D TextPos(Screen.X + 6.0f, Screen.Y - 14.0f);
				FCanvasTextItem TextItem(TextPos, FText::FromString(Label), Font, TextColor);
				TextItem.EnableShadow(FLinearColor::Black);
				TextItem.bOutlined = true;
				TextItem.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);
				Canvas->DrawItem(TextItem);
			}
		}
	};

	DrawRailLabels(GuidanceLeftPolylines, TEXT("L"), FLinearColor(0.85f, 1.0f, 0.2f, 1.0f));
	DrawRailLabels(GuidanceRightPolylines, TEXT("R"), FLinearColor(0.3f, 0.95f, 1.0f, 1.0f));

	// Summary: point counts help spot duplicate / empty rails.
	{
		int32 LeftN = 0;
		int32 RightN = 0;
		for (const TArray<FVector>& Rail : GuidanceLeftPolylines)
		{
			LeftN += Rail.Num();
		}
		for (const TArray<FVector>& Rail : GuidanceRightPolylines)
		{
			RightN += Rail.Num();
		}
		const FString Summary = FString::Printf(
			TEXT("GuidanceDebug  LRails=%d LPts=%d  RRails=%d RPts=%d  (yellow=tip orange=end)"),
			GuidanceLeftPolylines.Num(),
			LeftN,
			GuidanceRightPolylines.Num(),
			RightN);
		FCanvasTextItem SummaryItem(
			FVector2D(24.0f, 48.0f),
			FText::FromString(Summary),
			Font,
			FLinearColor(1.0f, 0.95f, 0.4f, 1.0f));
		SummaryItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(SummaryItem);
	}
}

void UMetaLandRoadInteractiveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	DrawDrawGuidanceRails(PDI);
	if (bDrawGuidanceInvalid && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991134,
			0.35f,
			FColor::Red,
			TEXT("接缝路径无效（红线）。"));
	}
	const bool bDrawArmedMouthBar = bContinueDrawMouthArmed
		&& IsContinueDrawHotkeyHeld(GCurrentLevelEditingViewportClient);
	bool bDrawSessionMouthBar = false;
	if (bContinueDrawSession && PDI
		&& FVector::Dist2D(ContinueDrawMouthLeft, ContinueDrawMouthRight) > 1.0)
	{
		int32 LiveNum = ContinueDrawOriginalNumPoints;
		if (AMoKuEditSplineActor* LiveRoad = ContinueDrawRoad.Get())
		{
			if (UMoKuEditSplinesComponent* LiveSpline = LiveRoad->GetSplineComponent())
			{
				LiveNum = LiveSpline->GetNumberOfSplinePoints();
			}
		}
		else if (AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor))
		{
			if (UMoKuEditSplinesComponent* LiveSpline = ActiveRoad->GetSplineComponent())
			{
				LiveNum = LiveSpline->GetNumberOfSplinePoints();
			}
		}
		bDrawSessionMouthBar = ContinueDrawOriginalNumPoints > 0
			&& LiveNum <= ContinueDrawOriginalNumPoints;
	}
	if ((bDrawArmedMouthBar || bDrawSessionMouthBar) && PDI)
	{
		const FLinearColor MouthColor(1.0f, 0.85f, 0.15f, 1.0f);
		FVector Left = ContinueDrawMouthLeft;
		FVector Right = ContinueDrawMouthRight;
		Left.Z += GuidanceMeshSurfaceZOffset + 8.0f;
		Right.Z += GuidanceMeshSurfaceZOffset + 8.0f;
		PDI->DrawLine(Left, Right, MouthColor, SDPG_Foreground, 3.5f);
		PDI->DrawLine(Left, Right, MouthColor, SDPG_World, 3.0f);
	}
	if (bHasDrawCrossing && PDI)
	{
		FVector Cross = DrawCrossingWorld;
		Cross.Z += GuidanceMeshSurfaceZOffset + 10.0f;
		FVector From = DrawCrossingFrom;
		From.Z += GuidanceMeshSurfaceZOffset + 8.0f;
		const FLinearColor CrossColor(0.15f, 1.0f, 0.55f, 1.0f);
		const float Arm = 36.0f;
		PDI->DrawLine(From, Cross, CrossColor, SDPG_Foreground, 2.0f);
		PDI->DrawLine(
			Cross + FVector(Arm, Arm, 0.0f),
			Cross + FVector(-Arm, -Arm, 0.0f),
			CrossColor,
			SDPG_Foreground,
			3.0f);
		PDI->DrawLine(
			Cross + FVector(Arm, -Arm, 0.0f),
			Cross + FVector(-Arm, Arm, 0.0f),
			CrossColor,
			SDPG_Foreground,
			3.0f);
	}
}
void UMetaLandRoadInteractiveTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{

}
bool UMetaLandRoadInteractiveTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// Alt+W/E/T: CompVis owns hover/pick. Skip world line-traces here  - they fight
	// key editing and add heavy per-frame cost (feels like a frozen viewport).
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		const FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
		if (Module.ShouldShowNativeEditOverlay()
			|| Module.IsSectionMode()
			|| Module.IsLaneEditMode())
		{
			return false;
		}
	}

	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (!World)
	{
		return false;
	}

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	AMoKuEditSplineActor* HoverActor = Cast<AMoKuEditSplineActor>(GetActiveActor());
	const bool bLaneGuidanceActive =
		IsLaneAnchorDrawSession()
		|| GetEditState()
		|| bContinueDrawSession
		|| (RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor);

	FHitResult Result;
	bool bHitWorld = false;
	const bool bNeedWorldTrace =
		!bHasGuidancePlaneZ || (IsDrawSessionActive() && !bContinueDrawSession);
	if (bNeedWorldTrace)
	{
		const FName TraceTagName = FName("WorldCollision");
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
		bHitWorld = World->LineTraceSingleByObjectType(
			Result,
			DevicePos.WorldRay.Origin,
			DevicePos.WorldRay.PointAt(HALF_WORLD_MAX),
			QueryParams,
			QueryCollisionParams);
	}

	if (bLaneGuidanceActive)
	{
		const float FallbackZ = bHasGuidancePlaneZ
			? GuidancePlaneZ
			: (GuidanceLastCursor.IsNearlyZero() ? 0.0f : static_cast<float>(GuidanceLastCursor.Z));
		ResultHitPos = ResolveGuidanceCursorForRay(
			DevicePos.WorldRay,
			RoadMode,
			HoverActor,
			StateOfEditor,
			GuidancePlaneZ,
			bHasGuidancePlaneZ,
			FallbackZ,
			bHitWorld ? Result.ImpactPoint : ResultHitPos,
			bHitWorld,
			World);
	}
	else if (bHitWorld)
	{
		ResultHitPos = Result.ImpactPoint;
		if (bHasGuidancePlaneZ)
		{
			ResultHitPos.Z = GuidancePlaneZ;
		}
		else if (Cast<AMoKuEditSplineActor>(Result.GetActor())
			|| Cast<AMoKuEditIntersectionActor>(Result.GetActor()))
		{
			ProjectPointOntoLandscapeSurface(World, ResultHitPos, HoverActor);
		}
	}
	else
	{
		FVector PlaneCursor;
		const float PlaneZ = bHasGuidancePlaneZ
			? GuidancePlaneZ
			: (GuidanceLastCursor.IsNearlyZero() ? 0.0f : static_cast<float>(GuidanceLastCursor.Z));
		if (ResolveGuidanceCursorOnPlane(DevicePos.WorldRay, PlaneZ, PlaneCursor))
		{
			ResultHitPos = PlaneCursor;
		}
	}

	if (bHitWorld)
	{
		HitActor = Result.GetActor();
	}
	else
	{
		HitActor = nullptr;
	}

	if (IsDrawSessionActive())
	{
		UpdateLaneDrawSnapFromWorldHit(bHitWorld, HitActor, ResultHitPos);
	}
	if (GetEditState() || bContinueDrawSession)
	{
		UpdateDrawCrossingJudge();
	}
	else
	{
		bHasDrawCrossing = false;
	}
	const bool bHadMixHover = bHasEndSnapMixHover;
	const int32 PrevMixLane = EndSnapMixHoverAnchor.LaneIndex;
	UpdateEndSnapMixHoverPreview(ResultHitPos);
	const bool bMixHoverChanged =
		bHadMixHover != bHasEndSnapMixHover
		|| EndSnapMixHoverAnchor.LaneIndex != PrevMixLane;

	UMoKuEditSplinesComponent* SelecComponet = HoverActor ? HoverActor->GetSplineComponent() : nullptr;
	const float MoveEpsCm = bContinueDrawSession ? 2.0f : (IsDrawSessionActive() ? 8.0f : 16.0f);
	const bool bCursorMoved = !ResultHitPos.Equals(LastGuidanceHoverSamplePos, MoveEpsCm);
	if (bCursorMoved)
	{
		LastGuidanceHoverSamplePos = ResultHitPos;
	}

	if (GetEditState() && PreviewComponent && SelecComponet && SelecComponet->GetNumberOfSplinePoints() >= 1 && bCursorMoved)
	{
		const bool bConnectionPreview = AllowsEndSnapConnectionHover()
			&& bSplineSnapActive
			&& HitActor
			&& HitActor->IsA<AMoKuEditSplineActor>()
			&& HitActor != ActiveActor;
		const FVector RubberEnd = bConnectionPreview ? FVector(DrawOutSplinePos) : ResultHitPos;
		FVector StartPos = SelecComponet->GetLocationAtSplinePoint(
			SelecComponet->GetNumberOfSplinePoints() - 1,
			ESplineCoordinateSpace::World);
		if (StateOfEditor == EMoKuEditState::Forward)
		{
			StartPos = SelecComponet->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		}
		while (PreviewComponent->GetNumberOfSplinePoints() < 2)
		{
			PreviewComponent->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local);
		}
		PreviewComponent->SetVisibility(false);
		PreviewComponent->bDrawDebug = false;
		PreviewComponent->SetWorldLocation(StartPos);
		PreviewComponent->SetLocationAtSplinePoint(0, StartPos, ESplineCoordinateSpace::World);
		PreviewComponent->SetLocationAtSplinePoint(1, RubberEnd, ESplineCoordinateSpace::World);
		PreviewComponent->UpdateSpline();
	}

	if (bCursorMoved || bMixHoverChanged)
	{
		if (bContinueDrawSession)
		{
			UpdateDrawGuidanceRails();
		}
		else if (GetEditState() && SelecComponet && SelecComponet->GetNumberOfSplinePoints() >= 1)
		{
			UpdateDrawGuidanceRails();
		}
		else if (IsLaneAnchorDrawSession() && ActiveActor)
		{
			UpdateDrawGuidanceRails();
		}
		else if (RoadMode)
		{
			if (RoadMode->GetEditTarget().bHasPendingAnchor
				&& !GetEditState()
				&& !IsLaneAnchorDrawSession())
			{
				UpdateAnchorPreviewGuidanceRails();
			}
			else if (!IsDrawSessionActive())
			{
				ClearDrawGuidanceRails();
			}
		}
		else
		{
			ClearDrawGuidanceRails();
		}
	}

	if (GEditor && (bCursorMoved || bMixHoverChanged))
	{
		const double Now = FPlatformTime::Seconds();
		const double RedrawGap = bContinueDrawSession ? 0.0 : 0.05;
		if ((Now - LastGuidanceHoverRedrawSeconds) >= RedrawGap)
		{
			GEditor->RedrawLevelEditingViewports(false);
			LastGuidanceHoverRedrawSeconds = Now;
		}
	}
	return true;
}

static bool DrawnRoadTouchesOtherRoadOrJunction(
	AMoKuEditSplineActor* ActiveRoad,
	UWorld* World,
	float MaxHeightDelta)
{
	UMoKuEditSplinesComponent* ActiveSpline = ActiveRoad ? ActiveRoad->GetSplineComponent() : nullptr;
	if (!ActiveSpline || !World || ActiveSpline->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
	{
		AMoKuEditSplineActor* OtherRoad = *It;
		UMoKuEditSplinesComponent* OtherSpline = OtherRoad ? OtherRoad->GetSplineComponent() : nullptr;
		if (!OtherSpline
			|| OtherRoad == ActiveRoad
			|| OtherSpline == ActiveSpline
			|| OtherSpline->GetNumberOfSplinePoints() < 2)
		{
			continue;
		}
		FVector IntersectWorld = FVector::ZeroVector;
		if (MoKuSplineEditorUtils::DoSplineIntersect(
				ActiveSpline, OtherSpline, IntersectWorld, MaxHeightDelta))
		{
			return true;
		}
	}

	return false;
}

bool UMetaLandRoadInteractiveTool::TryCookJunctionOnCtrlPressIfCrossed()
{
	AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!ActiveRoad || !GetEditState())
	{
		return false;
	}

	UMoKuEditSplinesComponent* ActiveSpline = ActiveRoad->GetSplineComponent();
	if (!ActiveSpline || ActiveSpline->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (!World)
	{
		return false;
	}

	const float MaxHeightDelta =
		OutputProperties ? OutputProperties->JunctionMaxHeightDeltaCm : 0.0f;
	if (!DrawnRoadTouchesOtherRoadOrJunction(ActiveRoad, World, MaxHeightDelta))
	{
		return false;
	}

	CookJunctionForActiveRoad(/*bFinishEditSession=*/true, /*bForceHubJunctionCook=*/true);
	ClearSelectionForDrawSession();
	return true;
}

void UMetaLandRoadInteractiveTool::CookJunctionForActiveRoad(
	bool bFinishEditSession,
	bool bForceHubJunctionCook)
{
	AMoKuEditSplineActor* FinishActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!FinishActor)
	{
		return;
	}

	UMoKuEditSplinesComponent* Comp = FinishActor->GetSplineComponent();
	if (!Comp || Comp->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	// Lane-anchor Direct skips hub cook during free extend; force=true for Ctrl+click road-hit interrupt only.
	const bool bLaneDirectConnectFinish =
		!bForceHubJunctionCook
		&& (bLaneAnchorSkipHubJunctionCook
			|| ((IsLaneAnchorDrawSession() || bRoadSpawnedFromLaneAnchorDraw)
				&& LaneDrawFinishMode == EMetaLandRoadDrawFinishMode::Direct));

	if (!bLaneDirectConnectFinish)
	{
		if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
		{
			const float MaxHeightDelta =
				OutputProperties ? OutputProperties->JunctionMaxHeightDeltaCm : 0.0f;
			RoadMode->ProcessInteractiveFinishedRoadDraw(
				FinishActor,
				/*bFullWorldRebuild=*/false,
				MaxHeightDelta);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				5.0f,
				FColor::Red,
				TEXT("Junction cook skipped: MetaLandRoad EditorMode not found"));
		}
	}

	if (PreviewComponent && PreviewComponent->GetOwner() == ActiveActor)
	{
		PreviewComponent->UnregisterComponent();
		PreviewComponent->DestroyComponent();
		PreviewComponent = nullptr;
	}

	// Refresh mesh without a second linked-junction Recook (cook above already did if needed).
	RefreshRoadMeshSuppressJunction(FinishActor);

	if (bFinishEditSession)
	{
		EndLaneExtendVisualSession(/*bClearActiveActor*/ false);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void UMetaLandRoadInteractiveTool::OnFinishedSplineEdit()
{
	if (TryCommitIdleMouthToMouthConnection())
	{
		return;
	}

	if (!IsDrawSessionActive())
	{
		return;
	}

	const bool bWasLaneAnchorDraw = IsLaneAnchorDrawSession() || bRoadSpawnedFromLaneAnchorDraw;
	const bool bContinueDrawFinish = bContinueDrawSession;
	const int32 SavedContinueDrawOriginalNumPoints = ContinueDrawOriginalNumPoints;
	const TArray<FVector> SavedGuidanceCenterline = GuidanceCenterline;
	const double SavedContinueDrawMouthS = ContinueDrawMouthS;
	const FVector SavedContinueDrawMouthCenter = ContinueDrawMouthCenter;
	const bool bSavedGuidanceInvalid = bDrawGuidanceInvalid;
	const bool bContinueDrawAttachAtStart =
		bContinueDrawFinish && StateOfEditor == EMoKuEditState::Forward;
	FMetaLandRoadDrawAnchor EndSnapAnchorCopy;
	const FMetaLandRoadDrawAnchor* EndSnapCommit = ResolveEndSnapCommitAnchor(EndSnapAnchorCopy);
	const bool bEndSnapFinish = EndSnapCommit && EndSnapCommit->IsValid();
	if (bEndSnapFinish)
	{
		EndSnapAnchorCopy = *EndSnapCommit;
	}
	const FMetaLandRoadDrawAnchor StartAnchorCopy = LaneDrawStartAnchor;

	AMoKuEditSplineActor* FinishActor = Cast<AMoKuEditSplineActor>(ActiveActor);

	EndLaneExtendVisualSession(/*bClearActiveActor*/ false);
	ContinueDrawOriginalNumPoints = SavedContinueDrawOriginalNumPoints;
	if (StartAnchorCopy.IsValid())
	{
		LaneDrawStartAnchor = StartAnchorCopy;
		bHasLaneDrawStartAnchor = true;
	}

	UMoKuEditSplinesComponent* Comp = FinishActor ? FinishActor->GetSplineComponent() : nullptr;
	const bool bCanFinishRoad = FinishActor
		&& Comp
		&& Comp->GetNumberOfSplinePoints() >= 2;

	if (ActiveActor && (GetEditState() || bCanFinishRoad))
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("Finished The Curvel's Operation!")));
		const bool bCookOnA = OutputProperties && OutputProperties->bCookJunctionOnAFinish;
		if (FinishActor && bCanFinishRoad && bEndSnapFinish && bSavedGuidanceInvalid)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					991134,
					3.0f,
					FColor::Red,
					TEXT("接缝路径无效（红线）。调整两端朝向后再按 A，不会落这条缝。"));
			}
			GEditor->EndTransaction();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports(true);
			}
			return;
		}
		if (FinishActor && bCanFinishRoad && bEndSnapFinish)
		{
			FinalizeLaneDrawEndConnection(
				FinishActor,
				EndSnapAnchorCopy,
				bContinueDrawAttachAtStart,
				bContinueDrawFinish,
				&SavedGuidanceCenterline,
				SavedContinueDrawMouthS,
				SavedContinueDrawMouthCenter);
			// Mouth connect only  - hub cook is click-on-road (unless Cook On A is enabled).
			if (bCookOnA)
			{
				CookJunctionForActiveRoad(/*bFinishEditSession=*/false, /*bForceHubJunctionCook=*/false);
			}
			else
			{
				RefreshRoadMeshSuppressJunction(FinishActor);
			}
		}
		else if (FinishActor && bCanFinishRoad)
		{
			if (bCookOnA)
			{
				CookJunctionForActiveRoad(/*bFinishEditSession=*/true, /*bForceHubJunctionCook=*/false);
			}
			else
			{
				RefreshRoadMeshSuppressJunction(FinishActor);
			}
		}
		this->Modify();

		if (Comp && FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
		{
			if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
				FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
			{
				RoadVis->ClearSectionHighlight();
			}
			MetaLandRoadLaneVisualization::RefreshLaneVisualization(Comp);
		}

		GEditor->EndTransaction();
	}

	if (bWasLaneAnchorDraw && FinishActor)
	{
		RefreshRoadMeshSuppressJunction(FinishActor);
		if (StartAnchorCopy.IsValid())
		{
			if (UMetaLandRoadSplineComponent* SourceSpline = StartAnchorCopy.SourceSpline.Get())
			{
				RefreshRoadMeshSuppressJunction(Cast<AMoKuEditSplineActor>(SourceSpline->GetOwner()));
			}
		}
		if (bEndSnapFinish && EndSnapAnchorCopy.IsValid())
		{
			if (UMetaLandRoadSplineComponent* TargetSpline = EndSnapAnchorCopy.SourceSpline.Get())
			{
				RefreshRoadMeshSuppressJunction(Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()));
			}
		}
	}

	// A = hard stop draw session (no silent re-open). Next extend: pick tip again + Ctrl.
	ResetDrawSessionState();

	if (FinishActor && Comp)
	{
		Comp->SetVisibility(true);
		Comp->SetHiddenInGame(false);
		Comp->bDrawDebug = false;
		Comp->bIsRenderPreviewMesh = false;
		Comp->bIsAddPreviewMeshPoint = false;
		ActiveActor = nullptr;
		RefreshGizmoInfo();
	}
	else
	{
		ActiveActor = nullptr;
	}

	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		RoadMode->ClearEditTarget();
	}

	FMetaLandRoadEditorModule::Get().RestoreAllRoadActorsViewportSelection();
	if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
	{
		RoadVis->ClearNativeKeySelection();
		RoadVis->EndEditing();
	}
	if (GEditor)
	{
		GEditor->SelectNone(false, true);
		GEditor->RedrawLevelEditingViewports(true);
	}
}
void UMetaLandRoadInteractiveTool::ResyncLaneExtendSessionAfterUndo()
{
	if (!bRoadSpawnedFromLaneAnchorDraw && !IsLaneAnchorDrawSession())
	{
		return;
	}

	AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMoKuEditSplinesComponent* Spline = RoadActor ? RoadActor->GetSplineComponent() : nullptr;
	if (!RoadActor || !Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		ResetDrawSessionState();
		return;
	}

	SetToolSession(EMetaLandRoadToolSession::LaneAnchor);
	bRoadSpawnedFromLaneAnchorDraw = true;
	bLaneAnchorSkipHubJunctionCook = true;
	UpdateEditState(true);

	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		if (!RoadMode->GetEditTarget().bHasPendingAnchor
			&& bHasLaneDrawStartAnchor
			&& LaneDrawStartAnchor.IsValid())
		{
			FMetaLandRoadEditTarget SourceTarget;
			SourceTarget.bHasPendingAnchor = true;
			SourceTarget.PendingAnchor = LaneDrawStartAnchor;
			SourceTarget.Spline = LaneDrawStartAnchor.SourceSpline;
			SourceTarget.RoadActor = SourceTarget.Spline.IsValid()
				? Cast<AMoKuEditSplineActor>(SourceTarget.Spline->GetOwner())
				: nullptr;
			TransferEditTargetToSpawnedBranch(RoadMode, SourceTarget, RoadActor);
		}
	}

	if (bEndSnapBundleUserLocked && LaneDrawEndSnapAnchor.IsValid())
	{
		UMetaLandRoadSplineComponent* EndSpline = LaneDrawEndSnapAnchor.SourceSpline.Get();
		if (!EndSpline || !IsValid(EndSpline->GetOwner()))
		{
			bEndSnapBundleUserLocked = false;
			bHasLaneDrawEndSnapAnchor = false;
			bHasEndSnapGhost = false;
			LaneDrawEndSnapAnchor = FMetaLandRoadDrawAnchor();
		}
		else
		{
			FVector TipLeft, TipRight, TipCenter, Outward;
			if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				EndSpline, LaneDrawEndSnapAnchor, TipLeft, TipRight, TipCenter, Outward))
			{
				LaneDrawEndSnapAnchor.SourceSpline = EndSpline;
				bHasLaneDrawEndSnapAnchor = true;
				EndSnapGhostLeft = TipLeft;
				EndSnapGhostRight = TipRight;
				ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
				ApplyGuidanceSurfaceZ(EndSnapGhostRight);
				bHasEndSnapGhost = true;
				DrawOutSplinePos = TipCenter;
				bSplineSnapActive = true;
				HitActor = EndSpline->GetOwner();
				LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
				bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
			}
			else
			{
				bEndSnapBundleUserLocked = false;
				bHasLaneDrawEndSnapAnchor = false;
				bHasEndSnapGhost = false;
			}
		}
	}

	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (World)
	{
		const FVector Probe = GuidanceLastCursor.IsNearlyZero() ? ResultHitPos : GuidanceLastCursor;
		FHitResult Hit;
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		const FVector TraceStart(Probe.X, Probe.Y, Probe.Z + 5000.0);
		const FVector TraceEnd(Probe.X, Probe.Y, Probe.Z - 5000.0);
		const bool bHitWorld = World->LineTraceSingleByObjectType(
			Hit, TraceStart, TraceEnd, QueryParams);
		const FVector HitPos = bHitWorld ? Hit.ImpactPoint : Probe;
		ResultHitPos = HitPos;
		HitActor = bHitWorld ? Hit.GetActor() : HitActor;
		UpdateLaneDrawSnapFromWorldHit(bHitWorld, HitActor, HitPos);
	}

	MetaLandRoadProfile::RefreshConnectedRoadMeshes(Spline);
	UpdateDrawGuidanceRails();
}

void UMetaLandRoadInteractiveTool::PostEditUndo()
{
	Super::PostEditUndo();

	const bool bLaneExtendActive = IsLaneAnchorDrawSession() || bRoadSpawnedFromLaneAnchorDraw;
	if (!bLaneExtendActive)
	{
		ClearDrawGuidanceRails();
		bSplineSnapActive = false;
	}
	else
	{
		ResyncLaneExtendSessionAfterUndo();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
	}

	IntersectData.TValue = 0;
	// ��ʱ���� ������Ҫ�Ż�
	for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair : GizmoListInfo)
	{
		for (auto Item : KeyValuePair.Value)
		{
			GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
		}
	}

	GizmoListInfo.Empty();
}
void UMetaLandRoadInteractiveTool::RefreshGizmoInfo()
{
	if (!ActiveActor)
	{
		return;
	}

	if (AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor))
	{
		if (UMoKuEditSplinesComponent* SplineComponent = SplineActor->GetSplineComponent())
		{
			const bool bEditing = GetEditState();
			SplineComponent->bIsRenderPreviewMesh = bEditing;
			SplineComponent->bIsAddPreviewMeshPoint = false;
			if (!bEditing)
			{
				SplineComponent->SetVisibility(true);
				SplineComponent->SetHiddenInGame(false);
			}
		}
	}
	ActiveActor->IsInit = false;
}



void USplineFinishedBehavior::Initialize(UMetaLandRoadInteractiveTool* InSplineEditTool)
{
	SplineEditTool = InSplineEditTool;
}
FInputCaptureRequest USplineFinishedBehavior::WantsCapture(const FInputDeviceState& Input)
{
	FDeviceButtonState ActiveKey = Input.Keyboard.ActiveKey;
	
	if (ActiveKey.Button == EKeys::F1 && Input.bShiftKeyDown && ActiveKey.bPressed == true)
	{
		SplineEditTool->SetHUDState();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);
	}

	return FInputCaptureRequest::Ignore();
}
FInputCaptureUpdate USplineFinishedBehavior::UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& data)
{
	return FInputCaptureUpdate::Ignore();
}
FInputCaptureUpdate USplineFinishedBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	return FInputCaptureUpdate::Ignore();
}
void USplineFinishedBehavior::ForceEndCapture(const FInputCaptureData& data)
{

}

#undef LOCTEXT_NAMESPACE
