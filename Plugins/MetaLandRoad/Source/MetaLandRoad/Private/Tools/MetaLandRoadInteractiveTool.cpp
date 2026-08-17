#include "MetaLandRoadInteractiveTool.h"
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
#include "CurveOp.h"
#include "EditorModeManager.h"
#include "Tools/UEdMode.h"
#include "EditSplineRoadInfo.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "PrimitiveDrawingUtils.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadModule.h"
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
#include "SceneView.h"


#define LOCTEXT_NAMESPACE "UMetaLandRoadInteractiveTool"

namespace
{
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

	static bool IsEndSnapMixAddModifierDown()
	{
		const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
		return ModKeys.IsAltDown() && !ModKeys.IsControlDown();
	}

	static bool IsEndSnapMixToggleModifierDown()
	{
		const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
		return ModKeys.IsShiftDown() && !ModKeys.IsControlDown();
	}

	static int32 CountDrawAnchorBundleLanes(const FMetaLandRoadDrawAnchor& Anchor)
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

	static void CollectSortedBundleLaneIndices(
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
	constexpr float GuidanceMeshSurfaceZOffset = 2.0f;

	struct FEndSnapTargetLanePick
	{
		UMetaLandRoadSplineComponent* Spline = nullptr;
		int32 Lane = METALANDROAD_LANE_INDEX_NONE;
		int32 Section = INDEX_NONE;
		EMetaLandRoadMouthSide Mouth = EMetaLandRoadMouthSide::End;
		double MouthS = 0.0;

		bool IsValid() const
		{
			return Spline && Lane != METALANDROAD_LANE_INDEX_NONE;
		}
	};

	static bool FindEndSnapTargetLanePick(
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

		const float PickCm = FMath::Clamp(SnapDistCm * 0.4f, 80.0f, 280.0f);
		const double SnapDistSq = static_cast<double>(PickCm) * static_cast<double>(PickCm);
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
			const double SplineLen = SnapSpline->GetSplineLength();
			for (int32 MouthPass = 0; MouthPass < 2; ++MouthPass)
			{
				const bool bNearEnd = (MouthPass == 0);
				const double MouthS = bNearEnd ? SplineLen : 0.0;
				const EMetaLandRoadMouthSide MouthSide =
					bNearEnd ? EMetaLandRoadMouthSide::End : EMetaLandRoadMouthSide::Start;
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
				const FVector MouthMid = (MouthLeft + MouthRight) * 0.5;
				if (FVector::DistSquared2D(CursorWorld, MouthMid) > SnapDistSq * 2.25)
				{
					continue;
				}

				// Dual Left|Right live on side sections — host section at tip is often empty.
				const int32 LeftSectionIndex = SnapSpline->GetRoadLayout().FindSideSection(
					SectionIndex, EMetaLandRoadLaneSectionSide::Left);
				const int32 RightSectionIndex = SnapSpline->GetRoadLayout().FindSideSection(
					SectionIndex, EMetaLandRoadLaneSectionSide::Right);

				auto Consider = [&](int32 LaneIndex)
				{
					const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
						SnapSpline, SectionIndex, LaneIndex);
					if (LaneSectionIndex == INDEX_NONE)
					{
						return;
					}
					const FVector Outer = SnapSpline->EvalLanePosition(
						LaneSectionIndex, LaneIndex, MouthS, 1.0, ESplineCoordinateSpace::World);
					const FVector Inner = SnapSpline->EvalLanePosition(
						LaneSectionIndex, LaneIndex, MouthS, 0.0, ESplineCoordinateSpace::World);
					const FVector Mid = (Outer + Inner) * 0.5;
					const double DistSq = FMath::Min(
						FVector::DistSquared2D(CursorWorld, Outer),
						FVector::DistSquared2D(CursorWorld, Mid));
					if (DistSq <= SnapDistSq && DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						OutPick.Spline = SnapSpline;
						OutPick.Lane = LaneIndex;
						OutPick.Section = SectionIndex;
						OutPick.Mouth = MouthSide;
						OutPick.MouthS = MouthS;
					}
				};

				if (RightSectionIndex != INDEX_NONE)
				{
					const FMetaLandRoadLaneSection& RightSection =
						SnapSpline->GetLaneSection(RightSectionIndex);
					for (int32 i = 0; i < RightSection.Right.Num(); ++i)
					{
						Consider(i + 1);
					}
				}
				if (LeftSectionIndex != INDEX_NONE)
				{
					const FMetaLandRoadLaneSection& LeftSection =
						SnapSpline->GetLaneSection(LeftSectionIndex);
					for (int32 i = 0; i < LeftSection.Left.Num(); ++i)
					{
						Consider(-(i + 1));
					}
				}
			}
		}

		return OutPick.IsValid();
	}

	static void DrawAnchorMouthBarPDI(
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

	static void DrawAnchorBundleMouthBarsPDI(
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
	constexpr float MinParallelGuidanceExtendCm = 150.0f;
	/** Shared sample step for green rails + mesh-key bake center path. */
	constexpr float GuidanceSampleStepCm = 80.0f;
	constexpr float GuidancePDILiftCm = 2.0f;
	/** Viewport PDI thickness (pixels) for green dual-rail guidance. */
	constexpr float GuidanceLineWidth = 28.0f;
	const FLinearColor GuidanceLeftRailColor(0.15f, 0.95f, 0.28f, 1.0f);
	const FLinearColor GuidanceRightRailColor(0.15f, 0.95f, 0.28f, 1.0f);
	/** Max mesh keys baked from guidance center (excludes live tip). Cap avoids 乱面. */
	constexpr int32 MaxGuidanceBakeMeshKeys = 5;

	/**
	 * MetaRoad FitSplineToBeginConnection style:
	 * pin mouth + CurveUser tangent = Forward * Dist(mouth, neighbor). Neighbor stays Curve.
	 */
	static void StabilizeLaneExtendMouthPoint(
		UMoKuEditSplinesComponent* SplineComponent,
		int32 MouthPointIndex,
		const FVector& MouthWorld,
		const FVector& MouthOutward,
		double MouthWidthCm = 0.0)
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
	static bool TryBuildMouthSnapAnchor(
		UMetaLandRoadSplineComponent* SnapSpline,
		bool bNearEnd,
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
		const double MouthS = bNearEnd ? SnapSpline->GetSplineLength() : 0.0;
		const EMetaLandRoadMouthSide MouthSide =
			bNearEnd ? EMetaLandRoadMouthSide::End : EMetaLandRoadMouthSide::Start;
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

	static bool ResolveLaneEdgeConnectionSnap(
		AMoKuEditSplineActor* SnapRoad,
		const FVector& ApproachFrom,
		const FVector& CursorWorld,
		float SnapDistCm,
		int32 DesiredLaneCount,
		FVector& OutSnapPos,
		FVector& OutArriveTangent,
		float& OutSampleDist,
		FMetaLandRoadDrawAnchor* OutAnchor = nullptr,
		FVector* OutGhostLeft = nullptr,
		FVector* OutGhostRight = nullptr)
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

		const float OutputKey = SnapSpline->FindInputKeyClosestToWorldLocation(CursorWorld);
		const float SampleDist = SnapSpline->GetDistanceAlongSplineAtSplineInputKey(OutputKey);
		const float SplineLen = SnapSpline->GetSplineLength();
		OutSampleDist = SampleDist;

		const float MouthSnapCm = FMath::Clamp(SnapDistCm * 0.4f, 80.0f, 280.0f);
		const bool bNearStart = SampleDist <= MouthSnapCm;
		const bool bNearEnd = SampleDist >= FMath::Max(0.0f, SplineLen - MouthSnapCm);
		if (bNearStart || bNearEnd)
		{
			const float MouthS = bNearEnd ? SplineLen : 0.0f;
			FVector MouthLeft, MouthRight, MouthCenter;
			if (SnapSpline->GetRoadEdgePointsAtS(
				MouthS, MouthLeft, MouthRight, MouthCenter, ESplineCoordinateSpace::World))
			{
				const FVector MouthMid = (MouthLeft + MouthRight) * 0.5;
				if (FVector::Dist2D(CursorWorld, MouthMid) <= static_cast<double>(MouthSnapCm))
				{
					const FVector SplineDir = SnapSpline->GetDirectionAtDistanceAlongSpline(
						MouthS, ESplineCoordinateSpace::World).GetSafeNormal2D();
					const FVector MouthOutward = bNearEnd ? SplineDir : (-SplineDir);
					OutSnapPos = MouthMid;
					FVector Arrive = (MouthMid - ApproachFrom).GetSafeNormal2D();
					if (Arrive.IsNearlyZero())
					{
						Arrive = (-MouthOutward).GetSafeNormal2D();
					}
					OutArriveTangent = Arrive;
					OutSampleDist = MouthS;
					FVector GhostL = MouthLeft;
					FVector GhostR = MouthRight;
					if (OutAnchor)
					{
						TryBuildMouthSnapAnchor(SnapSpline, bNearEnd, CursorWorld, DesiredLaneCount, *OutAnchor);
						// Prefer the snapped lane mouth (center + rails) so preview matches ConnectTo.
						FVector LaneL, LaneR, LaneC, LaneOut;
						if (OutAnchor->IsValid()
							&& MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
								SnapSpline, *OutAnchor, LaneL, LaneR, LaneC, LaneOut))
						{
							OutSnapPos = LaneC;
							GhostL = LaneL;
							GhostR = LaneR;
							Arrive = (LaneC - ApproachFrom).GetSafeNormal2D();
							if (Arrive.IsNearlyZero())
							{
								Arrive = (-LaneOut).GetSafeNormal2D();
							}
							OutArriveTangent = Arrive;
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
			}
		}

		// Mid-body edge snap disabled — MetaRoad EndConnection = tip mouth only.
		return false;
	}

	static void HideEditorSplineRibbon(UMoKuEditSplinesComponent* SplineComponent)
	{
		if (!SplineComponent)
		{
			return;
		}
		SplineComponent->SetVisibility(false);
		SplineComponent->SetHiddenInGame(true);
		SplineComponent->bDrawDebug = false;
	}

	static void ApplyGuidanceSurfaceZ(FVector& Point)
	{
		Point.Z += GuidanceMeshSurfaceZOffset + GuidancePDILiftCm;
	}

	/** Vertical snap to landscape / ground — ignore road & junction meshes so draw does not climb their tops. */
	static bool ProjectPointOntoLandscapeSurface(
		UWorld* World,
		FVector& InOutPoint,
		const AActor* ExtraIgnoreActor = nullptr)
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

	static void BlendRailTowardEdge(TArray<FVector>& Rail, const FVector& Edge, bool bFromStart)
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

	static void MatchEdgePairToRailTips(
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

	static FVector TransportOffsetRight(const FVector& Tangent, const FVector& PrevRight, const FVector& PreferRight)
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

	static void SoftenGuidanceRailStart(
		TArray<FVector>& Rail,
		const FVector& TipEdge,
		const FVector& TipTangent,
		double StepCm)
	{
		(void)TipTangent;
		(void)StepCm;
		BlendRailTowardEdge(Rail, TipEdge, true);
	}

	static void SoftenGuidanceRailFreeEnd(TArray<FVector>& Rail)
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
	static void FlattenGuidanceRailToPlaneZ(TArray<FVector>& Rail, float PlaneZ)
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
	static float ResolveGuidancePlaneZOnSurface(
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

	static int32 MapSpawnedBranchLaneIndex(int32 SourceLaneIndex, const TArray<int32>& SourceLanes)
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

	static void TransferEditTargetToSpawnedBranch(
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

	static double ResolveEditableTipS(
		const AMoKuEditSplineActor* RoadActor,
		const UMoKuEditSplinesComponent* Spline,
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

	static void BuildOffsetRails(
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

	static void AppendResampledSegment(
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
	static double CatmullRomKnotDelta(const FVector& A, const FVector& B)
	{
		return FMath::Max(FMath::Sqrt(FVector::Dist2D(A, B)), 1.0);
	}

	/**
	 * Ghost P0/P3 encode leave/arrive so a 2-point extend has determined tangents.
	 * P1 = mouth, P2 = cursor/snap. Chord-length ghosts keep tangent magnitude consistent.
	 */
	static void BuildCentripetalCatmullRomGhosts(
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

	static FVector EvalCentripetalCatmullRomPoint(
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
	static void ComputeCentripetalCatmullRomHermiteTangents(
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

	static void AppendHermiteSegment(
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
	static bool ComputeAsymmetricTangentBezier(
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

	static FVector EvalCubicBezier(
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

	static FVector EvalCubicBezierDerivative(
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

	static void AppendAsymmetricTangentBezierPath(
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

	static void ApplyAsymmetricTangentBezierConnection(
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
		// Cubic Bezier = 2 on-curve keys; leave/arrive tangents carry C1/C2.
		const int32 InteriorCount = 0;
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

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[MetaLandRoad] Bezier baked points=%d interiors=%d chord=%.1f"),
			SplineComponent->GetNumberOfSplinePoints(),
			InteriorCount,
			Chord);
	}

	/**
	 * Straight leave → cubic Bezier → straight arrive (asymmetric ray intersection).
	 */
	static void AppendRampCenterPath(
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
	static void BuildRampKeyPointsForSpline(
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
	static void SoftenLaneExtendCornerJoint(
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
	static void ApplyLaneExtendHermiteSegmentHandles(
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

	struct FLaneExtendPointFreeze
	{
		FVector WorldLocation = FVector::ZeroVector;
		FVector ArriveWorld = FVector::ZeroVector;
		FVector LeaveWorld = FVector::ZeroVector;
		TEnumAsByte<ESplinePointType::Type> PointType = ESplinePointType::Curve;
	};

	static void CaptureLaneExtendPointFreezes(
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
			OutFreezes.Add(Freeze);
		}
	}

	/** Restore committed points. Tip keeps its NEW Leave (for the new segment). */
	static void RestoreLaneExtendPointFreezes(
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

	/** MetaRoad: only mouth may stay CurveUser; everything else → Curve. */
	static void ClearNonMouthCustomTangents(
		UMoKuEditSplinesComponent* SplineComponent,
		int32 MouthPointIndex,
		int32 PreserveStubIndex = INDEX_NONE,
		int32 PreserveSoftJointIndex = INDEX_NONE)
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

	static void BuildCenterSamplesFromExtendPlan(
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
	static void BuildMeshKeysFromGuidanceCenter(
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

	/** Parallel / tapering edge rails: center path + path-normal offset (Fig3-style, no crossing). */
	static void BuildLaneWidthGuidanceRails(
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
		const FVector* OptionalEndLeft = nullptr,
		const FVector* OptionalEndRight = nullptr);

	static void AppendPerLaneGuidanceRails(
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

	static void BuildLaneWidthGuidanceRails(
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
		const FVector* OptionalEndRight)
	{
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
		const FVector TipT = TipTangent.GetSafeNormal2D();
		const FVector ChordDir = (PathEnd - PathCenter).GetSafeNormal2D();
		const double LeaveAlign = (!TipT.IsNearlyZero() && !ChordDir.IsNearlyZero())
			? FVector::DotProduct(TipT, ChordDir)
			: 1.0;
		const double RailStepCm = (bUseHermite && LeaveAlign < 0.75)
			? FMath::Min(SampleStepCm, 45.0)
			: SampleStepCm;
		if (bUseHermite && bTaperToMouth)
		{
			FAsymmetricTangentBezier Fusion;
			FVector Arrive = EndTangent.GetSafeNormal2D();
			if (Arrive.IsNearlyZero())
			{
				Arrive = ChordDir;
			}
			if (ComputeAsymmetricTangentBezier(PathCenter, PathEnd, TipT, Arrive, 0.4, Fusion))
			{
				const int32 Steps = 28;
				FVector StartL = FlatTipLeft;
				FVector StartR = FlatTipRight;
				FVector EndL = *OptionalEndLeft;
				FVector EndR = *OptionalEndRight;
				EndL.Z = PlaneZ;
				EndR.Z = PlaneZ;
				// Pair mouth corners by nearest distance — world Across-dot flips on facing mouths (X).
				if (FVector::Dist2D(StartL, EndR) + FVector::Dist2D(StartR, EndL)
					< FVector::Dist2D(StartL, EndL) + FVector::Dist2D(StartR, EndR))
				{
					Swap(EndL, EndR);
				}
				FVector Across0 = (StartR - StartL).GetSafeNormal2D();
				FVector Across1 = (EndR - EndL).GetSafeNormal2D();
				if (Across0.IsNearlyZero())
				{
					Across0 = FVector::CrossProduct(FVector::UpVector, TipT).GetSafeNormal2D();
				}
				if (Across1.IsNearlyZero())
				{
					Across1 = Across0;
				}

				OutLeft.Reset();
				OutRight.Reset();
				OutLeft.Reserve(Steps + 1);
				OutRight.Reserve(Steps + 1);
				for (int32 i = 0; i <= Steps; ++i)
				{
					const double T = static_cast<double>(i) / static_cast<double>(Steps);
					FVector Center = EvalCubicBezier(
						Fusion.Pos0, Fusion.Ctrl1, Fusion.Ctrl2, Fusion.Pos1, T);
					Center.Z = PlaneZ;
					FVector Across = FMath::Lerp(Across0, Across1, T).GetSafeNormal2D();
					if (Across.IsNearlyZero())
					{
						Across = Across0;
					}
					const double PairedEndHwL = FMath::Max(
						FVector::Dist2D(EndL, PathEnd), MetaLandRoad::DefaultRoadLaneWidth * 0.25);
					const double PairedEndHwR = FMath::Max(
						FVector::Dist2D(EndR, PathEnd), MetaLandRoad::DefaultRoadLaneWidth * 0.25);
					const double HwL = FMath::Lerp(HalfWidthLeft, PairedEndHwL, T);
					const double HwR = FMath::Lerp(HalfWidthRight, PairedEndHwR, T);
					if (i == 0)
					{
						OutLeft.Add(StartL);
						OutRight.Add(StartR);
					}
					else if (i == Steps)
					{
						OutLeft.Add(EndL);
						OutRight.Add(EndR);
					}
					else
					{
						OutLeft.Add(Center - Across * HwL);
						OutRight.Add(Center + Across * HwR);
					}
				}
				FlattenGuidanceRailToPlaneZ(OutLeft, PlaneZ);
				FlattenGuidanceRailToPlaneZ(OutRight, PlaneZ);
				return;
			}
		}

		if (bUseHermite)
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

		// Hermite only: blend onto mouth edges. Straight dual-rail must stay parallel (no pin).
		if (bUseHermite && OutLeft.Num() > 0 && OutRight.Num() > 0)
		{
			FVector StartL = FlatTipLeft;
			FVector StartR = FlatTipRight;
			MatchEdgePairToRailTips(OutLeft, OutRight, StartL, StartR, true);
			SoftenGuidanceRailStart(OutLeft, StartL, TipT, RailStepCm);
			SoftenGuidanceRailStart(OutRight, StartR, TipT, RailStepCm);
		}

		// EndConnection: blend green rail tips onto the target mouth (flattened).
		if (bTaperToMouth && OutLeft.Num() > 0 && OutRight.Num() > 0)
		{
			FVector EndL = *OptionalEndLeft;
			FVector EndR = *OptionalEndRight;
			EndL.Z = PlaneZ;
			EndR.Z = PlaneZ;
			MatchEdgePairToRailTips(OutLeft, OutRight, EndL, EndR, false);
			BlendRailTowardEdge(OutLeft, EndL, false);
			BlendRailTowardEdge(OutRight, EndR, false);
		}
		else
		{
			SoftenGuidanceRailFreeEnd(OutLeft);
			SoftenGuidanceRailFreeEnd(OutRight);
		}

		// Single PDI lift pass — BuildOffsetRails may have already lifted; normalize to plane+lift.
		FlattenGuidanceRailToPlaneZ(OutLeft, PlaneZ);
		FlattenGuidanceRailToPlaneZ(OutRight, PlaneZ);
	}

	static double ResolveDrawTipSForGuidance(
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

	static bool ResolveDrawTipLaneEdges(
		const AMoKuEditSplineActor* RoadActor,
		const UMetaLandRoadSplineComponent* Spline,
		double TipS,
		bool bForward,
		const FMetaLandRoadEditTarget* EditTarget,
		FVector& OutLeft,
		FVector& OutRight,
		FVector& OutCenter)
	{
		if (!RoadActor || !Spline)
		{
			return false;
		}

		if (EditTarget && EditTarget->bHasPendingAnchor
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
	static bool ResolveCtrlDrawRayHit(
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

	static FVector ProjectOntoParallelLaneExtend(
		const FVector& Origin,
		const FVector& ExtendTangent,
		const FVector& WorldPoint,
		bool bClampForwardOnly = true)
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

	static bool ResolveParallelLaneExtendFrame(
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

	static bool ProjectClickForParallelLaneExtend(
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

	static const FName MetaLandRoadLaneBranchTag(TEXT("MetaLandRoadLaneBranch"));

	static void RefreshRoadMeshSuppressJunction(AMoKuEditSplineActor* RoadActor)
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

	static bool IsLaneBranchRoad(const AMoKuEditSplineActor* RoadActor)
	{
		return RoadActor && RoadActor->Tags.Contains(MetaLandRoadLaneBranchTag);
	}

	static int32 CountAnchorBundleLanes(const FMetaLandRoadDrawAnchor& Anchor)
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

	static bool RestoreLaneDrawStartAnchorFromBranch(
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

	static bool ResolveGuidanceCursorOnPlane(
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

	static bool TryResolveGuidancePlaneZ(
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

	static FVector ResolveGuidanceCursorForRay(
		const FRay& WorldRay,
		const UMetaLandRoadEditorMode* RoadMode,
		const AMoKuEditSplineActor* ActiveRoad,
		EMoKuEditState DrawState,
		float StoredPlaneZ,
		bool bHasStoredPlaneZ,
		float FallbackZ,
		const FVector& TraceHitPos,
		bool bHasTraceHit,
		UWorld* /*WorldUnused*/ = nullptr)
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
}

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
		FMetaLandRoadModule::Get().SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
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
	GEditor->OnLevelActorAdded().AddUObject(this, &UMetaLandRoadInteractiveTool::Test);
	for (TActorIterator<AMoKuEditBaseActor>It(TargetWorld); It; ++It)
	{
		AMoKuEditBaseActor* SplineActor = *It;
		TArray<FSocketGizmoTransformInfo> TransformInfo;
		TArray<USPlineMarkGizmo*> GizmoInfos;
		TArray<FSocketGizmoTransformInfo>GizmoTransformInfos= SplineActor->GetGizmoInfo();
		for (const auto& Info:GizmoTransformInfos)
		{
			USPlineMarkGizmo* SPlineMarkGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<USPlineMarkGizmo>(TEXT("SocketGizmo"), FString(), this);
			SPlineMarkGizmo->CreateGizmoHandle(Info);
			SPlineMarkGizmo->SetSelectedObject(SplineActor);
			GizmoInfos.Add(SPlineMarkGizmo);
		}
		//GizmoListInfo.Add(SplineActor,GizmoInfos);
	}
	RoadOp = MakeUnique<FCurvesRoadOp>();
	//��ʱ����
	//CornerDataTable = LoadObject<UDataTable>(nullptr, *DataTablePath);
	//GetSelectedAsset();
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
		FMetaLandRoadModule::Get().SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
	}
	GEditor->OnLevelActorAdded().RemoveAll(this);
	//for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair :GizmoListInfo)
	//{

	//	for (auto Item : KeyValuePair.Value)
	//	{
	//		GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
	//	}
	//}
	CornerDataTable = nullptr;
	if (PreviewActor->IsValidLowLevel())
	{
		// ����Destroy����Actor
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		bIsDraw = false;

	}
	bIsInvert = false;
	bIsInsertCross = false;
}
FInputRayHit UMetaLandRoadInteractiveTool::ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos)
{
	FInputRayHit ToReturn;
	ToReturn.bHit = true;
	if (!bIsDraw && !bHasEndSnapGhost)
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
FInputRayHit UMetaLandRoadInteractiveTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bPendingAnchor = RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor;
	const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
	const bool bCtrlDown = ModKeys.IsControlDown();

	FVector Temp;
	const FInputRayHit RayHit = FindRayHit(PressPos.WorldRay, Temp);

	const bool bShouldCapture =
		GetEditState()
		|| IsLaneAnchorDrawSession()
		|| bHasEndSnapGhost
		|| (bPendingAnchor && bCtrlDown)
		|| (bCtrlDown && RayHit.bHit);

	if (!bShouldCapture)
	{
		return FInputRayHit();
	}
	return RayHit;
}
FInputRayHit UMetaLandRoadInteractiveTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bPendingAnchor = RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor;
	// Shift = pick/highlight only; once EditTarget is set, hover must drive guidance without Shift.
	if (!GetEditState() && !IsLaneAnchorDrawSession() && !bHasEndSnapGhost && !bPendingAnchor)
	{
		return FInputRayHit();
	}
	FVector Temp;
	return FindRayHit(PressPos.WorldRay, Temp);
}
void UMetaLandRoadInteractiveTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	if (!bCtrlModifierOn)
	{
		// Lane extend mix on TARGET tip (never EditTarget).
		// UE viewport steals Alt+LMB for camera orbit — Shift is the reliable click.
		// Alt alone = hover preview; Alt+Shift = add-only; Shift = pick/toggle.
		// Mix locks on Shift. Ctrl+LMB does not confirm EndConnection (A does).
		const FModifierKeysState& MixModKeys = FSlateApplication::Get().GetModifierKeys();
		UMetaLandRoadEditorMode* MixRoadMode = ResolveMetaLandRoadEditorMode(this);
		const bool bHasSourceMouth =
			bLaneAnchorDrawSession
			|| (MixRoadMode && MixRoadMode->GetEditTarget().bHasPendingAnchor);
		const bool bTargetMixClick =
			bHasSourceMouth
			&& !MixModKeys.IsControlDown()
			&& MixModKeys.IsShiftDown();
		if (bTargetMixClick)
		{
			const bool bAddOnly = MixModKeys.IsAltDown();
			if (TryHandleEndSnapTargetLaneMixPick(PressPos, bAddOnly))
			{
				return;
			}
			// Missed pick on another road — still don't retarget EditTarget to target mouth.
			if (HitActor && HitActor->IsA<AMoKuEditSplineActor>() && HitActor != ActiveActor)
			{
				return;
			}
		}

		if (!GetEditState() && !bLaneAnchorDrawSession && !bHasEndSnapGhost)
		{
			return;
		}

		if (!MixModKeys.IsShiftDown())
		{
			return;
		}

		FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
		if (ViewportClient && ViewportClient->Viewport)
		{
			FIntPoint MousePos;
			ViewportClient->Viewport->GetMousePos(MousePos);
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				ViewportClient->Viewport,
				ViewportClient->GetScene(),
				ViewportClient->EngineShowFlags));
			if (FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily))
			{
				const FViewportClick Click(
					View,
					ViewportClient,
					EKeys::LeftMouseButton,
					IE_Pressed,
					MousePos.X,
					MousePos.Y);
				if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
				{
					// During lane extend, Shift/Alt picks on other roads are target mix — not source EditTarget.
					if (bLaneAnchorDrawSession && HitActor && HitActor->IsA<AMoKuEditSplineActor>()
						&& HitActor != ActiveActor)
					{
						const FModifierKeysState& PickMods = FSlateApplication::Get().GetModifierKeys();
						if (PickMods.IsAltDown() || PickMods.IsShiftDown())
						{
							return;
						}
					}
					if (RoadMode->TryHandleLaneAnchorPickClick(ViewportClient, Click))
					{
						// Shift pick only (TryHandleLaneAnchorPickClick enforces Shift).
						FVector HitPos;
						if (FindRayHit(PressPos.WorldRay, HitPos).bHit)
						{
							ResultHitPos = HitPos;
						}
						LastGuidanceHoverSamplePos = FVector::ZeroVector;
						bIsDraw = false;
						if (RoadMode->GetEditTarget().bHasPendingAnchor)
						{
							SyncAnchorPreviewFromEditTarget(RoadMode->GetEditTarget());
						}
						if (GEditor)
						{
							GEditor->RedrawLevelEditingViewports(false);
						}
						return;
					}
				}
			}
		}
		return;
	}

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bHasLaneAnchor = RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor;

	if (bHasLaneAnchor && !GetEditState())
	{
		const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
		UMetaLandRoadSplineComponent* AnchorSpline = Target.Spline.Get();
		AMoKuEditSplineActor* TargetRoad = Target.RoadActor.Get();
		if (!AnchorSpline)
		{
			return;
		}

		// Mode re-enter: resume extend on an existing lane-draw branch (session flags were cleared on exit).
		if (!bLaneAnchorDrawSession
			&& TryResumeLaneExtendOnBranchRoad(Target, TargetRoad, AnchorSpline, PressPos))
		{
			return;
		}

		// After A: same tip EditTarget + same actor → resume extend (no tool toggle / no re-spawn).
		if (bLaneAnchorDrawSession
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
			return;
		}

		FTransform MouthTransform = FTransform::Identity;
		if (!AnchorSpline->EvalMouthTransform(Target.PendingAnchor, MouthTransform))
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("BasicAddPoint", "Basic Add Point"));
		const FName TraceTagName = FName("WorldCollision");
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FHitResult Result;
		FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
		const bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(
			Result,
			PressPos.WorldRay.Origin,
			PressPos.WorldRay.PointAt(HALF_WORLD_MAX),
			QueryParams,
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
			// Same center/edges as green guidance rails — first mesh ring must sit on this bar.
			MouthLoc = (MouthLeft + MouthRight) * 0.5;
			MouthWidthCm = FMath::Max(
				static_cast<float>(FVector::Dist2D(MouthLeft, MouthRight)),
				MouthWidthCm);
			// Outward = perpendicular to mouth bar (matches source CS), not a skewed EvalMouth forward.
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

		// Exact mouth mid — no outset gap (outset caused “衔接对不上”).
		ActiveActor = nullptr;
		AddSplinePoint(MouthLoc, HitRot, false);
		FLaneExtendCenterPlan SpawnPlan;
		ResolveLaneExtendCenterPlan(MouthLoc, MouthForward, CursorForExtend, true, SpawnPlan);
		SpawnPlan.StartCenter = MouthLoc;
		SpawnPlan.StartTangent = MouthForward;
		if (SpawnPlan.bUseHermite)
		{
			// Tip + end only; Hermite handles match green rails (no mid-key 折角).
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
			// MetaRoad FromConnection: FitSpline + ConnectTo (replaces ad-hoc mouth stabilize as source of truth).
			FinalizeLaneDrawFromConnection(SpawnedRoad, LaneDrawStartAnchor);
		}

		bIsDraw = false;
		bInsertJunction = false;
		bLaneAnchorDrawSession = true;
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
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	// Normal Ctrl draw (no lane anchor) OR continue lane-extend session.
	FScopedTransaction Transaction(LOCTEXT("BasicAddPoint", "Basic Add Point"));
	FHitResult Result;
	AActor* ResolvedHitActor = nullptr;
	const bool bHitWorld = ResolveCtrlDrawRayHit(TargetWorld, PressPos.WorldRay, Result, ResolvedHitActor);
	if (bCtrlModifierOn)
	{
		AMoKuEditSplineActor* ActiveSplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
		if (bLaneAnchorDrawSession || (RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor))
		{
			SyncLaneDrawCursorFromRay(PressPos.WorldRay);
		}
		else if (bHitWorld)
		{
			ResultHitPos = Result.ImpactPoint;
			// Always snap commit XY→landscape Z (ignore elevated road tops / stale guidance plane).
			ProjectPointOntoLandscapeSurface(
				TargetWorld, ResultHitPos, Cast<AMoKuEditSplineActor>(ActiveActor));
			// Guidance rails may stay planar; plane seeds from ground under first click.
			if (!bHasGuidancePlaneZ)
			{
				SetGuidancePlaneFromMouthCenter(ResultHitPos);
			}
		}
		HitActor = ResolvedHitActor;
		// Do NOT force parallel project here — ResolveLaneExtendCenterPlan chooses Hermite vs straight.
		const FVector3d HitPos = ResultHitPos;
		FRotator HitRot = FRotator::ZeroRotator;
		AMoKuEditIntersectionActor* JunctionActor = nullptr;
		if (HitActor && HitActor->IsA<AMoKuEditBaseActor>())
		{
			AMoKuEditSplineActor* SourceActor = Cast<AMoKuEditSplineActor>(HitActor);
			AMoKuEditIntersectionActor* HitJunction = Cast<AMoKuEditIntersectionActor>(HitActor);
			if (!bInsertJunction && !GetEditState() && !bLaneAnchorDrawSession)
			{
				ActiveActor = SourceActor;
				GetSplitIntersectionData(ActiveActor, ResultHitPos);
				PreIntersectData = IntersectData;
				PreIntersectData.PreHitActor = Cast<AMoKuEditBaseActor>(HitActor);
				bInsertJunction = true;
				if (bIsDraw)
				{
					UMoKuEditSplinesComponent* SplineComp = SourceActor->GetSplineComponent();
					FTransform InSertTransform = SplineComp->GetTransformAtDistanceAlongSpline(IntersectData.TValue, ESplineCoordinateSpace::World);
					AddSplinePoint(InSertTransform.GetLocation(), HitRot, false);
				}
				else
				{
					AddSplinePoint(ResultHitPos, HitRot, false);
				}
			}
			else if (!bInsertJunction && GetEditState())
			{
				SyncLaneDrawCursorFromRay(PressPos.WorldRay);
				if (SourceActor)
				{
					GetSplitIntersectionData(SourceActor, ResultHitPos);
				}
				const FVector EndCursor = bIsDraw ? FVector(DrawOutSplinePos) : ResultHitPos;

				// Lane-extend: Shift locks mix; A commits EndConnection. Ctrl+LMB only adds points.
				if (bLaneAnchorDrawSession)
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
						if (bHitOtherRoad && SourceActor->GetSplineComponent())
						{
							if (bIsDraw)
							{
								const FTransform InSertTransform =
									SourceActor->GetSplineComponent()->GetTransformAtDistanceAlongSpline(
										IntersectData.TValue, ESplineCoordinateSpace::World);
								AddLaneDrawSegmentFromCursor(
									InSertTransform.GetLocation(), HitRot, false, false, nullptr);
							}
							else
							{
								AddLaneDrawSegmentFromCursor(ResultHitPos, HitRot, false, false, nullptr);
							}
						}
						else
						{
							AddLaneDrawSegmentFromCursor(ResultHitPos, HitRot, false, false, nullptr);
						}

						AMoKuEditSplineActor* BranchActor = Cast<AMoKuEditSplineActor>(ActiveActor);
						if (bLaneAnchorDrawSession && IsLaneBranchRoad(BranchActor))
						{
							CookJunctionForActiveRoad(true, true);
							ClearSelectionForDrawSession();
							return;
						}

						CookJunctionForActiveRoad(true, true);
						ClearSelectionForDrawSession();
						return;
					}

					if (AddLaneDrawSegmentFromCursor(EndCursor, HitRot, false, false, nullptr))
					{
						UpdateDrawGuidanceRails();
						return;
					}
				}

				const bool bHitOtherRoad = SourceActor && SourceActor != ActiveSplineActor;
				const bool bHitExistingJunction = HitJunction != nullptr;
				if (bHitOtherRoad || bHitExistingJunction)
				{
					// Click onto another road/junction while drawing → cook junction + interrupt (no A).
					if (bHitOtherRoad && SourceActor->GetSplineComponent())
					{
						if (bIsDraw)
						{
							const FTransform InSertTransform =
								SourceActor->GetSplineComponent()->GetTransformAtDistanceAlongSpline(
									IntersectData.TValue, ESplineCoordinateSpace::World);
							AddSplinePoint(InSertTransform.GetLocation(), HitRot, false);
						}
						else
						{
							AddSplinePoint(ResultHitPos, HitRot, false);
						}
					}
					else
					{
						AddSplinePoint(ResultHitPos, HitRot, false);
					}

					CookJunctionForActiveRoad(true);
					ClearSelectionForDrawSession();
					return;
				}

				AddSplinePoint(ResultHitPos, HitRot, false);
				// Hit self / non-road base actor: still cook if new segment XY-crosses another road.
				if (TryCookJunctionOnCtrlPressIfCrossed())
				{
					return;
				}
				ClearSelectionForDrawSession();
				return;
			}
			else if (bInsertJunction && GetEditState())
			{
				ClearSelectionForDrawSession();
				GEditor->SelectActor(ActiveActor, true, true, false, true);
				AddSplinePoint(HitPos, HitRot, false);

				// Started from a road: cook on other road/junction hit, or XY cross under terrain.
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
				return;
			}
		}
		else if (HitActor && !HitActor->IsA<AMoKuEditSplineActor>() && bInsertJunction)
		{
			// Terrain click after starting from a road — raised ground often hides the crossed mesh.
			ClearSelectionForDrawSession();
			AddSplinePoint(HitPos, HitRot, false);
			bInsertJunction = false;
			if (TryCookJunctionOnCtrlPressIfCrossed())
			{
				return;
			}
		}
		else if ((!HitActor || !HitActor->IsA<AMoKuEditSplineActor>()) && !bInsertJunction)
		{
			SyncLaneDrawCursorFromRay(PressPos.WorldRay);
			const FVector ClickPos = ResultHitPos;
			if (bLaneAnchorDrawSession && GetEditState())
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
			}
			// Terrain / miss: detect centerline XY cross after the new point (no mesh hit required).
			if (TryCookJunctionOnCtrlPressIfCrossed())
			{
				return;
			}
		}
		if (0)
		{

			if (ActiveSplineActor)
			{
				UMoKuEditSplinesComponent* AciveEditComp = ActiveSplineActor->GetSplineComponent();
				if (SceneManagement)
				{
					//SceneManagement->SetActiveActor(ActiveSplineActor);
					SceneManagement->OctreeAddRoad(ActiveSplineActor);
					ActiveSplineActor->UpdateOctreeInfo();
					TArray<FInteresectionPointInfo> OutIntersectionInfo;
					OutDebugBox.Empty();
					MoKuSplineEditorUtils::OctreeSegmentsIntersect(SceneManagement->Octree, ActiveSplineActor, OutIntersectionInfo, OutDebugBox);

					for (auto& A : OutIntersectionInfo)
					{

						DebugCorner.Add(A.IntersectPoint);

					}
					TMap<TObjectPtr<AMoKuEditBaseActor>, TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>>> OutActorInteresectionMapPointInfo;
					MoKuSplineEditorUtils::ConvertPointInfoData(OutIntersectionInfo, OutActorInteresectionMapPointInfo);
					TArray<TObjectPtr<AMoKuEditBaseActor>> OutKeys;
					OutActorInteresectionMapPointInfo.GetKeys(OutKeys);
					TMap<FVector, TObjectPtr<AMoKuEditIntersectionActor>> RecordJunctionAndActorMap;
					for (const auto& Key : OutKeys)
					{
						AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(Key);
						TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>> CurvePointInfoMap = OutActorInteresectionMapPointInfo[Key];
						TArray<TObjectPtr<UMoKuEditSplinesComponent>>OutSplines;
						TMap<int32, TArray<FVector>> GatesInfo;
						CurvePointInfoMap.GetKeys(OutSplines);
						TObjectPtr<AMoKuEditIntersectionActor> CurrentJunctionActor;
						int32 Index = 0;
						for (const auto& Spline : OutSplines)
						{
							TArray<FActorInteresectionPointInfo> PointInfos = CurvePointInfoMap[Spline];
							for (int i = 0; i < PointInfos.Num(); i++)
							{
								FVector IntersectPos = PointInfos[i].IntersectPoint;
								if (PointInfos[i].Desc == TEXT("Cross"))
								{
									
									if (!RecordJunctionAndActorMap.Contains(IntersectPos))
									{
										AMoKuEditIntersectionActor* IntersectionActor = GetWorld()->SpawnActor<AMoKuEditIntersectionActor>(PointInfos[i].IntersectPoint, { 0,0,0 });
										if (IntersectionActor)
										{
											IntersectionActor->SetFolderPath(SCENE_JUNCTION_PATH);
											RecordJunctionAndActorMap.Add(IntersectPos, IntersectionActor);
										}
									}

									CurrentJunctionActor = *RecordJunctionAndActorMap.Find(IntersectPos);
									continue;
								}
								else
								{
									double SampleDist = PointInfos[i].Dist;
									if (Index % 2 == 0)
									{
										SampleDist -= 2250.0;
									}
									else
									{
										SampleDist += 2250.0;
									}
									FVector Pos = Spline->GetLocationAtDistanceAlongSpline(SampleDist, ESplineCoordinateSpace::World);
									if (!GatesInfo.Find(Index))
									{
										GatesInfo.Add(Index, { Pos });
									}
									else
									{
										GatesInfo[Index].Add(Pos);
									}
									Index++;
								}
							}
						}

						int32 CurIndex = 0;
						for (auto& Pair : GatesInfo)
						{

							//UE_LOG(LogTemp, Warning, TEXT("Key==%i"),Pair.Key)
							//if (Pair.Key % 2 == 0 && Pair.Key != GatesInfo.Num())
							//{
							//	CurIndex++;
							//	continue;
							//}
							FVector Sum(ForceInit);
							for (auto& Value : Pair.Value)
							{
								Sum += Value;
							}
							Sum /= Pair.Value.Num();

							if (AMoKuEditSplineActor* CurActor = Cast<AMoKuEditSplineActor>(Key))
							{
								TArray<FTransform> OutSplitSplinePosition;
								UMoKuEditSplinesComponent* EditComp = CurActor->GetSplineComponent();
								AMoKuEditSplineActor* NewSplineActor;
								float Dist = EditComp->GetDistanceAlongSplineAtLocation(Sum, ESplineCoordinateSpace::World);
								if (Pair.Key%2==0)
								{
									MoKuSplineEditorUtils::CutSplineFromRange(EditComp, 0, Dist, OutSplitSplinePosition);
									NewSplineActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(CurActor, OutSplitSplinePosition, true);
									SceneManagement->OctreeAddRoad(NewSplineActor);										
								}
								else
								{
									float EndDist = EditComp->GetSplineLength();
									FVector NextSum(ForceInit);
									if (CurIndex < GatesInfo.Num() - 1)
									{
										for (auto& Value : GatesInfo[CurIndex])
										{
											NextSum += Value;
										}
										NextSum /= GatesInfo[CurIndex].Num();
										EndDist = EditComp->GetDistanceAlongSplineAtLocation(NextSum, ESplineCoordinateSpace::World);
									}

									MoKuSplineEditorUtils::CutSplineFromRange(EditComp, Dist, EndDist, OutSplitSplinePosition);
									NewSplineActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(CurActor, OutSplitSplinePosition, true);
									SceneManagement->OctreeAddRoad(NewSplineActor);
								}
								if (CurrentJunctionActor)
								{
									CurrentJunctionActor->ConnectionActors.Add(NewSplineActor);
								}
							}
							CurIndex++;
						}
							
						
					}

					if (OutIntersectionInfo.Num() > 0)
					{
						for (auto& Item : OutIntersectionInfo)
						{
							for (auto& Road : Item.IntersectRoadList)
							{
								if (PreviewComponent)
								{
									if (PreviewComponent->GetOwner() == Road)
									{
										PreviewComponent->UnregisterComponent();
										PreviewComponent->DestroyComponent();
										ClearSelectionForDrawSession();
										UpdateEditState(false);
									}
								}
								Road->Destroy();
							}
						}
					}

					OutIntersectPoints.Empty();
					for (auto& Pair : RecordJunctionAndActorMap)
					{
						TObjectPtr<AMoKuEditIntersectionActor> Junction = Pair.Value;
						Junction->UpdateSplineCornerInfo();
						Junction->FillMeshActor();
						for (auto& Info : Junction->CornerInfo)
						{
							CornerInfo.Add(Info);
						}
					}
				}
			}
			Modify();
		}

	}

}


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

	if (!bIsDrawHud)
	{
		return;
	}

	FString HelpText = TEXT("LMB / Shift+LMB: Select lane anchor\n")
		TEXT("Hover: Mouth highlight\n")
		TEXT("Selected + move mouse: Lane-width guide rails\n")
		TEXT("Selected + Ctrl+LMB: Extend road from lane\n")
		TEXT("Extend + Shift+LMB on TARGET tip: toggle mix lane (use THIS — Alt+LMB is camera)\n")
		TEXT("Extend + Alt+Shift+LMB: add-only mix lane\n")
		TEXT("Extend + Alt/Shift hover: green preview on target tip lane\n")
		TEXT("Selected mix: cyan mouth bars — screen shows EndSnap mix lanes [..]\n")
		TEXT("Ctrl+LMB (no selection): Normal road draw\n")
		TEXT("Ctrl+Z: Undo\n")
		TEXT("Shift+F1: Toggle Help");
	MoKuSplineEditorUtils::DrawHelpCanvasTileItem(Canvas, HelpText);
}





void UMetaLandRoadInteractiveTool::UpdateSplineRoad(USplineComponent* IntersectCurve, USplineComponent* InEditingCurve, const FVector& InIntersectionPos,bool NeedInvert)
{
	if (!IntersectCurve|| !InEditingCurve)return;
	AActor* EditParentActor = InEditingCurve->GetAttachmentRootActor();
	AMoKuEditSplineActor* EditingSplineActor = Cast<AMoKuEditSplineActor>(EditParentActor);
	if (!EditingSplineActor)return;
	float EditingCurveClosestKey = 0;
	float EditingCurveClosestDist = 0;
	float EditingCurveClosestRatio = 0;
	MoKuSplineEditorUtils::GetSplineInterpInfo(InEditingCurve, InIntersectionPos, EditingCurveClosestKey, EditingCurveClosestDist, EditingCurveClosestRatio);

	for (int32 i = 0; i<InEditingCurve->GetNumberOfSplinePoints(); i++)
	{
		float PointSplineKey = float(i) / float(InEditingCurve->GetNumberOfSplinePoints() - 1);
		float RegionalRange = EditingCurveClosestRatio;
		if (NeedInvert)
		{
			PointSplineKey = 1 - PointSplineKey;
			RegionalRange = 1 - RegionalRange;
		}
		if (PointSplineKey <= RegionalRange)
		{
			float Offset = 1500;
			if (EditingCurveClosestRatio > 0.5)Offset *= -1;
			FVector3d FinalLocation = InEditingCurve->GetLocationAtDistanceAlongSpline(EditingCurveClosestDist + Offset, ESplineCoordinateSpace::World);
			InEditingCurve->SetLocationAtSplinePoint(i, FinalLocation, ESplineCoordinateSpace::World);
			if (NeedInvert)
			{
				FVector Tangent = InEditingCurve->GetTangentAtSplineInputKey(EditingCurveClosestKey, ESplineCoordinateSpace::World);
				FVector Direction = FVector::CrossProduct(Tangent.GetSafeNormal(), FVector::UpVector);
				Direction.Z = 0.0;
				FVector PrveLocation = InEditingCurve->GetLocationAtSplinePoint(i - 1, ESplineCoordinateSpace::World);

				InEditingCurve->SetTangentsAtSplinePoint(i - 1,
					InEditingCurve->GetArriveTangentAtSplinePoint(i - 1, ESplineCoordinateSpace::Local),
					FinalLocation - PrveLocation,
					ESplineCoordinateSpace::Local);
			}
			InEditingCurve->UpdateSpline();

			EditingSplineActor->SetSplineComponent(Cast<UMoKuEditSplinesComponent>(InEditingCurve));
			EditingSplineActor->RefreshDynamicRoadMesh();
			ActiveActor = EditingSplineActor;
		}
	}
}

void UMetaLandRoadInteractiveTool::UpdateRoadJunctionInfo
(
	FCurvesRoadOp* InRoadOp,
	AMoKuEditIntersectionActor*& InJunctionActor, 
	AMoKuEditBaseActor* IntersectActor, 
	USplineComponent* InEditingCurve,
	const FVector& InIntersectionPos,
	EIntersectionState IntersectionState
)
{
	if (!InRoadOp||!InJunctionActor || !IntersectActor || !InEditingCurve) return;
	AActor* EditParentActor =  InEditingCurve->GetAttachmentRootActor();
	AMoKuEditSplineActor* EditingSplineActor = Cast<AMoKuEditSplineActor>(EditParentActor);
	if (!EditingSplineActor)return;

	AMoKuEditSplineActor* IntersectSplineActor = Cast<AMoKuEditSplineActor>(IntersectActor);
	float EditingCurveClosestKey = 0;
	float EditingCurveClosestDist = 0;
	float EditingCurveClosestRatio = 0;
	MoKuSplineEditorUtils::GetSplineInterpInfo(InEditingCurve, InIntersectionPos, EditingCurveClosestKey, EditingCurveClosestDist, EditingCurveClosestRatio);
	TArray<FCornerInfo> SideEdges;
	TArray<FCornerInfo> CornerEdges;
	TArray<FVector> IntersectPos;
	InRoadOp->GetIntersectionInfo( InJunctionActor, EditingSplineActor, EditingCurveClosestRatio, CornerEdges, SideEdges, IntersectPos);
	for (auto SideEdge : SideEdges)
	{
		InJunctionActor->CornerInfo.Add(SideEdge);
	}
	if (IntersectionState==EIntersectionState::Right|| IntersectionState == EIntersectionState::Left)
	{
		USplineComponent* BoundSpline = nullptr;

		if (IntersectSplineActor)
		{
			IntersectSplineActor->RefreshDynamicRoadMesh();
			FSideRoadCurvePaths SideCurvePaths = IntersectSplineActor->GetSideCurvePaths();
			IntersectSplineActor->BuildSideSpline(SideCurvePaths.LeftCurvePath);
			IntersectSplineActor->BuildSideSpline(SideCurvePaths.RightCurvePath);
			if (IntersectionState == EIntersectionState::Right)
			{
				BoundSpline = IntersectSplineActor->LeftCurve;
			}
			if (IntersectionState == EIntersectionState::Left)
			{
				BoundSpline = IntersectSplineActor->RightCurve;
			}

			FCornerInfo SideCornerInfo = UpdateJunctionSideInfo(BoundSpline, IntersectPos, IntersectionState);
			InJunctionActor->CornerInfo.Add(SideCornerInfo);
		}
	}
	FIntersectionRoadInfo RoadInfo;
	RoadInfo.RoadActor = EditingSplineActor;
	InJunctionActor->IntersectionRoadInfos.Add(RoadInfo);
	InJunctionActor->ConnectionActors.Add(EditingSplineActor);
}

FCornerInfo UMetaLandRoadInteractiveTool::UpdateJunctionSideInfo(USplineComponent* IntersectComp, TArray<FVector> IntersectionPosList, EIntersectionState IntersectionState)
{
	FCornerInfo TmpCornerInfo;

	if (IntersectionPosList.Num() == 2)
	{
		float InputKey0 = IntersectComp->FindInputKeyClosestToWorldLocation(IntersectionPosList[0]);
		float InputKey1 = IntersectComp->FindInputKeyClosestToWorldLocation(IntersectionPosList[1]);
		float Tmp0 = IntersectComp->GetDistanceAlongSplineAtSplineInputKey(InputKey0);
		float Tmp1 = IntersectComp->GetDistanceAlongSplineAtSplineInputKey(InputKey1);
		if (Tmp0 > Tmp1)
		{
			IntersectionPosList.Swap(0, 1);
		}
	}
	if (IntersectComp)
	{
		TmpCornerInfo = MoKuSplineEditorUtils::CutJunctionCornerInfo(IntersectComp, IntersectionPosList, 20);
	}
	return TmpCornerInfo;
}
FVector UMetaLandRoadInteractiveTool::UpdateSplineRoad(AMoKuEditBaseActor* InHitActor, AMoKuEditIntersectionActor* InJunctionActor,bool NeedInvert)
{

	AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMoKuEditSplinesComponent* EditorSplineComp = SplineActor->GetSplineComponent();
	FVector IntersectPos;
	FEditSplineIntersectInfo IntersectedActorInfo = SplineActor->CheckIntersectionState(InHitActor);
	float FinalValue = 0;
	float CurveLength = EditorSplineComp->GetSplineLength();

	if (IntersectedActorInfo.State != EIntersectionState::None)
	{
		float LeftValue = CurveLength;
		float RightValue = CurveLength;
		if (!NeedInvert)
		{
			LeftValue = 0;
			RightValue = 0;
		}
		if (IntersectedActorInfo.LeftIntersectPositions.Num() > 0)
		{

			for (const FVector& Position : IntersectedActorInfo.LeftIntersectPositions)
			{
				float Ratio = SplineActor->LeftCurve->GetDistanceAlongSplineAtLocation(Position, ESplineCoordinateSpace::World);
				if (NeedInvert)
				{
					LeftValue = FMath::Min(LeftValue, Ratio);

				}
				else
				{
					LeftValue = FMath::Max(LeftValue, Ratio);
				}
			}
		}
		if (IntersectedActorInfo.RightIntersectPositions.Num() > 0)
		{

			for (const FVector& Position : IntersectedActorInfo.RightIntersectPositions)
			{
				float Ratio = SplineActor->RightCurve->GetDistanceAlongSplineAtLocation(Position, ESplineCoordinateSpace::World);
				if (NeedInvert)
				{
					RightValue = FMath::Min(RightValue, Ratio);
				}
				else
				{
					RightValue = FMath::Max(RightValue, Ratio);
				}

			}
		}
		if (NeedInvert)
		{
			FinalValue = FMath::Min(LeftValue, RightValue);
		}
		else
		{
			FinalValue = FMath::Max(LeftValue, RightValue);
		}

	}
	if (NeedInvert)
	{

		FinalValue -= 1000.0f;
		IntersectPos = EditorSplineComp->GetLocationAtDistanceAlongSpline(FinalValue, ESplineCoordinateSpace::World);
		int32 PointIndex = EditorSplineComp->GetNumberOfSplinePoints() - 1;
		EditorSplineComp->SetLocationAtSplinePoint(PointIndex, IntersectPos, ESplineCoordinateSpace::World);
	}
	else
	{
		FinalValue += 1000.0f;
		IntersectPos = EditorSplineComp->GetLocationAtDistanceAlongSpline(FinalValue, ESplineCoordinateSpace::World);
		EditorSplineComp->SetLocationAtSplinePoint(0, IntersectPos, ESplineCoordinateSpace::World);

	}

	EditorSplineComp->UpdateSpline();

	SplineActor->SetSplineComponent(Cast<UMoKuEditSplinesComponent>(EditorSplineComp));
	SplineActor->RefreshDynamicRoadMesh();
	if (NeedInvert)
	{
		SplineActor->EndOfJunction = InJunctionActor;
	}
	else
	{
		SplineActor->StartOfJunction = InJunctionActor;
	}
	return IntersectPos;
}
void UMetaLandRoadInteractiveTool::UpdateSplineAndJunction
(
	FCurvesRoadOp* InRoadOp, 
	AMoKuEditIntersectionActor*& InJunctionActor,
	AMoKuEditBaseActor* InHitActor,
	bool NeedInvert
)
{
	if (!InRoadOp || !InJunctionActor || !InHitActor||!ActiveActor)return;

	AMoKuEditSplineActor* CurrentHitActor = Cast<AMoKuEditSplineActor>(InHitActor);
	if (!CurrentHitActor)return;
	USplineComponent* LeftCurve = CurrentHitActor->LeftCurve;
	USplineComponent* RightCurve = CurrentHitActor->RightCurve;
	AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);

	UMoKuEditSplinesComponent* EditorSplineComp = SplineActor->GetSplineComponent();
	EIntersectionState IntersectState = InRoadOp->CheckRoadIntersectionState(EditorSplineComp, CurrentHitActor);
	USplineComponent* IntersectCurve = LeftCurve;
	if (IntersectState == EIntersectionState::Right)
	{
		IntersectCurve = RightCurve;
	}
	FVector IntersectPos = UpdateSplineRoad(InHitActor, InJunctionActor, NeedInvert);
	UpdateRoadJunctionInfo(InRoadOp, InJunctionActor, CurrentHitActor, EditorSplineComp, InJunctionActor->GetActorLocation(), IntersectState);

}

void UMetaLandRoadInteractiveTool::AddSplinePoint(const FVector& InHitPos, const FRotator& InHitRot, bool InteractiveGizmo,const FVector& InTangent,UInteractiveGizmo* InGizmoInfo)
{
	FScopedTransaction Transaction(LOCTEXT("MoKuEdit_Add Point", "Add MoKu Spline Control Point"));
	// Drawing clicks refresh road mesh — must not Recook junctions until A finish.
	TGuardValue<bool> SuppressJunctionRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
	UMoKuEditSplinesComponent* SplineComponent = nullptr;
	FVector Tangent = InTangent;
	// Commit points stick to landscape — never keep elevated road/junction mesh Z (walls).
	FVector SnapHitPos = InHitPos;
	ProjectPointOntoLandscapeSurface(
		TargetWorld ? TargetWorld : GetWorld(),
		SnapHitPos,
		Cast<AMoKuEditSplineActor>(ActiveActor));
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
		// Hide editor spline visualizer ribbon — guidance is PDI green dual-rail only.
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
				SplineActor->SetSplineComponent(SplineComponent);
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
	if (TryAdjustEndConnectLaneCount(+1, CurrentPos))
	{
		return;
	}
	if (!CornerDataTable)return;
	TArray<FName> RowNames = CornerDataTable->GetRowNames();
	int32 TotalRows = RowNames.Num();
	ScrollIndex+=1;
	int SelectIndex = abs(ScrollIndex) % TotalRows;
	GetSelectedAsset(SelectIndex);
	PreviewActor->GetStaticMeshComponent()->SetStaticMesh(CrossRoad);
}
void UMetaLandRoadInteractiveTool::GetSelectedAsset(int Index)
{
	if (!CornerDataTable)return;
	TArray<FName> RowNames = CornerDataTable->GetRowNames();
	const UScriptStruct* DataTableStruct = CornerDataTable->GetRowStruct();
	uint8* Data = CornerDataTable->FindRowUnchecked(RowNames[Index]);
	if (FProperty* StaticMeshData = CornerDataTable->FindTableProperty(FName("RecommandAsset")))
	{
		uint8* AnimSlotData = StaticMeshData->ContainerPtrToValuePtr<uint8>(Data, 0);
		FObjectProperty* ObjectProp = CastField<FObjectProperty>(StaticMeshData);
		UObject* ObjectValue = ObjectProp->GetPropertyValue(AnimSlotData);
		if (ObjectValue)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(ObjectValue);
			if (Mesh)
			{
				CrossRoad = Mesh;
			}


		}
	}

}
void UMetaLandRoadInteractiveTool::OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos)
{
	if (TryAdjustEndConnectLaneCount(-1, CurrentPos))
	{
		return;
	}
	if (!CornerDataTable)return;
	TArray<FName> RowNames = CornerDataTable->GetRowNames();
	int32 TotalRows = RowNames.Num();
	ScrollIndex -= 1;
	int SelectIndex = abs(ScrollIndex) % TotalRows;
	GetSelectedAsset(SelectIndex);
	PreviewActor->GetStaticMeshComponent()->SetStaticMesh(CrossRoad);

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
				bIsDraw = true;
				IntersectData.TValue = SplineComp->GetDistanceAlongSplineAtLocation(DrawOutSplinePos, ESplineCoordinateSpace::World);
				//UE_LOG(LogTemp,Warning,TEXT("t==%f"),IntersectData.tvalue)
				//DrawOutSplinePos.Z += 10;
				//FRotator Rotator = InSertTangent.GetSafeNormal().Rotation();
				//FTransform IntersectionTransform;
				//UStaticMeshComponent* Comp = nullptr;
				//if (!PreviewActor)
				//{
				//	PreviewActor = SplineActor->GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,0),FRotator(0,0,0));
				//	Comp = PreviewActor->GetStaticMeshComponent();
				//	Comp->SetStaticMesh(CrossRoad);
				//	Comp->SetWorldLocation(DrawOutSplinePos);
				//	Comp->SetWorldRotation(Rotator);
				//}
				//else
				//{
				//	Comp = PreviewActor->GetStaticMeshComponent();
				//	Comp->SetWorldLocation(DrawOutSplinePos);
				//	Comp->SetWorldRotation(Rotator);
				//	if (bIsInvert)
				//	{
				//		Comp->SetWorldScale3D(FVector(1.0, -1.0, 1.0));
				//	}
				//	else
				//	{
				//		Comp->SetWorldScale3D(FVector(1.0, 1.0, 1.0));
				//	}
				//}
			}
			else
			{
				bIsDraw = false;
				IntersectData.TValue = 0;
				IntersectData.HitTransform.SetLocation(InWorldPos);
				//if (PreviewActor->IsValidLowLevel()|| PreviewActor!=nullptr)
				//{
				//	PreviewActor->Destroy();
				//	PreviewActor = nullptr;
				//	bIsInvert = false;
				//}
			}
			GEditor->RedrawLevelEditingViewports(true);
		}
		else
		{
			bIsDraw = false;
			IntersectData.TValue = 0;
			IntersectData.HitTransform.SetLocation(InWorldPos);
		}
	}
}
void UMetaLandRoadInteractiveTool::UpdateEditState(bool InEditing)
{
	bIsEditing = InEditing;
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
	UE_LOG(LogTemp,Warning,TEXT("ParmChanged"))
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
	bHasEndSnapGhost = false;
	bHasLaneDrawEndSnapAnchor = false;
	bEndSnapBundleUserLocked = false;
	bHasEndSnapMixHover = false;
	EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();
	LaneDrawEndSnapAnchor = FMetaLandRoadDrawAnchor();
}

void UMetaLandRoadInteractiveTool::ClearSelectionForDrawSession(bool bNoteSelectionChange)
{
	FMetaLandRoadModule::Get().AbortSplineVisualizerTracking();
	if (GEditor)
	{
		GEditor->SelectNone(true, bNoteSelectionChange);
	}
}

void UMetaLandRoadInteractiveTool::EndLaneExtendVisualSession(bool bClearActiveActor)
{
	ClearDrawGuidanceRails();
	UpdateEditState(false);
	bIsDraw = false;
	bInsertJunction = false;
	bHasGuidancePlaneZ = false;
	bLaneAnchorDrawSession = false;
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
	return GetEditState() && bCtrlModifierOn && bIsDraw;
}

bool UMetaLandRoadInteractiveTool::ShouldSuppressLanePickEditHover() const
{
	return GetEditState() && bCtrlModifierOn && bIsDraw;
}

bool UMetaLandRoadInteractiveTool::ShouldSuppressEditTargetMouthBar() const
{
	return bLaneAnchorDrawSession || (GetEditState() && bIsDraw);
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
			TEXT("[MetaLandRoad] Two mouths ready — press A to generate the connection"));
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
	bIsDraw = false;
	bInsertJunction = false;
	bHasGuidancePlaneZ = false;
	bLaneAnchorDrawSession = false;
	bRoadSpawnedFromLaneAnchorDraw = false;
	bLaneAnchorSkipHubJunctionCook = false;
	bHasLaneDrawStartAnchor = false;
	LaneDrawStartAnchor = FMetaLandRoadDrawAnchor();
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	bHasLaneSnapArriveTangent = false;
	LaneSnapArriveTangent = FVector::ForwardVector;
	HitActor = nullptr;
	ActiveActor = nullptr;
	GuidanceLastCursor = FVector::ZeroVector;
	LastGuidanceHoverSamplePos = FVector::ZeroVector;
	IntersectData = IntersectBaseData();

	if (PreviewComponent && PreviewComponent->IsRegistered())
	{
		PreviewComponent->DestroyComponent();
	}
	PreviewComponent = nullptr;

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void UMetaLandRoadInteractiveTool::UpdateEndSnapMixHoverPreview(const FVector& CursorWorld)
{
	bHasEndSnapMixHover = false;
	EndSnapMixHoverAnchor = FMetaLandRoadDrawAnchor();

	UMetaLandRoadEditorMode* HoverRoadMode = ResolveMetaLandRoadEditorMode(this);
	const bool bHasSourceMouth =
		bLaneAnchorDrawSession
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
	if (!ActiveRoad || !TargetWorld)
	{
		return;
	}

	auto ApplyHoverGhost = [this](const FMetaLandRoadDrawAnchor& Anchor)
	{
		UMetaLandRoadSplineComponent* HoverSpline = Anchor.SourceSpline.Get();
		FVector TipLeft, TipRight, TipCenter, Outward;
		if (!HoverSpline
			|| !MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
				HoverSpline, Anchor, TipLeft, TipRight, TipCenter, Outward))
		{
			return;
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
		bIsDraw = true;
		if (AActor* Owner = HoverSpline->GetOwner())
		{
			HitActor = Owner;
		}
		LaneSnapArriveTangent = (-Outward).GetSafeNormal2D();
		bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
	};

	// Screen pick first — same path as Alt/Shift click (works with dual side sections).
	if (FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient)
	{
		if (FViewport* Viewport = ViewportClient->Viewport)
		{
			FIntPoint MousePos;
			Viewport->GetMousePos(MousePos);
			FMetaLandRoadDrawAnchor PickedAnchor;
			UMetaLandRoadSplineComponent* PickedSpline = nullptr;
			if (MetaLandRoadLaneEditPick::PickLaneAnchorAtMouse(
				TargetWorld,
				ViewportClient,
				Viewport,
				MousePos,
				/*bShiftDown*/ true,
				/*bSelectAllLanesOnSide*/ false,
				nullptr,
				PickedAnchor,
				PickedSpline)
				&& PickedAnchor.IsValid()
				&& PickedSpline
				&& Cast<AMoKuEditSplineActor>(PickedSpline->GetOwner()) != ActiveRoad)
			{
				EndSnapMixHoverAnchor = PickedAnchor;
				bHasEndSnapMixHover = true;
				ApplyHoverGhost(EndSnapMixHoverAnchor);
				return;
			}
		}
	}

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
		bLaneAnchorDrawSession
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
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					991123,
					1.5f,
					FColor::Orange,
					bAddOnly
						? TEXT("[MetaLandRoad] Alt+Shift+LMB: click TARGET tip lane to add mix")
						: TEXT("[MetaLandRoad] Shift+LMB: click TARGET tip lane to pick/toggle mix"));
			}
			return true;
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
	bIsDraw = true;
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
				TEXT("[MetaLandRoad] End mix [%s] count=%d — Shift+/- | A connects (Ctrl does not confirm)"),
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
	bLaneAnchorDrawSession = true;
	bRoadSpawnedFromLaneAnchorDraw = true;
	bLaneAnchorSkipHubJunctionCook = true;
	LaneDrawFinishMode = EMetaLandRoadDrawFinishMode::Direct;
	bIsDraw = false;
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
	// MetaRoad: Road Predecessor ↔ source Lane Successor (or Pred if pick Start mouth).
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

			// Match spawned tip width to the selected source mouth (avoids width step / 接缝).
			const double TargetHalf = FMath::Max(MouthWidthCm * 0.5, 1.0);
			const double CurrentHalf = FMath::Max(SpawnedSpline->GetRoadHalfWidthAtS(0.0), 1.0);
			if (FMath::Abs(TargetHalf - CurrentHalf) > 4.0 && SpawnedSpline->GetLaneSectionsNum() > 0)
			{
				const float Scale = static_cast<float>(TargetHalf / CurrentHalf);
				FMetaLandRoadLaneSection& TipSection = SpawnedSpline->GetLaneSection(0);
				auto ScaleLane = [Scale](FMetaLandRoadLane& Lane)
				{
					for (FRichCurveKey& Key : Lane.Width.Keys)
					{
						Key.Value *= Scale;
					}
					MetaLandRoad::EnsureLaneWidthCurveCubic(Lane.Width);
				};
				for (FMetaLandRoadLane& Lane : TipSection.Left)
				{
					ScaleLane(Lane);
				}
				for (FMetaLandRoadLane& Lane : TipSection.Right)
				{
					ScaleLane(Lane);
				}
				SpawnedSpline->GetRoadLayoutMutable().UpdateLayout(SpawnedSpline);
			}
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
	SpawnedRoad->RefreshDynamicRoadMesh();
	MetaLandRoadLaneVisualization::RefreshLaneVisualization(SpawnedSpline);

	MetaLandRoadLogDrawAnchor(
		bConnected ? TEXT("FromConnection OK") : TEXT("FromConnection Fit only (Connect failed)"),
		SourceAnchor);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			4.0f,
			bConnected ? FColor::Green : FColor::Yellow,
			bConnected
				? TEXT("[MetaLandRoad] FromConnection: FitSpline + ConnectTo")
				: TEXT("[MetaLandRoad] FromConnection: FitSpline only (ConnectTo skipped/failed)"));
	}
}


bool UMetaLandRoadInteractiveTool::TryCommitIdleMouthToMouthConnection()
{
	if (bLaneAnchorDrawSession || GetEditState())
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
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[MetaLandRoad] Idle A pin SrcC=%s TgtC=%s chord=%.1f srcLanes=%d tgtLanes=%d"),
		*SrcC.ToCompactString(),
		*TgtC.ToCompactString(),
		FVector::Dist2D(SrcC, TgtC),
		CountDrawAnchorBundleLanes(SourceTarget.PendingAnchor),
		CountDrawAnchorBundleLanes(*TargetAnchor));

	SpawnedRoad->Tags.AddUnique(MetaLandRoadLaneBranchTag);
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SpawnedSpline))
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		RoadSpline->ConnectRoadToDrawAnchor(LaneDrawStartAnchor, /*bAttachAtSplineStart*/ true);
		RoadSpline->BlendEndWidthToTargetMouth(*TargetAnchor, EMetaLandRoadEndWidthBlendSpan::EntireRoad);
		RoadSpline->ConnectRoadToDrawAnchor(*TargetAnchor, /*bAttachAtSplineStart*/ false);
		// Connect/blend must not keep a Fit-nudged stub — re-pin both mouths last.
		RoadSpline->SetLocationAtSplinePoint(0, SrcC, ESplineCoordinateSpace::World, false);
		if (RoadSpline->GetNumberOfSplinePoints() >= 2)
		{
			RoadSpline->SetLocationAtSplinePoint(
				RoadSpline->GetNumberOfSplinePoints() - 1, TgtC, ESplineCoordinateSpace::World, false);
		}
		ApplyAsymmetricTangentBezierConnection(SpawnedSpline, 0, TgtC, Leave, Arrive);
		RoadSpline->UpdateSpline();
		SpawnedRoad->SetSplineComponent(SpawnedSpline);
		SpawnedRoad->RefreshDynamicRoadMesh();
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(RoadSpline);
		if (UMetaLandRoadSplineComponent* TargetSpline = TargetAnchor->SourceSpline.Get())
		{
			if (AMoKuEditSplineActor* TargetRoad = Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()))
			{
				TargetRoad->RefreshDynamicRoadMesh();
			}
		}
		if (AMoKuEditSplineActor* SourceRoad = SourceTarget.RoadActor.Get())
		{
			SourceRoad->RefreshDynamicRoadMesh();
		}
	}

	if (UMetaLandRoadEditorMode* ClearMode = ResolveMetaLandRoadEditorMode(this))
	{
		ClearMode->ClearEditTarget();
	}
	ClearDrawGuidanceRails();
	UpdateEditState(false);
	bIsDraw = false;
	bLaneAnchorDrawSession = false;
	bHasLaneDrawStartAnchor = false;
	bHasLaneDrawEndSnapAnchor = false;
	bEndSnapBundleUserLocked = false;
	bHasEndSnapGhost = false;
	ActiveActor = nullptr;
	FMetaLandRoadModule::Get().RestoreAllRoadActorsViewportSelection();
	if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadModule::Get().GetRoadComponentVisualizer())
	{
		RoadVis->ClearNativeKeySelection();
		RoadVis->EndEditing();
	}
	if (GEditor)
	{
		GEditor->SelectNone(false, true);
	}

	GEditor->EndTransaction();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			3.0f,
			FColor::Green,
			TEXT("[MetaLandRoad] Mouth-to-mouth Bezier committed (A)"));
	}
	return true;
}

void UMetaLandRoadInteractiveTool::FinalizeLaneDrawEndConnection(
	AMoKuEditSplineActor* ActiveRoad,
	const FMetaLandRoadDrawAnchor& TargetAnchor)
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

	auto RefreshRoadMeshNoCook = [](AMoKuEditSplineActor* RoadActor)
	{
		if (!RoadActor)
		{
			return;
		}
		TGuardValue<bool> Suppress(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		RoadActor->RefreshDynamicRoadMesh();
		if (UMetaLandRoadSplineComponent* Spline = RoadActor->GetSplineComponent())
		{
			MetaLandRoadLaneVisualization::RefreshLaneVisualization(Spline);
		}
	};

	// Profile policy:
	// - LaneBranch (N-lane extend): NEVER grow to full junction / target road width.
	//   Hub cover fills the intersection; one-arm asphalt flare breaks 比例审美 (图一/二).
	// - Other roads: keep START count when source wider; else match TARGET mouth.
	const int32 SourceLaneCount = (bHasLaneDrawStartAnchor && SourceAnchorCopy.IsValid())
		? CountDrawAnchorBundleLanes(SourceAnchorCopy)
		: 0;
	const int32 TargetBundleCount = CountDrawAnchorBundleLanes(WorkingTarget);
	UMetaLandRoadSplineComponent* TargetSpline = WorkingTarget.SourceSpline.Get();
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

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991125,
			4.0f,
			FColor::Cyan,
			FString::Printf(
				TEXT("[MetaLandRoad] EndConnection: sourceLanes=%d targetBundle=%d expandActive=%d laneBranch=%d"),
				SourceLaneCount,
				CountDrawAnchorBundleLanes(WorkingTarget),
				bExpandActiveToTargetMix ? 1 : 0,
				bLaneBranch ? 1 : 0));
	}

	if (!ActiveSpline || ActiveSpline->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	FVector TargetLeft, TargetRight, TargetCenter, TargetOutward;
	const bool bHaveTargetFrame = TargetSpline
		&& MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame(
			TargetSpline, WorkingTarget, TargetLeft, TargetRight, TargetCenter, TargetOutward);

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

	if (bHaveTargetFrame && !LeaveDir.IsNearlyZero() && !ArriveDir.IsNearlyZero())
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

	// LaneBranch: taper down to a narrower target (2→1 mix) is allowed.
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
			if (!bDoEndWidthBlend)
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[EndConnection] LaneBranch skip width-grow (srcSum=%.0f tgtSum=%.0f lanes=%d/%d)"),
					SrcSum,
					TgtSum,
					SrcLanes.Num(),
					TgtLanes.Num());
			}
		}
	}
	if (bDoEndWidthBlend)
	{
		ActiveSpline->BlendEndWidthToTargetMouth(WorkingTarget, BlendSpan);
	}
	const bool bConnected = ActiveSpline->ConnectRoadToDrawAnchor(WorkingTarget, /*bAttachAtSplineStart*/ false);

	ActiveSpline->UpdateSpline();
	ActiveRoad->SetSplineComponent(ActiveSpline);
	RefreshRoadMeshNoCook(ActiveRoad);
	if (TargetSpline)
	{
		RefreshRoadMeshNoCook(Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()));
	}
	if (bHasLaneDrawStartAnchor && SourceAnchorCopy.IsValid() && SourceSpline)
	{
		RefreshRoadMeshNoCook(Cast<AMoKuEditSplineActor>(SourceSpline->GetOwner()));
	}

	MetaLandRoadLogDrawAnchor(
		bConnected ? TEXT("EndConnection OK") : TEXT("EndConnection Fit+Blend only"),
		WorkingTarget);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			4.0f,
			bConnected ? FColor::Green : FColor::Yellow,
			bConnected
				? TEXT("[MetaLandRoad] EndConnection: FitSpline + Blend + ConnectTo")
				: TEXT("[MetaLandRoad] EndConnection: FitSpline + Blend (Connect skipped/failed)"));
	}

	UpdateEditState(false);
	StateOfEditor = EMoKuEditState::Defult;
	bIsDraw = false;
	bInsertJunction = false;
	bLaneAnchorDrawSession = false;
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

	FMetaLandRoadModule::Get().RestoreAllRoadActorsViewportSelection();
	if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadModule::Get().GetRoadComponentVisualizer())
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
	const double TipS = ResolveDrawTipSForGuidance(Spline, bForward);
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	if (ResolveDrawTipLaneEdges(RoadActor, Spline, TipS, bForward, EditTarget, TipLeft, TipRight, TipCenter))
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
	// Auto: 贴边直行 length ≈ 0.8× tip width, then Hermite bend (red-line shape).
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
		// Wider tip → Hermite sooner (smaller angle). Narrow → tolerate more straight.
		// θ = θ_ref * sqrt(W0 / W), clamped.
		const float W0 = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);
		const float W = FMath::Max(ResolveActiveTipRoadWidthCm(), 50.0f);
		// Trigger Hermite earlier — delayed turns become tip→end chords (折角).
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
	return FMath::Clamp(W * 1.25f, 250.0f, 1200.0f);
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

	// Locked mix is authoritative (2-lane begin → 1-lane end is allowed).
	// Hover seeds 1 lane; Shift+/- grows/shrinks the target bundle.
	if (LockedEndCount > 0)
	{
		return LockedEndCount;
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
	if (!bLaneAnchorDrawSession || !GetEditState() || !bHasEndSnapGhost)
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
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991122,
			1.5f,
			FColor::Yellow,
			FString::Printf(TEXT("[MetaLandRoad] EndConnect target lanes: %d"), Next));
	}
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
	// Lateral ≈ 0.35× tip width → big side moves trigger Hermite with road scale.
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
	if (!GetEditState() && !bLaneAnchorDrawSession)
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
		bIsDraw = true;
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
		bIsDraw = false;
		HitActor = nullptr;
		return;
	}

	HitActor = InHitActor;
	const bool bAllowConnectionSnap = GetEditState() && bLaneAnchorDrawSession;
	AMoKuEditSplineActor* SnapRoad = Cast<AMoKuEditSplineActor>(InHitActor);
	AMoKuEditSplineActor* ActiveRoad = Cast<AMoKuEditSplineActor>(ActiveActor);
	if (bAllowConnectionSnap && SnapRoad && SnapRoad != ActiveRoad)
	{
		UMetaLandRoadSplineComponent* SnapSpline =
			Cast<UMetaLandRoadSplineComponent>(SnapRoad->GetSplineComponent());
		if (bKeepUserBundle
			&& SnapSpline
			&& LockedAnchor.SourceSpline.Get() == SnapSpline)
		{
			if (ApplyLockedEndSnapGhost(LockedAnchor))
			{
				FVector TipCenter = TraceHitPos;
				if (ActiveRoad)
				{
					if (UMoKuEditSplinesComponent* ActiveSpline = ActiveRoad->GetSplineComponent())
					{
						const int32 TipIndex = ActiveSpline->GetNumberOfSplinePoints() - 1;
						if (TipIndex >= 0)
						{
							TipCenter = ActiveSpline->GetLocationAtSplinePoint(
								TipIndex, ESplineCoordinateSpace::World);
						}
					}
				}
				LaneSnapArriveTangent = (FVector(DrawOutSplinePos) - TipCenter).GetSafeNormal2D();
				bHasLaneSnapArriveTangent = !LaneSnapArriveTangent.IsNearlyZero();
				return;
			}
		}

		FVector TipCenter = TraceHitPos;
		if (ActiveRoad)
		{
			if (UMoKuEditSplinesComponent* ActiveSpline = ActiveRoad->GetSplineComponent())
			{
				const bool bForward = (StateOfEditor == EMoKuEditState::Forward);
				const int32 TipIndex = bForward ? 0 : (ActiveSpline->GetNumberOfSplinePoints() - 1);
				if (ActiveSpline->GetNumberOfSplinePoints() > 0 && TipIndex >= 0)
				{
					TipCenter = ActiveSpline->GetLocationAtSplinePoint(TipIndex, ESplineCoordinateSpace::World);
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
			// Alt/Shift locked bundle: hover snap is position-only — never wipe BundledLaneIndices.
			if (bKeepUserBundle && ApplyLockedEndSnapGhost(LockedAnchor))
			{
				bIsDraw = true;
				DrawOutSplinePos = SnapPos;
				IntersectData.TValue = SampleDist;
				LaneSnapArriveTangent = ArriveTangent;
				bHasLaneSnapArriveTangent = true;
				return;
			}

			bEndSnapBundleUserLocked = false;
			bIsDraw = true;
			DrawOutSplinePos = SnapPos;
			IntersectData.TValue = SampleDist;
			LaneSnapArriveTangent = ArriveTangent;
			bHasLaneSnapArriveTangent = true;
			if (SnapAnchor.IsValid())
			{
				LaneDrawEndSnapAnchor = SnapAnchor;
				bHasLaneDrawEndSnapAnchor = true;
				// Hover tip with multi-lane start → Blend finish (MetaRoad "Shift to blend").
			}
			EndSnapGhostLeft = GhostL;
			EndSnapGhostRight = GhostR;
			ApplyGuidanceSurfaceZ(EndSnapGhostLeft);
			ApplyGuidanceSurfaceZ(EndSnapGhostRight);
			bHasEndSnapGhost = true;
		}
		else if (bKeepUserBundle && ApplyLockedEndSnapGhost(LockedAnchor))
		{
			return;
		}
		else
		{
			bIsDraw = false;
		}
	}
	else if (bKeepUserBundle && ApplyLockedEndSnapGhost(LockedAnchor))
	{
		return;
	}
	else
	{
		bIsDraw = false;
	}
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
		bLaneAnchorDrawSession
		|| GetEditState()
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
			// Hit road top — drop XY onto landscape so first point is not elevated on mesh.
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
		// Initial dual-rail (NumPts < 3): free tip → mouse. Do NOT project onto tip leave —
		// that "Ctrl locks" the rubber-band onto one axis and makes L/R cross when mouse goes aside.
		OutPlan.EndCenter = CursorOnPlane;
		OutPlan.EndTangent = (OutPlan.EndCenter - TipCenter).GetSafeNormal2D();
		if (OutPlan.EndTangent.IsNearlyZero())
		{
			OutPlan.EndTangent = OutPlan.StartTangent;
		}
		return;
	}

	if (bIsDraw && HitActor && HitActor->IsA<AMoKuEditSplineActor>())
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
				// Stale hover snap — ignore rather than twist onto centerline.
			}
			else if (FVector::Dist2D(TipCenter, SnapPos) < static_cast<double>(SnapDistCm) * 3.0)
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
		if (SplineActor)
		{
			SplineActor->RefreshDynamicRoadMesh();
			if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SplineComponent))
			{
				MetaLandRoadLaneVisualization::RefreshLaneVisualization(RoadSpline);
			}
		}
		GuidanceLastCursor = Plan.EndCenter;
		GuidanceMoveDir = (Plan.EndCenter - Plan.StartCenter).GetSafeNormal2D();
		return;
	}
	// Lane-extend key inserts refresh mesh — junction cook waits for A.
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
	// Joint where the new segment attaches (old tip). Soften this — it is the big 折角.
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

	// Re-apply committed freeze so Fit/Hermite cannot reshape prior segments (含第一段→第二段).
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

	if (SplineActor)
	{
		SplineActor->RefreshDynamicRoadMesh();
		if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SplineComponent))
		{
			MetaLandRoadLaneVisualization::RefreshLaneVisualization(RoadSpline);
		}
	}

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

	// Only true mouth leave. NumPts==2 is already segment-1 complete — must NOT re-apply mouth outward.
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
			Plan.EndTangent = FVector::CrossProduct(Across, FVector::UpVector).GetSafeNormal2D();
			if (Plan.EndTangent.IsNearlyZero())
			{
				Plan.EndTangent = (Plan.EndCenter - TipCenter).GetSafeNormal2D();
			}
		}
		Plan.bUseHermite = true;
	}

	// One corridor (L/R rails). Per-lane multi-rails cross on turns and hide width lerp.
	// EndSnap ghost / locked mix → taper TipMouth → EndMouth; else constant start mouth width.
	if (bHasEndSnapGhost)
	{
		TArray<FVector> SingleLeftRail;
		TArray<FVector> SingleRightRail;
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
			&EndSnapGhostRight);
		GuidanceLeftPolylines.Add(MoveTemp(SingleLeftRail));
		GuidanceRightPolylines.Add(MoveTemp(SingleRightRail));
		return;
	}
	(void)PerLaneAnchor;
	(void)TipS;

	TArray<FVector> SingleLeftRail;
	TArray<FVector> SingleRightRail;
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
		EndRightPtr);
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

	// Allow Shift multi-select preview while idle; only skip during Ctrl place.
	if (GetEditState() && bIsDraw)
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
			&& (bLaneAnchorDrawSession || bRoadSpawnedFromLaneAnchorDraw || GetEditState()))
		{
			AMoKuEditBaseActor* const SavedActive = ActiveActor;
			const bool bSavedSession = bLaneAnchorDrawSession;
			ActiveActor = BranchActor;
			bLaneAnchorDrawSession = true;
			UpdateDrawGuidanceRails();
			ActiveActor = SavedActive;
			bLaneAnchorDrawSession = bSavedSession;
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

	// Only after at least one control point (Ctrl click). Lane extend may preview before first Ctrl point.
	if (!ActiveActor || (!GetEditState() && !bLaneAnchorDrawSession))
	{
		return;
	}

	AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMoKuEditSplinesComponent* Spline = RoadActor ? RoadActor->GetSplineComponent() : nullptr;
	if (!Spline || Spline->GetNumberOfSplinePoints() < 1)
	{
		return;
	}

	const bool bConnectionGuidance = bLaneAnchorDrawSession
		&& GetEditState()
		&& bIsDraw
		&& HitActor
		&& HitActor->IsA<AMoKuEditSplineActor>()
		&& HitActor != ActiveActor;
	const bool bAimAtEndSnapMouth = bHasEndSnapGhost
		&& (bEndSnapBundleUserLocked || bHasEndSnapMixHover || bHasLaneDrawEndSnapAnchor);
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

	// Snap to lane edges at the active draw control point (not junction-trim mesh span).
	const double TipS = ResolveDrawTipSForGuidance(Spline, bForward);
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	const FMetaLandRoadEditTarget* EditTarget = nullptr;
	if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
	{
		EditTarget = &RoadMode->GetEditTarget();
	}
	if (!ResolveDrawTipLaneEdges(RoadActor, Spline, TipS, bForward, EditTarget, TipLeft, TipRight, TipCenter))
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

	// Prefer tip Leave handle — DirectionAtDistance can blend Arrive/Leave and fold guidance.
	const int32 TipIndex = bForward ? 0 : (NumPts - 1);
	FVector ExtendTangent = bForward
		? (-Spline->GetArriveTangentAtSplinePoint(TipIndex, ESplineCoordinateSpace::World).GetSafeNormal2D())
		: Spline->GetLeaveTangentAtSplinePoint(TipIndex, ESplineCoordinateSpace::World).GetSafeNormal2D();
	if (ExtendTangent.IsNearlyZero())
	{
		ExtendTangent = Spline->GetDirectionAtDistanceAlongSpline(TipS, ESplineCoordinateSpace::World).GetSafeNormal2D();
		if (bForward)
		{
			ExtendTangent = -ExtendTangent;
		}
	}
	// Tip leave = mouth outward (tangent only). Do not rewrite TipLeft/Right here — that shifts start.
	if (bLaneAnchorDrawSession && EditTarget && EditTarget->bHasPendingAnchor
		&& EditTarget->RoadActor.Get() == RoadActor)
	{
		FMetaLandRoadDrawAnchor TipAnchor = EditTarget->PendingAnchor;
		TipAnchor.AnchorS = TipS;
		TipAnchor.MouthSide = bForward
			? EMetaLandRoadMouthSide::Start
			: EMetaLandRoadMouthSide::End;
		FVector FrameTangent;
		if (MetaLandRoadLaneEditPick::ResolveMouthOutwardTangent(Spline, TipAnchor, FrameTangent)
			&& !FrameTangent.IsNearlyZero())
		{
			ExtendTangent = FrameTangent.GetSafeNormal2D();
		}
	}

	// Always path from mouth mid — must match spawn MouthLoc / mesh centerline.
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
	if (bHaveLiveStart)
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
			if (!BundleTangent.IsNearlyZero())
			{
				TipTangent = BundleTangent.GetSafeNormal2D();
			}
		}
	}

	// Plane = landscape under tip (not elevated mesh Z) so green rails hug ground like MetaRoad.
	const float PlaneZ = ResolveGuidancePlaneZOnSurface(TargetWorld, TipCenter, RoadActor);
	TipLeft.Z = PlaneZ;
	TipRight.Z = PlaneZ;
	TipCenter.Z = PlaneZ;
	RubberEnd.Z = PlaneZ;
	GuidancePlaneZ = PlaneZ;
	bHasGuidancePlaneZ = true;

	// Hermite rubber-band when spline already has a segment (NumPts >= 2).
	// NumPts < 2 (刚落第一点): tip→cursor stays parallel dual straight.
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
			// Multi-pass: some viewports ignore single-pass PDI thickness.
			PDI->DrawLine(Rail[i], Rail[i + 1], Color, SDPG_Foreground, GuidanceLineWidth);
			PDI->DrawLine(Rail[i], Rail[i + 1], Color, SDPG_Foreground, GuidanceLineWidth * 0.7f);
			PDI->DrawLine(Rail[i], Rail[i + 1], Color, SDPG_World, GuidanceLineWidth * 0.85f);
		}
	};
	for (const TArray<FVector>& Rail : GuidanceLeftPolylines)
	{
		DrawRail(Rail, GuidanceLeftRailColor);
	}
	for (const TArray<FVector>& Rail : GuidanceRightPolylines)
	{
		DrawRail(Rail, GuidanceRightRailColor);
	}

	// Default off — green dual-rails only; enable via Tool Details → Debug Guidance Points.
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
	// Only rubber-band dual-rail while placing the next point (not CornerInfo / debug overlays).
	DrawDrawGuidanceRails(PDI);
	if (SceneManagement)
	{
		for (auto& Road : SceneManagement->EditRoadsList)
		{
			if (AMoKuEditSplineActor* DebugActiveActor = Cast<AMoKuEditSplineActor>(Road))
			{
				const TArray<FMoKuSplineInterpPoint>D = DebugActiveActor->LeftCurve->GetProceduralPoints();
				//	if (OutIntersectionPoints.Num() > 0)
				{
					for (auto& d : D)
					{

						FVector3d Diff = d.EndPos - d.StartPos;
						FVector3d Center = (d.StartPos + d.EndPos) / 2;
						//FVector3d Direction = Diff.GetSafeNormal();
						double Length = FVector::Distance(d.EndPos,d.StartPos) / 2;
						FVector HalfExtents(Length, 100.0f, 100);;
						FVector3d Direction = FMath::Lerp(d.StartTangent, d.EndTangent, 0.5);
						Direction = Direction.GetSafeNormal();
						FVector3d bTangent = FVector::CrossProduct(Direction, FVector::UpVector);

						//const  FRotator Rot = d.WorldTransform.Rotator();
						//FQuat Quat = Rot.Quaternion();
						//bTangent = Quat.RotateVector(bTangent);

						FVector S1 = d.StartPos - bTangent * 1000;
						//FVector S2 = d.EndPos + bTangent * 100;
						//FVector S3 = d.StartPos - bTangent * 100;
						FVector S4 = d.StartPos + Direction * 1000;
						FVector S3 = d.StartPos + FVector::UpVector * 1000;

						TArray<FVector> Vertices;
						const FVector Axes[3] = {
							Direction,  // ��ת���X��
							bTangent,  // ��ת���Y��
							FVector::UpVector  // ��ת���Z��
						};

						for (int32 i = 0; i < 2; i++)
						{
							for (int32 j = 0; j < 2; j++)
							{
								for (int32 k = 0; k < 2; k++)
								{
									FVector Vertex = Center;
									Vertex += (i == 0 ? -HalfExtents.X : HalfExtents.X) * Axes[0];
									Vertex += (j == 0 ? -HalfExtents.Y : HalfExtents.Y) * Axes[1];
									Vertex += (k == 0 ? -HalfExtents.Z : HalfExtents.Z) * Axes[2];
									Vertices.Add(Vertex);
								}
							}
						}
						FBox Bound = FBox(Vertices);

						//UE_LOG(LogTemp, Warning, TEXT("bTangent1111==%s"), *bTangent.ToString())
						//PDI->DrawLine(d.StartPos, S3, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.StartPos, S4, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.StartPos, S1, FLinearColor(0.0f, 0.0f, 1.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.EndPos, S2, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.EndPos, S1, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						FMatrix M(Direction, bTangent,FVector::UpVector,FVector(0,0,0));
						//DrawWireBox(PDI, M, Bound, FColor(1, 0, 0), SDPG_World);
						//PDI->DrawPoint(
						//	d.StartPos,
						//	FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
						//	15.0f,
						//	SDPG_World);
					}
				}
			}
		}


	}


		// Cursor hit preview points removed (green/yellow follow-mouse markers in World Builder).
		//for (const auto& Bound : OutDebugBox)
		//{
		//	//DrawWireBox(PDI, Bound, FColor(1, 0, 0), SDPG_World);
		//}

		//for (auto Vertex : DebugCorner)
		//{
		//		PDI->DrawPoint(
		//			Vertex,
		//			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
		//			5.0f,
		//			SDPG_Foreground);
		//}

			//}
			//AMoKuEditSplineActor* RenderActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			//if (RenderActor)
			//{
			//	FSideRoadCurvePaths SideCurvePaths = RenderActor->GetSideCurvePaths();
			//	for (auto Vertex : RenderActor->EndConnection.ConnectEdge)
			//	{
			//		PDI->DrawPoint(
			//			Vertex,
			//			FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
			//			5.0f,
			//			SDPG_Foreground);

			//	}
			//	for (auto Vertex : RenderActor->StartConnection.ConnectEdge)
			//	{
			//		PDI->DrawPoint(
			//			Vertex,
			//			FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
			//			5.0f,
			//			SDPG_Foreground);

			//	}

			//}

	//}
}
void UMetaLandRoadInteractiveTool::InvertPreviewMesh()
{
	if (PreviewActor&& bIsInsertCross)
	{
		if (bIsInvert)
		{
			bIsInvert = false;
		}
		else
		{
			bIsInvert = true;
		}
	}

}
void UMetaLandRoadInteractiveTool::SwitchInsertCrossState()
{
	if (ActiveActor)
	{
		if (bIsInsertCross)
		{
			bIsInsertCross = false;
			bIsDraw = false;
			IntersectData.TValue = 0;
			//if (PreviewActor->IsValidLowLevel() || PreviewActor != nullptr)
			//{
			//	PreviewActor->Destroy();
			//	PreviewActor = nullptr;
			bIsInvert = false;
			//}
		}
		else
		{
			bIsInsertCross = true;
		}
	}

}
void UMetaLandRoadInteractiveTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{

}
bool UMetaLandRoadInteractiveTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// Alt+W/E/T: CompVis owns hover/pick. Skip world line-traces here — they fight
	// key editing and add heavy per-frame cost (feels like a frozen viewport).
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		const FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
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

	const FName TraceTagName = FName("WorldCollision");
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
	const bool bHitWorld = World->LineTraceSingleByObjectType(
		Result,
		DevicePos.WorldRay.Origin,
		DevicePos.WorldRay.PointAt(HALF_WORLD_MAX),
		QueryParams,
		QueryCollisionParams);

	UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this);
	AMoKuEditSplineActor* HoverActor = Cast<AMoKuEditSplineActor>(GetActiveActor());
	const bool bLaneGuidanceActive =
		bLaneAnchorDrawSession
		|| GetEditState()
		|| (RoadMode && RoadMode->GetEditTarget().bHasPendingAnchor);

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

	UpdateLaneDrawSnapFromWorldHit(bHitWorld, HitActor, ResultHitPos);
	UpdateEndSnapMixHoverPreview(ResultHitPos);

	UMoKuEditSplinesComponent* SelecComponet = HoverActor ? HoverActor->GetSplineComponent() : nullptr;
	const bool bCursorMoved = !ResultHitPos.Equals(LastGuidanceHoverSamplePos, 2.0);
	if (bCursorMoved)
	{
		LastGuidanceHoverSamplePos = ResultHitPos;
	}

	if (GetEditState() && PreviewComponent && SelecComponet && SelecComponet->GetNumberOfSplinePoints() >= 1 && bCursorMoved)
	{
		const bool bConnectionPreview = bLaneAnchorDrawSession
			&& bIsDraw
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

	// Green dual-rail: always refresh; throttle viewport redraw only.
	if (GetEditState() && SelecComponet && SelecComponet->GetNumberOfSplinePoints() >= 1)
	{
		UpdateDrawGuidanceRails();
	}
	else if (bLaneAnchorDrawSession && ActiveActor)
	{
		UpdateDrawGuidanceRails();
	}
	else if (RoadMode)
	{
		if (RoadMode->GetEditTarget().bHasPendingAnchor
			&& !GetEditState()
			&& !bLaneAnchorDrawSession)
		{
			UpdateAnchorPreviewGuidanceRails();
		}
		else if (!GetEditState() && !bLaneAnchorDrawSession)
		{
			ClearDrawGuidanceRails();
		}
	}
	else
	{
		ClearDrawGuidanceRails();
	}

	if (GEditor)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - LastGuidanceHoverRedrawSeconds) >= 0.033)
		{
			GEditor->RedrawLevelEditingViewports(false);
			LastGuidanceHoverRedrawSeconds = Now;
		}
	}
	return true;
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
		if (!MoKuSplineEditorUtils::DoSplineIntersect(
				ActiveSpline, OtherSpline, IntersectWorld, MaxHeightDelta))
		{
			continue;
		}

		// Local cook for current road only (not A / not full-world) — avoids the heavy A-finish path.
		CookJunctionForActiveRoad(/*bFinishEditSession=*/true, /*bForceHubJunctionCook=*/false);
		ClearSelectionForDrawSession();
		return true;
	}

	return false;
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
			|| ((bLaneAnchorDrawSession || bRoadSpawnedFromLaneAnchorDraw)
				&& LaneDrawFinishMode == EMetaLandRoadDrawFinishMode::Direct));

	if (!bLaneDirectConnectFinish)
	{
		if (UMetaLandRoadEditorMode* RoadMode = ResolveMetaLandRoadEditorMode(this))
		{
			const bool bFullWorldRebuild =
				OutputProperties && OutputProperties->bFullWorldJunctionRecookOnDrawFinish;
			const float MaxHeightDelta =
				OutputProperties ? OutputProperties->JunctionMaxHeightDeltaCm : 0.0f;
			RoadMode->ProcessInteractiveFinishedRoadDraw(FinishActor, bFullWorldRebuild, MaxHeightDelta);
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
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		FinishActor->RefreshDynamicRoadMesh();
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(Comp);
	}

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

	if (!bLaneAnchorDrawSession && !GetEditState())
	{
		return;
	}

	const bool bWasLaneAnchorDraw = bLaneAnchorDrawSession || bRoadSpawnedFromLaneAnchorDraw;
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

	UMoKuEditSplinesComponent* Comp = FinishActor ? FinishActor->GetSplineComponent() : nullptr;
	const bool bCanFinishRoad = FinishActor
		&& Comp
		&& Comp->GetNumberOfSplinePoints() >= 2;

	if (ActiveActor && (GetEditState() || bCanFinishRoad))
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("Finished The Curvel's Operation!")));
		const bool bCookOnA = OutputProperties && OutputProperties->bCookJunctionOnAFinish;
		if (FinishActor && bCanFinishRoad && bEndSnapFinish)
		{
			FinalizeLaneDrawEndConnection(FinishActor, EndSnapAnchorCopy);
			// Mouth connect only — hub cook is click-on-road (unless Cook On A is enabled).
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
			// Default: A = finish mesh + local cook if XY cross exists (no-op when no intersect).
			// Off: mesh only; cook via Ctrl+click onto another road/junction.
			if (bCookOnA)
			{
				CookJunctionForActiveRoad(/*bFinishEditSession=*/true, /*bForceHubJunctionCook=*/false);
			}
			else
			{
				RefreshRoadMeshSuppressJunction(FinishActor);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(
						-1, 3.0f, FColor::Cyan,
						TEXT("Draw finished (A). Click onto another road to cook junction."));
				}
			}
		}
		this->Modify();

		if (Comp && FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
		{
			if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
				FMetaLandRoadModule::Get().GetRoadComponentVisualizer())
			{
				RoadVis->ClearSectionHighlight();
			}
			MetaLandRoadLaneVisualization::RefreshLaneVisualization(Comp);
		}

		GEditor->EndTransaction();
	}

	auto RefreshRoadMeshSafe = [](AMoKuEditSplineActor* RoadActor)
	{
		if (!RoadActor)
		{
			return;
		}
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		RoadActor->RefreshDynamicRoadMesh();
		if (UMetaLandRoadSplineComponent* RoadSpline = RoadActor->GetSplineComponent())
		{
			MetaLandRoadLaneVisualization::RefreshLaneVisualization(RoadSpline);
		}
	};

	if (bWasLaneAnchorDraw && FinishActor)
	{
		RefreshRoadMeshSafe(FinishActor);
		if (StartAnchorCopy.IsValid())
		{
			if (UMetaLandRoadSplineComponent* SourceSpline = StartAnchorCopy.SourceSpline.Get())
			{
				RefreshRoadMeshSafe(Cast<AMoKuEditSplineActor>(SourceSpline->GetOwner()));
			}
		}
		if (bEndSnapFinish && EndSnapAnchorCopy.IsValid())
		{
			if (UMetaLandRoadSplineComponent* TargetSpline = EndSnapAnchorCopy.SourceSpline.Get())
			{
				RefreshRoadMeshSafe(Cast<AMoKuEditSplineActor>(TargetSpline->GetOwner()));
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

	FMetaLandRoadModule::Get().RestoreAllRoadActorsViewportSelection();
	if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadModule::Get().GetRoadComponentVisualizer())
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
	if (!bRoadSpawnedFromLaneAnchorDraw && !bLaneAnchorDrawSession)
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

	bLaneAnchorDrawSession = true;
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
				bIsDraw = true;
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

	const bool bLaneExtendActive = bLaneAnchorDrawSession || bRoadSpawnedFromLaneAnchorDraw;
	if (!bLaneExtendActive)
	{
		ClearDrawGuidanceRails();
		bIsDraw = false;
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
	if (PreviewActor->IsValidLowLevel() || PreviewActor != nullptr)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		bIsInvert = false;
	}
	// ��ʱ���� ������Ҫ�Ż�
	for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair : GizmoListInfo)
	{
		for (auto Item : KeyValuePair.Value)
		{
			GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
		}
	}

	GizmoListInfo.Empty();
	for (TActorIterator<AMoKuEditBaseActor>It(TargetWorld); It; ++It)
	{
		AMoKuEditBaseActor* SplineActor = *It;
		TArray<FSocketGizmoTransformInfo> TransformInfo;
		TArray<USPlineMarkGizmo*> GizmoInfos;
		TArray<FSocketGizmoTransformInfo>GizmoTransformInfos = SplineActor->GetGizmoInfo();
		for (const auto& Info : GizmoTransformInfos)
		{
			USPlineMarkGizmo* SPlineMarkGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<USPlineMarkGizmo>(TEXT("SocketGizmo"), FString(), this);
			SPlineMarkGizmo->CreateGizmoHandle(Info);
			SPlineMarkGizmo->SetSelectedObject(SplineActor);
			GizmoInfos.Add(SPlineMarkGizmo);
		}
		GizmoListInfo.Add(SplineActor, GizmoInfos);
	}
	if (ActiveActor)
	{
		if (!GizmoListInfo.Find(ActiveActor))
		{
			ActiveActor->UnregisterAllComponents();
			ActiveActor = nullptr;
		}
	}
}
void UMetaLandRoadInteractiveTool::RefreshGizmoInfo()
{
	if (!ActiveActor)
	{
		return;
	}

	ActiveActor->OnInitGizmo().ExecuteIfBound(ActiveActor);
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
	
	if (ActiveKey.Button == EKeys::A && ActiveKey.bPressed == true)
	{

		SplineEditTool->OnFinishedSplineEdit();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);

	}
	if (ActiveKey.Button == EKeys::Y && ActiveKey.bPressed == true)
	{
		SplineEditTool->InvertPreviewMesh();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);

	}
	if (ActiveKey.Button == EKeys::B && ActiveKey.bPressed == true)
	{
		SplineEditTool->SwitchInsertCrossState();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);
	}

	if (ActiveKey.Button == EKeys::F1&&Input.bShiftKeyDown&& ActiveKey.bPressed == true)
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
