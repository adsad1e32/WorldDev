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
#include "EngineUtils.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "LandscapeProxy.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UMetaLandRoadInteractiveTool"

namespace
{
bool IsCtrlClickRoadOrJunction(const AActor* Actor)
{
	return Actor
		&& (Actor->IsA<AMoKuEditSplineActor>() || Actor->IsA<AMoKuEditIntersectionActor>());
}

bool IsCtrlClickLandscape(const AActor* Actor)
{
	return Actor && Actor->IsA<ALandscapeProxy>();
}

/** Ray hits a road bed or passes within half-width of a centerline. Highest Ctrl priority. */
bool TryPickRoadAlongCtrlRay(
	UWorld* World,
	const FRay& ClickRay,
	AMoKuEditSplineActor*& OutRoad,
	FVector& OutCenter)
{
	OutRoad = nullptr;
	OutCenter = FVector::ZeroVector;
	if (!World)
	{
		return false;
	}

	AMoKuEditSplineActor* BedRoad = nullptr;
	FVector BedCenter = FVector::ZeroVector;
	double BedS = 0.0;
	float BedT = 0.0f;
	if (TryPickRoadBedAlongRay(World, ClickRay, BedRoad, BedCenter, BedS, BedT) && BedRoad)
	{
		OutRoad = BedRoad;
		OutCenter = BedCenter;
		return true;
	}

	AMoKuEditSplineActor* RayRoad = nullptr;
	FVector RayCenter = FVector::ZeroVector;
	double RayS = 0.0;
	float RayDist = 0.0f;
	if (TryPickClosestRoadToRay(World, ClickRay, RayRoad, RayCenter, RayS, RayDist) && RayRoad)
	{
		OutRoad = RayRoad;
		OutCenter = RayCenter;
		return true;
	}
	return false;
}

/** Road along the ray wins. No road → first landscape/road physics hit. */
bool ResolveCtrlClickWorldPos(
	UWorld* World,
	const FRay& ClickRay,
	FVector& OutWorldPos,
	AActor*& OutHitActor)
{
	OutHitActor = nullptr;
	if (!World || ClickRay.Direction.IsNearlyZero())
	{
		return false;
	}

	AMoKuEditSplineActor* RayRoad = nullptr;
	FVector RayCenter = FVector::ZeroVector;
	if (TryPickRoadAlongCtrlRay(World, ClickRay, RayRoad, RayCenter) && RayRoad)
	{
		OutHitActor = RayRoad;
		OutWorldPos = RayCenter;
		return true;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetaLandRoadCtrlClick), true);
	Params.bTraceComplex = true;

	auto PickSurface = [&OutWorldPos, &OutHitActor](const TArray<FHitResult>& Hits) -> bool
	{
		for (const FHitResult& Hit : Hits)
		{
			AActor* Actor = Hit.GetActor();
			if (IsCtrlClickRoadOrJunction(Actor) || IsCtrlClickLandscape(Actor))
			{
				OutWorldPos = Hit.ImpactPoint;
				OutHitActor = Actor;
				return true;
			}
		}
		return false;
	};

	TArray<FHitResult> Hits;
	bool bPhysicsHit = World->LineTraceMultiByChannel(
			Hits,
			ClickRay.Origin,
			ClickRay.PointAt(HALF_WORLD_MAX),
			ECC_Visibility,
			Params)
		&& PickSurface(Hits);
	if (!bPhysicsHit)
	{
		Hits.Reset();
		const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
		bPhysicsHit = World->LineTraceMultiByObjectType(
				Hits,
				ClickRay.Origin,
				ClickRay.PointAt(HALF_WORLD_MAX),
				Objects,
				Params)
			&& PickSurface(Hits);
	}
	return bPhysicsHit;
}

bool TryPickNearestLinkedRoadCenter(
	AMoKuEditIntersectionActor* Hub,
	const FVector& WorldPos,
	AMoKuEditSplineActor*& OutRoad,
	FVector& OutCenter)
{
	OutRoad = nullptr;
	OutCenter = FVector::ZeroVector;
	if (!Hub || !Hub->JunctionData)
	{
		return false;
	}
	double BestDist = TNumericLimits<double>::Max();
	for (AMoKuEditSplineActor* Linked : Hub->JunctionData->LinkedRoadActors)
	{
		USplineComponent* Spline = Linked ? Linked->GetSplineComponent() : nullptr;
		if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
		{
			continue;
		}
		const float Key = Spline->FindInputKeyClosestToWorldLocation(WorldPos);
		const FVector OnRoad = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
		const double Dist = FVector::Dist2D(WorldPos, OnRoad);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			OutRoad = Linked;
			OutCenter = OnRoad;
		}
	}
	return OutRoad != nullptr;
}

/** Idle Ctrl: click on a road / hub / road footprint → that centerline. Empty ground stays. */
bool TrySnapIdleCtrlClickOntoRoad(
	UWorld* World,
	const FRay& ClickRay,
	FVector& InOutWorldPos,
	AActor*& InOutHitActor)
{
	if (!World)
	{
		return false;
	}

	auto SnapRoad = [&InOutWorldPos, &InOutHitActor](AMoKuEditSplineActor* Road) -> bool
	{
		USplineComponent* Spline = Road ? Road->GetSplineComponent() : nullptr;
		if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
		{
			return false;
		}
		const float Key = Spline->FindInputKeyClosestToWorldLocation(InOutWorldPos);
		InOutWorldPos = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
		InOutHitActor = Road;
		return true;
	};

	AMoKuEditSplineActor* RayRoad = nullptr;
	FVector RayCenter = FVector::ZeroVector;
	if (TryPickRoadAlongCtrlRay(World, ClickRay, RayRoad, RayCenter) && RayRoad)
	{
		InOutWorldPos = RayCenter;
		return SnapRoad(RayRoad);
	}

	if (AMoKuEditSplineActor* HitRoad = Cast<AMoKuEditSplineActor>(InOutHitActor))
	{
		return SnapRoad(HitRoad);
	}

	if (AMoKuEditIntersectionActor* Hub = Cast<AMoKuEditIntersectionActor>(InOutHitActor))
	{
		AMoKuEditSplineActor* BestRoad = nullptr;
		FVector BestCenter = FVector::ZeroVector;
		if (TryPickNearestLinkedRoadCenter(Hub, InOutWorldPos, BestRoad, BestCenter) && BestRoad)
		{
			InOutWorldPos = BestCenter;
			InOutHitActor = BestRoad;
			return true;
		}
	}
	return false;
}

bool CtrlClickIsOnPendingMouth(
	UMetaLandRoadEditorMode* RoadMode,
	const FVector& ClickWorld)
{
	if (!RoadMode)
	{
		return false;
	}
	const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
	if (!Target.bHasPendingAnchor || !Target.Spline.IsValid())
	{
		return false;
	}
	FVector MouthL = FVector::ZeroVector;
	FVector MouthR = FVector::ZeroVector;
	FVector MouthC = FVector::ZeroVector;
	FVector MouthT = FVector::ZeroVector;
	if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			Target.Spline.Get(), Target.PendingAnchor, MouthL, MouthR, MouthC, MouthT))
	{
		return false;
	}
	const double MouthW = FMath::Max(FVector::Dist2D(MouthL, MouthR), 1.0);
	return FVector::Dist2D(ClickWorld, MouthC)
		<= MetaLandRoad::MouthAdsorbAcceptCm(MouthW);
}
}

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
		FVector MouthProbe = FVector::ZeroVector;
		AActor* MouthProbeActor = nullptr;
		const bool bHaveProbe = ResolveCtrlClickWorldPos(
			TargetWorld, PressPos.WorldRay, MouthProbe, MouthProbeActor);
		if (bHaveProbe && CtrlClickIsOnPendingMouth(RoadMode, MouthProbe))
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

	FHitResult Result;
	AActor* ResolvedHitActor = nullptr;
	const bool bHitWorld = ResolveCtrlDrawRayHit(TargetWorld, PressPos.WorldRay, Result, ResolvedHitActor);
	const bool bHitOtherRoadOrJunction =
		bHitWorld
		&& ResolvedHitActor
		&& ResolvedHitActor != Road
		&& (ResolvedHitActor->IsA<AMoKuEditSplineActor>()
			|| ResolvedHitActor->IsA<AMoKuEditIntersectionActor>());

	UMetaLandRoadEditorMode* ContinueRoadMode = ResolveMetaLandRoadEditorMode(this);
	const float FallbackZ = bHasGuidancePlaneZ
		? GuidancePlaneZ
		: (GuidanceLastCursor.IsNearlyZero() ? 0.0f : static_cast<float>(GuidanceLastCursor.Z));
	const FVector GuidanceCursor = ResolveGuidanceCursorForRay(
		PressPos.WorldRay,
		ContinueRoadMode,
		Cast<AMoKuEditSplineActor>(Road),
		StateOfEditor,
		GuidancePlaneZ,
		bHasGuidancePlaneZ,
		FallbackZ,
		bHitWorld ? Result.ImpactPoint : ResultHitPos,
		bHitWorld,
		TargetWorld);

	if (bHitOtherRoadOrJunction)
	{
		ResultHitPos = Result.ImpactPoint;
		ProjectPointOntoLandscapeSurface(TargetWorld, ResultHitPos, Road);
	}
	else
	{
		// Same XY as the green rails. First-hit is often this road's mesh, not the preview tip.
		ResultHitPos = GuidanceCursor;
		ResolvedHitActor = nullptr;
	}

	HitActor = ResolvedHitActor;
	if (bHitOtherRoadOrJunction)
	{
		UpdateLaneDrawSnapFromWorldHit(true, ResolvedHitActor, ResultHitPos);
	}

	// Same as Ctrl free-draw: snap to the other road centerline, then add + cook.
	AMoKuEditSplineActor* HitRoad = Cast<AMoKuEditSplineActor>(HitActor);
	AMoKuEditIntersectionActor* HitJunction = Cast<AMoKuEditIntersectionActor>(HitActor);
	const bool bHitOtherRoad = HitRoad && HitRoad != Road;
	const bool bHitExistingJunction = HitJunction != nullptr;
	const bool bInsertAtStart = (ContinueDrawState == EMoKuEditState::Forward);
	FVector PlacePos = ResultHitPos;
	FVector FromPos = FVector::ZeroVector;
	if (UMoKuEditSplinesComponent* LiveSpline = Road->GetSplineComponent())
	{
		const double LiveTipS = (ContinueDrawInsertS >= 0.0)
			? ContinueDrawInsertS
			: ContinueDrawMouthS;
		if (LiveTipS >= 0.0)
		{
			FromPos = LiveSpline->GetLocationAtDistanceAlongSpline(
				LiveTipS, ESplineCoordinateSpace::World);
		}
		else
		{
			const int32 Tip = bInsertAtStart ? 0 : (LiveSpline->GetNumberOfSplinePoints() - 1);
			FromPos = LiveSpline->GetLocationAtSplinePoint(Tip, ESplineCoordinateSpace::World);
		}
	}
	if (FromPos.IsNearlyZero())
	{
		FromPos = ContinueDrawMouthCenter;
	}
	FVector Crossing = FVector::ZeroVector;
	bool bSnappedCrossing = false;
	if (TrySnapDrawToRoadCrossing(
			Road,
			FromPos,
			ResultHitPos,
			TargetWorld,
			GetConnectionSnapDistanceCm(),
			Crossing))
	{
		PlacePos = Crossing;
		ProjectPointOntoLandscapeSurface(TargetWorld, PlacePos, Road);
		bSnappedCrossing = true;
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
	{
		FScopedTransaction ContinueAddTx(LOCTEXT("ContinueDrawAddPoint", "Continue Draw Add Point"));
		Modify();
		if (Road)
		{
			Road->Modify();
		}
		if (UMoKuEditSplinesComponent* LiveSpline = Road ? Road->GetSplineComponent() : nullptr)
		{
			LiveSpline->Modify();
		}
		const double InsertAfterS = (ContinueDrawInsertS >= 0.0)
			? ContinueDrawInsertS
			: ContinueDrawMouthS;
		AddContinueDrawSplinePoint(
			Road,
			PlacePos,
			bInsertAtStart,
			InsertAfterS,
			ContinueDrawMouthCenter,
			bHitOtherRoad || bHitExistingJunction || bSnappedCrossing || bHasEndSnapGhost);
		if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
		{
			if (bInsertAtStart)
			{
				ContinueDrawInsertS = Spline->GetDistanceAlongSplineAtSplinePoint(0);
			}
			else
			{
				ContinueDrawInsertS = Spline->GetDistanceAlongSplineAtSplinePoint(
					Spline->GetNumberOfSplinePoints() - 1);
			}
		}
	}

	if (bHitOtherRoad || bHitExistingJunction || bSnappedCrossing)
	{
		if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
		{
			Spline->bDeferRoadMeshRefresh = false;
		}
		CookJunctionForActiveRoad(/*bFinishEditSession=*/true, /*bForceHubJunctionCook=*/true);
		ClearSelectionForDrawSession();
		return true;
	}

	RefreshRoadMeshSuppressJunction(Road);
	UpdateDrawGuidanceRails();
	return true;
}

void UMetaLandRoadInteractiveTool::CommitCtrlFreeDrawAtWorld(
	const FVector& WorldPos,
	const FRotator& HitRot)
{
	if (IsLaneAnchorDrawSession())
	{
		if (AddLaneDrawSegmentFromCursor(WorldPos, HitRot, false, false, nullptr))
		{
			UpdateDrawGuidanceRails();
		}
	}
	else
	{
		AddSplinePoint(WorldPos, HitRot, false);
	}

	AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (!SplineActor)
	{
		return;
	}
	if (UMoKuEditSplinesComponent* Comp = SplineActor->GetSplineComponent();
		Comp && Comp->GetNumberOfSplinePoints() >= 2)
	{
		SplineActor->UpdateInterpolateType(Comp->GetNumberOfSplinePoints() - 2);
	}
	if (bInsertJunction)
	{
		CookJunctionForActiveRoad(/*bFinishEditSession=*/false, /*bForceHubJunctionCook=*/true);
		UpdateDrawGuidanceRails();
		return;
	}
	if (!IsLaneAnchorDrawSession() && TryCookJunctionOnCtrlPressIfCrossed())
	{
		return;
	}
	RefreshRoadMeshSuppressJunction(SplineActor);
}

bool UMetaLandRoadInteractiveTool::TryHandleCtrlClickOnRoadOrJunction(
	const FVector& WorldPos,
	const FRotator& HitRot,
	const FInputDeviceRay& PressPos,
	AActor* PressHitActor,
	AMoKuEditSplineActor* ActiveSplineActor)
{
	if (!PressHitActor || !PressHitActor->IsA<AMoKuEditBaseActor>())
	{
		return false;
	}

	AMoKuEditSplineActor* SourceActor = Cast<AMoKuEditSplineActor>(PressHitActor);
	AMoKuEditIntersectionActor* HitJunction = Cast<AMoKuEditIntersectionActor>(PressHitActor);
	FVector PressWorldPos = WorldPos;
	if (!SourceActor && HitJunction)
	{
		FVector ArmCenter = FVector::ZeroVector;
		if (TryPickNearestLinkedRoadCenter(HitJunction, PressWorldPos, SourceActor, ArmCenter)
			&& SourceActor)
		{
			PressWorldPos = ArmCenter;
		}
	}

	if (!bInsertJunction && !IsDrawSessionActive())
	{
		if (!SourceActor)
		{
			CommitCtrlFreeDrawAtWorld(PressWorldPos, HitRot);
			return true;
		}
		GetSplitIntersectionData(SourceActor, PressWorldPos);
		bInsertJunction = true;
		FVector SnapPos = PressWorldPos;
		if (USplineComponent* SourceSpline = SourceActor->GetSplineComponent())
		{
			const float ClosestKey = SourceSpline->FindInputKeyClosestToWorldLocation(PressWorldPos);
			IntersectData.TValue = SourceSpline->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);
			bSplineSnapActive = true;
			SnapPos = SourceSpline->GetLocationAtDistanceAlongSpline(
				IntersectData.TValue, ESplineCoordinateSpace::World);
		}
		PreIntersectData = IntersectData;
		PreIntersectData.PreHitActor = SourceActor;
		InsertJunctionSourceRoad = SourceActor;
		InsertJunctionWorld = SnapPos;
		if (UMetaLandRoadEditorMode* LiveMode = ResolveMetaLandRoadEditorMode(this))
		{
			if (!CtrlClickIsOnPendingMouth(LiveMode, SnapPos))
			{
				LiveMode->ClearEditTarget();
			}
		}
		// Spawn a new branch. Do not add keys onto the source road.
		ActiveActor = nullptr;
		AddSplinePoint(SnapPos, HitRot, false);
		bInsertJunction = true;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[CtrlT] start source=%s HubS=%.1f"),
			*SourceActor->GetName(),
			IntersectData.TValue);
		UpdateDrawGuidanceRails();
		return true;
	}

	if (!bInsertJunction && GetEditState())
	{
		if (IsLaneAnchorDrawSession())
		{
			SyncLaneDrawCursorFromRay(PressPos.WorldRay);
			PressWorldPos = ResultHitPos;
		}
		if (SourceActor)
		{
			GetSplitIntersectionData(SourceActor, PressWorldPos);
		}
		const FVector EndCursor = bSplineSnapActive ? FVector(DrawOutSplinePos) : PressWorldPos;

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
				return true;
			}

			if ((bHitOtherRoad || bHitExistingJunction) && !bEndSnapBundleUserLocked)
			{
				FVector PlacePos = PressWorldPos;
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
							PressWorldPos,
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
				return true;
			}

			AddLaneDrawSegmentFromCursor(EndCursor, HitRot, false, false, nullptr);
			UpdateDrawGuidanceRails();
			return true;
		}

		const bool bHitOtherRoad = SourceActor && SourceActor != ActiveSplineActor;
		const bool bHitExistingJunction = HitJunction != nullptr;
		if (bHitOtherRoad || bHitExistingJunction)
		{
			FVector PlacePos = PressWorldPos;
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
						PressWorldPos,
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
			return true;
		}

		CommitCtrlFreeDrawAtWorld(PressWorldPos, HitRot);
		return true;
	}

	if (bInsertJunction && GetEditState())
	{
		ClearSelectionForDrawSession();
		GEditor->SelectActor(ActiveActor, true, true, false, true);
		AddSplinePoint(WorldPos, HitRot, false);
		const bool bHitOtherRoad = SourceActor && SourceActor != ActiveSplineActor;
		const bool bHitExistingJunction = HitJunction != nullptr;
		if (bHitOtherRoad || bHitExistingJunction)
		{
			CookJunctionForActiveRoad(true);
			bInsertJunction = false;
		}
		else
		{
			CookJunctionForActiveRoad(/*bFinishEditSession=*/false, /*bForceHubJunctionCook=*/true);
			UpdateDrawGuidanceRails();
		}
	}
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

	if (!bCtrlModifierOn)
	{
		return;
	}

	AActor* ResolvedHitActor = nullptr;
	FVector PressWorldPos = FVector::ZeroVector;
	const bool bStartingFreeDraw =
		!IsDrawSessionActive()
		&& !IsLaneAnchorDrawSession()
		&& !bContinueDrawSession;

	if (IsLaneAnchorDrawSession())
	{
		SyncLaneDrawCursorFromRay(PressPos.WorldRay);
		PressWorldPos = ResultHitPos;
	}
	else if (!ResolveCtrlClickWorldPos(
		TargetWorld, PressPos.WorldRay, PressWorldPos, ResolvedHitActor))
	{
		return;
	}
	else
	{
		if (TrySnapIdleCtrlClickOntoRoad(
				TargetWorld, PressPos.WorldRay, PressWorldPos, ResolvedHitActor))
		{
			bSplineSnapActive = true;
		}
		else if (!IsCtrlClickRoadOrJunction(ResolvedHitActor))
		{
			ProjectPointOntoLandscapeSurface(
				TargetWorld, PressWorldPos, Cast<AMoKuEditSplineActor>(ActiveActor));
			bSplineSnapActive = false;
		}
		if (bStartingFreeDraw || !bHasGuidancePlaneZ)
		{
			SetGuidancePlaneFromMouthCenter(PressWorldPos);
		}
		if (bStartingFreeDraw && !IsCtrlClickRoadOrJunction(ResolvedHitActor))
		{
			FEndSnapTargetLanePick MouthPick;
			if (FindEndSnapTargetLanePick(
					TargetWorld,
					nullptr,
					PressWorldPos,
					GetConnectionSnapDistanceCm(),
					MouthPick)
				&& MouthPick.Spline)
			{
				FVector MouthL, MouthR, MouthC;
				if (MouthPick.Spline->GetRoadEdgePointsAtS(
						MouthPick.MouthS, MouthL, MouthR, MouthC, ESplineCoordinateSpace::World))
				{
					PressWorldPos = MouthC;
					ResolvedHitActor = MouthPick.Spline->GetOwner();
				}
			}
		}
	}

	if (!bStartingFreeDraw
		&& GetEditState()
		&& !IsLaneAnchorDrawSession()
		&& !bContinueDrawSession
		&& bHasEndSnapGhost)
	{
		PressWorldPos = (EndSnapGhostLeft + EndSnapGhostRight) * 0.5;
		if (LaneDrawEndSnapAnchor.SourceSpline.IsValid())
		{
			ResolvedHitActor = LaneDrawEndSnapAnchor.SourceSpline->GetOwner();
		}
	}

	ResultHitPos = PressWorldPos;
	HitActor = ResolvedHitActor;
	const FRotator HitRot = FRotator::ZeroRotator;

	if (bContinueDrawSession && GetEditState())
	{
		return;
	}

	AMoKuEditSplineActor* PickedRoad = Cast<AMoKuEditSplineActor>(ResolvedHitActor);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CtrlT] click session=%d insert=%d lane=%d hit=%s"),
		IsDrawSessionActive() ? 1 : 0,
		bInsertJunction ? 1 : 0,
		IsLaneAnchorDrawSession() ? 1 : 0,
		ResolvedHitActor ? *ResolvedHitActor->GetName() : TEXT("null"));

	if (PickedRoad
		&& !bInsertJunction
		&& !IsLaneAnchorDrawSession()
		&& !bContinueDrawSession)
	{
		if (IsDrawSessionActive())
		{
			ResetDrawSessionState();
		}
		GetSplitIntersectionData(PickedRoad, PressWorldPos);
		FVector SnapPos = PressWorldPos;
		if (USplineComponent* SourceSpline = PickedRoad->GetSplineComponent())
		{
			const float ClosestKey = SourceSpline->FindInputKeyClosestToWorldLocation(PressWorldPos);
			IntersectData.TValue = SourceSpline->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);
			bSplineSnapActive = true;
			SnapPos = SourceSpline->GetLocationAtDistanceAlongSpline(
				IntersectData.TValue, ESplineCoordinateSpace::World);
		}
		InsertJunctionSourceRoad = PickedRoad;
		InsertJunctionWorld = SnapPos;
		bInsertJunction = true;
		if (UMetaLandRoadEditorMode* LiveMode = ResolveMetaLandRoadEditorMode(this))
		{
			if (!CtrlClickIsOnPendingMouth(LiveMode, SnapPos))
			{
				LiveMode->ClearEditTarget();
			}
		}
		ActiveActor = nullptr;
		AddSplinePoint(SnapPos, HitRot, false);
		bInsertJunction = true;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[CtrlT] start source=%s HubS=%.1f"),
			*PickedRoad->GetName(),
			IntersectData.TValue);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				4.0f,
				FColor::Cyan,
				FString::Printf(
					TEXT("[CtrlT] snap %s  S=%.0f"),
					*PickedRoad->GetName(),
					IntersectData.TValue));
		}
		UpdateDrawGuidanceRails();
		return;
	}

	if (TryHandleCtrlClickOnRoadOrJunction(
		PressWorldPos,
		HitRot,
		PressPos,
		ResolvedHitActor,
		Cast<AMoKuEditSplineActor>(ActiveActor)))
	{
		return;
	}

	if (bInsertJunction && GetEditState() && !IsLaneAnchorDrawSession() && !bContinueDrawSession)
	{
		const bool bHitOtherRoad = ResolvedHitActor
			&& ResolvedHitActor->IsA<AMoKuEditSplineActor>()
			&& ResolvedHitActor != ActiveActor;
		const bool bHitExistingJunction = ResolvedHitActor
			&& ResolvedHitActor->IsA<AMoKuEditIntersectionActor>();
		if (bHitOtherRoad || bHitExistingJunction)
		{
			ClearSelectionForDrawSession();
			AddSplinePoint(PressWorldPos, HitRot, false);
			CookJunctionForActiveRoad(true, true);
			bInsertJunction = false;
			return;
		}
		CommitCtrlFreeDrawAtWorld(PressWorldPos, HitRot);
		return;
	}

	CommitCtrlFreeDrawAtWorld(PressWorldPos, HitRot);
}

#undef LOCTEXT_NAMESPACE
