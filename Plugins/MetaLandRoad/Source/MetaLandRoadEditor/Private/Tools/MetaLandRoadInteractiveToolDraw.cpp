#include "MetaLandRoadInteractiveToolDraw.h"

#include "MetaLandRoadInteractiveTool.h"
#include "MetaLandRoadEditorMode.h"
#include "MetaLandRoadLaneEditPick.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditorUtils.h"
#include "CanvasTypes.h"
#include "CollisionQueryParams.h"
#include "Curves/RichCurve.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "SceneManagement.h"
#include "Tools/UEdMode.h"

UMetaLandRoadEditorMode* ResolveMetaLandRoadEditorMode(const UMetaLandRoadInteractiveTool* Tool)
{
	if (Tool && Tool->GetCachedRoadEditorMode())
	{
		return Tool->GetCachedRoadEditorMode();
	}

	if (UMetaLandRoadEditorMode* RoadMode =
		MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(
			UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId))
	{
		return RoadMode;
	}

	if (UEdMode* ActiveMode = GLevelEditorModeTools().GetActiveScriptableMode(
		UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId))
	{
		return Cast<UMetaLandRoadEditorMode>(ActiveMode);
	}

	return nullptr;
}

bool IsEndSnapMixAddModifierDown()
{
	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	return ModKeys.IsAltDown() && !ModKeys.IsControlDown();
}

bool IsEndSnapMixToggleModifierDown()
{
	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	return ModKeys.IsShiftDown() && !ModKeys.IsControlDown();
}

int32 CountDrawAnchorBundleLanes(const FMetaLandRoadDrawAnchor& Anchor)
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
	return Count;
}

void CollectSortedBundleLaneIndices(
	const FMetaLandRoadDrawAnchor& Anchor,
	TArray<int32>& OutLaneIndices)
{
	OutLaneIndices.Reset();
	if (!Anchor.IsValid())
	{
		return;
	}
	OutLaneIndices.Add(Anchor.LaneIndex);
	for (const int32 BundledLaneIndex : Anchor.BundledLaneIndices)
	{
		OutLaneIndices.AddUnique(BundledLaneIndex);
	}
	OutLaneIndices.Sort([](int32 A, int32 B)
	{
		return FMath::Abs(A) < FMath::Abs(B);
	});
}

/** Match RefreshDynamicRoadMesh default surface lift. */
// MetaRoad-like: tiny lift so PDI is visible; large values make rails look like a floating tower.

bool FindEndSnapTargetLanePick(
	UWorld* World,
	AMoKuEditSplineActor* ActiveRoad,
	const FVector& CursorWorld,
	float SnapDistCm,
	FEndSnapTargetLanePick& OutPick)
{
	OutPick = FEndSnapTargetLanePick();
	if (!World)
	{
		return false;
	}

	double BestDistSq = TNumericLimits<double>::Max();

	for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
	{
		AMoKuEditSplineActor* RoadActor = *It;
		if (!IsValid(RoadActor) || RoadActor == ActiveRoad)
		{
			continue;
		}
		UMetaLandRoadSplineComponent* SnapSpline =
			Cast<UMetaLandRoadSplineComponent>(RoadActor->GetSplineComponent());
		if (!IsValid(SnapSpline) || SnapSpline->GetNumberOfSplinePoints() < 2)
		{
			continue;
		}

		SnapSpline->GetRoadLayoutMutable().UpdateLayout(SnapSpline);
		TArray<TPair<double, EMetaLandRoadMouthSide>> MouthStations;
		MetaLandRoad::CollectVisibleMouthStations(SnapSpline, MouthStations);
		for (const TPair<double, EMetaLandRoadMouthSide>& Station : MouthStations)
		{
			const double MouthS = Station.Key;
			const EMetaLandRoadMouthSide MouthSide = Station.Value;
			const int32 SectionIndex = SnapSpline->GetRoadLayout().FindSectionAtSOffset(MouthS);
			if (SectionIndex == INDEX_NONE || !SnapSpline->GetLaneSections().IsValidIndex(SectionIndex))
			{
				continue;
			}

			FVector MouthLeft, MouthRight, MouthCenter;
			if (!SnapSpline->GetRoadEdgePointsAtS(
				MouthS, MouthLeft, MouthRight, MouthCenter, ESplineCoordinateSpace::World))
			{
				continue;
			}

			const FVector2D A(MouthLeft.X, MouthLeft.Y);
			const FVector2D B(MouthRight.X, MouthRight.Y);
			const FVector2D P(CursorWorld.X, CursorWorld.Y);
			const FVector2D AB = B - A;
			const double AbLenSq = static_cast<double>(AB.SizeSquared());
			double DistSq = FVector::DistSquared2D(CursorWorld, MouthCenter);
			if (AbLenSq > 1.0)
			{
				const float T = FMath::Clamp(
					FVector2D::DotProduct(P - A, AB) / static_cast<float>(AbLenSq), 0.0f, 1.0f);
				DistSq = FVector2D::DistSquared(P, A + AB * T);
			}
			const double AcceptCm = FMath::Max(static_cast<double>(SnapDistCm), 1.0);
			if (DistSq > AcceptCm * AcceptCm || DistSq >= BestDistSq)
			{
				continue;
			}

			const int32 LeftSectionIndex = SnapSpline->GetRoadLayout().FindSideSection(
				SectionIndex, EMetaLandRoadLaneSectionSide::Left);
			const int32 RightSectionIndex = SnapSpline->GetRoadLayout().FindSideSection(
				SectionIndex, EMetaLandRoadLaneSectionSide::Right);
			int32 SeedLane = METALANDROAD_LANE_INDEX_NONE;
			if (RightSectionIndex != INDEX_NONE
				&& SnapSpline->GetLaneSection(RightSectionIndex).Right.Num() > 0)
			{
				SeedLane = 1;
			}
			else if (LeftSectionIndex != INDEX_NONE
				&& SnapSpline->GetLaneSection(LeftSectionIndex).Left.Num() > 0)
			{
				SeedLane = -1;
			}
			if (SeedLane == METALANDROAD_LANE_INDEX_NONE)
			{
				continue;
			}

			BestDistSq = DistSq;
			OutPick.Spline = SnapSpline;
			OutPick.Lane = SeedLane;
			OutPick.Section = SectionIndex;
			OutPick.Mouth = MouthSide;
			OutPick.MouthS = MouthS;
		}
	}

	return OutPick.IsValid();
}

bool BuildEndSnapFromLanePick(
	const FEndSnapTargetLanePick& Pick,
	int32 DesiredLaneCount,
	bool bSelectAllMouthLanes,
	FMetaLandRoadDrawAnchor& OutAnchor,
	FVector& OutGhostLeft,
	FVector& OutGhostRight,
	FVector& OutSnapPos,
	FVector& OutArriveTangent)
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	OutGhostLeft = OutGhostRight = OutSnapPos = FVector::ZeroVector;
	OutArriveTangent = FVector::ForwardVector;
	if (!Pick.IsValid())
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SnapSpline = Pick.Spline;
	if (!SnapSpline->BuildDrawAnchorForLaneMouth(
		Pick.Section,
		Pick.Lane,
		Pick.MouthS,
		Pick.Mouth,
		OutAnchor))
	{
		OutAnchor.SourceSpline = SnapSpline;
		OutAnchor.SectionIndex = Pick.Section;
		OutAnchor.LaneIndex = Pick.Lane;
		OutAnchor.AnchorS = Pick.MouthS;
		OutAnchor.MouthSide = Pick.Mouth;
		OutAnchor.bFromSuccessor = Pick.Mouth == EMetaLandRoadMouthSide::End;
		OutAnchor.BundledLaneIndices.Reset();
	}

	(void)DesiredLaneCount;
	(void)bSelectAllMouthLanes;
	MetaLandRoadLaneEditPick::TrySelectAllLanesOnMouthSide(
		SnapSpline, OutAnchor.SectionIndex, OutAnchor);

	FVector LaneL, LaneR, LaneC, LaneOut;
	if (!SnapSpline->GetRoadEdgePointsAtS(
		Pick.MouthS, LaneL, LaneR, LaneC, ESplineCoordinateSpace::World)
		&& !MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			SnapSpline, OutAnchor, LaneL, LaneR, LaneC, LaneOut))
	{
		return false;
	}
	if (LaneOut.IsNearlyZero())
	{
		FVector UnusedL, UnusedR, UnusedC;
		MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			SnapSpline, OutAnchor, UnusedL, UnusedR, UnusedC, LaneOut);
	}

	OutGhostLeft = LaneL;
	OutGhostRight = LaneR;
	OutSnapPos = LaneC;
	OutArriveTangent = (-LaneOut).GetSafeNormal2D();
	if (OutArriveTangent.IsNearlyZero())
	{
		OutArriveTangent = FVector::ForwardVector;
		return false;
	}
	return OutAnchor.IsValid();
}

void DrawAnchorMouthBarPDI(
	FPrimitiveDrawInterface* PDI,
	const UMetaLandRoadSplineComponent* SplineComp,
	const FMetaLandRoadDrawAnchor& Anchor,
	const FLinearColor& Color,
	float Thickness)
{
	if (!PDI || !SplineComp || !Anchor.IsValid())
	{
		return;
	}

	FVector MouthLeft, MouthRight, MouthCenter, MouthTangent;
	if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
		SplineComp, Anchor, MouthLeft, MouthRight, MouthCenter, MouthTangent))
	{
		return;
	}

	const float ZLift = SplineComp->LaneVisualizationHeightOffset + GuidanceMeshSurfaceZOffset;
	MouthLeft.Z += ZLift;
	MouthRight.Z += ZLift;
	PDI->DrawLine(MouthLeft, MouthRight, Color, SDPG_Foreground, Thickness);
	PDI->DrawLine(MouthLeft, MouthRight, Color, SDPG_World, Thickness * 0.85f);
}

void DrawAnchorBundleMouthBarsPDI(
	FPrimitiveDrawInterface* PDI,
	const UMetaLandRoadSplineComponent* SplineComp,
	const FMetaLandRoadDrawAnchor& Anchor,
	const FLinearColor& Color,
	float Thickness)
{
	if (!SplineComp || !Anchor.IsValid())
	{
		return;
	}

	TArray<int32> LaneIndices;
	LaneIndices.Add(Anchor.LaneIndex);
	for (int32 BundledLaneIndex : Anchor.BundledLaneIndices)
	{
		LaneIndices.AddUnique(BundledLaneIndex);
	}

	for (int32 LaneIndex : LaneIndices)
	{
		FMetaLandRoadDrawAnchor SingleLaneAnchor = Anchor;
		SingleLaneAnchor.LaneIndex = LaneIndex;
		SingleLaneAnchor.BundledLaneIndices.Reset();
		DrawAnchorMouthBarPDI(PDI, SplineComp, SingleLaneAnchor, Color, Thickness);
	}
}

/** Minimum visible parallel guide length at the mouth. */
/** Shared sample step for green rails + mesh-key bake center path. */
/** Viewport PDI thickness (pixels) for green dual-rail guidance. */
const FLinearColor GuidanceLeftRailColor(0.15f, 0.95f, 0.28f, 1.0f);
const FLinearColor GuidanceRightRailColor(0.15f, 0.95f, 0.28f, 1.0f);
const FLinearColor GuidanceErrorRailColor(0.92f, 0.12f, 0.12f, 1.0f);
/** Max mesh keys baked from guidance center (excludes live tip). Cap avoids 乱面. */

/**
 * MetaRoad FitSplineToBeginConnection style:
 * pin mouth + CurveUser tangent = Forward * Dist(mouth, neighbor). Neighbor stays Curve.
 */
void StabilizeLaneExtendMouthPoint(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 MouthPointIndex,
	const FVector& MouthWorld,
	const FVector& MouthOutward,
	double MouthWidthCm)
{
	if (!SplineComponent || !SplineComponent->SplineCurves.Position.Points.IsValidIndex(MouthPointIndex))
	{
		return;
	}

	const FVector Forward = MouthOutward.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	double HalfWidthCm = MouthWidthCm > 1.0 ? (MouthWidthCm * 0.5) : 0.0;
	if (const UMetaLandRoadSplineComponent* RoadSpline =
		Cast<UMetaLandRoadSplineComponent>(SplineComponent))
	{
		HalfWidthCm = FMath::Max(HalfWidthCm, RoadSpline->GetRoadHalfWidthAtS(0.0));
	}
	HalfWidthCm = FMath::Max(HalfWidthCm, MetaLandRoad::DefaultRoadLaneWidth * 0.5);

	const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
	if (NumPoints > 2)
	{
		// Continue: never move mouth — that reshapes segment 1 when drawing segment 2.
		return;
	}

	const double MinAlongCm = FMath::Clamp(HalfWidthCm * 0.35, 40.0, 200.0);
	// Pin exactly on the source mouth bar — outset caused FromConnection 接缝.
	const FVector MouthPinned = MouthWorld;
	SplineComponent->SetLocationAtSplinePoint(
		MouthPointIndex, MouthPinned, ESplineCoordinateSpace::World, false);

	if (NumPoints < 2)
	{
		SplineComponent->SetSplinePointType(MouthPointIndex, ESplinePointType::CurveCustomTangent, false);
		SplineComponent->SetTangentAtSplinePoint(
			MouthPointIndex, Forward * 200.0, ESplineCoordinateSpace::World, false);
		SplineComponent->UpdateSpline();
		return;
	}

	const int32 NextIndex =
		(MouthPointIndex <= 0) ? 1 : FMath::Clamp(MouthPointIndex - 1, 0, NumPoints - 1);
	FVector NextLoc = SplineComponent->GetLocationAtSplinePoint(
		NextIndex, ESplineCoordinateSpace::World);

	const double DistToNext = FVector::Dist2D(MouthPinned, NextLoc);
	const double Along = FVector::DotProduct(NextLoc - MouthPinned, Forward);
	if (DistToNext <= MinAlongCm * 2.0 && Along < 1.0)
	{
		const FVector Lateral = NextLoc - MouthPinned - Forward * Along;
		NextLoc = MouthPinned + Forward * MinAlongCm + Lateral;
		SplineComponent->SetLocationAtSplinePoint(
			NextIndex, NextLoc, ESplineCoordinateSpace::World, false);
	}

	const double ChordToNext = FVector::Dist2D(MouthPinned, NextLoc);
	const double TangentSize = FMath::Max(ChordToNext, MinAlongCm);
	SplineComponent->SetSplinePointType(MouthPointIndex, ESplinePointType::CurveCustomTangent, false);
	const FVector MouthArrive = SplineComponent->GetArriveTangentAtSplinePoint(
		MouthPointIndex, ESplineCoordinateSpace::World);
	SplineComponent->SetTangentsAtSplinePoint(
		MouthPointIndex,
		MouthArrive,
		Forward * TangentSize,
		ESplineCoordinateSpace::World,
		false);
	SplineComponent->UpdateSpline();
}

/**
 * Lane-anchor finish snap: outer edge (or mouth if near tip), arrive along approach.
 * Never snap to centerline + along-road tangent — that twists the mesh into a 麻花.
 */
bool TryBuildMouthSnapAnchor(
	UMetaLandRoadSplineComponent* SnapSpline,
	double MouthS,
	EMetaLandRoadMouthSide MouthSide,
	const FVector& CursorWorld,
	int32 DesiredLaneCount,
	FMetaLandRoadDrawAnchor& OutAnchor)
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	if (!SnapSpline)
	{
		return false;
	}

	SnapSpline->GetRoadLayoutMutable().UpdateLayout(SnapSpline);
	const int32 SectionIndex = SnapSpline->GetRoadLayout().FindSectionAtSOffset(MouthS);
	if (SectionIndex == INDEX_NONE || !SnapSpline->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	const int32 LeftSectionIndex = SnapSpline->GetRoadLayout().FindSideSection(
		SectionIndex, EMetaLandRoadLaneSectionSide::Left);
	const int32 RightSectionIndex = SnapSpline->GetRoadLayout().FindSideSection(
		SectionIndex, EMetaLandRoadLaneSectionSide::Right);
	int32 BestLane = METALANDROAD_LANE_INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	auto ConsiderLane = [&](int32 LaneIndex)
	{
		const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SnapSpline, SectionIndex, LaneIndex);
		if (LaneSectionIndex == INDEX_NONE)
		{
			return;
		}
		const FVector LaneOuter = SnapSpline->EvalLanePosition(
			LaneSectionIndex, LaneIndex, MouthS, 1.0, ESplineCoordinateSpace::World);
		const double DistSq = FVector::DistSquared2D(CursorWorld, LaneOuter);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestLane = LaneIndex;
		}
	};

	int32 RightLaneCount = 0;
	int32 LeftLaneCount = 0;
	if (RightSectionIndex != INDEX_NONE)
	{
		RightLaneCount = SnapSpline->GetLaneSection(RightSectionIndex).Right.Num();
		for (int32 i = 0; i < RightLaneCount; ++i)
		{
			ConsiderLane(i + 1);
		}
	}
	if (LeftSectionIndex != INDEX_NONE)
	{
		LeftLaneCount = SnapSpline->GetLaneSection(LeftSectionIndex).Left.Num();
		for (int32 i = 0; i < LeftLaneCount; ++i)
		{
			ConsiderLane(-(i + 1));
		}
	}
	if (BestLane == METALANDROAD_LANE_INDEX_NONE)
	{
		if (RightLaneCount > 0)
		{
			BestLane = 1;
		}
		else if (LeftLaneCount > 0)
		{
			BestLane = -1;
		}
		else
		{
			return false;
		}
	}

	if (!SnapSpline->BuildDrawAnchorForLaneMouth(
		SectionIndex, BestLane, MouthS, MouthSide, OutAnchor))
	{
		OutAnchor = FMetaLandRoadDrawAnchor();
		OutAnchor.SourceSpline = SnapSpline;
		OutAnchor.SectionIndex = SectionIndex;
		OutAnchor.LaneIndex = BestLane;
		OutAnchor.AnchorS = MouthS;
		OutAnchor.MouthSide = MouthSide;
		OutAnchor.bFromSuccessor = MouthSide == EMetaLandRoadMouthSide::End;
		OutAnchor.BundledLaneIndices.Reset();
	}

	// Hover auto-match tip lane count on the seed face only.
	// Explicit Shift bundles are owned by the pick path — do not force dual L|R.
	if (DesiredLaneCount > 1)
	{
		MetaLandRoadLaneEditPick::ExpandAnchorBundleToCount(
			SnapSpline, OutAnchor, DesiredLaneCount, &CursorWorld);
	}
	else
	{
		OutAnchor.BundledLaneIndices.Reset();
	}
	return OutAnchor.IsValid();
}

bool ResolveLaneEdgeConnectionSnap(
	AMoKuEditSplineActor* SnapRoad,
	const FVector& ApproachFrom,
	const FVector& CursorWorld,
	float SnapDistCm,
	int32 DesiredLaneCount,
	FVector& OutSnapPos,
	FVector& OutArriveTangent,
	float& OutSampleDist,
	FMetaLandRoadDrawAnchor* OutAnchor,
	FVector* OutGhostLeft,
	FVector* OutGhostRight)
{
	OutSnapPos = FVector::ZeroVector;
	OutArriveTangent = FVector::ForwardVector;
	OutSampleDist = 0.0f;
	if (OutAnchor)
	{
		*OutAnchor = FMetaLandRoadDrawAnchor();
	}
	if (!SnapRoad || SnapDistCm < 1.0f)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SnapSpline =
		Cast<UMetaLandRoadSplineComponent>(SnapRoad->GetSplineComponent());
	if (!SnapSpline || SnapSpline->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	auto DistToBar = [](const FVector& Pos, const FVector& Left, const FVector& Right, const FVector& Center) -> double
	{
		const FVector2D A(Left.X, Left.Y);
		const FVector2D B(Right.X, Right.Y);
		const FVector2D P(Pos.X, Pos.Y);
		const FVector2D AB = B - A;
		const double AbLenSq = static_cast<double>(AB.SizeSquared());
		if (AbLenSq <= 1.0)
		{
			return FVector::Dist2D(Pos, Center);
		}
		const float T = FMath::Clamp(
			FVector2D::DotProduct(P - A, AB) / static_cast<float>(AbLenSq), 0.0f, 1.0f);
		return static_cast<double>(FVector2D::Distance(P, A + AB * T));
	};

	TArray<TPair<double, EMetaLandRoadMouthSide>> MouthStations;
	MetaLandRoad::CollectVisibleMouthStations(SnapSpline, MouthStations);
	bool bHaveMouth = false;
	double BestDist = TNumericLimits<double>::Max();
	double MouthS = 0.0;
	EMetaLandRoadMouthSide MouthSide = EMetaLandRoadMouthSide::End;
	FVector MouthLeft = FVector::ZeroVector;
	FVector MouthRight = FVector::ZeroVector;
	FVector MouthMid = FVector::ZeroVector;
	for (const TPair<double, EMetaLandRoadMouthSide>& Station : MouthStations)
	{
		FVector Left, Right, Center;
		if (!SnapSpline->GetRoadEdgePointsAtS(
			Station.Key, Left, Right, Center, ESplineCoordinateSpace::World))
		{
			continue;
		}
		const double Dist = DistToBar(CursorWorld, Left, Right, Center);
		const double AcceptCm = FMath::Max(static_cast<double>(SnapDistCm), 1.0);
		if (Dist > AcceptCm || Dist >= BestDist)
		{
			continue;
		}
		BestDist = Dist;
		MouthS = Station.Key;
		MouthSide = Station.Value;
		MouthLeft = Left;
		MouthRight = Right;
		MouthMid = Center;
		bHaveMouth = true;
	}
	if (!bHaveMouth)
	{
		return false;
	}

	const FVector SplineDir = SnapSpline->GetDirectionAtDistanceAlongSpline(
		MouthS, ESplineCoordinateSpace::World).GetSafeNormal2D();
	const FVector MouthOutward = MouthSide == EMetaLandRoadMouthSide::End
		? SplineDir
		: (-SplineDir);
	OutSnapPos = MouthMid;
	// Arrive into the mouth along the target road — chord-to-cursor hooks the Bezier.
	OutArriveTangent = (-MouthOutward).GetSafeNormal2D();
	if (OutArriveTangent.IsNearlyZero())
	{
		OutArriveTangent = (MouthMid - ApproachFrom).GetSafeNormal2D();
	}
	OutSampleDist = MouthS;
	FVector GhostL = MouthLeft;
	FVector GhostR = MouthRight;
	if (OutAnchor)
	{
		TryBuildMouthSnapAnchor(SnapSpline, MouthS, MouthSide, CursorWorld, DesiredLaneCount, *OutAnchor);
		FVector LaneL, LaneR, LaneC, LaneOut;
		if (OutAnchor->IsValid()
			&& MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				SnapSpline, *OutAnchor, LaneL, LaneR, LaneC, LaneOut))
		{
			OutSnapPos = LaneC;
			GhostL = LaneL;
			GhostR = LaneR;
			OutArriveTangent = (-LaneOut).GetSafeNormal2D();
			if (OutArriveTangent.IsNearlyZero())
			{
				OutArriveTangent = (LaneC - ApproachFrom).GetSafeNormal2D();
			}
		}
	}
	if (OutGhostLeft)
	{
		*OutGhostLeft = GhostL;
	}
	if (OutGhostRight)
	{
		*OutGhostRight = GhostR;
	}
	return !OutArriveTangent.IsNearlyZero();
}

void HideEditorSplineRibbon(UMoKuEditSplinesComponent* SplineComponent)
{
	if (!SplineComponent)
	{
		return;
	}
	SplineComponent->SetVisibility(false);
	SplineComponent->SetHiddenInGame(true);
	SplineComponent->bDrawDebug = false;
}

void ApplyGuidanceSurfaceZ(FVector& Point)
{
	Point.Z += GuidanceMeshSurfaceZOffset + GuidancePDILiftCm;
}

/** Vertical snap to landscape / ground — ignore road & junction meshes so draw does not climb their tops. */
bool ProjectPointOntoLandscapeSurface(
	UWorld* World,
	FVector& InOutPoint,
	const AActor* ExtraIgnoreActor)
{
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetaLandRoadSurfaceSnap), true);
	Params.bTraceComplex = true;
	if (ExtraIgnoreActor)
	{
		Params.AddIgnoredActor(ExtraIgnoreActor);
	}
	for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			Params.AddIgnoredActor(*It);
		}
	}
	for (TActorIterator<AMoKuEditIntersectionActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			Params.AddIgnoredActor(*It);
		}
	}

	const FVector TraceStart(InOutPoint.X, InOutPoint.Y, InOutPoint.Z + 100000.0);
	const FVector TraceEnd(InOutPoint.X, InOutPoint.Y, InOutPoint.Z - 200000.0);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
	{
		return false;
	}

	InOutPoint.Z = Hit.ImpactPoint.Z;
	return true;
}

void BlendRailTowardEdge(TArray<FVector>& Rail, const FVector& Edge, bool bFromStart)
{
	if (Rail.Num() < 2)
	{
		return;
	}

	const int32 BlendCount = FMath::Clamp(Rail.Num() / 4, 2, 6);
	if (bFromStart)
	{
		Rail[0] = Edge;
		for (int32 i = 1; i < BlendCount && i < Rail.Num(); ++i)
		{
			const double A = 1.0 - static_cast<double>(i) / static_cast<double>(BlendCount);
			Rail[i] = FMath::Lerp(Rail[i], Edge, A * A);
		}
		return;
	}

	const int32 Last = Rail.Num() - 1;
	Rail[Last] = Edge;
	for (int32 k = 1; k < BlendCount && k <= Last; ++k)
	{
		const double A = 1.0 - static_cast<double>(k) / static_cast<double>(BlendCount);
		Rail[Last - k] = FMath::Lerp(Rail[Last - k], Edge, A * A);
	}
}

void MatchEdgePairToRailTips(
	const TArray<FVector>& Left,
	const TArray<FVector>& Right,
	FVector& EdgeL,
	FVector& EdgeR,
	bool bUseStart)
{
	if (Left.Num() == 0 || Right.Num() == 0)
	{
		return;
	}

	const FVector LeftTip = bUseStart ? Left[0] : Left.Last();
	const FVector RightTip = bUseStart ? Right[0] : Right.Last();
	const double LL = FVector::Dist2D(LeftTip, EdgeL);
	const double LR = FVector::Dist2D(LeftTip, EdgeR);
	const double RL = FVector::Dist2D(RightTip, EdgeL);
	const double RR = FVector::Dist2D(RightTip, EdgeR);
	if (LR + RL < LL + RR)
	{
		Swap(EdgeL, EdgeR);
	}
}

FVector TransportOffsetRight(const FVector& Tangent, const FVector& PrevRight, const FVector& PreferRight)
{
	FVector Right = FVector::CrossProduct(FVector::UpVector, Tangent).GetSafeNormal2D();
	if (Right.IsNearlyZero())
	{
		return PrevRight.IsNearlyZero() ? FVector::RightVector : PrevRight;
	}
	if (!PreferRight.IsNearlyZero() && PrevRight.IsNearlyZero()
		&& FVector::DotProduct(Right, PreferRight) < 0.0)
	{
		Right = -Right;
	}
	else if (!PrevRight.IsNearlyZero() && FVector::DotProduct(Right, PrevRight) < 0.0)
	{
		Right = -Right;
	}
	return Right;
}

void SoftenGuidanceRailStart(
	TArray<FVector>& Rail,
	const FVector& TipEdge,
	const FVector& TipTangent,
	double StepCm)
{
	(void)TipTangent;
	(void)StepCm;
	// Pin the mouth sample only. Blending later samples toward the corner
	// pulled the first segments back onto the bar and made the 折角 in C-extend.
	if (Rail.Num() > 0)
	{
		Rail[0] = TipEdge;
	}
}

void SoftenGuidanceRailFreeEnd(TArray<FVector>& Rail)
{
	if (Rail.Num() < 3)
	{
		return;
	}

	const int32 Last = Rail.Num() - 1;
	const FVector PrevDir = (Rail[Last - 1] - Rail[Last - 2]).GetSafeNormal2D();
	const FVector LastDir = (Rail[Last] - Rail[Last - 1]).GetSafeNormal2D();
	if (PrevDir.IsNearlyZero() || LastDir.IsNearlyZero())
	{
		return;
	}

	// Hermite arrive whip / Right flip → outward hook at free end.
	if (FVector::DotProduct(PrevDir, LastDir) < 0.55)
	{
		const double Len = FVector::Dist2D(Rail[Last], Rail[Last - 1]);
		FVector Fixed = Rail[Last - 1] + PrevDir * Len;
		Fixed.Z = Rail[Last].Z;
		Rail[Last] = Fixed;
	}
}

/** MetaRoad-style: dual-rail preview stays on one horizontal plane (+ small PDI lift). */
void FlattenGuidanceRailToPlaneZ(TArray<FVector>& Rail, float PlaneZ)
{
	for (FVector& P : Rail)
	{
		P.Z = PlaneZ;
		ApplyGuidanceSurfaceZ(P);
	}
}

/**
 * Guidance plane Z = landscape under tip XY (not elevated road-mesh / embankment Z).
 * Tip mesh can sit on a raised cliff from ZOffset; rails must stay MetaRoad-flat on the ground.
 */
float ResolveGuidancePlaneZOnSurface(
	UWorld* World,
	const FVector& TipCenter,
	const AActor* IgnoreActor)
{
	FVector Probe = TipCenter;
	if (World && ProjectPointOntoLandscapeSurface(World, Probe, IgnoreActor))
	{
		return static_cast<float>(Probe.Z);
	}
	return static_cast<float>(TipCenter.Z);
}

int32 MapSpawnedBranchLaneIndex(int32 SourceLaneIndex, const TArray<int32>& SourceLanes)
{
	if (SourceLaneIndex > 0)
	{
		int32 Rank = 0;
		for (int32 LaneIndex : SourceLanes)
		{
			if (LaneIndex <= 0)
			{
				continue;
			}
			++Rank;
			if (LaneIndex == SourceLaneIndex)
			{
				return Rank;
			}
		}
	}
	else if (SourceLaneIndex < 0)
	{
		int32 Rank = 0;
		for (int32 LaneIndex : SourceLanes)
		{
			if (LaneIndex >= 0)
			{
				continue;
			}
			++Rank;
			if (LaneIndex == SourceLaneIndex)
			{
				return -Rank;
			}
		}
	}
	return SourceLaneIndex;
}

void TransferEditTargetToSpawnedBranch(
	UMetaLandRoadEditorMode* RoadMode,
	const FMetaLandRoadEditTarget& SourceTarget,
	AMoKuEditSplineActor* SpawnedActor)
{
	if (!RoadMode || !SpawnedActor || !SourceTarget.bHasPendingAnchor)
	{
		return;
	}

	UMetaLandRoadSplineComponent* SpawnedSpline =
		Cast<UMetaLandRoadSplineComponent>(SpawnedActor->GetSplineComponent());
	if (!SpawnedSpline)
	{
		return;
	}

	TArray<int32> SourceLanes;
	SourceLanes.Add(SourceTarget.PendingAnchor.LaneIndex);
	for (int32 BundledLaneIndex : SourceTarget.PendingAnchor.BundledLaneIndices)
	{
		SourceLanes.AddUnique(BundledLaneIndex);
	}
	SourceLanes.Sort([](int32 A, int32 B)
	{
		return FMath::Abs(A) < FMath::Abs(B);
	});

	const int32 SpawnedPrimaryLane = MapSpawnedBranchLaneIndex(
		SourceTarget.PendingAnchor.LaneIndex,
		SourceLanes);

	// BundledLaneIndices = extras only (primary lives in LaneIndex). Collect adds primary.
	TArray<int32> SpawnedBundledLanes;
	for (int32 SourceLaneIndex : SourceLanes)
	{
		const int32 MappedLane = MapSpawnedBranchLaneIndex(SourceLaneIndex, SourceLanes);
		if (MappedLane != LANE_INDEX_NONE && MappedLane != SpawnedPrimaryLane)
		{
			SpawnedBundledLanes.AddUnique(MappedLane);
		}
	}

	const double TipS = FMath::Max(SpawnedSpline->GetSplineLength(), 0.0);
	FMetaLandRoadDrawAnchor SpawnedAnchor;
	if (!SpawnedSpline->BuildDrawAnchorForLaneMouth(
		0,
		SpawnedPrimaryLane,
		TipS,
		EMetaLandRoadMouthSide::End,
		SpawnedAnchor))
	{
		return;
	}
	SpawnedAnchor.BundledLaneIndices = MoveTemp(SpawnedBundledLanes);

	FMetaLandRoadEditTarget NewTarget;
	NewTarget.RoadActor = SpawnedActor;
	NewTarget.Spline = SpawnedSpline;
	NewTarget.PendingAnchor = SpawnedAnchor;
	NewTarget.bHasPendingAnchor = true;
	RoadMode->SetEditTarget(NewTarget);
}

double ResolveEditableTipS(
	const AMoKuEditSplineActor* RoadActor,
	const USplineComponent* Spline,
	bool bForward)
{
	if (RoadActor)
	{
		TArray<TPair<double, double>> MeshSpans;
		RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
		if (MeshSpans.Num() > 0)
		{
			return bForward ? MeshSpans[0].Key : MeshSpans.Last().Value;
		}
	}
	const double Length = Spline ? Spline->GetSplineLength() : 0.0;
	return bForward ? 0.0 : Length;
}

void BuildOffsetRails(
	const TArray<FVector>& CenterSamples,
	double HalfWidthLeft,
	double HalfWidthRight,
	TArray<FVector>& OutLeft,
	TArray<FVector>& OutRight,
	const FVector& ForcedStartTangent = FVector::ZeroVector,
	const FVector& ForcedEndTangent = FVector::ZeroVector)
{
	OutLeft.Reset();
	OutRight.Reset();
	if (CenterSamples.Num() < 2)
	{
		return;
	}
	OutLeft.Reserve(CenterSamples.Num());
	OutRight.Reserve(CenterSamples.Num());
	const FVector StartT = ForcedStartTangent.GetSafeNormal2D();
	const FVector EndT = ForcedEndTangent.GetSafeNormal2D();
	const int32 LastIdx = CenterSamples.Num() - 1;
	FVector PrevRight = FVector::ZeroVector;
	const FVector PreferRight = !StartT.IsNearlyZero()
		? FVector::CrossProduct(FVector::UpVector, StartT).GetSafeNormal2D()
		: FVector::ZeroVector;
	for (int32 i = 0; i < CenterSamples.Num(); ++i)
	{
		FVector Tangent = FVector::ZeroVector;
		if (i == 0 && !StartT.IsNearlyZero())
		{
			Tangent = StartT;
		}
		else if (i == LastIdx && !EndT.IsNearlyZero() && i > 0)
		{
			const FVector PathT = (CenterSamples[i] - CenterSamples[i - 1]).GetSafeNormal2D();
			Tangent = (!PathT.IsNearlyZero() && FVector::DotProduct(EndT, PathT) > 0.35)
				? EndT
				: (PathT.IsNearlyZero() ? EndT : PathT);
		}
		else
		{
			if (i + 1 < CenterSamples.Num())
			{
				Tangent += (CenterSamples[i + 1] - CenterSamples[i]).GetSafeNormal2D();
			}
			if (i > 0)
			{
				Tangent += (CenterSamples[i] - CenterSamples[i - 1]).GetSafeNormal2D();
			}
		}
		if (!Tangent.Normalize())
		{
			Tangent = !StartT.IsNearlyZero() ? StartT : FVector::ForwardVector;
		}
		const FVector Right = TransportOffsetRight(Tangent, PrevRight, PreferRight);
		PrevRight = Right;
		OutLeft.Add(CenterSamples[i] - Right * HalfWidthLeft);
		OutRight.Add(CenterSamples[i] + Right * HalfWidthRight);
	}
}

void AppendResampledSegment(
	const FVector& Start,
	const FVector& End,
	double StepCm,
	TArray<FVector>& OutCenter,
	bool bIncludeStart)
{
	const FVector Delta = End - Start;
	const double Len = Delta.Size2D();
	if (Len < KINDA_SMALL_NUMBER)
	{
		if (bIncludeStart || OutCenter.Num() == 0)
		{
			OutCenter.Add(Start);
		}
		return;
	}
	const int32 Segs = FMath::Max(1, FMath::CeilToInt(Len / FMath::Max(StepCm, 10.0)));
	for (int32 i = bIncludeStart ? 0 : 1; i <= Segs; ++i)
	{
		const double T = static_cast<double>(i) / static_cast<double>(Segs);
		OutCenter.Add(FMath::Lerp(Start, End, T));
	}
}

/** Centripetal Catmull-Rom (α=0.5): knot spacing = √|ΔP|. */
double CatmullRomKnotDelta(const FVector& A, const FVector& B)
{
	return FMath::Max(FMath::Sqrt(FVector::Dist2D(A, B)), 1.0);
}

/**
 * Ghost P0/P3 encode leave/arrive so a 2-point extend has determined tangents.
 * P1 = mouth, P2 = cursor/snap. Chord-length ghosts keep tangent magnitude consistent.
 */
void BuildCentripetalCatmullRomGhosts(
	const FVector& P1,
	const FVector& P2,
	const FVector& StartTangent,
	const FVector& EndTangent,
	FVector& OutP0,
	FVector& OutP3)
{
	const double Chord = FMath::Max(FVector::Dist2D(P1, P2), 50.0);
	const FVector ChordDir = (P2 - P1).GetSafeNormal2D();
	FVector T0 = StartTangent.GetSafeNormal2D();
	FVector T1 = EndTangent.GetSafeNormal2D();
	if (T0.IsNearlyZero())
	{
		T0 = ChordDir;
	}
	if (T1.IsNearlyZero())
	{
		T1 = ChordDir;
	}
	// Arrive pointing back at the source makes P3 fold over P2 → V kink at the mouth.
	if (!ChordDir.IsNearlyZero() && FVector::DotProduct(T1, ChordDir) < 0.0)
	{
		T1 = -T1;
	}
	OutP0 = P1 - T0 * Chord;
	OutP3 = P2 + T1 * Chord;
	OutP0.Z = P1.Z;
	OutP3.Z = P2.Z;
}

FVector EvalCentripetalCatmullRomPoint(
	const FVector& P0,
	const FVector& P1,
	const FVector& P2,
	const FVector& P3,
	double S)
{
	const double T0 = 0.0;
	const double T1 = T0 + CatmullRomKnotDelta(P0, P1);
	const double T2 = T1 + CatmullRomKnotDelta(P1, P2);
	const double T3 = T2 + CatmullRomKnotDelta(P2, P3);
	const double T = FMath::Lerp(T1, T2, FMath::Clamp(S, 0.0, 1.0));

	auto LerpKnot = [](double TA, const FVector& A, double TB, const FVector& B, double TNow) -> FVector
	{
		const double Den = TB - TA;
		if (FMath::Abs(Den) < KINDA_SMALL_NUMBER)
		{
			return A;
		}
		return A + (B - A) * ((TNow - TA) / Den);
	};

	const FVector A1 = LerpKnot(T0, P0, T1, P1, T);
	const FVector A2 = LerpKnot(T1, P1, T2, P2, T);
	const FVector A3 = LerpKnot(T2, P2, T3, P3, T);
	const FVector B1 = LerpKnot(T0, A1, T2, A2, T);
	const FVector B2 = LerpKnot(T1, A2, T3, A3, T);
	FVector C = LerpKnot(T1, B1, T2, B2, T);
	C.Z = FMath::Lerp(P1.Z, P2.Z, static_cast<float>(FMath::Clamp(S, 0.0, 1.0)));
	return C;
}

/** dP/ds on s∈[0,1] for Unreal CurveCustomTangent (matches the CR preview). */
void ComputeCentripetalCatmullRomHermiteTangents(
	const FVector& P1,
	const FVector& P2,
	const FVector& StartTangent,
	const FVector& EndTangent,
	FVector& OutM0,
	FVector& OutM1)
{
	FVector P0;
	FVector P3;
	BuildCentripetalCatmullRomGhosts(P1, P2, StartTangent, EndTangent, P0, P3);
	const double T0 = 0.0;
	const double T1 = T0 + CatmullRomKnotDelta(P0, P1);
	const double T2 = T1 + CatmullRomKnotDelta(P1, P2);
	const double T3 = T2 + CatmullRomKnotDelta(P2, P3);
	const double DtSeg = FMath::Max(T2 - T1, KINDA_SMALL_NUMBER);
	OutM0 = (P2 - P0) / FMath::Max(T2 - T0, KINDA_SMALL_NUMBER) * DtSeg;
	OutM1 = (P3 - P1) / FMath::Max(T3 - T1, KINDA_SMALL_NUMBER) * DtSeg;
	OutM0.Z = 0.0;
	OutM1.Z = 0.0;
}

void AppendHermiteSegment(
	const FVector& P0,
	const FVector& P1,
	const FVector& Tangent0,
	const FVector& Tangent1,
	double StepCm,
	TArray<FVector>& OutCenter,
	bool bIncludeStart)
{
	const double Chord = FVector::Dist2D(P0, P1);
	const int32 Segs = FMath::Max(1, FMath::CeilToInt(Chord / FMath::Max(StepCm, 10.0)));
	FVector GhostP0;
	FVector GhostP3;
	BuildCentripetalCatmullRomGhosts(P0, P1, Tangent0, Tangent1, GhostP0, GhostP3);
	for (int32 i = bIncludeStart ? 0 : 1; i <= Segs; ++i)
	{
		const double T = static_cast<double>(i) / static_cast<double>(Segs);
		OutCenter.Add(EvalCentripetalCatmullRomPoint(GhostP0, P0, P1, GhostP3, T));
	}
}

struct FAsymmetricTangentBezier
{
	FVector Pos0 = FVector::ZeroVector;
	FVector Pos1 = FVector::ZeroVector;
	FVector Dir0 = FVector::ForwardVector;
	FVector Dir1 = FVector::ForwardVector;
	FVector Pos0Ext = FVector::ZeroVector;
	FVector Pos1Ext = FVector::ZeroVector;
	FVector Ctrl1 = FVector::ZeroVector;
	FVector Ctrl2 = FVector::ZeroVector;
	double ExtLen0 = 0.0;
	double ExtLen1 = 0.0;
	double Scale0 = 0.0;
	double Scale1 = 0.0;
	bool bValid = false;
};

/** Mouth leave + mouth arrive → one cubic Bezier (no straight stubs). */
bool ComputeAsymmetricTangentBezier(
	const FVector& Pos0,
	const FVector& Pos1,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double StraightFactor,
	FAsymmetricTangentBezier& Out)
{
	Out = FAsymmetricTangentBezier();
	Out.Pos0 = Pos0;
	Out.Pos1 = Pos1;
	FVector Dir0 = LeaveDir.GetSafeNormal2D();
	FVector Dir1 = ArriveDir.GetSafeNormal2D();
	const FVector Chord = (Pos1 - Pos0).GetSafeNormal2D();
	const double BaseDist = FMath::Max(FVector::Dist2D(Pos0, Pos1), 1.0);
	if (Dir0.IsNearlyZero())
	{
		Dir0 = Chord;
	}
	if (Dir1.IsNearlyZero())
	{
		Dir1 = Chord;
	}
	if (Dir0.IsNearlyZero() || Dir1.IsNearlyZero())
	{
		return false;
	}

	// Keep leave/arrive on the chord side so the Hermite cannot fold back (折线).
	auto AlignToChord = [&Chord](FVector Dir)
	{
		const double Align = FVector::DotProduct(Dir, Chord);
		if (Align >= 0.25)
		{
			return Dir;
		}
		return (Dir * FMath::Max(Align + 0.5, 0.15) + Chord * (0.25 - FMath::Min(Align, 0.25))).GetSafeNormal2D();
	};
	Dir0 = AlignToChord(Dir0);
	Dir1 = AlignToChord(Dir1);
	if (Dir0.IsNearlyZero())
	{
		Dir0 = Chord;
	}
	if (Dir1.IsNearlyZero())
	{
		Dir1 = Chord;
	}

	const FVector Dir1Rev = -Dir1;
	const FVector W0 = Pos0 - Pos1;
	const double A = 1.0;
	const double B = FVector::DotProduct(Dir0, Dir1Rev);
	const double C = 1.0;
	const double D = FVector::DotProduct(Dir0, W0);
	const double E = FVector::DotProduct(Dir1Rev, W0);
	const double Denom = A * C - B * B;
	double T0 = BaseDist * 0.5;
	double T1 = BaseDist * 0.5;
	if (FMath::Abs(Denom) >= 0.0001)
	{
		T0 = (B * E - C * D) / Denom;
		T1 = (A * E - B * D) / Denom;
	}
	if (T0 < 0.0)
	{
		T0 = BaseDist * 0.5;
	}
	if (T1 < 0.0)
	{
		T1 = BaseDist * 0.5;
	}

	double Factor = StraightFactor;
	if (Factor <= 0.0)
	{
		Factor = 0.45;
	}
	// Follow each mouth tangent toward the virtual intersection (asymmetric blend).
	double Scale0 = T0 * Factor;
	double Scale1 = T1 * Factor;
	const double MinH = BaseDist * 0.18;
	const double MaxH = BaseDist * 0.38;
	Scale0 = FMath::Clamp(Scale0, MinH, MaxH);
	Scale1 = FMath::Clamp(Scale1, MinH, MaxH);

	Out.Dir0 = Dir0;
	Out.Dir1 = Dir1;
	Out.Scale0 = Scale0;
	Out.Scale1 = Scale1;
	Out.ExtLen0 = Scale0;
	Out.ExtLen1 = Scale1;
	Out.Ctrl1 = Pos0 + Dir0 * Scale0;
	Out.Ctrl2 = Pos1 - Dir1 * Scale1;
	Out.Ctrl1.Z = Pos0.Z;
	Out.Ctrl2.Z = Pos1.Z;
	Out.Pos0Ext = Out.Ctrl1;
	Out.Pos1Ext = Out.Ctrl2;
	Out.bValid = true;
	return true;
}

FVector EvalCubicBezier(
	const FVector& P0,
	const FVector& C1,
	const FVector& C2,
	const FVector& P1,
	double T)
{
	const double U = 1.0 - FMath::Clamp(T, 0.0, 1.0);
	const double S = 1.0 - U;
	return (U * U * U) * P0
		+ (3.0 * U * U * S) * C1
		+ (3.0 * U * S * S) * C2
		+ (S * S * S) * P1;
}

FVector EvalCubicBezierDerivative(
	const FVector& P0,
	const FVector& C1,
	const FVector& C2,
	const FVector& P1,
	double T)
{
	const double S = FMath::Clamp(T, 0.0, 1.0);
	const double U = 1.0 - S;
	return 3.0 * U * U * (C1 - P0)
		+ 6.0 * U * S * (C2 - C1)
		+ 3.0 * S * S * (P1 - C2);
}

void AppendAsymmetricTangentBezierPath(
	const FAsymmetricTangentBezier& Path,
	double StepCm,
	TArray<FVector>& OutCenter,
	bool bIncludeStart)
{
	if (!Path.bValid)
	{
		return;
	}

	const double Chord = FMath::Max(FVector::Dist2D(Path.Pos0, Path.Pos1), 1.0);
	const int32 CurveSteps = FMath::Clamp(
		FMath::CeilToInt(Chord / FMath::Max(StepCm, 25.0)), 16, 36);

	if (bIncludeStart || OutCenter.Num() == 0)
	{
		OutCenter.Add(Path.Pos0);
	}
	for (int32 i = 1; i <= CurveSteps; ++i)
	{
		const double T = static_cast<double>(i) / static_cast<double>(CurveSteps);
		OutCenter.Add(EvalCubicBezier(Path.Pos0, Path.Ctrl1, Path.Ctrl2, Path.Pos1, T));
	}
}

void ApplyAsymmetricTangentBezierConnection(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 StartIndex,
	const FVector& EndPos,
	const FVector& LeaveDir,
	const FVector& ArriveDir)
{
	if (!SplineComponent
		|| StartIndex < 0
		|| StartIndex >= SplineComponent->GetNumberOfSplinePoints())
	{
		return;
	}

	FAsymmetricTangentBezier Path;
	const FVector Pos0 = SplineComponent->GetLocationAtSplinePoint(
		StartIndex, ESplineCoordinateSpace::World);
	if (!ComputeAsymmetricTangentBezier(Pos0, EndPos, LeaveDir, ArriveDir, 0.35, Path))
	{
		return;
	}

	while (SplineComponent->GetNumberOfSplinePoints() > StartIndex + 1)
	{
		SplineComponent->RemoveSplinePoint(SplineComponent->GetNumberOfSplinePoints() - 1, false);
	}

	const double Chord = FMath::Max(FVector::Dist2D(Path.Pos0, Path.Pos1), 1.0);
	(void)Chord;
	// Cubic Bezier = 2 on-curve keys; leave/arrive tangents carry C1/C2.
	const int32 SampleCount = 2;
	for (int32 i = 1; i < SampleCount; ++i)
	{
		const double T = static_cast<double>(i) / static_cast<double>(SampleCount - 1);
		const FVector Sample = (i == SampleCount - 1)
			? Path.Pos1
			: EvalCubicBezier(Path.Pos0, Path.Ctrl1, Path.Ctrl2, Path.Pos1, T);
		SplineComponent->AddSplinePointLocation(Sample, SplineComponent->GetNumberOfSplinePoints());
	}

	const double Dt = 1.0 / static_cast<double>(SampleCount - 1);
	for (int32 i = 0; i < SampleCount; ++i)
	{
		const int32 PointIndex = StartIndex + i;
		if (!SplineComponent->SplineCurves.Position.Points.IsValidIndex(PointIndex))
		{
			break;
		}
		const double T = static_cast<double>(i) * Dt;
		const FVector Deriv = EvalCubicBezierDerivative(
			Path.Pos0, Path.Ctrl1, Path.Ctrl2, Path.Pos1, T);
		const FVector Tan = Deriv * Dt;
		SplineComponent->SetSplinePointType(PointIndex, ESplinePointType::CurveCustomTangent, false);
		// 2-point connector: Arrive==Leave so the main-line tangent is one handle,
		// not a leftover mouth Arrive stacked on the Bezier Leave.
		if (i == 0 && SplineComponent->GetNumberOfSplinePoints() > 2)
		{
			const FVector MouthArrive = SplineComponent->GetArriveTangentAtSplinePoint(
				PointIndex, ESplineCoordinateSpace::World);
			SplineComponent->SetTangentsAtSplinePoint(
				PointIndex, MouthArrive, Tan, ESplineCoordinateSpace::World, false);
		}
		else
		{
			SplineComponent->SetTangentsAtSplinePoint(
				PointIndex, Tan, Tan, ESplineCoordinateSpace::World, false);
		}
	}

}

/**
 * Straight leave → cubic Bezier → straight arrive (asymmetric ray intersection).
 */
void AppendRampCenterPath(
	const FVector& P0,
	const FVector& P1,
	const FVector& Tangent0,
	const FVector& Tangent1,
	double StubLengthCm,
	double StepCm,
	TArray<FVector>& OutCenter,
	bool bIncludeStart)
{
	(void)StubLengthCm;
	FAsymmetricTangentBezier Path;
	if (ComputeAsymmetricTangentBezier(P0, P1, Tangent0, Tangent1, 0.35, Path))
	{
		AppendAsymmetricTangentBezierPath(Path, StepCm, OutCenter, bIncludeStart);
		return;
	}
	AppendHermiteSegment(P0, P1, Tangent0, Tangent1, StepCm, OutCenter, bIncludeStart);
}

/**
 * Sparse mesh keys (excludes P0): optional stub key (贴边) + centripetal Catmull-Rom interiors + end.
 * bForceSmoothTurn: never allow tip→end-only on turns (that is the big 折角).
 */
void BuildRampKeyPointsForSpline(
	const FVector& P0,
	const FVector& P1,
	const FVector& Tangent0,
	const FVector& Tangent1,
	double RampStubLengthCm,
	double StepCm,
	bool bAllowStub,
	int32 MaxInteriorKeys,
	TArray<FVector>& OutKeys,
	bool bForceSmoothTurn = true)
{
	OutKeys.Reset();
	const FVector T0 = Tangent0.GetSafeNormal2D();
	const double Chord2D = FVector::Dist2D(P0, P1);
	if (Chord2D <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double Step = FMath::Max(StepCm, 100.0);
	FVector CurveStart = P0;
	FVector CurveTangent0 = Tangent0;

	constexpr double MinCurveTailCm = 200.0;
	const bool bUseStub = bAllowStub
		&& RampStubLengthCm >= 50.0
		&& !T0.IsNearlyZero()
		&& Chord2D > RampStubLengthCm + MinCurveTailCm;
	if (bUseStub)
	{
		const double StubLen = FMath::Min(static_cast<double>(RampStubLengthCm), Chord2D - MinCurveTailCm);
		FVector StubEnd = P0 + T0 * StubLen;
		StubEnd.Z = FMath::Lerp(P0.Z, P1.Z, static_cast<float>(StubLen / Chord2D));
		OutKeys.Add(StubEnd);
		CurveStart = StubEnd;
		CurveTangent0 = T0;
	}

	const double CurveLen = FVector::Dist2D(CurveStart, P1);
	const FVector CurveDir = (P1 - CurveStart).GetSafeNormal2D();
	const double LeaveAlign = FVector::DotProduct(CurveTangent0.GetSafeNormal2D(), CurveDir);
	// Sharper turn → more interiors (ignore coarse Step which often yielded 0).
	int32 MinInterior = 0;
	if (bForceSmoothTurn)
	{
		if (LeaveAlign < 0.2)
		{
			MinInterior = 4;
		}
		else if (LeaveAlign < 0.55)
		{
			MinInterior = 3;
		}
		else if (LeaveAlign < 0.88)
		{
			MinInterior = 2;
		}
		else if (LeaveAlign < 0.97)
		{
			MinInterior = 1;
		}
	}
	const int32 InteriorCount = FMath::Clamp(
		FMath::Max(FMath::CeilToInt(CurveLen / Step) - 1, MinInterior),
		0,
		FMath::Max(0, MaxInteriorKeys));
	if (InteriorCount > 0)
	{
		FVector GhostP0;
		FVector GhostP3;
		const FVector ArriveDir = Tangent1.GetSafeNormal2D().IsNearlyZero() ? CurveDir : Tangent1.GetSafeNormal2D();
		BuildCentripetalCatmullRomGhosts(CurveStart, P1, CurveTangent0, ArriveDir, GhostP0, GhostP3);
		for (int32 Index = 1; Index <= InteriorCount; ++Index)
		{
			const double T = static_cast<double>(Index) / static_cast<double>(InteriorCount + 1);
			OutKeys.Add(EvalCentripetalCatmullRomPoint(GhostP0, CurveStart, P1, GhostP3, T));
		}
	}

	const FVector LastKey = OutKeys.Num() > 0 ? OutKeys.Last() : P0;
	if (FVector::Dist2D(LastKey, P1) > 10.0)
	{
		OutKeys.Add(P1);
	}
	else if (OutKeys.Num() > 0)
	{
		OutKeys.Last() = P1;
	}
	else
	{
		OutKeys.Add(P1);
	}
}

/** MetaRoad: mid joints are Curve (auto tangents), never CurveUser bisectors. */
void SoftenLaneExtendCornerJoint(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 JointIndex)
{
	if (!SplineComponent
		|| JointIndex <= 0
		|| JointIndex >= SplineComponent->GetNumberOfSplinePoints() - 1)
	{
		return;
	}
	if (SplineComponent->GetSplinePointType(JointIndex) != ESplinePointType::Curve)
	{
		SplineComponent->SetSplinePointType(JointIndex, ESplinePointType::Curve, false);
	}
}

/**
 * Tip + end only. Leave/Arrive from centripetal Catmull-Rom (mouth outward + end arrive).
 */
void ApplyLaneExtendHermiteSegmentHandles(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 StartIndex,
	int32 EndIndex,
	const FVector& StartTangent,
	const FVector& EndTangent,
	bool bPreserveStartArrive)
{
	if (!SplineComponent
		|| !SplineComponent->SplineCurves.Position.Points.IsValidIndex(StartIndex)
		|| !SplineComponent->SplineCurves.Position.Points.IsValidIndex(EndIndex)
		|| StartIndex == EndIndex)
	{
		return;
	}

	const FVector P0 = SplineComponent->GetLocationAtSplinePoint(StartIndex, ESplineCoordinateSpace::World);
	const FVector P1 = SplineComponent->GetLocationAtSplinePoint(EndIndex, ESplineCoordinateSpace::World);
	const double Chord = FVector::Dist2D(P0, P1);
	if (Chord <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FVector T0 = StartTangent.GetSafeNormal2D();
	FVector T1 = EndTangent.GetSafeNormal2D();
	const FVector ChordDir = (P1 - P0).GetSafeNormal2D();
	if (T0.IsNearlyZero())
	{
		T0 = ChordDir;
	}
	if (T1.IsNearlyZero())
	{
		T1 = ChordDir;
	}

	FVector Handle0World;
	FVector Handle1World;
	ComputeCentripetalCatmullRomHermiteTangents(P0, P1, T0, T1, Handle0World, Handle1World);

	SplineComponent->SetSplinePointType(StartIndex, ESplinePointType::CurveCustomTangent, false);
	if (bPreserveStartArrive)
	{
		// Continue click: only Leave for the NEW segment — keep Arrive so prior segments stay frozen.
		const FVector ArriveWorld = SplineComponent->GetArriveTangentAtSplinePoint(
			StartIndex, ESplineCoordinateSpace::World);
		SplineComponent->SetTangentsAtSplinePoint(
			StartIndex,
			ArriveWorld,
			Handle0World,
			ESplineCoordinateSpace::World,
			false);
	}
	else
	{
		SplineComponent->SetTangentAtSplinePoint(
			StartIndex, Handle0World, ESplineCoordinateSpace::World, false);
	}

	SplineComponent->SetSplinePointType(EndIndex, ESplinePointType::CurveCustomTangent, false);
	SplineComponent->SetTangentAtSplinePoint(
		EndIndex, Handle1World, ESplineCoordinateSpace::World, false);
}

void CaptureLaneExtendPointFreezes(
	const UMoKuEditSplinesComponent* SplineComponent,
	int32 IndexBegin,
	int32 IndexEndExclusive,
	TArray<FLaneExtendPointFreeze>& OutFreezes)
{
	OutFreezes.Reset();
	if (!SplineComponent || IndexBegin >= IndexEndExclusive)
	{
		return;
	}
	OutFreezes.Reserve(IndexEndExclusive - IndexBegin);
	for (int32 Index = IndexBegin; Index < IndexEndExclusive; ++Index)
	{
		if (!SplineComponent->SplineCurves.Position.Points.IsValidIndex(Index))
		{
			break;
		}
		FLaneExtendPointFreeze Freeze;
		Freeze.WorldLocation = SplineComponent->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
		Freeze.ArriveWorld = SplineComponent->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::World);
		Freeze.LeaveWorld = SplineComponent->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::World);
		Freeze.PointType = SplineComponent->GetSplinePointType(Index);
		Freeze.DistanceS = SplineComponent->GetDistanceAlongSplineAtSplinePoint(Index);
		OutFreezes.Add(Freeze);
	}
}

/** Restore committed points. Tip keeps its NEW Leave (for the new segment). */
void RestoreLaneExtendPointFreezes(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 IndexBegin,
	const TArray<FLaneExtendPointFreeze>& Freezes,
	int32 TipIndexKeepNewLeave)
{
	if (!SplineComponent || Freezes.IsEmpty())
	{
		return;
	}
	for (int32 Offset = 0; Offset < Freezes.Num(); ++Offset)
	{
		const int32 Index = IndexBegin + Offset;
		if (!SplineComponent->SplineCurves.Position.Points.IsValidIndex(Index))
		{
			break;
		}
		const FLaneExtendPointFreeze& Freeze = Freezes[Offset];
		SplineComponent->SetLocationAtSplinePoint(
			Index, Freeze.WorldLocation, ESplineCoordinateSpace::World, false);
		SplineComponent->SetSplinePointType(Index, Freeze.PointType, false);
		if (Index == TipIndexKeepNewLeave)
		{
			const FVector NewLeave = SplineComponent->GetLeaveTangentAtSplinePoint(
				Index, ESplineCoordinateSpace::World);
			SplineComponent->SetTangentsAtSplinePoint(
				Index,
				Freeze.ArriveWorld,
				NewLeave,
				ESplineCoordinateSpace::World,
				false);
		}
		else
		{
			SplineComponent->SetTangentsAtSplinePoint(
				Index,
				Freeze.ArriveWorld,
				Freeze.LeaveWorld,
				ESplineCoordinateSpace::World,
				false);
		}
	}
}

void CollectJunctionInterfaceStations(
	const UMoKuEditSplinesComponent* Spline,
	TArray<double>& OutStations)
{
	OutStations.Reset();
	const AMoKuEditSplineActor* Road = Spline
		? Cast<AMoKuEditSplineActor>(Spline->GetOwner())
		: nullptr;
	if (!Road || !Spline)
	{
		return;
	}

	const double Length = Spline->GetSplineLength();
	const double HalfW = Cast<UMetaLandRoadSplineComponent>(Spline)
		? FMath::Max(Cast<UMetaLandRoadSplineComponent>(Spline)->GetRoadHalfWidthAtS(Length * 0.5), 80.0)
		: 80.0;
	(void)HalfW;

	auto AddStation = [&OutStations](double S)
	{
		if (S < 0.0)
		{
			return;
		}
		for (const double Existing : OutStations)
		{
			if (FMath::IsNearlyEqual(Existing, S, 8.0))
			{
				return;
			}
		}
		OutStations.Add(S);
	};

	TArray<TPair<double, double>> Spans;
	Road->GetJunctionMeshGenerationSpans(Spans);
	for (const TPair<double, double>& Span : Spans)
	{
		if (Span.Key > 25.0)
		{
			AddStation(Span.Key);
		}
		if (Span.Value < Length - 25.0)
		{
			AddStation(Span.Value);
		}
	}

	if (Road->StartJunctionMeshTrimS >= 0.0)
	{
		AddStation(Road->StartJunctionMeshTrimS);
	}
	if (Road->EndJunctionMeshTrimS >= 0.0)
	{
		AddStation(Road->EndJunctionMeshTrimS);
	}
	for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
	{
		AddStation(Zone.ApproachCutS);
		AddStation(Zone.DepartCutS);
	}
}

static bool IsNearJunctionInterfaceS(double PointS, const TArray<double>& Stations)
{
	const double Tol = 40.0;
	for (const double Station : Stations)
	{
		if (FMath::Abs(PointS - Station) <= Tol)
		{
			return true;
		}
	}
	return false;
}

static bool SegmentTouchesJunctionInterface(
	const UMoKuEditSplinesComponent* Spline,
	int32 IndexA,
	int32 IndexB,
	const TArray<double>& Stations)
{
	if (!Spline
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(IndexA)
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(IndexB)
		|| Stations.IsEmpty())
	{
		return false;
	}

	const double A = Spline->GetDistanceAlongSplineAtSplinePoint(IndexA);
	const double B = Spline->GetDistanceAlongSplineAtSplinePoint(IndexB);
	const double Lo = FMath::Min(A, B) - 8.0;
	const double Hi = FMath::Max(A, B) + 8.0;
	for (const double Station : Stations)
	{
		if (Station >= Lo && Station <= Hi)
		{
			return true;
		}
	}
	return false;
}

void RestoreJunctionInterfaceFreezes(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 IndexBegin,
	const TArray<FLaneExtendPointFreeze>& Freezes)
{
	if (!SplineComponent || Freezes.IsEmpty())
	{
		return;
	}

	TArray<double> Stations;
	CollectJunctionInterfaceStations(SplineComponent, Stations);
	if (Stations.IsEmpty())
	{
		return;
	}

	for (int32 Offset = 0; Offset < Freezes.Num(); ++Offset)
	{
		const int32 Index = IndexBegin + Offset;
		if (!SplineComponent->SplineCurves.Position.Points.IsValidIndex(Index))
		{
			break;
		}
		const FLaneExtendPointFreeze& Freeze = Freezes[Offset];
		if (!IsNearJunctionInterfaceS(Freeze.DistanceS, Stations))
		{
			continue;
		}

		SplineComponent->SetLocationAtSplinePoint(
			Index, Freeze.WorldLocation, ESplineCoordinateSpace::World, false);
		SplineComponent->SetSplinePointType(Index, Freeze.PointType, false);
		SplineComponent->SetTangentsAtSplinePoint(
			Index,
			Freeze.ArriveWorld,
			Freeze.LeaveWorld,
			ESplineCoordinateSpace::World,
			false);
	}
}

static void ShiftStationsAfterBeginInsert(AMoKuEditSplineActor* Road, double AddedLen)
{
	if (!Road || AddedLen <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	Road->Modify();
	if (Road->StartJunctionMeshTrimS >= 0.0)
	{
		Road->StartJunctionMeshTrimS += AddedLen;
	}
	if (Road->EndJunctionMeshTrimS >= 0.0)
	{
		Road->EndJunctionMeshTrimS += AddedLen;
	}
	for (FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
	{
		if (Zone.ApproachCutS >= 0.0)
		{
			Zone.ApproachCutS += AddedLen;
		}
		if (Zone.DepartCutS >= 0.0)
		{
			Zone.DepartCutS += AddedLen;
		}
	}

	UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Road->GetSplineComponent());
	if (!Spline)
	{
		return;
	}

	FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
	for (FRichCurveKey& Key : Layout.ROffset.Keys)
	{
		Key.Time += static_cast<float>(AddedLen);
	}

	TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSectionsMutable();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
		if (SectionIndex == 0)
		{
			continue;
		}
		Section.SOffset += AddedLen;
	}
	Spline->UpdateLaneSectionBounds();
}

static void ShiftStationsAfterEndInsert(AMoKuEditSplineActor* Road, double MouthS, double AddedLen)
{
	if (!Road || AddedLen <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double Cut = MouthS + 1.0;
	auto ShiftIfAfter = [Cut, AddedLen](double& Station)
	{
		if (Station > Cut)
		{
			Station += AddedLen;
		}
	};

	Road->Modify();
	ShiftIfAfter(Road->StartJunctionMeshTrimS);
	ShiftIfAfter(Road->EndJunctionMeshTrimS);
	for (FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
	{
		ShiftIfAfter(Zone.ApproachCutS);
		ShiftIfAfter(Zone.DepartCutS);
	}

	UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Road->GetSplineComponent());
	if (!Spline)
	{
		return;
	}

	FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
	for (FRichCurveKey& Key : Layout.ROffset.Keys)
	{
		if (static_cast<double>(Key.Time) > Cut)
		{
			Key.Time += static_cast<float>(AddedLen);
		}
	}

	TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSectionsMutable();
	for (FMetaLandRoadLaneSection& Section : Sections)
	{
		if (Section.SOffset > Cut)
		{
			Section.SOffset += AddedLen;
		}
	}
	Spline->UpdateLaneSectionBounds();
}

FVector MouthOutwardFromSplinePoints(
	const USplineComponent* Spline,
	bool bFromBegin)
{
	if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
	{
		return FVector::ZeroVector;
	}

	if (bFromBegin)
	{
		const FVector P0 = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		const FVector P1 = Spline->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World);
		return (P0 - P1).GetSafeNormal2D();
	}

	const int32 Last = Spline->GetNumberOfSplinePoints() - 1;
	const FVector Pn = Spline->GetLocationAtSplinePoint(Last, ESplineCoordinateSpace::World);
	const FVector Prev = Spline->GetLocationAtSplinePoint(Last - 1, ESplineCoordinateSpace::World);
	return (Pn - Prev).GetSafeNormal2D();
}

static double ResolveJointHalfWidthCm(const USplineComponent* Spline, int32 PointIndex)
{
	if (const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		const double S = RoadSpline->GetDistanceAlongSplineAtSplinePoint(
			FMath::Clamp(PointIndex, 0, FMath::Max(RoadSpline->GetNumberOfSplinePoints() - 1, 0)));
		return FMath::Max(RoadSpline->GetRoadHalfWidthAtS(S), 80.0);
	}
	return FMath::Max(MetaLandRoad::DefaultRoadLaneWidth * 0.5, 80.0);
}

static double InterfaceMinAlongCm(double HalfWidthCm)
{
	return FMath::Max(HalfWidthCm, 80.0);
}

static double BodyCornerNeedAlongCm(double HalfWidthCm, double AngleRad)
{
	return MetaLandRoad::CornerFilletNeedAlongCm(HalfWidthCm, AngleRad);
}

static double WidthAwareHandleLenCm(
	double InLen,
	double OutLen,
	double AngleRad,
	double HalfWidthCm)
{
	(void)InLen;
	const double HalfW = FMath::Max(HalfWidthCm, 1.0);
	const double Span = FMath::Max(OutLen, 1.0);
	if (AngleRad < FMath::DegreesToRadians(8.0))
	{
		return FMath::Min(Span * 0.32, HalfW * 0.45);
	}

	const double Handle =
		(4.0 / 3.0) * HalfW * FMath::Tan(FMath::Clamp(AngleRad * 0.25, 0.0, 0.65));
	return FMath::Clamp(Handle, 16.0, Span * 0.32);
}

void ApplyWidthAwareNewSpanTangents(
	UMoKuEditSplinesComponent* Spline,
	int32 JointIndex,
	int32 NewTipIndex)
{
	if (!Spline
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(JointIndex)
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(NewTipIndex)
		|| JointIndex == NewTipIndex)
	{
		return;
	}

	const FVector JointPos = Spline->GetLocationAtSplinePoint(JointIndex, ESplineCoordinateSpace::World);
	const FVector TipPos = Spline->GetLocationAtSplinePoint(NewTipIndex, ESplineCoordinateSpace::World);
	const bool bTipBeforeJoint = NewTipIndex < JointIndex;
	const int32 BodyIndex = bTipBeforeJoint ? (JointIndex + 1) : (JointIndex - 1);
	if (!Spline->SplineCurves.Position.Points.IsValidIndex(BodyIndex))
	{
		return;
	}

	const FVector BodyPos = Spline->GetLocationAtSplinePoint(BodyIndex, ESplineCoordinateSpace::World);
	FVector NewDir = bTipBeforeJoint
		? (JointPos - TipPos).GetSafeNormal2D()
		: (TipPos - JointPos).GetSafeNormal2D();
	FVector BodyDir = bTipBeforeJoint
		? (BodyPos - JointPos).GetSafeNormal2D()
		: (JointPos - BodyPos).GetSafeNormal2D();
	if (NewDir.IsNearlyZero() || BodyDir.IsNearlyZero())
	{
		return;
	}

	double NewLen = FMath::Max(FVector::Dist2D(JointPos, TipPos), 80.0);
	const double BodyLen = FMath::Max(FVector::Dist2D(JointPos, BodyPos), 80.0);
	const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(BodyDir, NewDir), -1.0, 1.0));
	const double HalfW = ResolveJointHalfWidthCm(Spline, JointIndex);

	TArray<double> InterfaceStations;
	CollectJunctionInterfaceStations(Spline, InterfaceStations);
	const bool bJointIsInterface = IsNearJunctionInterfaceS(
		Spline->GetDistanceAlongSplineAtSplinePoint(JointIndex), InterfaceStations)
		|| SegmentTouchesJunctionInterface(Spline, BodyIndex, JointIndex, InterfaceStations);
	const double MinAlongCm = bJointIsInterface
		? InterfaceMinAlongCm(HalfW)
		: BodyCornerNeedAlongCm(HalfW, Angle);
	if (NewLen < MinAlongCm)
	{
		FVector Pushed = JointPos + NewDir * MinAlongCm;
		Pushed.Z = TipPos.Z;
		Spline->SetLocationAtSplinePoint(NewTipIndex, Pushed, ESplineCoordinateSpace::World, false);
		NewLen = MinAlongCm;
	}
	const double Handle = WidthAwareHandleLenCm(BodyLen, NewLen, Angle, HalfW);

	FVector Arrive = Spline->GetArriveTangentAtSplinePoint(JointIndex, ESplineCoordinateSpace::World);
	FVector Leave = Spline->GetLeaveTangentAtSplinePoint(JointIndex, ESplineCoordinateSpace::World);
	Spline->SetSplinePointType(JointIndex, ESplinePointType::CurveCustomTangent, false);
	if (bJointIsInterface)
	{
		if (bTipBeforeJoint)
		{
			Spline->SetTangentsAtSplinePoint(
				JointIndex,
				NewDir * Handle,
				Leave,
				ESplineCoordinateSpace::World,
				false);
		}
		else
		{
			Spline->SetTangentsAtSplinePoint(
				JointIndex,
				Arrive,
				NewDir * Handle,
				ESplineCoordinateSpace::World,
				false);
		}
	}
	else if (bTipBeforeJoint)
	{
		Spline->SetTangentsAtSplinePoint(
			JointIndex,
			NewDir * Handle,
			BodyDir * Handle,
			ESplineCoordinateSpace::World,
			false);
	}
	else
	{
		Spline->SetTangentsAtSplinePoint(
			JointIndex,
			BodyDir * Handle,
			NewDir * Handle,
			ESplineCoordinateSpace::World,
			false);
	}

	if (Spline->SplineCurves.Position.Points.IsValidIndex(NewTipIndex))
	{
		Spline->SetSplinePointType(NewTipIndex, ESplinePointType::CurveCustomTangent, false);
		Spline->SetTangentsAtSplinePoint(
			NewTipIndex,
			NewDir * Handle,
			NewDir * Handle,
			ESplineCoordinateSpace::World,
			false);
	}
}

static int32 FindContinueDrawInsertIndex(
	const USplineComponent* Spline,
	bool bInsertAtStart,
	double MouthS)
{
	const int32 N = Spline->GetNumberOfSplinePoints();
	if (MouthS < 0.0)
	{
		return bInsertAtStart ? 0 : N;
	}
	if (bInsertAtStart)
	{
		for (int32 i = 0; i < N; ++i)
		{
			if (Spline->GetDistanceAlongSplineAtSplinePoint(i) >= MouthS - 1.0)
			{
				return i;
			}
		}
		return 0;
	}
	// Insert before leftover hub keys. +10cm wrote the U after them (fork).
	// +1cm still left keys sitting in (MouthS, MouthS+1] in front of the U.
	for (int32 i = 0; i < N; ++i)
	{
		if (Spline->GetDistanceAlongSplineAtSplinePoint(i) > MouthS + 0.01)
		{
			return i;
		}
	}
	return N;
}

bool TrySnapDrawToRoadCrossing(
	AMoKuEditSplineActor* DrawingRoad,
	const FVector& FromPos,
	const FVector& Cursor,
	UWorld* World,
	float ReachCm,
	FVector& OutCross)
{
	if (!DrawingRoad || !World)
	{
		return false;
	}

	const FVector2D A(FromPos.X, FromPos.Y);
	FVector2D B(Cursor.X, Cursor.Y);
	FVector2D AB = B - A;
	if (AB.SizeSquared() < 1.0)
	{
		return false;
	}
	const double Reach = FMath::Max(static_cast<double>(ReachCm), 280.0);
	AB.Normalize();
	B = FVector2D(FromPos.X, FromPos.Y) + AB * (FVector::Dist2D(FromPos, Cursor) + Reach);

	double BestDist = 1.0e12;
	FVector Best = FVector::ZeroVector;
	bool bFound = false;
	for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
	{
		AMoKuEditSplineActor* Other = *It;
		USplineComponent* Spline = Other ? Other->GetSplineComponent() : nullptr;
		if (!Spline
			|| Other == DrawingRoad
			|| Spline->GetNumberOfSplinePoints() < 2)
		{
			continue;
		}
		const float Length = Spline->GetSplineLength();
		if (Length <= 1.0f)
		{
			continue;
		}
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 250.0f) * 8, 16, 128);
		FVector Prev = Spline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const FVector Cur = Spline->GetLocationAtDistanceAlongSpline(
				Length * static_cast<float>(Step) / static_cast<float>(Steps),
				ESplineCoordinateSpace::World);
			FVector2D Hit;
			double T1 = 0.0;
			double T2 = 0.0;
			if (!MoKuSplineEditorUtils::DoSegmentsIntersect(
					A,
					B,
					FVector2D(Prev.X, Prev.Y),
					FVector2D(Cur.X, Cur.Y),
					Hit,
					T1,
					T2))
			{
				Prev = Cur;
				continue;
			}
			const FVector Cross(Hit.X, Hit.Y, 0.5 * (FromPos.Z + Cur.Z));
			const double Dist = FVector::Dist2D(Cross, Cursor);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = Cross;
				bFound = true;
			}
			Prev = Cur;
		}
	}
	if (!bFound)
	{
		return false;
	}
	OutCross = Best;
	return true;
}

static void MoveOwnedMouthCut(
	AMoKuEditSplineActor* Road,
	bool bAttachAtStart,
	double OldTipS,
	double NewTipS)
{
	if (!Road || NewTipS < 0.0)
	{
		return;
	}

	Road->Modify();
	constexpr double OwnMouthTol = 80.0;
	auto Near = [OldTipS](double Station) -> bool
	{
		return Station >= 0.0 && FMath::Abs(Station - OldTipS) <= OwnMouthTol;
	};

	if (bAttachAtStart && Near(Road->StartJunctionMeshTrimS))
	{
		Road->StartJunctionMeshTrimS = NewTipS;
	}
	else if (!bAttachAtStart && Near(Road->EndJunctionMeshTrimS))
	{
		Road->EndJunctionMeshTrimS = NewTipS;
	}

	int32 BestZone = INDEX_NONE;
	bool bBestApproach = false;
	double BestAbs = OwnMouthTol + 1.0;
	for (int32 i = 0; i < Road->JunctionTrimZones.Num(); ++i)
	{
		const double Approach = Road->JunctionTrimZones[i].ApproachCutS;
		const double Depart = Road->JunctionTrimZones[i].DepartCutS;
		if (Near(Approach))
		{
			const double Abs = FMath::Abs(Approach - OldTipS);
			if (Abs < BestAbs)
			{
				BestAbs = Abs;
				BestZone = i;
				bBestApproach = true;
			}
		}
		if (Near(Depart))
		{
			const double Abs = FMath::Abs(Depart - OldTipS);
			if (Abs < BestAbs)
			{
				BestAbs = Abs;
				BestZone = i;
				bBestApproach = false;
			}
		}
	}
	if (BestZone != INDEX_NONE)
	{
		FMetaLandRoadJunctionTrimZone& Zone = Road->JunctionTrimZones[BestZone];
		const double Approach = Zone.ApproachCutS;
		const double Depart = Zone.DepartCutS;
		if (bBestApproach)
		{
			if (Depart < 0.0 || (NewTipS > Approach && NewTipS <= Depart + 1.0))
			{
				Zone.ApproachCutS = NewTipS;
			}
		}
		else if (Approach < 0.0 || (NewTipS < Depart && NewTipS >= Approach - 1.0))
		{
			Zone.DepartCutS = NewTipS;
		}
	}
}

bool AddContinueDrawSplinePoint(
	AMoKuEditSplineActor* Road,
	const FVector& WorldPos,
	bool bInsertAtStart,
	double MouthS,
	const FVector& MouthCenter)
{
	if (!Road)
	{
		return false;
	}

	UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent();
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return false;
	}

	FVector SnapPos = WorldPos;
	ProjectPointOntoLandscapeSurface(Road->GetWorld(), SnapPos, Road);

	const int32 NumBefore = Spline->GetNumberOfSplinePoints();
	TArray<FLaneExtendPointFreeze> Freezes;
	CaptureLaneExtendPointFreezes(Spline, 0, NumBefore, Freezes);

	const int32 MouthIndex = bInsertAtStart ? 0 : (NumBefore - 1);
	FVector MouthPos = (MouthS >= 0.0 && !MouthCenter.IsNearlyZero())
		? MouthCenter
		: Spline->GetLocationAtSplinePoint(MouthIndex, ESplineCoordinateSpace::World);
	if (MouthS >= 0.0)
	{
		MouthPos = Spline->GetLocationAtDistanceAlongSpline(MouthS, ESplineCoordinateSpace::World);
	}
	FVector Outward;
	if (MouthS >= 0.0)
	{
		Outward = Spline->GetDirectionAtDistanceAlongSpline(MouthS, ESplineCoordinateSpace::World).GetSafeNormal2D();
		if (bInsertAtStart)
		{
			Outward = -Outward;
		}
	}
	else
	{
		Outward = MouthOutwardFromSplinePoints(Spline, bInsertAtStart);
	}
	const double HalfW = ResolveJointHalfWidthCm(Spline, MouthIndex);
	const double MinAlongCm = InterfaceMinAlongCm(HalfW);
	const double Along = FVector::DotProduct(SnapPos - MouthPos, Outward);
	if (!Outward.IsNearlyZero() && Along < MinAlongCm)
	{
		const FVector Lateral = SnapPos - MouthPos - Outward * Along;
		SnapPos = MouthPos + Outward * MinAlongCm + Lateral;
		ProjectPointOntoLandscapeSurface(Road->GetWorld(), SnapPos, Road);
	}

	Road->Modify();
	Spline->Modify();
	const int32 InsertIndex = FindContinueDrawInsertIndex(Spline, bInsertAtStart, MouthS);
	Spline->AddSplinePointAtIndex(SnapPos, InsertIndex, ESplineCoordinateSpace::World, false);

	const int32 NewIndex = InsertIndex;
	const int32 NeighborIndex = bInsertAtStart
		? FMath::Min(NewIndex + 1, Spline->GetNumberOfSplinePoints() - 1)
		: FMath::Max(NewIndex - 1, 0);
	const int32 RestoreBegin = bInsertAtStart ? (NewIndex + 1) : 0;
	RestoreJunctionInterfaceFreezes(Spline, RestoreBegin, Freezes);
	ApplyWidthAwareNewSpanTangents(Spline, NeighborIndex, NewIndex);
	RestoreJunctionInterfaceFreezes(Spline, RestoreBegin, Freezes);

	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
		if (!bInsertAtStart)
		{
			RoadSpline->StretchTipSectionToSplineLength(/*bAttachAtSplineStart*/ false);
		}
	}
	else
	{
		Spline->UpdateSpline();
	}

	if (bInsertAtStart && InsertIndex == 0 && Spline->GetNumberOfSplinePoints() >= 2)
	{
		ShiftStationsAfterBeginInsert(Road, Spline->GetDistanceAlongSplineAtSplinePoint(1));
		if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
		{
			RoadSpline->StretchTipSectionToSplineLength(/*bAttachAtSplineStart*/ true);
		}
	}
	const double NewTipS = Spline->GetDistanceAlongSplineAtSplinePoint(NewIndex);
	MoveOwnedMouthCut(Road, bInsertAtStart, MouthS, NewTipS);

	Road->UpdateInterpolateType(NeighborIndex);
	HideEditorSplineRibbon(Spline);
	return true;
}

double CrossXY(const FVector& A, const FVector& B)
{
	return A.X * B.Y - A.Y * B.X;
}

bool SegmentsIntersectXY(const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1)
{
	const FVector DA = A1 - A0;
	const FVector DB = B1 - B0;
	const double Den = CrossXY(DA, DB);
	if (FMath::Abs(Den) < 1e-6)
	{
		return false;
	}
	const FVector AB = B0 - A0;
	const double T = CrossXY(AB, DB) / Den;
	const double U = CrossXY(AB, DA) / Den;
	return T > 0.02 && T < 0.98 && U > 0.02 && U < 0.98;
}

int32 CountGuidanceRailCrossings(const TArray<FVector>& Left, const TArray<FVector>& Right)
{
	int32 Hits = 0;
	for (int32 i = 0; i + 1 < Left.Num(); ++i)
	{
		for (int32 j = 0; j + 1 < Right.Num(); ++j)
		{
			if (SegmentsIntersectXY(Left[i], Left[i + 1], Right[j], Right[j + 1]))
			{
				++Hits;
			}
		}
	}
	return Hits;
}

static double MinCenterlineRadiusCm(const TArray<FVector>& Center)
{
	double MinR = 1.0e12;
	for (int32 i = 1; i + 1 < Center.Num(); ++i)
	{
		const FVector A = (Center[i] - Center[i - 1]).GetSafeNormal2D();
		const FVector B = (Center[i + 1] - Center[i]).GetSafeNormal2D();
		if (A.IsNearlyZero() || B.IsNearlyZero())
		{
			continue;
		}
		const double Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0));
		if (Turn < 0.03)
		{
			continue;
		}
		const double Seg = 0.5 * (
			FVector::Dist2D(Center[i - 1], Center[i]) + FVector::Dist2D(Center[i], Center[i + 1]));
		MinR = FMath::Min(MinR, Seg / Turn);
	}
	return MinR;
}

bool IsLeaveArrivePlanUnusable(const FLeaveArrivePlan& Plan, double HalfWidthCm)
{
	if (Plan.Kind != FLeaveArrivePlan::EKind::DubinsCSC)
	{
		return false;
	}
	// Link invalid = inner radius would go negative. Body-corner NeedR is not a C-link gate.
	return Plan.Radius + 1.0 < FMath::Max(HalfWidthCm, 80.0);
}

static double DiffuseRadiusForLink(
	const FVector& P0,
	const FVector& P1,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double HalfWidthCm)
{
	(void)P0;
	(void)P1;
	(void)LeaveDir;
	(void)ArriveDir;
	// Inner offset exists only when R > half-width. 1.15× leaves inner ≈ 0.15×HalfW (Fig1 缺口).
	// 2×HalfW keeps inner radius ≈ HalfW. Do not use body-corner DiffuseR (θ≈π → 红线).
	return FMath::Max(HalfWidthCm * 2.0, 80.0);
}

static bool IsGuidanceCorridorUnusable(
	const FLeaveArrivePlan* Plan,
	const TArray<FVector>& Center,
	const TArray<FVector>& Left,
	const TArray<FVector>& Right,
	double HalfWidthCm,
	bool bMixToMouth)
{
	if (CountGuidanceRailCrossings(Left, Right) > 0)
	{
		return true;
	}
	if (Plan && IsLeaveArrivePlanUnusable(*Plan, HalfWidthCm))
	{
		return true;
	}
	if (bMixToMouth && Center.Num() >= 3
		&& MinCenterlineRadiusCm(Center) + 1.0 < FMath::Max(HalfWidthCm, 80.0))
	{
		return true;
	}
	return false;
}

static void BuildErrorMouthLinkRails(
	const FVector& TipLeft,
	const FVector& TipRight,
	const FVector& EndLeft,
	const FVector& EndRight,
	float PlaneZ,
	TArray<FVector>& OutLeft,
	TArray<FVector>& OutRight,
	TArray<FVector>* OutCenter)
{
	FVector EL = EndLeft;
	FVector ER = EndRight;
	const FVector TipAcross = (TipRight - TipLeft).GetSafeNormal2D();
	const FVector EndAcross = (ER - EL).GetSafeNormal2D();
	if (!TipAcross.IsNearlyZero() && !EndAcross.IsNearlyZero()
		&& FVector::DotProduct(TipAcross, EndAcross) < 0.0)
	{
		Swap(EL, ER);
	}
	const bool bAlignedCross = SegmentsIntersectXY(TipLeft, EL, TipRight, ER);
	const bool bSwappedCross = SegmentsIntersectXY(TipLeft, ER, TipRight, EL);
	if (bAlignedCross && !bSwappedCross)
	{
		Swap(EL, ER);
	}
	OutLeft.Reset();
	OutRight.Reset();
	OutLeft.Add(TipLeft);
	OutLeft.Add(EL);
	OutRight.Add(TipRight);
	OutRight.Add(ER);
	FlattenGuidanceRailToPlaneZ(OutLeft, PlaneZ);
	FlattenGuidanceRailToPlaneZ(OutRight, PlaneZ);
	if (OutCenter)
	{
		OutCenter->Reset();
		OutCenter->Add((TipLeft + TipRight) * 0.5);
		OutCenter->Add((EL + ER) * 0.5);
		FlattenGuidanceRailToPlaneZ(*OutCenter, PlaneZ);
	}
}

FVector EvalCubicHermitePoint(
	const FVector& A,
	const FVector& B,
	const FVector& DirA,
	const FVector& DirB,
	double HandleA,
	double HandleB,
	double Alpha)
{
	const double T = FMath::Clamp(Alpha, 0.0, 1.0);
	const double T2 = T * T;
	const double T3 = T2 * T;
	const FVector M0 = DirA * HandleA;
	const FVector M1 = DirB * HandleB;
	FVector Point = A * (2.0 * T3 - 3.0 * T2 + 1.0)
		+ M0 * (T3 - 2.0 * T2 + T)
		+ B * (-2.0 * T3 + 3.0 * T2)
		+ M1 * (T3 - T2);
	Point.Z = FMath::Lerp(A.Z, B.Z, static_cast<float>(T));
	return Point;
}

FVector EvalCubicHermiteTangent(
	const FVector& A,
	const FVector& B,
	const FVector& DirA,
	const FVector& DirB,
	double HandleA,
	double HandleB,
	double Alpha)
{
	const double T = FMath::Clamp(Alpha, 0.0, 1.0);
	const double T2 = T * T;
	const FVector M0 = DirA * HandleA;
	const FVector M1 = DirB * HandleB;
	const FVector D = A * (6.0 * T2 - 6.0 * T)
		+ M0 * (3.0 * T2 - 4.0 * T + 1.0)
		+ B * (-6.0 * T2 + 6.0 * T)
		+ M1 * (3.0 * T2 - 2.0 * T);
	return D.GetSafeNormal2D();
}

FVector EvalPoseHermitePoint(const FLeaveArrivePlan& Plan, double Alpha)
{
	return EvalCubicHermitePoint(
		Plan.P0, Plan.P1, Plan.Leave, Plan.Arrive, Plan.Handle0, Plan.Handle1, Alpha);
}

FVector EvalPoseHermiteTangent(const FLeaveArrivePlan& Plan, double Alpha)
{
	return EvalCubicHermiteTangent(
		Plan.P0, Plan.P1, Plan.Leave, Plan.Arrive, Plan.Handle0, Plan.Handle1, Alpha);
}

FVector EvalLeaveArriveArcPoint(const FLeaveArrivePlan& Plan, double Alpha)
{
	const FVector RA = Plan.FilletA - Plan.FilletCenter;
	const FVector RB = Plan.FilletB - Plan.FilletCenter;
	const double Radius = FMath::Max(RA.Size2D(), 1.0);
	const double Ang0 = FMath::Atan2(RA.Y, RA.X);
	const double Ang1 = FMath::Atan2(RB.Y, RB.X);
	double Sweep = Ang1 - Ang0;
	while (Sweep <= -PI)
	{
		Sweep += 2.0 * PI;
	}
	while (Sweep > PI)
	{
		Sweep -= 2.0 * PI;
	}
	if (Plan.SweepSign * Sweep < 0.0)
	{
		Sweep += (Plan.SweepSign > 0.0) ? (2.0 * PI) : (-2.0 * PI);
	}
	const double Ang = Ang0 + Sweep * FMath::Clamp(Alpha, 0.0, 1.0);
	FVector Point = Plan.FilletCenter + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * Radius;
	Point.Z = FMath::Lerp(Plan.FilletA.Z, Plan.FilletB.Z, static_cast<float>(Alpha));
	return Point;
}

static double DubinsMod2Pi(double Angle)
{
	const double Wrapped = FMath::Fmod(Angle, 2.0 * PI);
	return (Wrapped < 0.0) ? (Wrapped + 2.0 * PI) : Wrapped;
}

static bool DubinsWordLSL(double Alpha, double Beta, double D, double& T, double& P, double& Q)
{
	const double Tmp0 = D + FMath::Sin(Alpha) - FMath::Sin(Beta);
	const double PSq = 2.0 + D * D - 2.0 * FMath::Cos(Alpha - Beta)
		+ 2.0 * D * (FMath::Sin(Alpha) - FMath::Sin(Beta));
	if (PSq < 0.0)
	{
		return false;
	}
	const double Tmp1 = FMath::Atan2(FMath::Cos(Beta) - FMath::Cos(Alpha), Tmp0);
	T = DubinsMod2Pi(Tmp1 - Alpha);
	P = FMath::Sqrt(PSq);
	Q = DubinsMod2Pi(Beta - Tmp1);
	return true;
}

static bool DubinsWordRSR(double Alpha, double Beta, double D, double& T, double& P, double& Q)
{
	const double Tmp0 = D - FMath::Sin(Alpha) + FMath::Sin(Beta);
	const double PSq = 2.0 + D * D - 2.0 * FMath::Cos(Alpha - Beta)
		+ 2.0 * D * (FMath::Sin(Beta) - FMath::Sin(Alpha));
	if (PSq < 0.0)
	{
		return false;
	}
	const double Tmp1 = FMath::Atan2(FMath::Cos(Alpha) - FMath::Cos(Beta), Tmp0);
	T = DubinsMod2Pi(Alpha - Tmp1);
	P = FMath::Sqrt(PSq);
	Q = DubinsMod2Pi(-Beta + Tmp1);
	return true;
}

static bool DubinsWordLSR(double Alpha, double Beta, double D, double& T, double& P, double& Q)
{
	const double PSq = -2.0 + D * D + 2.0 * FMath::Cos(Alpha - Beta)
		+ 2.0 * D * (FMath::Sin(Alpha) + FMath::Sin(Beta));
	if (PSq < 0.0)
	{
		return false;
	}
	P = FMath::Sqrt(PSq);
	const double Tmp0 = FMath::Atan2(-FMath::Cos(Alpha) - FMath::Cos(Beta), D + FMath::Sin(Alpha) + FMath::Sin(Beta))
		- FMath::Atan2(-2.0, P);
	T = DubinsMod2Pi(Tmp0 - Alpha);
	Q = DubinsMod2Pi(Tmp0 - DubinsMod2Pi(Beta));
	return true;
}

static bool DubinsWordRSL(double Alpha, double Beta, double D, double& T, double& P, double& Q)
{
	const double PSq = -2.0 + D * D + 2.0 * FMath::Cos(Alpha - Beta)
		- 2.0 * D * (FMath::Sin(Alpha) + FMath::Sin(Beta));
	if (PSq < 0.0)
	{
		return false;
	}
	P = FMath::Sqrt(PSq);
	const double Tmp0 = FMath::Atan2(FMath::Cos(Alpha) + FMath::Cos(Beta), D - FMath::Sin(Alpha) - FMath::Sin(Beta))
		- FMath::Atan2(2.0, P);
	T = DubinsMod2Pi(Alpha - Tmp0);
	Q = DubinsMod2Pi(Beta - Tmp0);
	return true;
}

static void DubinsAdvance(
	FVector& Pos,
	double& Heading,
	double Radius,
	double NormalizedLen,
	int32 SegType)
{
	if (SegType == 1)
	{
		const double Dist = NormalizedLen * Radius;
		Pos += FVector(FMath::Cos(Heading), FMath::Sin(Heading), 0.0) * Dist;
		return;
	}
	if (SegType == 0)
	{
		const double H0 = Heading;
		Pos.X += Radius * (FMath::Sin(H0 + NormalizedLen) - FMath::Sin(H0));
		Pos.Y += Radius * (-FMath::Cos(H0 + NormalizedLen) + FMath::Cos(H0));
		Heading = H0 + NormalizedLen;
		return;
	}
	const double H0 = Heading;
	Pos.X += Radius * (-FMath::Sin(H0 - NormalizedLen) + FMath::Sin(H0));
	Pos.Y += Radius * (FMath::Cos(H0 - NormalizedLen) - FMath::Cos(H0));
	Heading = H0 - NormalizedLen;
}

static void DubinsSegTypes(int32 Word, int32& A, int32& B, int32& C)
{
	// 0=L 1=S 2=R
	switch (Word)
	{
	case 1: A = 0; B = 1; C = 2; break;
	case 2: A = 2; B = 1; C = 0; break;
	case 3: A = 2; B = 1; C = 2; break;
	default: A = 0; B = 1; C = 0; break;
	}
}

void EvalDubinsCSC(
	const FLeaveArrivePlan& Plan,
	double Alpha,
	FVector& OutPos,
	FVector& OutTangent)
{
	OutPos = Plan.P0;
	const double Heading0 = FMath::Atan2(Plan.Leave.Y, Plan.Leave.X);
	double Heading = Heading0;
	const double R = FMath::Max(Plan.Radius, 1.0);
	const double TotalNorm = Plan.DubinsT + Plan.DubinsP + Plan.DubinsQ;
	double Remain = FMath::Clamp(Alpha, 0.0, 1.0) * TotalNorm;
	int32 SegA = 0, SegB = 1, SegC = 0;
	DubinsSegTypes(Plan.DubinsWord, SegA, SegB, SegC);
	const double Lens[3] = { Plan.DubinsT, Plan.DubinsP, Plan.DubinsQ };
	const int32 Types[3] = { SegA, SegB, SegC };
	for (int32 i = 0; i < 3; ++i)
	{
		const double Take = FMath::Min(Remain, Lens[i]);
		DubinsAdvance(OutPos, Heading, R, Take, Types[i]);
		Remain -= Take;
	}
	OutPos.Z = FMath::Lerp(Plan.P0.Z, Plan.P1.Z, static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0)));
	OutTangent = FVector(FMath::Cos(Heading), FMath::Sin(Heading), 0.0);
}

bool TryMakeLeaveArrivePlan_DubinsCSC(
	const FVector& P0,
	const FVector& P1,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double MinRadiusCm,
	FLeaveArrivePlan& OutPlan)
{
	OutPlan = FLeaveArrivePlan();
	OutPlan.P0 = P0;
	OutPlan.P1 = P1;
	const FVector Leave = LeaveDir.GetSafeNormal2D();
	const FVector Arrive = ArriveDir.GetSafeNormal2D();
	if (Leave.IsNearlyZero() || Arrive.IsNearlyZero())
	{
		return false;
	}

	const double InvertR = FMath::Max(MinRadiusCm, 80.0);
	const double ComfortR = InvertR * 1.2;
	OutPlan.Leave = Leave;
	OutPlan.Arrive = Arrive;

	const FVector Delta = P1 - P0;
	const double Dist = Delta.Size2D();
	if (Dist < 1.0)
	{
		return false;
	}

	const double Theta = FMath::Atan2(Delta.Y, Delta.X);
	const double Head0 = FMath::Atan2(Leave.Y, Leave.X);
	const double Head1 = FMath::Atan2(Arrive.Y, Arrive.X);

	auto TryWordsAtRadius = [&](double Radius, bool bSameSideOnly, bool bCrossOnly, int32& OutWord, double& OutT, double& OutP, double& OutQ) -> bool
	{
		const double D = Dist / Radius;
		const double Alpha = DubinsMod2Pi(Head0 - Theta);
		const double Beta = DubinsMod2Pi(Head1 - Theta);
		double BestLen = 1.0e12;
		int32 BestWord = INDEX_NONE;
		double BestT = 0.0, BestP = 0.0, BestQ = 0.0;
		auto Consider = [&](int32 Word, double T, double P, double Q)
		{
			if (T > PI + 0.15 || Q > PI + 0.15)
			{
				return;
			}
			const double Len = T + P + Q;
			if (Len < BestLen)
			{
				BestLen = Len;
				BestWord = Word;
				BestT = T;
				BestP = P;
				BestQ = Q;
			}
		};

		double T = 0.0, P = 0.0, Q = 0.0;
		if (DubinsWordLSL(Alpha, Beta, D, T, P, Q)) { Consider(0, T, P, Q); }
		if (DubinsWordRSR(Alpha, Beta, D, T, P, Q)) { Consider(3, T, P, Q); }
		if (!bSameSideOnly)
		{
			if (DubinsWordLSR(Alpha, Beta, D, T, P, Q)) { Consider(1, T, P, Q); }
			if (DubinsWordRSL(Alpha, Beta, D, T, P, Q)) { Consider(2, T, P, Q); }
		}
		if (BestWord == INDEX_NONE)
		{
			return false;
		}
		OutWord = BestWord;
		OutT = BestT;
		OutP = BestP;
		OutQ = BestQ;
		return true;
	};

	auto CollectRadii = [](double Lo, double Hi, TArray<double>& OutRadii)
	{
		OutRadii.Reset();
		const double Low = FMath::Max(Lo, 80.0);
		const double High = FMath::Max(Hi, Low);
		for (double Radius = High; Radius > Low * 1.02; Radius *= 0.8)
		{
			OutRadii.Add(Radius);
		}
		OutRadii.Add(Low);
	};

	int32 Word = INDEX_NONE;
	double T = 0.0, P = 0.0, Q = 0.0;
	double UsedR = ComfortR;
	bool bFound = false;
	double BestCost = 1.0e12;
	TArray<double> Radii;
	Radii.Reserve(12);
	auto Search = [&](double Lo, double Hi, bool bSameSideOnly, bool bCrossOnly)
	{
		CollectRadii(Lo, Hi, Radii);
		for (const double Radius : Radii)
		{
			int32 TryWord = INDEX_NONE;
			double TryT = 0.0, TryP = 0.0, TryQ = 0.0;
			if (!TryWordsAtRadius(Radius, bSameSideOnly, bCrossOnly, TryWord, TryT, TryP, TryQ))
			{
				continue;
			}
			const double Cost = (TryT + TryP + TryQ) * Radius;
			if (Cost < BestCost)
			{
				BestCost = Cost;
				Word = TryWord;
				T = TryT;
				P = TryP;
				Q = TryQ;
				UsedR = Radius;
				bFound = true;
			}
		}
	};

	Search(InvertR, ComfortR, /*bSameSideOnly*/ true, /*bCrossOnly*/ false);
	Search(InvertR, ComfortR, /*bSameSideOnly*/ false, /*bCrossOnly*/ false);
	if (!bFound)
	{
		return false;
	}

	OutPlan.Radius = UsedR;
	OutPlan.MinRadius = UsedR;
	OutPlan.DubinsWord = Word;
	OutPlan.DubinsT = T;
	OutPlan.DubinsP = P;
	OutPlan.DubinsQ = Q;
	OutPlan.Kind = FLeaveArrivePlan::EKind::DubinsCSC;
	return true;
}

static FVector SnapTravelToMouthBar(
	const FVector& MouthLeft,
	const FVector& MouthRight,
	const FVector& Travel)
{
	const FVector Across = (MouthRight - MouthLeft).GetSafeNormal2D();
	if (Across.IsNearlyZero())
	{
		return Travel.GetSafeNormal2D();
	}
	FVector BarN(-Across.Y, Across.X, 0.0);
	if (!BarN.Normalize())
	{
		return Travel.GetSafeNormal2D();
	}
	const FVector T = Travel.GetSafeNormal2D();
	if (T.IsNearlyZero())
	{
		return BarN;
	}
	return (FVector::DotProduct(T, BarN) >= 0.0) ? BarN : -BarN;
}

static void MouthBarAtSplinePoint(
	const USplineComponent* Spline,
	int32 PointIndex,
	FVector& OutLeft,
	FVector& OutRight)
{
	OutLeft = FVector::ZeroVector;
	OutRight = FVector::ZeroVector;
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return;
	}
	const int32 Index = FMath::Clamp(PointIndex, 0, Spline->GetNumberOfSplinePoints() - 1);
	const FVector Center = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
	double S = 0.0;
	if (const UMetaLandRoadSplineComponent* Road = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		S = Road->GetDistanceAlongSplineAtSplinePoint(Index);
	}
	FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(S, ESplineCoordinateSpace::World);
	Right.Z = 0.0;
	if (!Right.Normalize())
	{
		Right = FVector::RightVector;
	}
	const double HalfW = ResolveJointHalfWidthCm(Spline, Index);
	OutLeft = Center - Right * HalfW;
	OutRight = Center + Right * HalfW;
}

static double MouthReserveAlongCm(const FVector& Dir, const FVector& Chord, double HalfW)
{
	if (Dir.IsNearlyZero() || Chord.IsNearlyZero())
	{
		return 0.0;
	}
	const double Hook = FMath::Acos(
		FMath::Clamp(FVector::DotProduct(Dir, Chord), -1.0, 1.0));
	if (Hook < FMath::DegreesToRadians(12.0))
	{
		return 0.0;
	}
	// Floor HalfW so the corridor leaves along the mouth before Dubins turns (Fig3).
	return FMath::Max(MetaLandRoad::CornerFilletNeedAlongCm(HalfW, Hook), HalfW);
}

static void FitMouthReserveLeads(
	double Dist,
	double HalfW,
	double MinRadiusCm,
	double& Lead0,
	double& Lead1)
{
	const double MinDubins = FMath::Max(HalfW * 2.0, MinRadiusCm);
	const double Budget = Dist - MinDubins;
	if (Lead0 + Lead1 <= FMath::Max(Budget, 0.0))
	{
		return;
	}
	if (Budget <= 1.0)
	{
		Lead0 = 0.0;
		Lead1 = 0.0;
		return;
	}
	const double Scale = Budget / FMath::Max(Lead0 + Lead1, 1.0);
	Lead0 *= Scale;
	Lead1 *= Scale;
}

bool TryMakeLeaveArrivePlan(
	const FVector& P0,
	const FVector& P1,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double MinRadiusCm,
	FLeaveArrivePlan& OutPlan)
{
	const FVector Leave = LeaveDir.GetSafeNormal2D();
	const FVector Arrive = ArriveDir.GetSafeNormal2D();
	if (!TryMakeLeaveArrivePlan_DubinsCSC(
		P0, P1, Leave, Arrive, MinRadiusCm, OutPlan))
	{
		return false;
	}

	// Mouth-reserve leads were a straight stub along the bar, then Dubins from
	// an inset point. Green kinked at the orange mouth; A overlapped that stub.
	OutPlan.Stub0 = P0;
	OutPlan.Stub1 = P1;
	OutPlan.LeaveLen = 0.0;
	OutPlan.ArriveLen = 0.0;
	return true;
}

void AppendArcXY(
	const FVector& Center,
	const FVector& Start,
	const FVector& End,
	double SweepSign,
	double StepCm,
	TArray<FVector>& OutSamples,
	bool bIncludeStart)
{
	const FVector RA = Start - Center;
	const FVector RB = End - Center;
	const double Radius = FMath::Max(RA.Size2D(), 1.0);
	const double Ang0 = FMath::Atan2(RA.Y, RA.X);
	const double Ang1 = FMath::Atan2(RB.Y, RB.X);
	double Sweep = Ang1 - Ang0;
	while (Sweep <= -PI)
	{
		Sweep += 2.0 * PI;
	}
	while (Sweep > PI)
	{
		Sweep -= 2.0 * PI;
	}
	if (SweepSign * Sweep < 0.0)
	{
		Sweep += (SweepSign > 0.0) ? (2.0 * PI) : (-2.0 * PI);
	}
	const double ArcLen = FMath::Abs(Sweep) * Radius;
	const int32 Segs = MetaLandRoad::LeaveArriveSampleSegs(ArcLen, StepCm);
	for (int32 i = bIncludeStart ? 0 : 1; i <= Segs; ++i)
	{
		const double Alpha = static_cast<double>(i) / static_cast<double>(Segs);
		const double Ang = Ang0 + Sweep * Alpha;
		FVector Point = Center + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * Radius;
		Point.Z = FMath::Lerp(Start.Z, End.Z, static_cast<float>(Alpha));
		OutSamples.Add(Point);
	}
}

void SampleLeaveArrivePlan(
	const FLeaveArrivePlan& Plan,
	double HalfWidthCm,
	TArray<FVector>& OutSamples,
	int32 MaxSegs)
{
	OutSamples.Reset();
	const double StepCm = MetaLandRoad::LeaveArriveChordStepCm(
		Plan.Radius > 1.0 ? Plan.Radius : Plan.MinRadius,
		HalfWidthCm);
	const int32 SegCap = (MaxSegs > 0)
		? FMath::Min(MaxSegs, MetaLandRoad::LeaveArriveSampleSegCap)
		: MetaLandRoad::LeaveArrivePreviewSegCap;
	switch (Plan.Kind)
	{
	case FLeaveArrivePlan::EKind::CornerFillet:
		AppendResampledSegment(Plan.P0, Plan.FilletA, StepCm, OutSamples, true);
		AppendArcXY(Plan.FilletCenter, Plan.FilletA, Plan.FilletB, Plan.SweepSign, StepCm, OutSamples, false);
		AppendResampledSegment(Plan.FilletB, Plan.P1, StepCm, OutSamples, false);
		break;
	case FLeaveArrivePlan::EKind::CornerSharp:
		AppendResampledSegment(Plan.P0, Plan.Corner, StepCm, OutSamples, true);
		AppendResampledSegment(Plan.Corner, Plan.P1, StepCm, OutSamples, false);
		break;
	case FLeaveArrivePlan::EKind::PoseHermite:
	{
		const double EstLen = FMath::Max(
			FVector::Dist2D(Plan.P0, Plan.P1),
			0.5 * (Plan.Handle0 + Plan.Handle1));
		const int32 Segs = FMath::Min(MetaLandRoad::LeaveArriveSampleSegs(EstLen, StepCm), SegCap);
		for (int32 i = 0; i <= Segs; ++i)
		{
			OutSamples.Add(EvalPoseHermitePoint(Plan, static_cast<double>(i) / static_cast<double>(Segs)));
		}
		break;
	}
	case FLeaveArrivePlan::EKind::DubinsCSC:
	{
		if (Plan.LeaveLen > 1.0 && FVector::Dist2D(Plan.Stub0, Plan.P0) > 1.0)
		{
			AppendResampledSegment(Plan.Stub0, Plan.P0, StepCm, OutSamples, true);
		}
		const double TotalNorm = FMath::Max(Plan.DubinsT + Plan.DubinsP + Plan.DubinsQ, 0.01);
		const double TotalLen = TotalNorm * FMath::Max(Plan.Radius, 1.0);
		const int32 Segs = FMath::Min(MetaLandRoad::LeaveArriveSampleSegs(TotalLen, StepCm), SegCap);
		const bool bHasLeaveStub = OutSamples.Num() > 0;
		for (int32 i = bHasLeaveStub ? 1 : 0; i <= Segs; ++i)
		{
			FVector Pos, Tan;
			EvalDubinsCSC(Plan, static_cast<double>(i) / static_cast<double>(Segs), Pos, Tan);
			OutSamples.Add(Pos);
		}
		if (Plan.ArriveLen > 1.0 && FVector::Dist2D(Plan.P1, Plan.Stub1) > 1.0)
		{
			AppendResampledSegment(Plan.P1, Plan.Stub1, StepCm, OutSamples, false);
		}
		break;
	}
	case FLeaveArrivePlan::EKind::LeadOutHermite:
	{
		AppendResampledSegment(Plan.P0, Plan.Stub0, StepCm, OutSamples, true);
		const double EstLen = FMath::Max(
			FVector::Dist2D(Plan.Stub0, Plan.P1),
			0.5 * (Plan.Handle0 + Plan.Handle1));
		const int32 Segs = FMath::Min(MetaLandRoad::LeaveArriveSampleSegs(EstLen, StepCm), SegCap);
		for (int32 i = 1; i <= Segs; ++i)
		{
			OutSamples.Add(EvalCubicHermitePoint(
				Plan.Stub0,
				Plan.P1,
				Plan.Leave,
				Plan.Arrive,
				Plan.Handle0,
				Plan.Handle1,
				static_cast<double>(i) / static_cast<double>(Segs)));
		}
		break;
	}
	case FLeaveArrivePlan::EKind::OffsetS:
	default:
		AppendResampledSegment(Plan.P0, Plan.Stub0, StepCm, OutSamples, true);
		AppendResampledSegment(Plan.Stub0, Plan.Stub1, StepCm, OutSamples, false);
		AppendResampledSegment(Plan.Stub1, Plan.P1, StepCm, OutSamples, false);
		break;
	}
}

void KeyLeaveArrivePlan(const FLeaveArrivePlan& Plan, TArray<FVector>& OutKeys)
{
	OutKeys.Reset();
	auto AddAway = [&OutKeys](const FVector& Point, const FVector& Ref, double MinCm)
	{
		if (FVector::Dist2D(Point, Ref) > MinCm)
		{
			OutKeys.Add(Point);
		}
	};

	switch (Plan.Kind)
	{
	case FLeaveArrivePlan::EKind::DubinsCSC:
	{
		const FVector EndMouth = (Plan.ArriveLen > 1.0) ? Plan.Stub1 : Plan.P1;
		const double Total = FMath::Max(Plan.DubinsT + Plan.DubinsP + Plan.DubinsQ, 0.01);
		const double R = FMath::Max(Plan.Radius > 1.0 ? Plan.Radius : Plan.MinRadius, 1.0);
		const double StepCm = MetaLandRoad::LeaveArriveChordStepCm(R, R);
		auto AtNorm = [&Plan, Total](double Norm)
		{
			FVector Pos, Tan;
			EvalDubinsCSC(Plan, Norm / Total, Pos, Tan);
			return Pos;
		};
		auto AddArcBreaks = [&](double Sweep, double Norm0, const FVector& AwayFrom)
		{
			if (Sweep <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			if (Sweep > PI * 0.5)
			{
				AddAway(AtNorm(Norm0 + Sweep * 0.5), AwayFrom, StepCm);
			}
			AddAway(AtNorm(Norm0 + Sweep), AwayFrom, StepCm);
		};
		AddArcBreaks(Plan.DubinsT, 0.0, Plan.P0);
		if (Plan.DubinsP * R > 2.0 * StepCm)
		{
			AddAway(AtNorm(Plan.DubinsT + Plan.DubinsP * 0.5), Plan.P0, StepCm);
		}
		AddAway(AtNorm(Plan.DubinsT + Plan.DubinsP), Plan.P1, StepCm);
		AddArcBreaks(Plan.DubinsQ, Plan.DubinsT + Plan.DubinsP, Plan.P1);
		OutKeys.Add(EndMouth);
		break;
	}
	case FLeaveArrivePlan::EKind::CornerFillet:
		AddAway(Plan.FilletA, Plan.P0, 40.0);
		OutKeys.Add(EvalLeaveArriveArcPoint(Plan, 0.5));
		AddAway(Plan.FilletB, Plan.P1, 40.0);
		OutKeys.Add(Plan.P1);
		break;
	case FLeaveArrivePlan::EKind::LeadOutHermite:
		AddAway(Plan.Stub0, Plan.P0, 40.0);
		OutKeys.Add(EvalCubicHermitePoint(
			Plan.Stub0, Plan.P1, Plan.Leave, Plan.Arrive,
			Plan.Handle0, Plan.Handle1, 0.55));
		OutKeys.Add(Plan.P1);
		break;
	case FLeaveArrivePlan::EKind::PoseHermite:
		OutKeys.Add(EvalPoseHermitePoint(Plan, 0.40));
		OutKeys.Add(EvalPoseHermitePoint(Plan, 0.75));
		OutKeys.Add(Plan.P1);
		break;
	case FLeaveArrivePlan::EKind::CornerSharp:
		AddAway(Plan.Corner, Plan.P0, 40.0);
		OutKeys.Add(Plan.P1);
		break;
	case FLeaveArrivePlan::EKind::OffsetS:
	default:
		AddAway(Plan.Stub0, Plan.P0, 40.0);
		AddAway(Plan.Stub1, Plan.P1, 40.0);
		OutKeys.Add(Plan.P1);
		break;
	}

	while (OutKeys.Num() > MetaLandRoad::LeaveArrivePreviewSegCap)
	{
		OutKeys.RemoveAt(OutKeys.Num() - 2);
	}
	const FVector Tail = (Plan.Kind == FLeaveArrivePlan::EKind::DubinsCSC && Plan.ArriveLen > 1.0)
		? Plan.Stub1
		: Plan.P1;
	if (OutKeys.Num() == 0 || FVector::Dist2D(OutKeys.Last(), Tail) > 8.0)
	{
		OutKeys.Add(Tail);
	}
}

/** Authoring keys only (mouth / breaks / tip). Green-rail samples stay preview. */
static void CollectSamePlanBakeKeys(
	const FLeaveArrivePlan& Plan,
	double HalfWidthCm,
	TArray<FVector>& OutKeys)
{
	(void)HalfWidthCm;
	KeyLeaveArrivePlan(Plan, OutKeys);
}

void OffsetParallelWidthRails(
	const TArray<FVector>& CenterPath,
	double HalfWidthLeft0,
	double HalfWidthRight0,
	double HalfWidthLeft1,
	double HalfWidthRight1,
	const FVector& PreferRight,
	const FVector& FallbackTangent,
	float PlaneZ,
	TArray<FVector>& OutLeft,
	TArray<FVector>& OutRight)
{
	OutLeft.Reset();
	OutRight.Reset();
	if (CenterPath.Num() < 2)
	{
		return;
	}

	TArray<double> Arc;
	Arc.SetNum(CenterPath.Num());
	Arc[0] = 0.0;
	for (int32 i = 1; i < CenterPath.Num(); ++i)
	{
		Arc[i] = Arc[i - 1] + FVector::Dist2D(CenterPath[i - 1], CenterPath[i]);
	}
	const double Total = FMath::Max(Arc.Last(), 1.0);

	FVector PrevRight = FVector::ZeroVector;
	OutLeft.Reserve(CenterPath.Num());
	OutRight.Reserve(CenterPath.Num());
	auto DropCusp = [](TArray<FVector>& Rail, const FVector& Next)
	{
		if (Rail.Num() < 2)
		{
			Rail.Add(Next);
			return;
		}
		const FVector PrevMove = (Rail.Last() - Rail[Rail.Num() - 2]).GetSafeNormal2D();
		const FVector NewMove = (Next - Rail.Last()).GetSafeNormal2D();
		if (!PrevMove.IsNearlyZero() && !NewMove.IsNearlyZero()
			&& FVector::DotProduct(PrevMove, NewMove) < 0.0)
		{
			Rail.Last() = Next;
			return;
		}
		Rail.Add(Next);
	};
	for (int32 i = 0; i < CenterPath.Num(); ++i)
	{
		FVector Tangent = FVector::ZeroVector;
		if (i + 1 < CenterPath.Num())
		{
			Tangent += (CenterPath[i + 1] - CenterPath[i]).GetSafeNormal2D();
		}
		if (i > 0)
		{
			Tangent += (CenterPath[i] - CenterPath[i - 1]).GetSafeNormal2D();
		}
		if (!Tangent.Normalize())
		{
			Tangent = FallbackTangent;
		}
		const FVector Right = TransportOffsetRight(Tangent, PrevRight, PreferRight);
		PrevRight = Right;
		const double Alpha = Arc[i] / Total;
		FVector Center = CenterPath[i];
		Center.Z = PlaneZ;
		DropCusp(OutLeft, Center - Right * FMath::Lerp(HalfWidthLeft0, HalfWidthLeft1, Alpha));
		DropCusp(OutRight, Center + Right * FMath::Lerp(HalfWidthRight0, HalfWidthRight1, Alpha));
	}
}

static double CornerKeyTangentSize(double SegBeforeCm, double SegAfterCm)
{
	// CurveCustomTangent is dP/dInputKey and Δkey≈1, so |handle| ≈ span.
	// 0.28×span flattened the Hermite into a polyline (pink L). Cap at 1.05×min
	// so a leftover sharp V cannot loop.
	const double A = FMath::Max(SegBeforeCm, 1.0);
	const double B = FMath::Max(SegAfterCm, 1.0);
	return FMath::Clamp(0.5 * (A + B), 1.0, FMath::Min(A, B) * 1.05);
}

static FVector UnitTangentOnLeaveArrivePlan(const FLeaveArrivePlan& Plan, const FVector& Point)
{
	auto DistToSegSq = [](const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const double LenSq = AB.SizeSquared2D();
		if (LenSq < 1.0)
		{
			return FVector::DistSquared2D(P, A);
		}
		const double T = FMath::Clamp(FVector::DotProduct(P - A, AB) / LenSq, 0.0, 1.0);
		return FVector::DistSquared2D(P, A + AB * T);
	};

	switch (Plan.Kind)
	{
	case FLeaveArrivePlan::EKind::CornerFillet:
	{
		const double Rad = FMath::Max(FVector::Dist2D(Plan.FilletCenter, Plan.FilletA), 1.0);
		const double DistToCircle = FMath::Abs(FVector::Dist2D(Point, Plan.FilletCenter) - Rad);
		if (DistToCircle <= FMath::Max(60.0, Rad * 0.12))
		{
			const FVector Radial = (Point - Plan.FilletCenter).GetSafeNormal2D();
			FVector Tan = FVector::CrossProduct(FVector::UpVector, Radial).GetSafeNormal2D();
			if (Plan.SweepSign < 0.0)
			{
				Tan = -Tan;
			}
			if (!Tan.IsNearlyZero())
			{
				return Tan;
			}
		}
		return (FVector::Dist2D(Point, Plan.FilletA) <= FVector::Dist2D(Point, Plan.FilletB))
			? Plan.Leave
			: Plan.Arrive;
	}
	case FLeaveArrivePlan::EKind::OffsetS:
	{
		const double D0 = DistToSegSq(Point, Plan.P0, Plan.Stub0);
		const double D1 = DistToSegSq(Point, Plan.Stub0, Plan.Stub1);
		const double D2 = DistToSegSq(Point, Plan.Stub1, Plan.P1);
		if (D0 <= D1 && D0 <= D2)
		{
			return Plan.Leave.IsNearlyZero()
				? (Plan.Stub0 - Plan.P0).GetSafeNormal2D()
				: Plan.Leave;
		}
		if (D2 <= D1)
		{
			return Plan.Arrive.IsNearlyZero()
				? (Plan.P1 - Plan.Stub1).GetSafeNormal2D()
				: Plan.Arrive;
		}
		return (Plan.Stub1 - Plan.Stub0).GetSafeNormal2D();
	}
	case FLeaveArrivePlan::EKind::PoseHermite:
	{
		double BestAlpha = 0.0;
		double BestDist = 1.0e12;
		for (int32 i = 0; i <= 16; ++i)
		{
			const double Alpha = static_cast<double>(i) / 16.0;
			const double Dist = FVector::Dist2D(Point, EvalPoseHermitePoint(Plan, Alpha));
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestAlpha = Alpha;
			}
		}
		const FVector Tan = EvalPoseHermiteTangent(Plan, BestAlpha);
		return Tan.IsNearlyZero() ? Plan.Leave : Tan;
	}
	case FLeaveArrivePlan::EKind::DubinsCSC:
	{
		double BestAlpha = 0.0;
		double BestDist = 1.0e12;
		FVector BestTan = Plan.Leave;
		for (int32 i = 0; i <= 16; ++i)
		{
			const double Alpha = static_cast<double>(i) / 16.0;
			FVector Pos, Tan;
			EvalDubinsCSC(Plan, Alpha, Pos, Tan);
			const double Dist = FVector::Dist2D(Point, Pos);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestAlpha = Alpha;
				BestTan = Tan;
			}
		}
		(void)BestAlpha;
		return BestTan.IsNearlyZero() ? Plan.Leave : BestTan;
	}
	case FLeaveArrivePlan::EKind::LeadOutHermite:
	{
		if (FVector::Dist2D(Point, Plan.P0) + 1.0
			<= FVector::Dist2D(Point, Plan.Stub0) + FVector::Dist2D(Plan.P0, Plan.Stub0) * 0.15)
		{
			return Plan.Leave;
		}
		double BestAlpha = 0.55;
		double BestDist = 1.0e12;
		for (int32 i = 0; i <= 12; ++i)
		{
			const double Alpha = static_cast<double>(i) / 12.0;
			const double Dist = FVector::Dist2D(
				Point,
				EvalCubicHermitePoint(
					Plan.Stub0, Plan.P1, Plan.Leave, Plan.Arrive,
					Plan.Handle0, Plan.Handle1, Alpha));
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestAlpha = Alpha;
			}
		}
		const FVector Tan = EvalCubicHermiteTangent(
			Plan.Stub0, Plan.P1, Plan.Leave, Plan.Arrive,
			Plan.Handle0, Plan.Handle1, BestAlpha);
		return Tan.IsNearlyZero() ? Plan.Leave : Tan;
	}
	default:
		return Plan.Leave.IsNearlyZero()
			? (Plan.P1 - Plan.P0).GetSafeNormal2D()
			: Plan.Leave;
	}
}

/** Lock CurveCustomTangent to the plan direction; clamp handle length to the adjacent span. */
static void ApplyClampedLeaveArriveTangents(
	UMoKuEditSplinesComponent* Spline,
	int32 FirstIndex,
	int32 LastIndex,
	const FLeaveArrivePlan& Plan,
	bool bTravelP0ToP1,
	const FVector& FrozenArriveAtP0,
	const FVector& FrozenLeaveAtP0)
{
	if (!Spline || FirstIndex > LastIndex)
	{
		return;
	}

	for (int32 Index = FirstIndex; Index <= LastIndex; ++Index)
	{
		if (!Spline->SplineCurves.Position.Points.IsValidIndex(Index))
		{
			break;
		}

		const FVector Here = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
		FVector Dir = UnitTangentOnLeaveArrivePlan(Plan, Here);
		if (!bTravelP0ToP1)
		{
			Dir = -Dir;
		}
		if (Dir.IsNearlyZero())
		{
			if (Index < LastIndex)
			{
				Dir = (Spline->GetLocationAtSplinePoint(Index + 1, ESplineCoordinateSpace::World) - Here)
					.GetSafeNormal2D();
			}
			else if (Index > FirstIndex)
			{
				Dir = (Here - Spline->GetLocationAtSplinePoint(Index - 1, ESplineCoordinateSpace::World))
					.GetSafeNormal2D();
			}
		}

		double SegBefore = 0.0;
		double SegAfter = 0.0;
		if (Index > FirstIndex)
		{
			SegBefore = FVector::Dist2D(
				Spline->GetLocationAtSplinePoint(Index - 1, ESplineCoordinateSpace::World), Here);
		}
		if (Index < LastIndex)
		{
			SegAfter = FVector::Dist2D(
				Here, Spline->GetLocationAtSplinePoint(Index + 1, ESplineCoordinateSpace::World));
		}
		if (SegBefore < 1.0)
		{
			SegBefore = SegAfter;
		}
		if (SegAfter < 1.0)
		{
			SegAfter = SegBefore;
		}
		double TanSize = CornerKeyTangentSize(SegBefore, SegAfter);
		if (Plan.Kind == FLeaveArrivePlan::EKind::DubinsCSC)
		{
			double Sweep = 0.0;
			if (Index < LastIndex)
			{
				const FVector NextPos = Spline->GetLocationAtSplinePoint(
					Index + 1, ESplineCoordinateSpace::World);
				FVector NextDir = UnitTangentOnLeaveArrivePlan(Plan, NextPos);
				if (!bTravelP0ToP1)
				{
					NextDir = -NextDir;
				}
				if (!NextDir.IsNearlyZero())
				{
					Sweep = FMath::Max(
						Sweep,
						FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, NextDir), -1.0, 1.0)));
				}
			}
			if (Index > FirstIndex)
			{
				const FVector PrevPos = Spline->GetLocationAtSplinePoint(
					Index - 1, ESplineCoordinateSpace::World);
				FVector PrevDir = UnitTangentOnLeaveArrivePlan(Plan, PrevPos);
				if (!bTravelP0ToP1)
				{
					PrevDir = -PrevDir;
				}
				if (!PrevDir.IsNearlyZero())
				{
					Sweep = FMath::Max(
						Sweep,
						FMath::Acos(FMath::Clamp(FVector::DotProduct(PrevDir, Dir), -1.0, 1.0)));
				}
			}
			if (Sweep > 0.04)
			{
				const double Circular = 4.0 * FMath::Max(Plan.Radius, 1.0)
					* FMath::Tan(FMath::Min(Sweep, 2.2) * 0.25);
				TanSize = FMath::Clamp(
					Circular,
					1.0,
					FMath::Max(SegBefore, SegAfter) * 2.0);
			}
		}
		const FVector Handle = Dir * TanSize;
		Spline->SetSplinePointType(Index, ESplinePointType::CurveCustomTangent, false);

		const bool bAtP0 = bTravelP0ToP1 ? (Index == FirstIndex) : (Index == LastIndex);
		if (bAtP0)
		{
			if (bTravelP0ToP1)
			{
				const FVector Arrive = FrozenArriveAtP0.IsNearlyZero() ? Handle : FrozenArriveAtP0;
				const FVector LeaveDir = Plan.Leave.IsNearlyZero() ? Dir : Plan.Leave;
				Spline->SetTangentsAtSplinePoint(
					Index, Arrive, LeaveDir * TanSize, ESplineCoordinateSpace::World, false);
			}
			else
			{
				const FVector Leave = FrozenLeaveAtP0.IsNearlyZero() ? Handle : FrozenLeaveAtP0;
				const FVector ArriveDir = Plan.Leave.IsNearlyZero() ? Dir : (-Plan.Leave);
				Spline->SetTangentsAtSplinePoint(
					Index, ArriveDir * TanSize, Leave, ESplineCoordinateSpace::World, false);
			}
		}
		else
		{
			Spline->SetTangentsAtSplinePoint(
				Index, Handle, Handle, ESplineCoordinateSpace::World, false);
		}
	}
}

static bool BakeCornerRampAfterMouth(
	UMoKuEditSplinesComponent* Spline,
	int32 MouthIndex,
	const FVector& EndPos,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double MinRadiusCm,
	FLeaveArrivePlan* OutPlan)
{
	if (!Spline || !Spline->SplineCurves.Position.Points.IsValidIndex(MouthIndex))
	{
		return false;
	}

	while (Spline->GetNumberOfSplinePoints() > MouthIndex + 1)
	{
		Spline->RemoveSplinePoint(Spline->GetNumberOfSplinePoints() - 1, false);
	}

	const FVector MouthPos = Spline->GetLocationAtSplinePoint(
		MouthIndex, ESplineCoordinateSpace::World);
	const FVector Leave = LeaveDir.GetSafeNormal2D();
	const FVector Arrive = ArriveDir.GetSafeNormal2D();
	const FVector ChordDir = (EndPos - MouthPos).GetSafeNormal2D();

	FLeaveArrivePlan Plan;
	if (!TryMakeLeaveArrivePlan(
		MouthPos,
		EndPos,
		Leave.IsNearlyZero() ? ChordDir : Leave,
		Arrive.IsNearlyZero() ? ChordDir : Arrive,
		MinRadiusCm,
		Plan))
	{
		return false;
	}
	double HalfW = FMath::Max(MinRadiusCm, 80.0);
	if (const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		const double MouthS = RoadSpline->GetDistanceAlongSplineAtSplinePoint(MouthIndex);
		HalfW = FMath::Max(RoadSpline->GetRoadHalfWidthAtS(MouthS), 80.0);
	}
	if (IsLeaveArrivePlanUnusable(Plan, HalfW))
	{
		return false;
	}

	TArray<FVector> Keys;
	CollectSamePlanBakeKeys(Plan, HalfW, Keys);

	for (const FVector& Key : Keys)
	{
		const int32 NewIndex = Spline->GetNumberOfSplinePoints();
		Spline->AddSplinePointAtIndex(Key, NewIndex, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(NewIndex, ESplinePointType::Linear, false);
	}
	if (OutPlan)
	{
		*OutPlan = Plan;
	}
	return true;
}

static bool BakeCornerRampBeforeMouth(
	UMoKuEditSplinesComponent* Spline,
	const FVector& EndPos,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double MinRadiusCm,
	FLeaveArrivePlan* OutPlan)
{
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return false;
	}

	const FVector MouthPos = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	const FVector Leave = LeaveDir.GetSafeNormal2D();
	const FVector Arrive = ArriveDir.GetSafeNormal2D();
	const FVector ChordDir = (EndPos - MouthPos).GetSafeNormal2D();

	FLeaveArrivePlan Plan;
	if (!TryMakeLeaveArrivePlan(
		MouthPos,
		EndPos,
		Leave.IsNearlyZero() ? ChordDir : Leave,
		Arrive.IsNearlyZero() ? ChordDir : Arrive,
		MinRadiusCm,
		Plan))
	{
		return false;
	}
	double HalfW = FMath::Max(MinRadiusCm, 80.0);
	if (const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		HalfW = FMath::Max(RoadSpline->GetRoadHalfWidthAtS(0.0), 80.0);
	}
	if (IsLeaveArrivePlanUnusable(Plan, HalfW))
	{
		return false;
	}

	TArray<FVector> Keys;
	CollectSamePlanBakeKeys(Plan, HalfW, Keys);
	for (const FVector& Key : Keys)
	{
		Spline->AddSplinePointAtIndex(Key, 0, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
	}
	if (OutPlan)
	{
		*OutPlan = Plan;
	}
	return true;
}

static void DecimateBakeKeysFromMouth(
	const TArray<FVector>& Centerline,
	const FVector& MouthPos,
	const FVector& LeaveDir,
	TArray<FVector>& OutKeys,
	double HalfWidthCm)
{
	OutKeys.Reset();
	const double HalfW = FMath::Clamp(HalfWidthCm, 80.0, 800.0);
	const double KeepStraightCm = FMath::Max(MetaLandRoad::MeshRibbonStepCm(HalfW, 0.0), 50.0);
	const double KeepTurnCm = FMath::Max(HalfW * 0.22, 40.0);
	const double TurnKeepRad = FMath::DegreesToRadians(8.0);
	const double KeepTipCm = 40.0;
	FVector LastKept = MouthPos;
	FVector LastDir = LeaveDir.GetSafeNormal2D();
	FVector PrevSample = MouthPos;
	for (const FVector& Point : Centerline)
	{
		if (FVector::Dist2D(Point, MouthPos) <= 8.0)
		{
			PrevSample = Point;
			continue;
		}
		const FVector SegDir = (Point - PrevSample).GetSafeNormal2D();
		double Turn = 0.0;
		if (!LastDir.IsNearlyZero() && !SegDir.IsNearlyZero())
		{
			Turn = FMath::Acos(
				FMath::Clamp(FVector::DotProduct(LastDir, SegDir), -1.0, 1.0));
		}
		const double Need = (Turn >= TurnKeepRad) ? KeepTurnCm : KeepStraightCm;
		if (FVector::Dist2D(LastKept, Point) >= Need)
		{
			OutKeys.Add(Point);
			LastKept = Point;
			if (!SegDir.IsNearlyZero())
			{
				LastDir = SegDir;
			}
		}
		PrevSample = Point;
		if (OutKeys.Num() >= MetaLandRoad::LeaveArriveSampleSegCap - 1)
		{
			break;
		}
	}
	if (Centerline.Num() >= 2)
	{
		const FVector Tail = Centerline.Last();
		if (OutKeys.Num() == 0 || FVector::Dist2D(OutKeys.Last(), Tail) > KeepTipCm)
		{
			OutKeys.Add(Tail);
		}
		else
		{
			OutKeys.Last() = Tail;
		}
	}
}

/** Lerp source→target Z, then mix with landscape so the U sits on ground. */
static void MixCorridorBakeKeyHeights(
	UWorld* World,
	AMoKuEditSplineActor* Road,
	TArray<FVector>& Keys,
	double SourceZ,
	double TargetZ)
{
	if (Keys.Num() < 1)
	{
		return;
	}

	TArray<double> Arc;
	Arc.SetNum(Keys.Num());
	Arc[0] = 0.0;
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		Arc[i] = Arc[i - 1] + FVector::Dist2D(Keys[i - 1], Keys[i]);
	}
	const double Total = FMath::Max(Arc.Last(), 1.0);
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const float T = FMath::SmoothStep(0.0f, 1.0f, static_cast<float>(Arc[i] / Total));
		const double MixZ = FMath::Lerp(SourceZ, TargetZ, static_cast<double>(T));
		FVector OnGround = Keys[i];
		OnGround.Z = MixZ;
		if (World && ProjectPointOntoLandscapeSurface(World, OnGround, Road))
		{
			Keys[i].Z = FMath::Lerp(MixZ, static_cast<double>(OnGround.Z), 0.65);
		}
		else
		{
			Keys[i].Z = MixZ;
		}
	}
	Keys[0].Z = SourceZ;
	Keys.Last().Z = TargetZ;
}

static int32 BakePreviewCenterlineAfterMouth(
	UMoKuEditSplinesComponent* Spline,
	int32 InsertIndex,
	const TArray<FVector>& Centerline,
	const FVector& WeldPos = FVector::ZeroVector,
	UWorld* World = nullptr,
	AMoKuEditSplineActor* Road = nullptr,
	double SourceZ = 0.0,
	double TargetZ = 0.0)
{
	if (!Spline || Centerline.Num() < 2 || InsertIndex < 0)
	{
		return 0;
	}

	const FVector PrevPos = (InsertIndex > 0)
		? Spline->GetLocationAtSplinePoint(InsertIndex - 1, ESplineCoordinateSpace::World)
		: Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	const FVector MouthPos = WeldPos.IsNearlyZero() ? Centerline[0] : WeldPos;
	FVector Leave = (Centerline[1] - Centerline[0]).GetSafeNormal2D();
	if (Leave.IsNearlyZero())
	{
		Leave = (Centerline.Last() - MouthPos).GetSafeNormal2D();
	}

	TArray<FVector> Keys;
	DecimateBakeKeysFromMouth(
		Centerline,
		MouthPos,
		Leave,
		Keys,
		ResolveJointHalfWidthCm(Spline, FMath::Max(InsertIndex - 1, 0)));
	if (Keys.Num() < 1)
	{
		return 0;
	}
	if (!FMath::IsNearlyEqual(SourceZ, TargetZ, 0.5) || World)
	{
		MixCorridorBakeKeyHeights(World, Road, Keys, SourceZ, TargetZ);
	}

	int32 WriteAt = InsertIndex;
	if (!WeldPos.IsNearlyZero() && FVector::Dist2D(PrevPos, WeldPos) > 40.0)
	{
		Spline->AddSplinePointAtIndex(WeldPos, WriteAt, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(WriteAt, ESplinePointType::Linear, false);
		++WriteAt;
	}

	for (const FVector& Key : Keys)
	{
		Spline->AddSplinePointAtIndex(Key, WriteAt, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(WriteAt, ESplinePointType::Linear, false);
		++WriteAt;
	}
	return WriteAt - InsertIndex;
}

static bool BakePreviewCenterlineBeforeMouth(
	UMoKuEditSplinesComponent* Spline,
	const TArray<FVector>& Centerline)
{
	if (!Spline || Centerline.Num() < 2)
	{
		return false;
	}

	const FVector MouthPos = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	const FVector Leave = (Centerline.Last() - MouthPos).GetSafeNormal2D();
	TArray<FVector> Keys;
	DecimateBakeKeysFromMouth(
		Centerline, MouthPos, Leave, Keys, ResolveJointHalfWidthCm(Spline, 0));
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		Spline->AddSplinePointAtIndex(Keys[i], 0, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
	}
	return Keys.Num() > 0;
}

static bool ResolveMouthFrameAtS(
	UMoKuEditSplinesComponent* Spline,
	double MouthS,
	bool bFromBegin,
	FVector& OutLeft,
	FVector& OutRight,
	FVector& OutCenter,
	FVector& OutLeave)
{
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		if (MouthS >= 0.0
			&& RoadSpline->GetRoadEdgePointsAtS(
				MouthS, OutLeft, OutRight, OutCenter, ESplineCoordinateSpace::World))
		{
			OutLeave = RoadSpline->GetDirectionAtDistanceAlongSpline(
				MouthS, ESplineCoordinateSpace::World).GetSafeNormal2D();
			if (bFromBegin)
			{
				OutLeave = -OutLeave;
			}
			OutLeave = SnapTravelToMouthBar(OutLeft, OutRight, OutLeave);
			return !OutCenter.IsNearlyZero();
		}
	}

	const int32 Idx = bFromBegin ? 0 : FMath::Max(Spline->GetNumberOfSplinePoints() - 1, 0);
	MouthBarAtSplinePoint(Spline, Idx, OutLeft, OutRight);
	OutCenter = Spline->GetLocationAtSplinePoint(Idx, ESplineCoordinateSpace::World);
	OutLeave = SnapTravelToMouthBar(
		OutLeft, OutRight, MouthOutwardFromSplinePoints(Spline, bFromBegin));
	return true;
}

static void StripLeaveInsetBakeKey(const FLeaveArrivePlan& Plan, TArray<FVector>& Keys)
{
	if (Keys.Num() < 1 || Plan.LeaveLen <= 1.0)
	{
		return;
	}
	if (FVector::Dist2D(Keys[0], Plan.P0) < 40.0
		&& FVector::Dist2D(Keys[0], Plan.Stub0) > 40.0)
	{
		Keys.RemoveAt(0);
	}
}

static int32 WriteLinearBakeKeysAfterInsert(
	UMoKuEditSplinesComponent* Spline,
	int32 InsertIndex,
	const TArray<FVector>& Keys,
	const FVector& WeldPos)
{
	if (!Spline || Keys.Num() < 1 || InsertIndex < 0)
	{
		return 0;
	}

	const FVector PrevPos = (InsertIndex > 0)
		? Spline->GetLocationAtSplinePoint(InsertIndex - 1, ESplineCoordinateSpace::World)
		: Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	int32 WriteAt = InsertIndex;
	(void)WeldPos;
	FVector Last = PrevPos;
	for (const FVector& Key : Keys)
	{
		if (FVector::Dist2D(Last, Key) <= 40.0)
		{
			continue;
		}
		Spline->AddSplinePointAtIndex(Key, WriteAt, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(WriteAt, ESplinePointType::Linear, false);
		Last = Key;
		++WriteAt;
	}
	return WriteAt - InsertIndex;
}

static void EvalDubinsNearest(
	const FLeaveArrivePlan& Plan,
	const FVector& Query,
	FVector& OutPos,
	FVector& OutTan)
{
	double BestA = 0.0;
	double BestD = TNumericLimits<double>::Max();
	for (int32 i = 0; i <= 48; ++i)
	{
		FVector Pos, Tan;
		const double Alpha = static_cast<double>(i) / 48.0;
		EvalDubinsCSC(Plan, Alpha, Pos, Tan);
		const double Dist = FVector::Dist2D(Pos, Query);
		if (Dist < BestD)
		{
			BestD = Dist;
			BestA = Alpha;
		}
	}
	EvalDubinsCSC(Plan, BestA, OutPos, OutTan);
}

static double DubinsNeighborHandleCm(const USplineComponent* Spline, int32 Index)
{
	if (!Spline || !Spline->SplineCurves.Position.Points.IsValidIndex(Index))
	{
		return 1.0;
	}
	const FVector Cur = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
	double Before = 200.0;
	double After = 200.0;
	if (Index > 0)
	{
		Before = FVector::Dist2D(
			Spline->GetLocationAtSplinePoint(Index - 1, ESplineCoordinateSpace::World), Cur);
	}
	if (Index + 1 < Spline->GetNumberOfSplinePoints())
	{
		After = FVector::Dist2D(
			Cur, Spline->GetLocationAtSplinePoint(Index + 1, ESplineCoordinateSpace::World));
	}
	const double A = FMath::Max(Before, 1.0);
	const double B = FMath::Max(After, 1.0);
	return FMath::Clamp(0.5 * (A + B), 1.0, FMath::Min(A, B) * 1.05);
}

static void ApplyDubinsCustomTangent(
	UMoKuEditSplinesComponent* Spline,
	int32 Index,
	const FVector& WorldTan,
	bool bLeaveOnly)
{
	const FVector N = WorldTan.GetSafeNormal2D();
	if (!Spline || N.IsNearlyZero() || !Spline->SplineCurves.Position.Points.IsValidIndex(Index))
	{
		return;
	}
	const double Handle = DubinsNeighborHandleCm(Spline, Index);
	Spline->SetSplinePointType(Index, ESplinePointType::CurveCustomTangent, false);
	if (bLeaveOnly)
	{
		const FVector Arrive = Spline->GetArriveTangentAtSplinePoint(
			Index, ESplineCoordinateSpace::World);
		Spline->SetTangentsAtSplinePoint(
			Index,
			Arrive.IsNearlyZero() ? (-N * Handle) : Arrive,
			N * Handle,
			ESplineCoordinateSpace::World,
			false);
		return;
	}
	Spline->SetTangentsAtSplinePoint(
		Index,
		N * Handle,
		N * Handle,
		ESplineCoordinateSpace::World,
		false);
}

static FVector CorridorLeaveFromCenterline(const TArray<FVector>* Centerline)
{
	if (!Centerline || Centerline->Num() < 2)
	{
		return FVector::ZeroVector;
	}
	const FVector Origin = (*Centerline)[0];
	for (int32 i = 1; i < Centerline->Num(); ++i)
	{
		const FVector Delta = (*Centerline)[i] - Origin;
		if (Delta.Size2D() >= 20.0)
		{
			return Delta.GetSafeNormal2D();
		}
	}
	return FVector::ZeroVector;
}

static FVector SnapTravelToOutwardBar(const FVector& Outward, const FVector& Travel)
{
	const FVector Out = Outward.GetSafeNormal2D();
	if (Out.IsNearlyZero())
	{
		return Travel.GetSafeNormal2D();
	}
	const FVector Across(-Out.Y, Out.X, 0.0);
	return SnapTravelToMouthBar(-Across, Across, Travel);
}

static void SetCorridorPointTravel(
	UMoKuEditSplinesComponent* Spline,
	int32 Index,
	const FVector& WorldTan,
	bool bLeaveOnly,
	double HandleCm)
{
	const FVector N = WorldTan.GetSafeNormal2D();
	if (!Spline || N.IsNearlyZero() || !Spline->SplineCurves.Position.Points.IsValidIndex(Index))
	{
		return;
	}
	const double Handle = FMath::Max(HandleCm, 20.0);
	Spline->SetSplinePointType(Index, ESplinePointType::CurveCustomTangent, false);
	if (bLeaveOnly)
	{
		const FVector Arrive = Spline->GetArriveTangentAtSplinePoint(
			Index, ESplineCoordinateSpace::World);
		Spline->SetTangentsAtSplinePoint(
			Index,
			Arrive.IsNearlyZero() ? (-N * Handle) : Arrive,
			N * Handle,
			ESplineCoordinateSpace::World,
			false);
		return;
	}
	Spline->SetTangentsAtSplinePoint(
		Index,
		N * Handle,
		N * Handle,
		ESplineCoordinateSpace::World,
		false);
}

/** Mouth leave follows the green; tip faces the yellow bar. Do not move keys. */
static void ApplyCorridorEndCaps(
	UMoKuEditSplinesComponent* Spline,
	int32 MouthIndex,
	int32 TipIndex,
	const TArray<FVector>* Centerline,
	const FVector& SrcLeft,
	const FVector& SrcRight,
	const FVector& SrcLeave,
	const FVector& TargetOutward)
{
	if (!Spline
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(MouthIndex)
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(TipIndex)
		|| MouthIndex == TipIndex)
	{
		return;
	}

	(void)SrcLeft;
	(void)SrcRight;
	(void)SrcLeave;
	(void)Centerline;
	(void)TargetOutward;
	Spline->SetSplinePointType(MouthIndex, ESplinePointType::Linear, false);
	Spline->SetSplinePointType(TipIndex, ESplinePointType::Linear, false);
}

static int32 WriteDubinsTangentBakeKeysAfterInsert(
	UMoKuEditSplinesComponent* Spline,
	int32 InsertIndex,
	const FLeaveArrivePlan& Plan,
	const TArray<FVector>& Keys)
{
	if (!Spline || Keys.Num() < 1 || InsertIndex < 0)
	{
		return 0;
	}

	const FVector PrevPos = (InsertIndex > 0)
		? Spline->GetLocationAtSplinePoint(InsertIndex - 1, ESplineCoordinateSpace::World)
		: Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	int32 WriteAt = InsertIndex;
	FVector Last = PrevPos;
	for (const FVector& Key : Keys)
	{
		if (FVector::Dist2D(Last, Key) <= 40.0)
		{
			continue;
		}
		Spline->AddSplinePointAtIndex(Key, WriteAt, ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(WriteAt, ESplinePointType::CurveCustomTangent, false);
		Last = Key;
		++WriteAt;
	}
	const int32 Written = WriteAt - InsertIndex;
	if (Written <= 0)
	{
		return 0;
	}

	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
	}
	else
	{
		Spline->UpdateSpline();
	}

	const int32 MouthIndex = InsertIndex - 1;
	if (Spline->SplineCurves.Position.Points.IsValidIndex(MouthIndex))
	{
		FVector Pos, Tan;
		EvalDubinsCSC(Plan, 0.0, Pos, Tan);
		ApplyDubinsCustomTangent(Spline, MouthIndex, Tan, /*bLeaveOnly*/ true);
	}
	for (int32 Index = InsertIndex; Index < WriteAt; ++Index)
	{
		FVector Pos, Tan;
		EvalDubinsNearest(
			Plan,
			Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World),
			Pos,
			Tan);
		ApplyDubinsCustomTangent(Spline, Index, Tan, /*bLeaveOnly*/ false);
	}
	return Written;
}

static void HideLeftoverAfterContinueBake(
	AMoKuEditSplineActor* Road,
	UMoKuEditSplinesComponent* Spline,
	int32 TipIndex,
	bool bAttachAtStart)
{
	if (!Road || !Spline || !Spline->SplineCurves.Position.Points.IsValidIndex(TipIndex))
	{
		return;
	}

	const int32 NextIndex = bAttachAtStart ? (TipIndex - 1) : (TipIndex + 1);
	if (!Spline->SplineCurves.Position.Points.IsValidIndex(NextIndex))
	{
		return;
	}

	const FVector TipPos = Spline->GetLocationAtSplinePoint(
		TipIndex, ESplineCoordinateSpace::World);
	const FVector NextPos = Spline->GetLocationAtSplinePoint(
		NextIndex, ESplineCoordinateSpace::World);
	const int32 PrevIndex = bAttachAtStart ? (TipIndex + 1) : (TipIndex - 1);
	FVector Leave = FVector::ZeroVector;
	if (Spline->SplineCurves.Position.Points.IsValidIndex(PrevIndex))
	{
		Leave = (TipPos - Spline->GetLocationAtSplinePoint(
			PrevIndex, ESplineCoordinateSpace::World)).GetSafeNormal2D();
		if (bAttachAtStart)
		{
			Leave = -Leave;
		}
	}
	const FVector ToNext = (NextPos - TipPos).GetSafeNormal2D();
	if (!Leave.IsNearlyZero() && FVector::DotProduct(Leave, ToNext) > 0.25)
	{
		return;
	}

	const double TipS = Spline->GetDistanceAlongSplineAtSplinePoint(TipIndex);
	const int32 Step = bAttachAtStart ? -1 : 1;
	int32 Walk = NextIndex;
	FVector WalkPos = NextPos;
	FVector WalkDir = ToNext;
	double OtherS = Spline->GetDistanceAlongSplineAtSplinePoint(Walk);
	while (Spline->SplineCurves.Position.Points.IsValidIndex(Walk + Step))
	{
		const FVector AfterPos = Spline->GetLocationAtSplinePoint(
			Walk + Step, ESplineCoordinateSpace::World);
		const FVector AfterDir = (AfterPos - WalkPos).GetSafeNormal2D();
		if (AfterDir.IsNearlyZero() || FVector::DotProduct(WalkDir, AfterDir) > 0.25)
		{
			break;
		}
		Walk += Step;
		WalkPos = AfterPos;
		WalkDir = AfterDir;
		OtherS = Spline->GetDistanceAlongSplineAtSplinePoint(Walk);
	}

	const double Approach = bAttachAtStart ? OtherS : TipS;
	const double Depart = bAttachAtStart ? TipS : OtherS;
	if (Depart <= Approach + 40.0)
	{
		return;
	}

	Road->Modify();
	for (FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
	{
		const bool bNearApproach =
			Zone.ApproachCutS >= 0.0 && FMath::Abs(Zone.ApproachCutS - Approach) < 80.0;
		const bool bNearDepart =
			Zone.DepartCutS >= 0.0 && FMath::Abs(Zone.DepartCutS - Depart) < 80.0;
		if (bNearApproach || bNearDepart)
		{
			Zone.ApproachCutS = Approach;
			Zone.DepartCutS = Depart;
			return;
		}
	}

	FMetaLandRoadJunctionTrimZone Zone;
	Zone.ApproachCutS = Approach;
	Zone.DepartCutS = Depart;
	Road->JunctionTrimZones.Add(Zone);
}

static void SealContinueDrawBakeTip(
	AMoKuEditSplineActor* Road,
	UMoKuEditSplinesComponent* Spline,
	bool bAttachAtStart,
	double OldTipS,
	int32 TipIndex)
{
	if (!Road || !Spline || !Spline->SplineCurves.Position.Points.IsValidIndex(TipIndex))
	{
		return;
	}

	const double NewTipS = Spline->GetDistanceAlongSplineAtSplinePoint(TipIndex);
	MoveOwnedMouthCut(Road, bAttachAtStart, OldTipS, NewTipS);
	HideLeftoverAfterContinueBake(Road, Spline, TipIndex, bAttachAtStart);
}

static int32 BakeDubinsCorridorAfterInsert(
	UMoKuEditSplinesComponent* Spline,
	int32 InsertIndex,
	const FVector& StartPos,
	const FVector& EndPos,
	const FVector& Leave,
	const FVector& Arrive,
	const FVector& WeldPos = FVector::ZeroVector)
{
	const FVector LeaveN = Leave.GetSafeNormal2D();
	const FVector ArriveN = Arrive.GetSafeNormal2D();
	const FVector Chord = (EndPos - StartPos).GetSafeNormal2D();
	double HalfW = 80.0;
	if (const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		const int32 At = FMath::Max(InsertIndex - 1, 0);
		const double S = RoadSpline->GetDistanceAlongSplineAtSplinePoint(At);
		HalfW = FMath::Max(RoadSpline->GetRoadHalfWidthAtS(S), 80.0);
	}
	const double MinR = DiffuseRadiusForLink(StartPos, EndPos, LeaveN, ArriveN, HalfW);
	FLeaveArrivePlan Plan;
	// Bake from the live mouth. Mouth-reserve Lead0 was a straight stub along
	// the old CutS leave; A then curved from the previous key → 红框重叠.
	if (!TryMakeLeaveArrivePlan_DubinsCSC(
			StartPos,
			EndPos,
			LeaveN.IsNearlyZero() ? Chord : LeaveN,
			ArriveN.IsNearlyZero() ? Chord : ArriveN,
			MinR,
			Plan)
		|| IsLeaveArrivePlanUnusable(Plan, HalfW))
	{
		return 0;
	}
	Plan.Stub0 = StartPos;
	Plan.Stub1 = EndPos;
	Plan.LeaveLen = 0.0;
	Plan.ArriveLen = 0.0;
	TArray<FVector> Keys;
	KeyLeaveArrivePlan(Plan, Keys);
	StripLeaveInsetBakeKey(Plan, Keys);
	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
	{
		MixCorridorBakeKeyHeights(
			Road->GetWorld(), Road, Keys, StartPos.Z, EndPos.Z);
	}
	return WriteDubinsTangentBakeKeysAfterInsert(Spline, InsertIndex, Plan, Keys);
}

static void ResolveContinueDisplayTip(
	AMoKuEditSplineActor* Road,
	UMoKuEditSplinesComponent* Spline,
	bool& bAttachAtStart,
	double PreviewMouthS,
	const FVector& PreviewMouthCenter,
	const TArray<FVector>* PreviewCenterline,
	double& OutTipS,
	FVector& OutTipPos)
{
	TArray<TPair<double, bool>> Tips;
	if (Road)
	{
		TArray<TPair<double, double>> Spans;
		Road->GetJunctionMeshGenerationSpans(Spans);
		for (const TPair<double, double>& Span : Spans)
		{
			Tips.Emplace(Span.Key, false);
			Tips.Emplace(Span.Value, true);
		}
	}
	if (Tips.Num() == 0)
	{
		Tips.Emplace(0.0, false);
		Tips.Emplace(Spline->GetSplineLength(), true);
	}

	bool bUseEnd = !bAttachAtStart;
	double BestS = bUseEnd ? Tips.Last().Key : Tips[0].Key;
	if (PreviewMouthS >= 0.0)
	{
		double BestDelta = TNumericLimits<double>::Max();
		for (const TPair<double, bool>& Tip : Tips)
		{
			const double Delta = FMath::Abs(PreviewMouthS - Tip.Key);
			if (Delta < BestDelta)
			{
				BestDelta = Delta;
				bUseEnd = Tip.Value;
				BestS = Tip.Key;
			}
		}
	}
	if (PreviewMouthS < 0.0)
	{
		FVector Probe = !PreviewMouthCenter.IsNearlyZero()
			? PreviewMouthCenter
			: ((PreviewCenterline && PreviewCenterline->Num() > 0)
				? (*PreviewCenterline)[0]
				: FVector::ZeroVector);
		if (!Probe.IsNearlyZero())
		{
			double BestDist = TNumericLimits<double>::Max();
			for (const TPair<double, bool>& Tip : Tips)
			{
				const FVector TipP = Spline->GetLocationAtDistanceAlongSpline(
					Tip.Key, ESplineCoordinateSpace::World);
				const double Dist = FVector::Dist2D(Probe, TipP);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					bUseEnd = Tip.Value;
					BestS = Tip.Key;
				}
			}
		}
	}

	bAttachAtStart = !bUseEnd;
	OutTipS = BestS;
	OutTipPos = Spline->GetLocationAtDistanceAlongSpline(BestS, ESplineCoordinateSpace::World);
}

/** Pin the baked tip onto the target CutS bar so cover and remaining-road share one mouth. */
static void PinBakedTipToTargetMouthBar(
	UMoKuEditSplinesComponent* Spline,
	int32& TipIndex,
	const FVector& TargetCenter,
	const FVector& TargetOutward,
	bool bAttachAtStart,
	int32 DoNotNudgeIndex)
{
	if (!Spline
		|| !Spline->SplineCurves.Position.Points.IsValidIndex(TipIndex)
		|| TargetCenter.IsNearlyZero())
	{
		return;
	}

	(void)DoNotNudgeIndex;
	Spline->SetLocationAtSplinePoint(TipIndex, TargetCenter, ESplineCoordinateSpace::World, false);
	FVector Outward = TargetOutward.GetSafeNormal2D();
	if (Outward.IsNearlyZero())
	{
		return;
	}
	const FVector TipDir = SnapTravelToOutwardBar(
		Outward,
		bAttachAtStart ? Outward : (-Outward));
	const int32 Prev = bAttachAtStart ? (TipIndex + 1) : (TipIndex - 1);
	double BeforeCm = 90.0;
	if (Spline->SplineCurves.Position.Points.IsValidIndex(Prev))
	{
		BeforeCm = FVector::Dist2D(
			Spline->GetLocationAtSplinePoint(TipIndex, ESplineCoordinateSpace::World),
			Spline->GetLocationAtSplinePoint(Prev, ESplineCoordinateSpace::World));
	}
	SetCorridorPointTravel(
		Spline,
		TipIndex,
		TipDir,
		/*bLeaveOnly*/ false,
		FMath::Clamp(0.22 * BeforeCm, 16.0, 36.0));
}

bool PinContinueDrawTipToTargetMouth(
	AMoKuEditSplineActor* Road,
	UMoKuEditSplinesComponent* Spline,
	bool& bAttachAtSplineStart,
	int32 OriginalNumPoints,
	const FVector& TargetCenter,
	const FVector& TargetOutward,
	const TArray<FVector>* PreviewCenterline,
	double PreviewMouthS,
	const FVector& PreviewMouthCenter)
{
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return false;
	}

	double DisplayTipS = PreviewMouthS;
	FVector DisplayTipPos = PreviewMouthCenter;
	ResolveContinueDisplayTip(
		Road,
		Spline,
		bAttachAtSplineStart,
		PreviewMouthS,
		PreviewMouthCenter,
		PreviewCenterline,
		DisplayTipS,
		DisplayTipPos);

	const TArray<FVector>* CenterlineToBake = PreviewCenterline;
	if (PreviewCenterline && PreviewCenterline->Num() >= 2
		&& FVector::Dist2D((*PreviewCenterline)[0], DisplayTipPos) > 80.0)
	{
		CenterlineToBake = nullptr;
	}

	const int32 NumBefore = Spline->GetNumberOfSplinePoints();
	const int32 OriginalNum = (OriginalNumPoints > 0) ? OriginalNumPoints : NumBefore;
	TArray<FLaneExtendPointFreeze> BodyFreezes;
	if (bAttachAtSplineStart && OriginalNum > 0 && NumBefore > OriginalNum)
	{
		CaptureLaneExtendPointFreezes(Spline, NumBefore - OriginalNum, NumBefore, BodyFreezes);
	}
	else
	{
		CaptureLaneExtendPointFreezes(Spline, 0, FMath::Min(OriginalNum, NumBefore), BodyFreezes);
	}

	Spline->Modify();
	bool bBaked = false;
	const bool bHavePreview = CenterlineToBake && CenterlineToBake->Num() >= 2;
	int32 PreviewLastBakeIndex = INDEX_NONE;
	int32 CMouthIndex = INDEX_NONE;
	FVector SrcLeft = FVector::ZeroVector;
	FVector SrcRight = FVector::ZeroVector;
	FVector SrcLeave = FVector::ZeroVector;

	if (bAttachAtSplineStart)
	{
		int32 MouthIndex = (Spline->GetNumberOfSplinePoints() > OriginalNum)
			? (Spline->GetNumberOfSplinePoints() - OriginalNum)
			: 0;
		while (MouthIndex > 0)
		{
			Spline->RemoveSplinePoint(0, false);
			--MouthIndex;
		}
		RestoreLaneExtendPointFreezes(Spline, 0, BodyFreezes, INDEX_NONE);

		{
			if (bHavePreview)
			{
				bBaked = BakePreviewCenterlineBeforeMouth(Spline, *CenterlineToBake);
			}
			if (!bBaked)
			{
				const double MouthS = DisplayTipS;
				FVector MouthL, MouthR, MouthC, Leave;
				ResolveMouthFrameAtS(Spline, MouthS, /*bFromBegin*/ true, MouthL, MouthR, MouthC, Leave);
				const FVector LeaveN = Leave.GetSafeNormal2D();
				const FVector ArriveN = (-TargetOutward).GetSafeNormal2D();
				const FVector Chord = (TargetCenter - MouthC).GetSafeNormal2D();
				double HalfW = 80.0;
				if (const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
				{
					HalfW = FMath::Max(RoadSpline->GetRoadHalfWidthAtS(FMath::Max(MouthS, 0.0)), 80.0);
				}
				FLeaveArrivePlan Plan;
				if (TryMakeLeaveArrivePlan_DubinsCSC(
						MouthC,
						TargetCenter,
						LeaveN.IsNearlyZero() ? Chord : LeaveN,
						ArriveN.IsNearlyZero() ? Chord : ArriveN,
						DiffuseRadiusForLink(MouthC, TargetCenter, LeaveN, ArriveN, HalfW),
						Plan)
					&& !IsLeaveArrivePlanUnusable(Plan, HalfW))
				{
					Plan.Stub0 = MouthC;
					Plan.Stub1 = TargetCenter;
					Plan.LeaveLen = 0.0;
					Plan.ArriveLen = 0.0;
				TArray<FVector> Keys;
				KeyLeaveArrivePlan(Plan, Keys);
				StripLeaveInsetBakeKey(Plan, Keys);
				MixCorridorBakeKeyHeights(
					Road ? Road->GetWorld() : nullptr, Road, Keys, MouthC.Z, TargetCenter.Z);
				bBaked = WriteDubinsTangentBakeKeysAfterInsert(Spline, 0, Plan, Keys) > 0;
				}
			}
			if (bBaked)
			{
				PreviewLastBakeIndex = 0;
			}
		}
		const int32 NewMouthIndex = FMath::Max(Spline->GetNumberOfSplinePoints() - OriginalNum, 0);
		CMouthIndex = NewMouthIndex;
		RestoreLaneExtendPointFreezes(Spline, NewMouthIndex, BodyFreezes, INDEX_NONE);
		if (bBaked && Spline->SplineCurves.Position.Points.IsValidIndex(NewMouthIndex))
		{
			FVector MouthL, MouthR, MouthC, MouthLeave;
			ResolveMouthFrameAtS(
				Spline, DisplayTipS, /*bFromBegin*/ true, MouthL, MouthR, MouthC, MouthLeave);
			ApplyDubinsCustomTangent(Spline, NewMouthIndex, MouthLeave, /*bLeaveOnly*/ true);
		}
		if (Road && bBaked)
		{
			if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
			{
				RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
			}
			if (Spline->SplineCurves.Position.Points.IsValidIndex(NewMouthIndex))
			{
				ShiftStationsAfterBeginInsert(
					Road, Spline->GetDistanceAlongSplineAtSplinePoint(NewMouthIndex));
			}
		}
	}
	else
	{
		const int32 BodyLast = FMath::Max(OriginalNum - 1, 0);
		if (!Spline->SplineCurves.Position.Points.IsValidIndex(BodyLast))
		{
			return false;
		}

		RestoreLaneExtendPointFreezes(Spline, 0, BodyFreezes, BodyLast);
		const double MouthS = DisplayTipS;
		const double LengthBeforeBake = Spline->GetSplineLength();
		int32 InsertIndex = FindContinueDrawInsertIndex(Spline, /*bInsertAtStart*/ false, MouthS);
		const int32 Extra = FMath::Max(Spline->GetNumberOfSplinePoints() - OriginalNum, 0);
		for (int32 i = 0; i < Extra; ++i)
		{
			if (Spline->GetNumberOfSplinePoints() > InsertIndex)
			{
				Spline->RemoveSplinePoint(InsertIndex, false);
			}
		}
		InsertIndex = FindContinueDrawInsertIndex(Spline, /*bInsertAtStart*/ false, MouthS);
		CMouthIndex = FMath::Max(InsertIndex - 1, 0);
		FVector SrcCenter = FVector::ZeroVector;
		ResolveMouthFrameAtS(
			Spline, MouthS, /*bFromBegin*/ false, SrcLeft, SrcRight, SrcCenter, SrcLeave);
		(void)SrcCenter;
		const FVector PrevMouth = Spline->GetLocationAtSplinePoint(
			CMouthIndex, ESplineCoordinateSpace::World);
		if (FVector::Dist2D(PrevMouth, DisplayTipPos) > 40.0)
		{
			Spline->AddSplinePointAtIndex(
				DisplayTipPos, InsertIndex, ESplineCoordinateSpace::World, false);
			Spline->SetSplinePointType(InsertIndex, ESplinePointType::Curve, false);
			CMouthIndex = InsertIndex;
			++InsertIndex;
		}

		int32 Written = 0;
		if (bHavePreview)
		{
			Written = BakePreviewCenterlineAfterMouth(
				Spline,
				InsertIndex,
				*CenterlineToBake,
				FVector::ZeroVector,
				Road ? Road->GetWorld() : nullptr,
				Road,
				DisplayTipPos.Z,
				TargetCenter.Z);
		}
		if (Written <= 0)
		{
			FVector MouthL, MouthR, MouthC, Leave;
			ResolveMouthFrameAtS(
				Spline, MouthS, /*bFromBegin*/ false, MouthL, MouthR, MouthC, Leave);
			Written = BakeDubinsCorridorAfterInsert(
				Spline,
				InsertIndex,
				MouthC,
				TargetCenter,
				Leave,
				(-TargetOutward).GetSafeNormal2D(),
				FVector::ZeroVector);
		}
		bBaked = Written > 0;
		if (bBaked)
		{
			PreviewLastBakeIndex = InsertIndex + Written - 1;
			int32 WeldAt = CMouthIndex;
			for (int32 i = InsertIndex; i < InsertIndex + Written; ++i)
			{
				if (!Spline->SplineCurves.Position.Points.IsValidIndex(i))
				{
					break;
				}
				if (FVector::Dist2D(
					Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World),
					DisplayTipPos) <= 40.0)
				{
					WeldAt = i;
					break;
				}
			}
			CMouthIndex = WeldAt;
		}
		const int32 MouthIndex = FMath::Max(InsertIndex - 1, 0);
		TArray<FLaneExtendPointFreeze> BodyOnly;
		for (int32 i = 0; i < MouthIndex && i < BodyFreezes.Num(); ++i)
		{
			BodyOnly.Add(BodyFreezes[i]);
		}
		RestoreLaneExtendPointFreezes(Spline, 0, BodyOnly, INDEX_NONE);
		if (bBaked)
		{
			if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
			{
				RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
			}
			else
			{
				Spline->UpdateSpline();
			}
			const double AddedLen = Spline->GetSplineLength() - LengthBeforeBake;
			ShiftStationsAfterEndInsert(Road, MouthS, AddedLen);
		}
	}

	if (bBaked && PreviewLastBakeIndex != INDEX_NONE
		&& Spline->SplineCurves.Position.Points.IsValidIndex(PreviewLastBakeIndex)
		&& !TargetCenter.IsNearlyZero())
	{
		int32 TipIndex = bAttachAtSplineStart ? 0 : PreviewLastBakeIndex;
		PinBakedTipToTargetMouthBar(
			Spline,
			TipIndex,
			TargetCenter,
			TargetOutward,
			bAttachAtSplineStart,
			CMouthIndex);
		PreviewLastBakeIndex = TipIndex;
	}

	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
		if (bBaked)
		{
			RoadSpline->StretchTipSectionToSplineLength(bAttachAtSplineStart);
		}
	}
	else
	{
		Spline->UpdateSpline();
	}
	if (bBaked && !bAttachAtSplineStart
		&& CMouthIndex != INDEX_NONE
		&& PreviewLastBakeIndex != INDEX_NONE)
	{
		ApplyCorridorEndCaps(
			Spline,
			CMouthIndex,
			PreviewLastBakeIndex,
			CenterlineToBake,
			SrcLeft,
			SrcRight,
			SrcLeave,
			TargetOutward);
		if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
		{
			RoadSpline->UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
		}
		else
		{
			Spline->UpdateSpline();
		}
	}
	if (bBaked && Road && PreviewLastBakeIndex != INDEX_NONE
		&& Spline->SplineCurves.Position.Points.IsValidIndex(PreviewLastBakeIndex))
	{
		SealContinueDrawBakeTip(
			Road, Spline, bAttachAtSplineStart, DisplayTipS, PreviewLastBakeIndex);
		Road->UpdateInterpolateType(0);
	}
	return bBaked;
}

/** MetaRoad: only mouth may stay CurveUser; everything else → Curve. */
void ClearNonMouthCustomTangents(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 MouthPointIndex,
	int32 PreserveStubIndex,
	int32 PreserveSoftJointIndex)
{
	(void)PreserveStubIndex;
	(void)PreserveSoftJointIndex;
	if (!SplineComponent)
	{
		return;
	}
	for (int32 PointIndex = 0; PointIndex < SplineComponent->GetNumberOfSplinePoints(); ++PointIndex)
	{
		if (PointIndex == MouthPointIndex)
		{
			continue;
		}
		if (SplineComponent->GetSplinePointType(PointIndex) == ESplinePointType::CurveCustomTangent)
		{
			SplineComponent->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
		}
	}
}

void BuildCenterSamplesFromExtendPlan(
	const FVector& StartCenter,
	const FVector& EndCenter,
	const FVector& StartTangent,
	const FVector& EndTangent,
	bool bUseHermite,
	double RampStubLengthCm,
	double StepCm,
	bool bIncludeStart,
	TArray<FVector>& OutSamples)
{
	OutSamples.Reset();
	if (bUseHermite)
	{
		AppendRampCenterPath(
			StartCenter,
			EndCenter,
			StartTangent,
			EndTangent,
			RampStubLengthCm,
			StepCm,
			OutSamples,
			bIncludeStart);
	}
	else
	{
		AppendResampledSegment(StartCenter, EndCenter, StepCm, OutSamples, bIncludeStart);
	}
}

/**
 * Bake the SAME center path as green guidance into sparse mesh keys (excludes tip P0).
 * GuidanceSampleStepCm defines the curve; MeshKeySpacingCm decimates for spline keys.
 */
void BuildMeshKeysFromGuidanceCenter(
	const FVector& StartCenter,
	const FVector& EndCenter,
	const FVector& StartTangent,
	const FVector& EndTangent,
	bool bUseHermite,
	double MeshKeySpacingCm,
	TArray<FVector>& OutKeys)
{
	OutKeys.Reset();
	TArray<FVector> CenterSamples;
	// Hermite guidance: no stub — stub+Hermite was the green-rail 折角.
	const double StubForPath = 0.0;
	BuildCenterSamplesFromExtendPlan(
		StartCenter,
		EndCenter,
		StartTangent,
		EndTangent,
		bUseHermite,
		StubForPath,
		static_cast<double>(GuidanceSampleStepCm),
		/*bIncludeStart*/ true,
		CenterSamples);
	if (CenterSamples.Num() < 2)
	{
		OutKeys.Add(EndCenter);
		return;
	}

	const double Spacing = FMath::Max(MeshKeySpacingCm, 180.0);
	// Keep first key clear of mouth so FitSpline leave doesn't fight a near tip sample.
	const double MouthClearCm = FMath::Max(Spacing * 0.55, 160.0);
	FVector LastKept = CenterSamples[0];
	for (int32 Index = 1; Index < CenterSamples.Num() - 1; ++Index)
	{
		if (FVector::Dist2D(CenterSamples[Index], CenterSamples[0]) < MouthClearCm)
		{
			continue;
		}
		if (FVector::Dist2D(CenterSamples[Index], LastKept) < Spacing)
		{
			continue;
		}
		if (OutKeys.Num() >= MaxGuidanceBakeMeshKeys - 1)
		{
			break;
		}
		OutKeys.Add(CenterSamples[Index]);
		LastKept = CenterSamples[Index];
	}

	// Sharp turn with only tip→end collapses to a V: force one mid sample on the Hermite.
	if (OutKeys.Num() == 0 && CenterSamples.Num() >= 3)
	{
		const FVector ChordDir = (EndCenter - StartCenter).GetSafeNormal2D();
		const double LeaveAlign = FVector::DotProduct(StartTangent.GetSafeNormal2D(), ChordDir);
		if (LeaveAlign < 0.92)
		{
			OutKeys.Add(CenterSamples[CenterSamples.Num() / 2]);
		}
	}

	const FVector& Tail = CenterSamples.Last();
	if (OutKeys.Num() == 0 || FVector::Dist2D(OutKeys.Last(), Tail) > 10.0)
	{
		OutKeys.Add(Tail);
	}
	else
	{
		OutKeys.Last() = Tail;
	}
	if (FVector::Dist2D(OutKeys.Last(), EndCenter) > 10.0)
	{
		OutKeys.Last() = EndCenter;
	}
}

void AppendPerLaneGuidanceRails(
	const FMetaLandRoadDrawAnchor& LaneAnchor,
	double TipS,
	const FVector& TipTangent,
	const FVector& Cursor,
	bool bUseHermite,
	const FVector& EndTangent,
	double RampStubLengthCm,
	const FMetaLandRoadDrawAnchor* EndSnapAnchor,
	const FVector* FallbackEndLeft,
	const FVector* FallbackEndRight,
	TArray<TArray<FVector>>& OutLeftPolylines,
	TArray<TArray<FVector>>& OutRightPolylines)
{
	UMetaLandRoadSplineComponent* Spline = LaneAnchor.SourceSpline.Get();
	if (!Spline)
	{
		return;
	}

	TArray<int32> SourceLaneIndices;
	CollectSortedBundleLaneIndices(LaneAnchor, SourceLaneIndices);

	TArray<int32> TargetLaneIndices;
	UMetaLandRoadSplineComponent* EndSnapSpline = nullptr;
	if (EndSnapAnchor && EndSnapAnchor->IsValid())
	{
		EndSnapSpline = EndSnapAnchor->SourceSpline.Get();
		if (EndSnapSpline)
		{
			// Preview uses user-picked target bundle only — no auto-expand to source count.
			CollectSortedBundleLaneIndices(*EndSnapAnchor, TargetLaneIndices);
		}
	}

	for (int32 LaneIdx = 0; LaneIdx < SourceLaneIndices.Num(); ++LaneIdx)
	{
		// User-locked EndSnap: only draw paired source↔target lanes (no Hermite fan-out).
		if (EndSnapAnchor && EndSnapAnchor->IsValid() && TargetLaneIndices.Num() > 0
			&& !TargetLaneIndices.IsValidIndex(LaneIdx))
		{
			continue;
		}

		const int32 LaneIndex = SourceLaneIndices[LaneIdx];
		FMetaLandRoadDrawAnchor SingleLaneAnchor = LaneAnchor;
		SingleLaneAnchor.LaneIndex = LaneIndex;
		SingleLaneAnchor.BundledLaneIndices.Reset();
		SingleLaneAnchor.AnchorS = TipS;

		FVector LaneLeft, LaneRight, LaneCenter, LaneOutward;
		if (!MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			Spline, SingleLaneAnchor, LaneLeft, LaneRight, LaneCenter, LaneOutward))
		{
			continue;
		}

		const FVector* LaneEndLeftPtr = FallbackEndLeft;
		const FVector* LaneEndRightPtr = FallbackEndRight;
		FVector LaneEndLeft, LaneEndRight;
		if (EndSnapSpline && TargetLaneIndices.IsValidIndex(LaneIdx))
		{
			FMetaLandRoadDrawAnchor SingleTarget = *EndSnapAnchor;
			SingleTarget.LaneIndex = TargetLaneIndices[LaneIdx];
			SingleTarget.BundledLaneIndices.Reset();
			FVector TargetLeft, TargetRight, TargetCenter, TargetOutward;
			if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				EndSnapSpline, SingleTarget, TargetLeft, TargetRight, TargetCenter, TargetOutward))
			{
				LaneEndLeft = TargetLeft;
				LaneEndRight = TargetRight;
				ApplyGuidanceSurfaceZ(LaneEndLeft);
				ApplyGuidanceSurfaceZ(LaneEndRight);
				LaneEndLeftPtr = &LaneEndLeft;
				LaneEndRightPtr = &LaneEndRight;
			}
		}

		TArray<FVector> LaneLeftRail;
		TArray<FVector> LaneRightRail;
		BuildLaneWidthGuidanceRails(
			LaneLeft,
			LaneRight,
			LaneCenter,
			TipTangent,
			Cursor,
			bUseHermite,
			EndTangent,
			RampStubLengthCm,
			static_cast<double>(GuidanceSampleStepCm),
			LaneLeftRail,
			LaneRightRail,
			LaneEndLeftPtr,
			LaneEndRightPtr);
		OutLeftPolylines.Add(MoveTemp(LaneLeftRail));
		OutRightPolylines.Add(MoveTemp(LaneRightRail));
	}
}

void BuildLaneWidthGuidanceRails(
	const FVector& TipLeft,
	const FVector& TipRight,
	const FVector& TipCenter,
	const FVector& TipTangent,
	const FVector& Cursor,
	bool bUseHermite,
	const FVector& EndTangent,
	double RampStubLengthCm,
	double SampleStepCm,
	TArray<FVector>& OutLeft,
	TArray<FVector>& OutRight,
	const FVector* OptionalEndLeft,
	const FVector* OptionalEndRight,
	TArray<FVector>* OutCenter,
	FCornerCurveDebug* OutDebug)
{
	if (OutCenter)
	{
		OutCenter->Reset();
	}
	if (OutDebug)
	{
		*OutDebug = FCornerCurveDebug();
	}
	OutLeft.Reset();
	OutRight.Reset();

	const bool bTaperToMouth = OptionalEndLeft && OptionalEndRight
		&& FVector::Dist2D(*OptionalEndLeft, *OptionalEndRight) > 1.0;

	const double MouthSpanCm = FMath::Max(
		FVector::Dist2D(TipLeft, TipRight),
		static_cast<double>(MetaLandRoad::DefaultRoadLaneWidth));

	FVector EffectiveEnd = Cursor;
	if (bTaperToMouth)
	{
		EffectiveEnd = (*OptionalEndLeft + *OptionalEndRight) * 0.5;
	}
	else if (FVector::Dist2D(TipCenter, EffectiveEnd) <= 1.0)
	{
		EffectiveEnd = TipCenter + TipTangent.GetSafeNormal2D() * MouthSpanCm;
	}

	// Mouth mid = spawn MouthLoc / mesh centerline. Lock entire preview to this plane (MetaRoad).
	FVector PathCenter = (TipLeft + TipRight) * 0.5;
	const float PlaneZ = static_cast<float>(PathCenter.Z);
	PathCenter.Z = PlaneZ;
	FVector PathEnd = EffectiveEnd;
	PathEnd.Z = PlaneZ;
	FVector FlatTipLeft = TipLeft;
	FVector FlatTipRight = TipRight;
	FlatTipLeft.Z = PlaneZ;
	FlatTipRight.Z = PlaneZ;

	const double HalfWidthLeft = FMath::Max(
		FVector::Dist2D(FlatTipLeft, PathCenter),
		MetaLandRoad::DefaultRoadLaneWidth * 0.25);
	const double HalfWidthRight = FMath::Max(
		FVector::Dist2D(FlatTipRight, PathCenter),
		MetaLandRoad::DefaultRoadLaneWidth * 0.25);
	const double EndHalfWidthLeft = bTaperToMouth
		? FMath::Max(FVector::Dist2D(*OptionalEndLeft, PathEnd), MetaLandRoad::DefaultRoadLaneWidth * 0.25)
		: HalfWidthLeft;
	const double EndHalfWidthRight = bTaperToMouth
		? FMath::Max(FVector::Dist2D(*OptionalEndRight, PathEnd), MetaLandRoad::DefaultRoadLaneWidth * 0.25)
		: HalfWidthRight;

	// Straight rubber-band (initial draw / no Hermite): tip → end ONLY.
	// Dense 80cm resampling produced 100+ pts (L0..L130) on a simple tip→cursor line.
	TArray<FVector> CenterSamples;
	CenterSamples.Reserve(bUseHermite ? 64 : 2);
	FVector TipT = SnapTravelToMouthBar(FlatTipLeft, FlatTipRight, TipTangent);
	const FVector ChordDir = (PathEnd - PathCenter).GetSafeNormal2D();
	const double LeaveAlign = (!TipT.IsNearlyZero() && !ChordDir.IsNearlyZero())
		? FVector::DotProduct(TipT, ChordDir)
		: 1.0;
	const double RailStepCm = SampleStepCm;
	if (bUseHermite && bTaperToMouth)
	{
		FVector Arrive = EndTangent.GetSafeNormal2D();
		if (Arrive.IsNearlyZero())
		{
			Arrive = ChordDir;
		}
		Arrive = SnapTravelToMouthBar(*OptionalEndLeft, *OptionalEndRight, Arrive);
		const double HalfW = FMath::Max(
			FMath::Max(HalfWidthLeft, HalfWidthRight),
			FMath::Max(EndHalfWidthLeft, EndHalfWidthRight));
		const double MinRadiusCm = DiffuseRadiusForLink(
			PathCenter,
			PathEnd,
			TipT.IsNearlyZero() ? ChordDir : TipT,
			Arrive,
			HalfW);
		FLeaveArrivePlan Plan;
		if (TryMakeLeaveArrivePlan(
			PathCenter,
			PathEnd,
			TipT.IsNearlyZero() ? ChordDir : TipT,
			Arrive,
			MinRadiusCm,
			Plan)
			&& !IsLeaveArrivePlanUnusable(Plan, HalfW))
		{
			TArray<FVector> CenterPath;
			SampleLeaveArrivePlan(Plan, HalfW, CenterPath);
			if (CenterPath.Num() >= 2)
			{
				const FVector PreferRight = (FlatTipRight - FlatTipLeft).GetSafeNormal2D();
				OffsetParallelWidthRails(
					CenterPath,
					HalfWidthLeft,
					HalfWidthRight,
					EndHalfWidthLeft,
					EndHalfWidthRight,
					PreferRight,
					TipT.IsNearlyZero() ? ChordDir : TipT,
					PlaneZ,
					OutLeft,
					OutRight);
				FlattenGuidanceRailToPlaneZ(OutLeft, PlaneZ);
				FlattenGuidanceRailToPlaneZ(OutRight, PlaneZ);
				if (!IsGuidanceCorridorUnusable(
					&Plan, CenterPath, OutLeft, OutRight, HalfW, true))
				{
					if (OutCenter)
					{
						*OutCenter = CenterPath;
						FlattenGuidanceRailToPlaneZ(*OutCenter, PlaneZ);
					}
					if (OutDebug)
					{
						OutDebug->Kind = Plan.Kind;
						OutDebug->LeaveLen = Plan.LeaveLen;
						OutDebug->ArriveLen = Plan.ArriveLen;
						OutDebug->Radius = Plan.Radius;
						OutDebug->MinRadius = Plan.MinRadius;
						OutDebug->HalfWidth0 = HalfWidthLeft + HalfWidthRight;
						OutDebug->HalfWidth1 = EndHalfWidthLeft + EndHalfWidthRight;
						OutDebug->CenterPts = CenterPath.Num();
						TArray<FVector> BakeKeys;
						CollectSamePlanBakeKeys(Plan, HalfW, BakeKeys);
						OutDebug->BakeKeys = BakeKeys.Num();
						OutDebug->CrossHits = 0;
						OutDebug->bInvalid = false;
					}
					return;
				}
			}
		}
		FVector EndL = *OptionalEndLeft;
		FVector EndR = *OptionalEndRight;
		EndL.Z = PlaneZ;
		EndR.Z = PlaneZ;
		BuildErrorMouthLinkRails(
			FlatTipLeft, FlatTipRight, EndL, EndR, PlaneZ, OutLeft, OutRight, OutCenter);
		if (OutDebug)
		{
			OutDebug->bInvalid = true;
			OutDebug->HalfWidth0 = HalfWidthLeft + HalfWidthRight;
			OutDebug->HalfWidth1 = EndHalfWidthLeft + EndHalfWidthRight;
			OutDebug->CenterPts = OutCenter ? OutCenter->Num() : 2;
		}
		return;
	}
	else if (bUseHermite)
	{
		BuildCenterSamplesFromExtendPlan(
			PathCenter,
			PathEnd,
			TipTangent,
			EndTangent,
			true,
			/*StubForRails*/ 0.0,
			RailStepCm,
			true,
			CenterSamples);
	}
	else
	{
		CenterSamples.Add(PathCenter);
		CenterSamples.Add(PathEnd);
	}

	for (FVector& Sample : CenterSamples)
	{
		Sample.Z = PlaneZ;
	}

	if (CenterSamples.Num() < 2)
	{
		CenterSamples.Reset();
		CenterSamples.Add(PathCenter);
		CenterSamples.Add(PathCenter + TipT * MouthSpanCm);
	}

	if (!bUseHermite && !bTaperToMouth)
	{
		// Parallel dual straight: ONE Right from tip→cursor for BOTH ends.
		// Pinning mouth TipLeft/Right + offset(chord) caused L/R swap → green X (图二).
		FVector Chord = ChordDir;
		if (Chord.IsNearlyZero())
		{
			Chord = TipT.IsNearlyZero() ? FVector::ForwardVector : TipT;
		}
		FVector Right = FVector::CrossProduct(FVector::UpVector, Chord).GetSafeNormal2D();
		const FVector MouthAcross = (FlatTipRight - FlatTipLeft).GetSafeNormal2D();
		if (!MouthAcross.IsNearlyZero() && FVector::DotProduct(MouthAcross, Right) < 0.0)
		{
			Right = -Right;
		}
		OutLeft.Reset();
		OutRight.Reset();
		OutLeft.Add(PathCenter - Right * HalfWidthLeft);
		OutLeft.Add(PathEnd - Right * HalfWidthLeft);
		OutRight.Add(PathCenter + Right * HalfWidthRight);
		OutRight.Add(PathEnd + Right * HalfWidthRight);
	}
	else if (bTaperToMouth)
	{
		OutLeft.Reserve(CenterSamples.Num());
		OutRight.Reserve(CenterSamples.Num());
		const int32 Last = CenterSamples.Num() - 1;
		FVector PrevRight = FVector::ZeroVector;
		const FVector PreferRight = (FlatTipRight - FlatTipLeft).GetSafeNormal2D();
		for (int32 i = 0; i < CenterSamples.Num(); ++i)
		{
			FVector Tangent = FVector::ZeroVector;
			if (i + 1 < CenterSamples.Num())
			{
				Tangent += (CenterSamples[i + 1] - CenterSamples[i]).GetSafeNormal2D();
			}
			if (i > 0)
			{
				Tangent += (CenterSamples[i] - CenterSamples[i - 1]).GetSafeNormal2D();
			}
			if (!Tangent.Normalize())
			{
				Tangent = TipT.IsNearlyZero() ? FVector::ForwardVector : TipT;
			}
			const FVector Right = TransportOffsetRight(Tangent, PrevRight, PreferRight);
			PrevRight = Right;
			const double Alpha = Last > 0 ? static_cast<double>(i) / static_cast<double>(Last) : 1.0;
			const double HwL = FMath::Lerp(HalfWidthLeft, EndHalfWidthLeft, Alpha);
			const double HwR = FMath::Lerp(HalfWidthRight, EndHalfWidthRight, Alpha);
			OutLeft.Add(CenterSamples[i] - Right * HwL);
			OutRight.Add(CenterSamples[i] + Right * HwR);
		}
	}
	else
	{
		const FVector EndTForRails = EndTangent.GetSafeNormal2D();
		BuildOffsetRails(
			CenterSamples,
			HalfWidthLeft,
			HalfWidthRight,
			OutLeft,
			OutRight,
			TipT,
			EndTForRails.IsNearlyZero() ? TipT : EndTForRails);
	}

	// Meet the source mouth only. Never pull each rail onto a target corner — that crosses.
	if (bUseHermite && !bTaperToMouth && OutLeft.Num() > 0 && OutRight.Num() > 0)
	{
		FVector StartL = FlatTipLeft;
		FVector StartR = FlatTipRight;
		MatchEdgePairToRailTips(OutLeft, OutRight, StartL, StartR, true);
		SoftenGuidanceRailStart(OutLeft, StartL, TipT, RailStepCm);
		SoftenGuidanceRailStart(OutRight, StartR, TipT, RailStepCm);
	}

	if (!bTaperToMouth)
	{
		SoftenGuidanceRailFreeEnd(OutLeft);
		SoftenGuidanceRailFreeEnd(OutRight);
	}

	// Single PDI lift pass — BuildOffsetRails may have already lifted; normalize to plane+lift.
	FlattenGuidanceRailToPlaneZ(OutLeft, PlaneZ);
	FlattenGuidanceRailToPlaneZ(OutRight, PlaneZ);

	if (OutCenter && OutCenter->Num() < 2)
	{
		OutCenter->Reset();
		const int32 PairCount = FMath::Min(OutLeft.Num(), OutRight.Num());
		for (int32 i = 0; i < PairCount; ++i)
		{
			OutCenter->Add((OutLeft[i] + OutRight[i]) * 0.5);
		}
		if (OutDebug && OutDebug->Kind == FLeaveArrivePlan::EKind::None)
		{
			OutDebug->Kind = FLeaveArrivePlan::EKind::OffsetS;
			OutDebug->CenterPts = OutCenter->Num();
			OutDebug->CrossHits = CountGuidanceRailCrossings(OutLeft, OutRight);
			OutDebug->HalfWidth0 = HalfWidthLeft + HalfWidthRight;
			OutDebug->HalfWidth1 = EndHalfWidthLeft + EndHalfWidthRight;
		}
	}
	if (OutDebug)
	{
		const TArray<FVector>& CenterForError = (OutCenter && OutCenter->Num() >= 2)
			? *OutCenter
			: CenterSamples;
		OutDebug->bInvalid = OutDebug->bInvalid || IsGuidanceCorridorUnusable(
			nullptr,
			CenterForError,
			OutLeft,
			OutRight,
			FMath::Max(HalfWidthLeft, HalfWidthRight),
			bTaperToMouth);
	}
}

double ResolveDrawTipSForGuidance(
	const UMetaLandRoadSplineComponent* Spline,
	bool bForward)
{
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return 0.0;
	}

	const int32 TipIndex = bForward ? 0 : Spline->GetNumberOfSplinePoints() - 1;
	return Spline->GetDistanceAlongSplineAtSplinePoint(TipIndex);
}

bool ResolveDrawTipLaneEdges(
	const AMoKuEditSplineActor* RoadActor,
	const UMetaLandRoadSplineComponent* Spline,
	double TipS,
	bool bForward,
	const FMetaLandRoadEditTarget* EditTarget,
	FVector& OutLeft,
	FVector& OutRight,
	FVector& OutCenter,
	bool bForceFullRoadMouth)
{
	if (!RoadActor || !Spline)
	{
		return false;
	}

	if (!bForceFullRoadMouth
		&& EditTarget && EditTarget->bHasPendingAnchor
		&& EditTarget->RoadActor.Get() == RoadActor
		&& EditTarget->Spline.Get() == Spline)
	{
		FMetaLandRoadDrawAnchor TipAnchor = EditTarget->PendingAnchor;
		TipAnchor.AnchorS = TipS;
		TipAnchor.MouthSide = bForward
			? EMetaLandRoadMouthSide::Start
			: EMetaLandRoadMouthSide::End;
		TipAnchor.bFromSuccessor = !bForward;

		FVector OutTangent;
		if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			Spline, TipAnchor, OutLeft, OutRight, OutCenter, OutTangent))
		{
			return true;
		}
	}

	return Spline->GetRoadEdgePointsAtS(TipS, OutLeft, OutRight, OutCenter, ESplineCoordinateSpace::World);
}

/** Prefer road/junction along ray — raised landscape often wins LineTraceSingle and blocks Ctrl cook. */
bool ResolveCtrlDrawRayHit(
	UWorld* World,
	const FRay& WorldRay,
	FHitResult& OutHit,
	AActor*& OutHitActor)
{
	OutHitActor = nullptr;
	if (!World)
	{
		return false;
	}

	const FName TraceTagName = FName("WorldCollision");
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
	TArray<FHitResult> Hits;
	const bool bHitAny = World->LineTraceMultiByObjectType(
		Hits,
		WorldRay.Origin,
		WorldRay.PointAt(HALF_WORLD_MAX),
		QueryParams,
		QueryCollisionParams);
	if (!bHitAny || Hits.IsEmpty())
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		AActor* Actor = Hit.GetActor();
		if (Actor
			&& (Actor->IsA<AMoKuEditSplineActor>() || Actor->IsA<AMoKuEditIntersectionActor>()))
		{
			OutHit = Hit;
			OutHitActor = Actor;
			return true;
		}
	}

	OutHit = Hits[0];
	OutHitActor = OutHit.GetActor();
	return true;
}

FVector ProjectOntoParallelLaneExtend(
	const FVector& Origin,
	const FVector& ExtendTangent,
	const FVector& WorldPoint,
	bool bClampForwardOnly)
{
	const FVector Tangent2D = ExtendTangent.GetSafeNormal2D();
	if (Tangent2D.IsNearlyZero())
	{
		return WorldPoint;
	}

	const double Along = FVector::DotProduct(WorldPoint - Origin, Tangent2D);
	if (bClampForwardOnly && Along <= 0.0)
	{
		FVector StubEnd = Origin + Tangent2D * MinParallelGuidanceExtendCm;
		StubEnd.Z = WorldPoint.Z;
		return StubEnd;
	}

	// Keep lane heading fixed but use cursor distance so guides are not stuck near the actor bounds box.
	const double ExtendLength = FMath::Max(
		FVector::Dist2D(Origin, WorldPoint),
		static_cast<double>(MinParallelGuidanceExtendCm));
	FVector Projected = Origin + Tangent2D * ExtendLength;
	Projected.Z = WorldPoint.Z;
	return Projected;
}

bool ResolveParallelLaneExtendFrame(
	const AMoKuEditSplineActor* RoadActor,
	const UMetaLandRoadSplineComponent* Spline,
	double TipS,
	bool bForward,
	const FMetaLandRoadEditTarget* EditTarget,
	FVector& OutTipLeft,
	FVector& OutTipRight,
	FVector& OutTipCenter,
	FVector& OutExtendTangent,
	bool bUseMouthOutward = false)
{
	if (!ResolveDrawTipLaneEdges(
		RoadActor, Spline, TipS, bForward, EditTarget, OutTipLeft, OutTipRight, OutTipCenter))
	{
		return false;
	}

	OutExtendTangent = Spline->GetDirectionAtDistanceAlongSpline(TipS, ESplineCoordinateSpace::World).GetSafeNormal2D();
	if (bForward)
	{
		OutExtendTangent = -OutExtendTangent;
	}

	// Mouth outward only on first leave. Continue keeps tip leave (spline Direction).
	if (bUseMouthOutward
		&& EditTarget
		&& EditTarget->bHasPendingAnchor
		&& EditTarget->RoadActor.Get() == RoadActor)
	{
		FMetaLandRoadDrawAnchor TipAnchor = EditTarget->PendingAnchor;
		TipAnchor.AnchorS = TipS;
		FVector FrameTangent;
		if (MetaLandRoadLaneEditPick::ResolveMouthOutwardTangent(Spline, TipAnchor, FrameTangent))
		{
			OutExtendTangent = FrameTangent.GetSafeNormal2D();
		}
		else if (MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			Spline, TipAnchor, OutTipLeft, OutTipRight, OutTipCenter, FrameTangent))
		{
			OutExtendTangent = FrameTangent.GetSafeNormal2D();
		}
	}

	if (OutExtendTangent.IsNearlyZero())
	{
		OutExtendTangent = FVector::ForwardVector;
	}
	return true;
}

bool ProjectClickForParallelLaneExtend(
	const AMoKuEditSplineActor* RoadActor,
	const UMoKuEditSplinesComponent* Spline,
	bool bForward,
	const FMetaLandRoadEditTarget& EditTarget,
	FVector& InOutHitPos)
{
	if (!RoadActor || !Spline || !EditTarget.bHasPendingAnchor || EditTarget.RoadActor.Get() != RoadActor)
	{
		return false;
	}

	const UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline);
	if (!RoadSpline)
	{
		return false;
	}

	const double TipS = ResolveDrawTipSForGuidance(Spline, bForward);
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector ExtendTangent;
	if (!ResolveParallelLaneExtendFrame(
		RoadActor, RoadSpline, TipS, bForward, &EditTarget, TipLeft, TipRight, TipCenter, ExtendTangent))
	{
		return false;
	}

	InOutHitPos = ProjectOntoParallelLaneExtend(TipCenter, ExtendTangent, InOutHitPos);
	return true;
}

const FName MetaLandRoadLaneBranchTag(TEXT("MetaLandRoadLaneBranch"));

void RefreshRoadMeshSuppressJunction(AMoKuEditSplineActor* RoadActor)
{
	if (!RoadActor)
	{
		return;
	}
	TGuardValue<bool> SuppressJunctionRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
	RoadActor->RefreshDynamicRoadMesh();
	if (UMetaLandRoadSplineComponent* Spline = RoadActor->GetSplineComponent())
	{
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(Spline);
	}
}

bool IsLaneBranchRoad(const AMoKuEditSplineActor* RoadActor)
{
	return RoadActor && RoadActor->Tags.Contains(MetaLandRoadLaneBranchTag);
}

int32 CountAnchorBundleLanes(const FMetaLandRoadDrawAnchor& Anchor)
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
	return Count;
}

bool RestoreLaneDrawStartAnchorFromBranch(
	UMetaLandRoadSplineComponent* BranchSpline,
	const FMetaLandRoadDrawAnchor& BranchTipAnchor,
	FMetaLandRoadDrawAnchor& OutStartAnchor)
{
	OutStartAnchor = FMetaLandRoadDrawAnchor();
	if (!BranchSpline)
	{
		return false;
	}

	UMetaLandRoadConnection* Pred = BranchSpline->GetPredecessorConnection();
	if (!Pred || !Pred->IsConnected())
	{
		return false;
	}

	UMetaLandRoadLaneConnection* OuterLane = Pred->OuterLaneConnection.Get();
	if (!OuterLane || !MetaLandRoadBuildDrawAnchorFromLaneConnection(*OuterLane, OutStartAnchor))
	{
		return false;
	}

	const int32 BranchLaneCount = CountAnchorBundleLanes(BranchTipAnchor);
	if (BranchLaneCount > 1 && OutStartAnchor.SourceSpline.IsValid())
	{
		MetaLandRoadLaneEditPick::ExpandAnchorBundleToCount(
			OutStartAnchor.SourceSpline.Get(), OutStartAnchor, BranchLaneCount, nullptr);
	}
	return OutStartAnchor.IsValid();
}

bool ResolveGuidanceCursorOnPlane(
	const FRay& WorldRay,
	float PlaneZ,
	FVector& OutWorldPos)
{
	const FVector Dir = WorldRay.Direction;
	if (FMath::Abs(Dir.Z) <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float T = (PlaneZ - WorldRay.Origin.Z) / Dir.Z;
	if (T <= 0.0f)
	{
		return false;
	}

	OutWorldPos = WorldRay.Origin + Dir * T;
	return true;
}

bool TryResolveGuidancePlaneZ(
	const UMetaLandRoadEditorMode* RoadMode,
	const AMoKuEditSplineActor* ActiveRoad,
	EMoKuEditState DrawState,
	float FallbackZ,
	float& OutPlaneZ)
{
	if (RoadMode)
	{
		const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
		if (Target.bHasPendingAnchor && Target.Spline.IsValid())
		{
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
				OutPlaneZ = static_cast<float>(TipCenter.Z);
				return true;
			}
		}
	}

	if (ActiveRoad)
	{
		if (const UMoKuEditSplinesComponent* Spline = ActiveRoad->GetSplineComponent())
		{
			if (Spline->GetNumberOfSplinePoints() > 0)
			{
				const int32 TipIndex = (DrawState == EMoKuEditState::Forward)
					? 0
					: Spline->GetNumberOfSplinePoints() - 1;
				OutPlaneZ = static_cast<float>(Spline->GetLocationAtSplinePoint(
					TipIndex,
					ESplineCoordinateSpace::World).Z);
				return true;
			}
		}
	}

	if (!FMath::IsNearlyZero(FallbackZ))
	{
		OutPlaneZ = FallbackZ;
		return true;
	}

	return false;
}

FVector ResolveGuidanceCursorForRay(
	const FRay& WorldRay,
	const UMetaLandRoadEditorMode* RoadMode,
	const AMoKuEditSplineActor* ActiveRoad,
	EMoKuEditState DrawState,
	float StoredPlaneZ,
	bool bHasStoredPlaneZ,
	float FallbackZ,
	const FVector& TraceHitPos,
	bool bHasTraceHit,
	UWorld* /*WorldUnused*/)
{
	// MetaRoad-style: stay on one horizontal plane (mouth / tip Z). Do NOT mix tip Z with
	// landscape Z — that pulls green rails into a vertical "tower".
	if (bHasStoredPlaneZ)
	{
		FVector PlaneCursor;
		if (ResolveGuidanceCursorOnPlane(WorldRay, StoredPlaneZ, PlaneCursor))
		{
			return PlaneCursor;
		}
	}

	float PlaneZ = 0.0f;
	if (TryResolveGuidancePlaneZ(RoadMode, ActiveRoad, DrawState, FallbackZ, PlaneZ))
	{
		FVector PlaneCursor;
		if (ResolveGuidanceCursorOnPlane(WorldRay, PlaneZ, PlaneCursor))
		{
			return PlaneCursor;
		}
	}

	if (bHasTraceHit)
	{
		// Free draw without a stored plane: keep hit XY, lock Z to tip/fallback plane if any.
		FVector Cursor = TraceHitPos;
		if (!FMath::IsNearlyZero(FallbackZ))
		{
			Cursor.Z = FallbackZ;
		}
		return Cursor;
	}

	FVector PlaneCursor;
	if (ResolveGuidanceCursorOnPlane(WorldRay, FallbackZ, PlaneCursor))
	{
		return PlaneCursor;
	}

	return TraceHitPos;
}
