#include "MetaLandRoadInteractiveTool.h"
#include "MetaLandRoadInteractiveToolDraw.h"
#include "MetaLandRoadEditorMode.h"
#include "MetaLandRoadLaneEditPick.h"
#include "MetaLandRoadSplineComponent.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditBaseActor.h"
#include "CollisionQueryParams.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UMetaLandRoadInteractiveTool"

void UMetaLandRoadInteractiveTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bShiftPick =
		ModKeys.IsShiftDown()
		&& !IsDrawSessionActive();
	const bool bLaneExtendCtrl =
		bCtrlModifierOn
		&& RoadMode
		&& RoadMode->GetEditTarget().bHasPendingAnchor
		&& !IsDrawSessionActive()
		&& !bContinueDrawSession;
	// Shift without Ctrl = mouth pick. Shift+Ctrl with a pending source mouth = start extend.
	if (!bLaneExtendCtrl && (!bCtrlModifierOn || bShiftPick))
	{
		HandleClickPressWithoutCtrl(PressPos);
		return;
	}
	const bool bLaneExtendPending =
		RoadMode
		&& RoadMode->GetEditTarget().bHasPendingAnchor
		&& !IsLaneAnchorDrawSession()
		&& !bContinueDrawSession;
	if (bLaneExtendPending)
	{
		if (!bContinueDrawSession)
		{
			ClearContinueDrawMouthArm();
		}
		if (HandleClickPressLaneAnchorStart(PressPos, RoadMode))
		{
			return;
		}
	}
	if (!bLaneExtendPending && HasLiveContinueDrawTarget())
	{
		if (HandleClickPressContinueDraw(PressPos))
		{
			return;
		}
	}
	else if (!bLaneExtendPending && !bContinueDrawMouthArmed)
	{
		AbandonStaleContinueDraw();
	}
	if (HandleClickPressLaneAnchorStart(PressPos, RoadMode))
	{
		return;
	}

	HandleClickPressDrawOrExtend(PressPos, RoadMode);
}

void UMetaLandRoadInteractiveTool::HandleClickPressWithoutCtrl(const FInputDeviceRay& PressPos)
{
	const FModifierKeysState& ContinueMods = FSlateApplication::Get().GetModifierKeys();
	if (!ContinueMods.IsShiftDown() && !ContinueMods.IsAltDown())
	{
		if (TryHandleContinueDrawClick(PressPos))
		{
			return;
		}
	}

	const FModifierKeysState& MixModKeys = FSlateApplication::Get().GetModifierKeys();
	UMetaLandRoadEditorMode* MixRoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bHasSourceMouth =
		IsLaneAnchorDrawSession()
		|| bContinueDrawSession
		|| (MixRoadMode && MixRoadMode->GetEditTarget().bHasPendingAnchor);
	const bool bTargetMixClick =
		bHasSourceMouth
		&& MixModKeys.IsShiftDown()
		&& !MixModKeys.IsControlDown();
	if (bTargetMixClick)
	{
		const bool bAddOnly = MixModKeys.IsAltDown();
		if (TryHandleEndSnapTargetLaneMixPick(PressPos, bAddOnly))
		{
			return;
		}
		AMoKuEditSplineActor* SourceRoad = MixRoadMode
			? MixRoadMode->GetEditTarget().RoadActor.Get()
			: nullptr;
		FHitResult PressHit;
		AActor* PressHitActor = nullptr;
		ResolveCtrlDrawRayHit(TargetWorld, PressPos.WorldRay, PressHit, PressHitActor);
		if (PressHitActor
			&& PressHitActor->IsA<AMoKuEditSplineActor>()
			&& PressHitActor != ActiveActor
			&& PressHitActor != SourceRoad)
		{
			return;
		}
	}

	if (!IsDrawSessionActive() && !bHasEndSnapGhost && !bHasSourceMouth)
	{
		return;
	}

	if (!MixModKeys.IsShiftDown())
	{
		return;
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return;
	}

	FIntPoint MousePos;
	ViewportClient->Viewport->GetMousePos(MousePos);
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return;
	}

	const FViewportClick Click(
		View,
		ViewportClient,
		EKeys::LeftMouseButton,
		IE_Pressed,
		MousePos.X,
		MousePos.Y);
	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	if (!RoadMode)
	{
		return;
	}

	if (IsLaneAnchorDrawSession() && HitActor && HitActor->IsA<AMoKuEditSplineActor>()
		&& HitActor != ActiveActor)
	{
		const FModifierKeysState& PickMods = FSlateApplication::Get().GetModifierKeys();
		if (PickMods.IsAltDown() || PickMods.IsShiftDown())
		{
			return;
		}
	}
	if (!RoadMode->TryHandleLaneAnchorPickClick(ViewportClient, Click))
	{
		return;
	}

	FVector HitPos;
	if (FindRayHit(PressPos.WorldRay, HitPos).bHit)
	{
		ResultHitPos = HitPos;
	}
	LastGuidanceHoverSamplePos = FVector::ZeroVector;
	bSplineSnapActive = false;
	if (RoadMode->GetEditTarget().bHasPendingAnchor)
	{
		SyncAnchorPreviewFromEditTarget(RoadMode->GetEditTarget());
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

bool UMetaLandRoadInteractiveTool::HandleClickPressLaneAnchorStart(
	const FInputDeviceRay& PressPos,
	UMetaLandRoadEditorMode* RoadMode)
{
	const bool bHasLaneAnchor = RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor;
	if (!bHasLaneAnchor || GetEditState())
	{
		return false;
	}

	const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
	UMetaLandRoadSplineComponent* AnchorSpline = Target.Spline.Get();
	AMoKuEditSplineActor* TargetRoad = Target.RoadActor.Get();
	if (!AnchorSpline)
	{
		return true;
	}

	if (!IsLaneAnchorDrawSession()
		&& TryResumeLaneExtendOnBranchRoad(Target, TargetRoad, AnchorSpline, PressPos))
	{
		return true;
	}

	if (IsLaneAnchorDrawSession()
		&& ActiveActor
		&& TargetRoad
		&& ActiveActor == TargetRoad
		&& AnchorSpline->GetNumberOfSplinePoints() >= 2)
	{
		FScopedTransaction ResumeTx(LOCTEXT("ResumeLaneExtend", "Resume Lane Extend"));
		UpdateEditState(true);
		SyncLaneDrawCursorFromRay(PressPos.WorldRay);
		const FRotator HitRot = FRotationMatrix::MakeFromX(
			(ResultHitPos - GuidanceLastCursor).GetSafeNormal2D()).Rotator();
		if (!AddLaneDrawSegmentFromCursor(ResultHitPos, HitRot, false, false, nullptr))
		{
			AddSplinePoint(ResultHitPos, HitRot, false);
		}
		UpdateDrawGuidanceRails();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return true;
	}

	FTransform MouthTransform = FTransform::Identity;
	if (!AnchorSpline->EvalMouthTransform(Target.PendingAnchor, MouthTransform))
	{
		return true;
	}

	FScopedTransaction Transaction(LOCTEXT("BasicAddPoint", "Basic Add Point"));
	FHitResult Result;
	FCollisionQueryParams QueryCollisionParams(FName("WorldCollision"), true);
	const bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(
		Result,
		PressPos.WorldRay.Origin,
		PressPos.WorldRay.PointAt(HALF_WORLD_MAX),
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
		QueryCollisionParams);

	FVector MouthLeft = FVector::ZeroVector;
	FVector MouthRight = FVector::ZeroVector;
	FVector MouthCenter = FVector::ZeroVector;
	FVector MouthTangent = FVector::ZeroVector;
	FVector MouthLoc = MouthTransform.GetLocation();
	FVector MouthForward = MouthTransform.GetRotation().GetForwardVector().GetSafeNormal2D();
	float MouthWidthCm = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);
	if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		AnchorSpline, Target.PendingAnchor, MouthLeft, MouthRight, MouthCenter, MouthTangent))
	{
		MouthLoc = (MouthLeft + MouthRight) * 0.5;
		MouthWidthCm = FMath::Max(
			static_cast<float>(FVector::Dist2D(MouthLeft, MouthRight)),
			MouthWidthCm);
		const FVector Across = (MouthRight - MouthLeft).GetSafeNormal2D();
		if (!Across.IsNearlyZero())
		{
			FVector FromBar = FVector::CrossProduct(FVector::UpVector, Across).GetSafeNormal2D();
			if (FVector::DotProduct(FromBar, MouthForward) < 0.0)
			{
				FromBar = -FromBar;
			}
			if (!FromBar.IsNearlyZero())
			{
				MouthForward = FromBar;
			}
		}
		else if (!MouthTangent.IsNearlyZero())
		{
			MouthForward = MouthTangent.GetSafeNormal2D();
		}
	}
	(void)MouthWidthCm;
	SetGuidancePlaneFromMouthCenter(MouthLoc);
	const FVector CursorForExtend = ResolveGuidanceCursorForRay(
		PressPos.WorldRay,
		RoadMode,
		nullptr,
		StateOfEditor,
		GuidancePlaneZ,
		bHasGuidancePlaneZ,
		static_cast<float>(MouthLoc.Z),
		bHitWorld ? Result.ImpactPoint : MouthLoc + MouthForward * 500.0f,
		bHitWorld,
		TargetWorld);
	const FRotator HitRot = MouthForward.Rotation();

	ActiveActor = nullptr;
	AddSplinePoint(MouthLoc, HitRot, false);
	FLaneExtendCenterPlan SpawnPlan;
	ResolveLaneExtendCenterPlan(MouthLoc, MouthForward, CursorForExtend, true, SpawnPlan);
	SpawnPlan.StartCenter = MouthLoc;
	SpawnPlan.StartTangent = MouthForward;
	if (SpawnPlan.bUseHermite)
	{
		TArray<FVector> RampKeys;
		RampKeys.Add(SpawnPlan.EndCenter);
		AppendLaneDrawKeysToActiveSpline(RampKeys, SpawnPlan, HitRot, /*bPinConnectionEnd*/ false);
	}
	else
	{
		AddSplinePoint(SpawnPlan.EndCenter, HitRot, false);
	}

	LaneDrawStartAnchor = Target.PendingAnchor;
	bHasLaneDrawStartAnchor = LaneDrawStartAnchor.IsValid();

	if (AMoKuEditSplineActor* SpawnedRoad = Cast<AMoKuEditSplineActor>(ActiveActor))
	{
		FinalizeLaneDrawFromConnection(SpawnedRoad, LaneDrawStartAnchor);
	}

	bSplineSnapActive = false;
	bInsertJunction = false;
	SetToolSession(EMetaLandRoadToolSession::LaneAnchor);
	bRoadSpawnedFromLaneAnchorDraw = true;
	bLaneAnchorSkipHubJunctionCook = true;
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	UpdateEditState(true);
	GuidanceLastCursor = CursorForExtend;
	GuidanceMoveDir = (CursorForExtend - MouthLoc).GetSafeNormal2D();
	SetGuidancePlaneFromMouthCenter(MouthLoc);
	if (RoadMode)
	{
		TransferEditTargetToSpawnedBranch(RoadMode, Target, Cast<AMoKuEditSplineActor>(ActiveActor));
	}
	UpdateDrawGuidanceRails();
	{
		AActor* FirstHitActor = nullptr;
		FHitResult FirstHit;
		if (ResolveCtrlDrawRayHit(TargetWorld, PressPos.WorldRay, FirstHit, FirstHitActor))
		{
			AMoKuEditSplineActor* HitRoad = Cast<AMoKuEditSplineActor>(FirstHitActor);
			const bool bHitOtherRoad = HitRoad && HitRoad != ActiveActor && HitRoad != TargetRoad;
			const bool bHitExistingJunction = FirstHitActor->IsA<AMoKuEditIntersectionActor>();
			if (bHitOtherRoad || bHitExistingJunction)
			{
				CookJunctionForActiveRoad(/*bFinishEditSession=*/true, /*bForceHubJunctionCook=*/true);
				ClearSelectionForDrawSession();
				return true;
			}
		}
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
	return true;
}

bool UMetaLandRoadInteractiveTool::HandleClickPressContinueDraw(const FInputDeviceRay& PressPos)
{
	if (!bCtrlModifierOn)
	{
		return false;
	}

	if (!bContinueDrawSession)
	{
		return false;
	}

	AMoKuEditSplineActor* Road = ContinueDrawRoad.Get();
	if (!Road)
	{
		Road = Cast<AMoKuEditSplineActor>(ActiveActor);
	}
	if (!Road)
	{
		AbandonStaleContinueDraw();
		return false;
	}

	ActiveActor = Road;
	StateOfEditor = ContinueDrawState;
	UpdateEditState(true);
	bContinueDrawSession = true;
	if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
	{
		Spline->bDeferRoadMeshRefresh = true;
	}

	FHitResult Result;
	AActor* ResolvedHitActor = nullptr;
	const bool bHitWorld = ResolveCtrlDrawRayHit(TargetWorld, PressPos.WorldRay, Result, ResolvedHitActor);
	if (bHitWorld)
	{
		ResultHitPos = Result.ImpactPoint;
		ProjectPointOntoLandscapeSurface(
			TargetWorld, ResultHitPos, Cast<AMoKuEditSplineActor>(ActiveActor));
	}
	else
	{
		FVector PlaneCursor = ResultHitPos;
		if (ResolveGuidanceCursorOnPlane(PressPos.WorldRay, GuidancePlaneZ, PlaneCursor))
		{
			ResultHitPos = PlaneCursor;
		}
	}
	if (bHasGuidancePlaneZ)
	{
		ResultHitPos.Z = GuidancePlaneZ;
	}

	HitActor = ResolvedHitActor;
	UpdateLaneDrawSnapFromWorldHit(bHitWorld, ResolvedHitActor, ResultHitPos);

	// Same as Ctrl free-draw: snap to the other road centerline, then add + cook.
	AMoKuEditSplineActor* HitRoad = Cast<AMoKuEditSplineActor>(HitActor);
	AMoKuEditIntersectionActor* HitJunction = Cast<AMoKuEditIntersectionActor>(HitActor);
	const bool bHitOtherRoad = HitRoad && HitRoad != Road;
	const bool bHitExistingJunction = HitJunction != nullptr;
	const bool bInsertAtStart = (ContinueDrawState == EMoKuEditState::Forward);
	FVector PlacePos = ResultHitPos;
	FVector FromPos = ContinueDrawMouthCenter;
	if (FromPos.IsNearlyZero())
	{
		if (UMoKuEditSplinesComponent* LiveSpline = Road->GetSplineComponent())
		{
			if (ContinueDrawMouthS >= 0.0)
			{
				FromPos = LiveSpline->GetLocationAtDistanceAlongSpline(
					ContinueDrawMouthS, ESplineCoordinateSpace::World);
			}
			else
			{
				const int32 Tip = bInsertAtStart ? 0 : (LiveSpline->GetNumberOfSplinePoints() - 1);
				FromPos = LiveSpline->GetLocationAtSplinePoint(Tip, ESplineCoordinateSpace::World);
			}
		}
	}
	FVector Crossing = FVector::ZeroVector;
	if (TryApplyJudgedDrawCrossing(PlacePos))
	{
		ProjectPointOntoLandscapeSurface(TargetWorld, PlacePos, Road);
	}
	else if (TrySnapDrawToRoadCrossing(
			Road,
			FromPos,
			ResultHitPos,
			TargetWorld,
			GetConnectionSnapDistanceCm(),
			Crossing))
	{
		PlacePos = Crossing;
		ProjectPointOntoLandscapeSurface(TargetWorld, PlacePos, Road);
	}
	else if (bHitOtherRoad && HitRoad->GetSplineComponent())
	{
		GetSplitIntersectionData(HitRoad, ResultHitPos);
		if (bSplineSnapActive)
		{
			PlacePos = HitRoad->GetSplineComponent()->GetLocationAtDistanceAlongSpline(
				IntersectData.TValue, ESplineCoordinateSpace::World);
		}
	}
	AddContinueDrawSplinePoint(
		Road,
		PlacePos,
		bInsertAtStart,
		ContinueDrawMouthS,
		ContinueDrawMouthCenter);
	if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
	{
		Spline->bDeferRoadMeshRefresh = true;
		const double OldMouthS = ContinueDrawMouthS;
		if (bInsertAtStart)
		{
			ContinueDrawMouthS = Spline->GetDistanceAlongSplineAtSplinePoint(0);
		}
		else if (OldMouthS >= 0.0)
		{
			for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
			{
				const double S = Spline->GetDistanceAlongSplineAtSplinePoint(i);
				if (S > OldMouthS + 0.01)
				{
					ContinueDrawMouthS = S;
					break;
				}
			}
		}
		else
		{
			ContinueDrawMouthS = Spline->GetDistanceAlongSplineAtSplinePoint(
				Spline->GetNumberOfSplinePoints() - 1);
		}
		ContinueDrawMouthCenter = Spline->GetLocationAtDistanceAlongSpline(
			FMath::Max(ContinueDrawMouthS, 0.0), ESplineCoordinateSpace::World);
	}

	if (bHitOtherRoad || bHitExistingJunction)
	{
		if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
		{
			Spline->bDeferRoadMeshRefresh = false;
		}
		CookJunctionForActiveRoad(/*bFinishEditSession=*/true);
		ClearSelectionForDrawSession();
		return true;
	}

	// A owns mesh cook. Recooking each C-click hitch + half-drawn 折角面.
	UpdateDrawGuidanceRails();
	return true;
}

void UMetaLandRoadInteractiveTool::HandleClickPressDrawOrExtend(
	const FInputDeviceRay& PressPos,
	UMetaLandRoadEditorMode* RoadMode)
{
	if (bContinueDrawSession && !IsLaneAnchorDrawSession() && HasLiveContinueDrawTarget())
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("BasicAddPoint", "Basic Add Point"));
	FHitResult Result;
	AActor* ResolvedHitActor = nullptr;
	const bool bHitWorld = ResolveCtrlDrawRayHit(TargetWorld, PressPos.WorldRay, Result, ResolvedHitActor);
	if (!bCtrlModifierOn)
	{
		return;
	}

	AMoKuEditSplineActor* ActiveSplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (IsLaneAnchorDrawSession()
		|| (RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor))
	{
		SyncLaneDrawCursorFromRay(PressPos.WorldRay);
	}
	else if (bHitWorld)
	{
		ResultHitPos = Result.ImpactPoint;
		ProjectPointOntoLandscapeSurface(
			TargetWorld, ResultHitPos, Cast<AMoKuEditSplineActor>(ActiveActor));
		const bool bStartingFreeDraw =
			!IsDrawSessionActive()
			&& !IsLaneAnchorDrawSession()
			&& !bContinueDrawSession;
		if (bStartingFreeDraw || !bHasGuidancePlaneZ)
		{
			SetGuidancePlaneFromMouthCenter(ResultHitPos);
		}
	}
	HitActor = ResolvedHitActor;
	const FVector3d HitPos = ResultHitPos;
	FRotator HitRot = FRotator::ZeroRotator;

	if (bContinueDrawSession && GetEditState())
	{
		return;
	}

	if (HitActor && HitActor->IsA<AMoKuEditBaseActor>())
	{
		AMoKuEditSplineActor* SourceActor = Cast<AMoKuEditSplineActor>(HitActor);
		AMoKuEditIntersectionActor* HitJunction = Cast<AMoKuEditIntersectionActor>(HitActor);
		if (!bInsertJunction && !IsDrawSessionActive())
		{
			ActiveActor = SourceActor;
			GetSplitIntersectionData(ActiveActor, ResultHitPos);
			PreIntersectData = IntersectData;
			PreIntersectData.PreHitActor = Cast<AMoKuEditBaseActor>(HitActor);
			bInsertJunction = true;
			if (bSplineSnapActive)
			{
				UMoKuEditSplinesComponent* SplineComp = SourceActor->GetSplineComponent();
				FTransform InSertTransform = SplineComp->GetTransformAtDistanceAlongSpline(
					IntersectData.TValue, ESplineCoordinateSpace::World);
				AddSplinePoint(InSertTransform.GetLocation(), HitRot, false);
			}
			else
			{
				AddSplinePoint(ResultHitPos, HitRot, false);
			}
			return;
		}

		if (!bInsertJunction && GetEditState())
		{
			SyncLaneDrawCursorFromRay(PressPos.WorldRay);
			if (SourceActor)
			{
				GetSplitIntersectionData(SourceActor, ResultHitPos);
			}
			const FVector EndCursor = bSplineSnapActive ? FVector(DrawOutSplinePos) : ResultHitPos;

			if (IsLaneAnchorDrawSession())
			{
				const bool bHitOtherRoad = SourceActor && SourceActor != ActiveSplineActor;
				const bool bHitExistingJunction = HitJunction != nullptr;
				const bool bHitMixTarget = bEndSnapBundleUserLocked
					&& LaneDrawEndSnapAnchor.IsValid()
					&& SourceActor
					&& LaneDrawEndSnapAnchor.SourceSpline.Get()
					&& LaneDrawEndSnapAnchor.SourceSpline.Get()->GetOwner() == SourceActor;

				if (bHitMixTarget)
				{
					AddLaneDrawSegmentFromCursor(EndCursor, HitRot, false, false, nullptr);
					UpdateDrawGuidanceRails();
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(
							991123,
							2.0f,
							FColor::Cyan,
							TEXT("[MetaLandRoad] End mix locked — press A to connect (Ctrl does not confirm)"));
					}
					return;
				}

				if ((bHitOtherRoad || bHitExistingJunction) && !bEndSnapBundleUserLocked)
				{
					FVector PlacePos = ResultHitPos;
					if (ActiveSplineActor && ActiveSplineActor->GetSplineComponent())
					{
						const int32 Last = ActiveSplineActor->GetSplineComponent()->GetNumberOfSplinePoints() - 1;
						const FVector From = ActiveSplineActor->GetSplineComponent()->GetLocationAtSplinePoint(
							FMath::Max(Last, 0), ESplineCoordinateSpace::World);
						FVector Crossing = FVector::ZeroVector;
						if (!TryApplyJudgedDrawCrossing(PlacePos)
							&& TrySnapDrawToRoadCrossing(
								ActiveSplineActor,
								From,
								ResultHitPos,
								TargetWorld,
								GetConnectionSnapDistanceCm(),
								Crossing))
						{
							PlacePos = Crossing;
						}
						else if (!bHasDrawCrossing
							&& bHitOtherRoad
							&& SourceActor->GetSplineComponent()
							&& bSplineSnapActive)
						{
							PlacePos = SourceActor->GetSplineComponent()->GetLocationAtDistanceAlongSpline(
								IntersectData.TValue, ESplineCoordinateSpace::World);
						}
					}
					AddLaneDrawSegmentFromCursor(PlacePos, HitRot, false, false, nullptr);

					CookJunctionForActiveRoad(true, true);
					ClearSelectionForDrawSession();
					return;
				}

				if (AddLaneDrawSegmentFromCursor(EndCursor, HitRot, false, false, nullptr))
				{
					UpdateDrawGuidanceRails();
				}
				return;
			}

			const bool bHitOtherRoad = SourceActor && SourceActor != ActiveSplineActor;
			const bool bHitExistingJunction = HitJunction != nullptr;
			if (bHitOtherRoad || bHitExistingJunction)
			{
				FVector PlacePos = ResultHitPos;
				if (ActiveSplineActor && ActiveSplineActor->GetSplineComponent())
				{
					const int32 Last = ActiveSplineActor->GetSplineComponent()->GetNumberOfSplinePoints() - 1;
					const FVector From = ActiveSplineActor->GetSplineComponent()->GetLocationAtSplinePoint(
						FMath::Max(Last, 0), ESplineCoordinateSpace::World);
					FVector Crossing = FVector::ZeroVector;
					if (!TryApplyJudgedDrawCrossing(PlacePos)
						&& TrySnapDrawToRoadCrossing(
							ActiveSplineActor,
							From,
							ResultHitPos,
							TargetWorld,
							GetConnectionSnapDistanceCm(),
							Crossing))
					{
						PlacePos = Crossing;
					}
					else if (bHitOtherRoad && SourceActor && SourceActor->GetSplineComponent() && bSplineSnapActive)
					{
						PlacePos = SourceActor->GetSplineComponent()->GetLocationAtDistanceAlongSpline(
							IntersectData.TValue, ESplineCoordinateSpace::World);
					}
				}
				AddSplinePoint(PlacePos, HitRot, false);

				CookJunctionForActiveRoad(true);
				ClearSelectionForDrawSession();
				return;
			}

			AddSplinePoint(ResultHitPos, HitRot, false);
			UpdateDrawGuidanceRails();
			return;
		}

		if (bInsertJunction && GetEditState())
		{
			ClearSelectionForDrawSession();
			GEditor->SelectActor(ActiveActor, true, true, false, true);
			AddSplinePoint(HitPos, HitRot, false);

			const bool bHitOtherRoad = SourceActor && SourceActor != ActiveSplineActor;
			const bool bHitExistingJunction = HitJunction != nullptr;
			if (bHitOtherRoad || bHitExistingJunction)
			{
				CookJunctionForActiveRoad(true);
			}
			else
			{
				TryCookJunctionOnCtrlPressIfCrossed();
			}
			bInsertJunction = false;
		}
		return;
	}

	if (HitActor && !HitActor->IsA<AMoKuEditSplineActor>() && bInsertJunction)
	{
		ClearSelectionForDrawSession();
		AddSplinePoint(HitPos, HitRot, false);
		bInsertJunction = false;
		TryCookJunctionOnCtrlPressIfCrossed();
		return;
	}

	if ((!HitActor || !HitActor->IsA<AMoKuEditSplineActor>()) && !bInsertJunction)
	{
		SyncLaneDrawCursorFromRay(PressPos.WorldRay);
		const FVector ClickPos = ResultHitPos;
		if (IsLaneAnchorDrawSession())
		{
			if (AddLaneDrawSegmentFromCursor(ClickPos, HitRot, false, false, nullptr))
			{
				UpdateDrawGuidanceRails();
			}
		}
		else
		{
			AddSplinePoint(ClickPos, HitRot, false);
		}
		if (AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor))
		{
			if (UMoKuEditSplinesComponent* Comp = SplineActor->GetSplineComponent();
				Comp && Comp->GetNumberOfSplinePoints() >= 2)
			{
				const int32 PointIndex = Comp->GetNumberOfSplinePoints() - 2;
				SplineActor->UpdateInterpolateType(PointIndex);
			}
			if (!IsLaneAnchorDrawSession() && TryCookJunctionOnCtrlPressIfCrossed())
			{
				return;
			}
			RefreshRoadMeshSuppressJunction(SplineActor);
		}
	}
}

#undef LOCTEXT_NAMESPACE

#undef LOCTEXT_NAMESPACE
