#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

#include "MetaLandRoadSectionHover.h"
#include "MetaLandRoadEditorColors.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadModule.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadStyle.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "EngineUtils.h"
#include "Layout/WidgetPath.h"
#include "Modules/ModuleManager.h"
#include "SEditorViewport.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Styling/StyleColors.h"
#include "UnrealWidget.h"

#define LOCTEXT_NAMESPACE "FMoKuEditSplinesComponentVisualizer"

namespace MetaLandRoadSectionComponentVisualizerPrivate
{
	static float GetSplineVisualHeightOffset(const UMetaLandRoadSplineComponent* SplineComp)
	{
		// Match Alt+E LineBatch pink main (+8) so PDI hover/proxies align with visible center.
		return SplineComp ? SplineComp->LaneVisualizationHeightOffset + 8.0f : 23.0f;
	}

	static FVector RaiseSplineVisualPoint(const UMetaLandRoadSplineComponent* SplineComp, const FVector& WorldLocation)
	{
		return WorldLocation + FVector(0.0f, 0.0f, GetSplineVisualHeightOffset(SplineComp));
	}

	/** Same height as dashed lane LineBatch — what the user actually aims at. */
	static FVector RaiseLaneVizPoint(const UMetaLandRoadSplineComponent* SplineComp, const FVector& WorldLocation)
	{
		const float Z = SplineComp ? SplineComp->LaneVisualizationHeightOffset : 15.0f;
		return WorldLocation + FVector(0.0f, 0.0f, Z);
	}

	static UMetaLandRoadSplineComponent* FindSelectedRoadSplineFromEditor()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		for (FSelectionIterator It(GEditor->GetSelectedComponentIterator()); It; ++It)
		{
			if (UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(*It))
			{
				return SplineComp;
			}
		}

		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			if (!Actor)
			{
				continue;
			}

			if (UMetaLandRoadSplineComponent* SplineComp = Actor->FindComponentByClass<UMetaLandRoadSplineComponent>())
			{
				return SplineComp;
			}
		}

		return nullptr;
	}

	static bool IsRoadSectionEditingActive(const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState)
	{
		return SelectionState
			&& SelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
	}

	/** Main center road: UMoKuEditSplinesComponent with Tag MainRoad, or any other UMetaLandRoadSplineComponent. */
	static bool IsMainRoadSpline(const UActorComponent* Component)
	{
		if (!Component)
		{
			return false;
		}
		if (const UMoKuEditSplinesComponent* MoKuSpline = Cast<const UMoKuEditSplinesComponent>(Component))
		{
			return MoKuSpline->Tag == TEXT("MainRoad");
		}
		return Cast<const UMetaLandRoadSplineComponent>(Component) != nullptr;
	}

	static bool IsEngineSplineEditProxy(const HHitProxy* HitProxy)
	{
		if (!HitProxy)
		{
			return false;
		}

		const FName TypeName = HitProxy->GetType()->GetName();
		return TypeName == TEXT("HSplineKeyProxy")
			|| TypeName == TEXT("HSplineTangentHandleProxy")
			|| TypeName == TEXT("HSplineSegmentProxy");
	}

	static bool IsMetaLandRoadVisProxy(const HHitProxy* HitProxy)
	{
		if (!HitProxy)
		{
			return false;
		}

		return HitProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType())
			|| HitProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType())
			|| HitProxy->IsA(HMetaLandRoadSplineVisProxy::StaticGetType());
	}

	static bool GetSectionBoundaryCrossEndpoints(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		FVector& OutLeft,
		FVector& OutRight,
		FVector& OutCenter)
	{
		if (!SplineComp || SectionIndex <= 0 || SectionIndex >= SplineComp->GetLaneSectionsNum())
		{
			return false;
		}

		const double S = SplineComp->GetLaneSection(SectionIndex).SOffset;
		const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(S);
		OutCenter = RaiseSplineVisualPoint(
			SplineComp,
			SplineComp->EvalLanePosition(SectionIndex, LANE_INDEX_NONE, S, 0.0, ESplineCoordinateSpace::World));

		const FMetaLandRoadLayout& Layout = SplineComp->GetRoadLayout();
		const FVector RightVector = SplineComp->GetRightVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
		const double FallbackHalfWidth = MetaLandRoad::DefaultRoadLaneWidth * 2.0;

		OutLeft = OutCenter;
		OutRight = OutCenter;

		const FMetaLandRoadLaneSection& SectionAtBoundary = SplineComp->GetLaneSection(SectionIndex);

		// Both: full cross on this section. Left/Right-only: half-cross (separate face breaks).
		if (SectionAtBoundary.Side == EMetaLandRoadLaneSectionSide::Both)
		{
			if (!SectionAtBoundary.Left.IsEmpty())
			{
				OutLeft = RaiseSplineVisualPoint(
					SplineComp,
					SplineComp->EvalLanePosition(
						SectionIndex, -SectionAtBoundary.Left.Num(), S, 1.0, ESplineCoordinateSpace::World));
			}
			else
			{
				OutLeft = OutCenter - RightVector * FallbackHalfWidth;
			}
			if (!SectionAtBoundary.Right.IsEmpty())
			{
				OutRight = RaiseSplineVisualPoint(
					SplineComp,
					SplineComp->EvalLanePosition(
						SectionIndex, SectionAtBoundary.Right.Num(), S, 1.0, ESplineCoordinateSpace::World));
			}
			else
			{
				OutRight = OutCenter + RightVector * FallbackHalfWidth;
			}
			(void)Layout;
			return true;
		}

		const bool bWantLeft = MetaLandRoad::HasLeftSide(SectionAtBoundary.Side);
		const bool bWantRight = MetaLandRoad::HasRightSide(SectionAtBoundary.Side);

		if (bWantLeft)
		{
			const int32 LeftSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
			if (LeftSectionIndex != INDEX_NONE)
			{
				const FMetaLandRoadLaneSection& LeftSection = SplineComp->GetLaneSection(LeftSectionIndex);
				if (!LeftSection.Left.IsEmpty())
				{
					OutLeft = RaiseSplineVisualPoint(
						SplineComp,
						SplineComp->EvalLanePosition(LeftSectionIndex, -LeftSection.Left.Num(), S, 1.0, ESplineCoordinateSpace::World));
				}
				else
				{
					OutLeft = OutCenter - RightVector * FallbackHalfWidth;
				}
			}
			else
			{
				OutLeft = OutCenter - RightVector * FallbackHalfWidth;
			}
		}

		if (bWantRight)
		{
			const int32 RightSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);
			if (RightSectionIndex != INDEX_NONE)
			{
				const FMetaLandRoadLaneSection& RightSection = SplineComp->GetLaneSection(RightSectionIndex);
				if (!RightSection.Right.IsEmpty())
				{
					OutRight = RaiseSplineVisualPoint(
						SplineComp,
						SplineComp->EvalLanePosition(RightSectionIndex, RightSection.Right.Num(), S, 1.0, ESplineCoordinateSpace::World));
				}
				else
				{
					OutRight = OutCenter + RightVector * FallbackHalfWidth;
				}
			}
			else
			{
				OutRight = OutCenter + RightVector * FallbackHalfWidth;
			}
		}

		// Left-only / Right-only: half-cross (center <-> outer).
		if (!bWantLeft)
		{
			OutLeft = OutCenter;
		}
		if (!bWantRight)
		{
			OutRight = OutCenter;
		}

		return true;
	}

	static void DrawSectionBoundaryHandles(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		int32 InHoveredSectionIndex = INDEX_NONE)
	{
		if (!SplineComp || !PDI || SplineComp->GetLaneSectionsNum() < 2)
		{
			return;
		}

		const bool bHitTesting = PDI->IsHitTesting();

		for (int32 SectionIndex = 1; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
		{
			FVector LeftLoc;
			FVector RightLoc;
			FVector CenterLoc;
			if (!GetSectionBoundaryCrossEndpoints(SplineComp, SectionIndex, LeftLoc, RightLoc, CenterLoc))
			{
				continue;
			}

			const bool bHovered = InHoveredSectionIndex == SectionIndex;
			const bool bSelected = SelectionState
				&& SelectionState->IsSectionBoundarySelected()
				&& SelectionState->IsSelected(SplineComp, SectionIndex);
			const FColor OrangeSplit(255, 115, 13);
			const FColor CrossColor = bHovered
				? FColor::Yellow
				: (bSelected ? FColor(255, 200, 64) : OrangeSplit);
			const float LineThickness = (bHovered || bSelected) ? 6.0f : 4.0f;

			if (bHitTesting)
			{
				PDI->SetHitProxy(new HMetaLandRoadSectionKeyVisProxy(SplineComp, SectionIndex, HPP_Foreground));
				PDI->DrawLine(LeftLoc, RightLoc, FColor::White, SDPG_Foreground, 12.0f, 0.0f, true);
				PDI->SetHitProxy(nullptr);
			}
			else
			{
				PDI->DrawLine(LeftLoc, RightLoc, CrossColor, SDPG_Foreground, LineThickness, 0.0f, true);
			}
		}
	}

	static bool PickSectionBoundaryByScreenDistance(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView& View,
		const FIntPoint& ClickPos,
		float MaxScreenDistance,
		int32& OutSectionIndex)
	{
		OutSectionIndex = INDEX_NONE;
		if (!SplineComp || SplineComp->GetLaneSectionsNum() < 2)
		{
			return false;
		}

		const FVector2D ClickPos2D(ClickPos.X, ClickPos.Y);
		const float MaxDistSq = MaxScreenDistance * MaxScreenDistance;
		float BestDistSq = MaxDistSq;
		const float ZDelta = GetSplineVisualHeightOffset(SplineComp) - (SplineComp->LaneVisualizationHeightOffset);

		for (int32 SectionIndex = 1; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
		{
			FVector LeftLoc;
			FVector RightLoc;
			FVector CenterLoc;
			if (!GetSectionBoundaryCrossEndpoints(SplineComp, SectionIndex, LeftLoc, RightLoc, CenterLoc))
			{
				continue;
			}

			// Test both raised PDI height and lane-viz height (yellow hover overlay).
			const FVector HeightCandidates[][3] = {
				{ LeftLoc, RightLoc, CenterLoc },
				{ LeftLoc - FVector(0, 0, ZDelta), RightLoc - FVector(0, 0, ZDelta), CenterLoc - FVector(0, 0, ZDelta) }
			};

			for (int32 HeightIdx = 0; HeightIdx < 2; ++HeightIdx)
			{
				FVector2D LeftScreen;
				FVector2D RightScreen;
				FVector2D CenterScreen;
				if (!View.WorldToPixel(HeightCandidates[HeightIdx][0], LeftScreen)
					|| !View.WorldToPixel(HeightCandidates[HeightIdx][1], RightScreen)
					|| !View.WorldToPixel(HeightCandidates[HeightIdx][2], CenterScreen))
				{
					continue;
				}

				const float DistSq = FMath::Min(
					FMath::Square(MetaLandRoad::DistancePointToSegment2D(ClickPos2D, LeftScreen, RightScreen)),
					FVector2D::DistSquared(ClickPos2D, CenterScreen));

				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutSectionIndex = SectionIndex;
				}
			}
		}

		return OutSectionIndex != INDEX_NONE;
	}

	static float ResolveSplitSplineKey(
		const UMetaLandRoadSplineComponent* SplineComp,
		UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
		float HoverSplineKey = -1.0f)
	{
		if (!SplineComp || !SelectionState)
		{
			return -1.0f;
		}

		static constexpr double MinSplitMargin = 100.0;
		const double SplineLength = SplineComp->GetSplineLength();
		if (SplineLength <= MinSplitMargin * 2.0)
		{
			return -1.0f;
		}

		const float LiveMouseKey = FMetaLandRoadSectionHover::PickSplineKeyFromMouseRay(
			SplineComp,
			GCurrentLevelEditingViewportClient);
		const float LastPickKey = SelectionState->GetLastPickSplineKey();
		const float HoverKey = HoverSplineKey >= 0.0f
			? HoverSplineKey
			: FMetaLandRoadSectionHover::GetLastHoveredSplineKey();

		float Key = -1.0f;
		if (LiveMouseKey >= 0.0f)
		{
			Key = LiveMouseKey;
		}
		else if (HoverKey >= 0.0f)
		{
			Key = HoverKey;
		}
		else if (LastPickKey >= 0.0f)
		{
			Key = LastPickKey;
		}

		if (Key < 0.0f)
		{
			return -1.0f;
		}

		double S = SplineComp->GetDistanceAlongSplineAtSplineInputKey(Key);
		S = FMath::Clamp(S, MinSplitMargin, SplineLength - MinSplitMargin);
		for (const FMetaLandRoadLaneSection& Existing : SplineComp->GetLaneSections())
		{
			if (FMath::IsNearlyEqual(Existing.SOffset, S, 5.0))
			{
				if (S + 10.0 < SplineLength - MinSplitMargin)
				{
					S += 10.0;
				}
				else if (S - 10.0 > MinSplitMargin)
				{
					S -= 10.0;
				}
				break;
			}
		}

		if (S <= MinSplitMargin || S >= SplineLength - MinSplitMargin)
		{
			return -1.0f;
		}

		Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(S);
		SelectionState->RecordPickSplineKey(Key);
		return Key;
	}

	static bool IsSplineOwnerSelected(const UMetaLandRoadSplineComponent* SplineComp)
	{
		if (!SplineComp)
		{
			return false;
		}

		if (CompVisUtils::IsSelectedInViewport(SplineComp))
		{
			return true;
		}

		const AActor* Owner = SplineComp->GetOwner();
		if (!Owner)
		{
			return false;
		}

		if (Owner->IsSelected())
		{
			return true;
		}

		if (GEditor)
		{
			if (GEditor->GetSelectedActors()->IsSelected(const_cast<AActor*>(Owner)))
			{
				return true;
			}

			for (FSelectionIterator It(GEditor->GetSelectedComponentIterator()); It; ++It)
			{
				const UActorComponent* SelectedComponent = Cast<UActorComponent>(*It);
				if (SelectedComponent && SelectedComponent->GetOwner() == Owner)
				{
					return true;
				}
			}
		}

		return false;
	}

	static void DrawSectionCenterLineSpan(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		int32 SectionIndex,
		const FLinearColor& Color,
		float LineThickness,
		bool bWithHitProxy)
	{
		if (!SplineComp || !PDI || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
		{
			return;
		}

		const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
		const double StartS = Section.SOffset;
		const double EndS = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
		if (EndS <= StartS + KINDA_SMALL_NUMBER)
		{
			return;
		}

		TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
		SplineComp->BuildLinearApproximation(
			Points,
			[&](double S)
			{
				return SplineComp->EvalROffset(S);
			},
			StartS,
			EndS,
			MetaLandRoad::DefaultReparamStepsPerSegment,
			MetaLandRoad::DefaultMinNumStepsPerSection,
			ESplineCoordinateSpace::World);

		auto DrawSpan = [&](const FLinearColor& DrawColor, float Thickness, bool bProxy)
		{
			if (bProxy)
			{
				PDI->SetHitProxy(new HMetaLandRoadLaneVisProxy(SplineComp, SectionIndex, LANE_INDEX_NONE, HPP_Foreground));
			}
			if (Points.Num() >= 2)
			{
				for (int32 StepIdx = 1; StepIdx < Points.Num(); ++StepIdx)
				{
					const FVector PointA = RaiseSplineVisualPoint(SplineComp, Points[StepIdx - 1].Position);
					const FVector PointB = RaiseSplineVisualPoint(SplineComp, Points[StepIdx].Position);
					PDI->DrawLine(PointA, PointB, DrawColor, SDPG_Foreground, Thickness, 0.0f, true);
				}
			}
			else
			{
				const float StartKey = SplineComp->GetInputKeyValueAtDistanceAlongSpline(StartS);
				const float EndKey = SplineComp->GetInputKeyValueAtDistanceAlongSpline(EndS);
				constexpr int32 NumSteps = 24;
				FVector OldPos = RaiseSplineVisualPoint(
					SplineComp,
					SplineComp->GetLocationAtSplineInputKey(StartKey, ESplineCoordinateSpace::World));
				for (int32 StepIdx = 1; StepIdx <= NumSteps; ++StepIdx)
				{
					const float Key = FMath::Lerp(StartKey, EndKey, StepIdx / static_cast<float>(NumSteps));
					const FVector NewPos = RaiseSplineVisualPoint(
						SplineComp,
						SplineComp->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World));
					PDI->DrawLine(OldPos, NewPos, DrawColor, SDPG_Foreground, Thickness, 0.0f, true);
					OldPos = NewPos;
				}
			}
			if (bProxy)
			{
				PDI->SetHitProxy(nullptr);
			}
		};

		// Wide pick band ONLY during hit-proxy render — solid black DrawLine is visible in color pass.
		if (bWithHitProxy && PDI->IsHitTesting())
		{
			DrawSpan(FLinearColor::White, FMath::Max(LineThickness * 2.5f, 48.0f), true);
		}
		DrawSpan(Color, LineThickness, bWithHitProxy && PDI->IsHitTesting());
	}

	/** Full-spline center fallback when layout sections are empty / uncached. */
	static void DrawFullCenterEditLine(
		const UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const FLinearColor& Color,
		float LineThickness)
	{
		if (!SplineComp || !PDI || SplineComp->GetNumberOfSplinePoints() < 2)
		{
			return;
		}

		const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
		const FInterpCurveVector& SplineInfo = SplineComp->GetSplinePointsPosition();
		const int32 NumSegments = SplineInfo.bIsLooped ? NumPoints : NumPoints - 1;
		FVector OldKeyPos = FVector::ZeroVector;
		for (int32 KeyIdx = 0; KeyIdx < NumSegments + 1; ++KeyIdx)
		{
			const FVector NewKeyPos = RaiseSplineVisualPoint(
				SplineComp,
				SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World));
			if (KeyIdx > 0)
			{
				constexpr int32 NumSteps = 20;
				FVector OldPos = OldKeyPos;
				for (int32 StepIdx = 1; StepIdx <= NumSteps; ++StepIdx)
				{
					const float Key = (KeyIdx - 1) + StepIdx / static_cast<float>(NumSteps);
					const FVector StepPos = RaiseSplineVisualPoint(
						SplineComp,
						SplineComp->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World));
					PDI->DrawLine(OldPos, StepPos, Color, SDPG_Foreground, LineThickness, 0.0f, true);
					OldPos = StepPos;
				}
			}
			OldKeyPos = NewKeyPos;
		}
	}

	/** Alt+E center line — one draw pass, color switches pink↔yellow (no second overlay line). */
	static void DrawSectionEditLines(
		UMetaLandRoadSplineComponent* SplineComp,
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSectionComponentVisualizerSelectionState* InSelectionState,
		int32 InHoveredSectionIndex)
	{
		if (!SplineComp || !PDI)
		{
			return;
		}

		const FLinearColor Pink(1.0f, 0.25f, 0.75f, 1.0f);
		const FLinearColor Yellow(1.0f, 1.0f, 0.0f, 1.0f);
		const float NormalThickness = 4.0f;
		const float HighlightThickness = 6.0f;

		const bool bSpanSelected = InSelectionState
			&& InSelectionState->GetSelectedSpline() == SplineComp
			&& !InSelectionState->IsSectionBoundarySelected()
			&& InSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE
			&& InSelectionState->GetState() >= EMetaLandRoadSectionSelectionState::Section
			&& InSelectionState->GetSelectedSectionIndex() != INDEX_NONE;
		const bool bHighlight = InHoveredSectionIndex != INDEX_NONE || bSpanSelected;
		const FLinearColor LineColor = bHighlight ? Yellow : Pink;
		const float LineThickness = bHighlight ? HighlightThickness : NormalThickness;

		if (SplineComp->GetLaneSectionsNum() == 0)
		{
			DrawFullCenterEditLine(SplineComp, PDI, LineColor, LineThickness);
			return;
		}

		// Single path per section (hit proxy + visible). Whole main uses one color.
		for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
		{
			DrawSectionCenterLineSpan(
				SplineComp,
				PDI,
				SectionIndex,
				LineColor,
				LineThickness,
				true);
		}
	}

}

IMPLEMENT_HIT_PROXY(HMetaLandRoadSectionKeyVisProxy, HMetaLandRoadLaneVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadLaneWidthKeyVisProxy, HMetaLandRoadLaneWidthSegmentVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadLaneWidthSegmentVisProxy, HMetaLandRoadLaneVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadLaneVisProxy, HMetaLandRoadSectionVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSectionVisProxy, HMetaLandRoadSplineVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSplineVisProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadNativeSplineVisProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSplineKeyVisProxy, HMetaLandRoadNativeSplineVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSplineSegmentVisProxy, HMetaLandRoadNativeSplineVisProxy);

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedData(const FVector& Position, const FQuat& Rotation, float SplineKey)
{
	CachedPosition = Position;
	CachedRotation = Rotation;
	CachedSplineKey = SplineKey;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::RecordPickSplineKey(float Key)
{
	LastPickSplineKey = Key;
	bSectionBoundarySelected = false;
	SetCachedDataAtSplineInputKey(Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedDataAtSplineInputKey(float Key)
{
	const UMetaLandRoadSplineComponent* SplineComp = GetSelectedSpline();
	if (!IsValid(SplineComp) || SplineComp->GetLaneSectionsNum() == 0)
	{
		ResetCachedData();
		return;
	}

	const float S = SplineComp->GetDistanceAlongSplineAtSplineInputKey(Key);
	const int32 SectionIndex = FMath::Clamp(SplineComp->GetRoadLayout().FindSectionAtSOffset(S), 0, SplineComp->GetLaneSectionsNum() - 1);
	const FVector Loc = SplineComp->EvalLanePosition(SectionIndex, LANE_INDEX_NONE, S, 1.0, ESplineCoordinateSpace::World);
	const FQuat Rot = SplineComp->GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	SetCachedData(Loc, Rot, Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedDataAtLane(
	int32 SectionIndex,
	int32 LaneIndex,
	double SOffset,
	double Alpha)
{
	const UMetaLandRoadSplineComponent* SplineComp = GetSelectedSpline();
	if (!IsValid(SplineComp)
		|| !SplineComp->GetLaneSections().IsValidIndex(SectionIndex)
		|| LaneIndex == LANE_INDEX_NONE)
	{
		ResetCachedData();
		return;
	}

	const FVector Loc = MetaLandRoadSectionComponentVisualizerPrivate::RaiseLaneVizPoint(
		SplineComp,
		SplineComp->EvalLanePosition(SectionIndex, LaneIndex, SOffset, Alpha, ESplineCoordinateSpace::World));
	const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(SOffset);
	const FQuat Rot = SplineComp->GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	SetCachedData(Loc, Rot, Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetCachedDataAtSectionStart(int32 SectionIndex)
{
	const UMetaLandRoadSplineComponent* SplineComp = GetSelectedSpline();
	if (!IsValid(SplineComp) || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		ResetCachedData();
		return;
	}

	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(Section.SOffset);
	// Match Alt+E yellow LineBatch / lane viz height so the translate gizmo sits on the highlight.
	const FVector Loc = MetaLandRoadSectionComponentVisualizerPrivate::RaiseLaneVizPoint(
		SplineComp,
		SplineComp->EvalLanePosition(SectionIndex, LANE_INDEX_NONE, Section.SOffset, 0.0, ESplineCoordinateSpace::World));
	const FQuat Rot = SplineComp->GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	SetCachedData(Loc, Rot, Key);
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ResetCachedData()
{
	CachedRotation = FQuat::Identity;
	CachedPosition = FVector::ZeroVector;
	CachedSplineKey = 0.0f;
	LastPickSplineKey = -1.0f;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::FixState()
{
	const EMetaLandRoadSectionSelectionState StateVerified = GetStateVerified();
	if (StateVerified != State)
	{
		if (StateVerified < EMetaLandRoadSectionSelectionState::Key)
		{
			SelectedKeyIndex = INDEX_NONE;
		}
		if (StateVerified < EMetaLandRoadSectionSelectionState::Lane)
		{
			SelectedLaneIndex = LANE_INDEX_NONE;
		}
		if (StateVerified < EMetaLandRoadSectionSelectionState::Section)
		{
			SelectedSectionIndex = INDEX_NONE;
		}
		if (StateVerified < EMetaLandRoadSectionSelectionState::Component)
		{
			SplinePropertyPath = FComponentPropertyPath();
		}
		State = StateVerified;
	}
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::ResetSelection(bool bSaveSplineSelection)
{
	SelectedSectionIndex = INDEX_NONE;
	SelectedLaneIndex = LANE_INDEX_NONE;
	SelectedKeyIndex = INDEX_NONE;
	bSectionBoundarySelected = false;

	if (bSaveSplineSelection && SplinePropertyPath.IsValid())
	{
		State = EMetaLandRoadSectionSelectionState::Component;
	}
	else
	{
		State = EMetaLandRoadSectionSelectionState::None;
		SplinePropertyPath = FComponentPropertyPath();
	}

	ResetCachedData();
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedSpline(FComponentPropertyPath& InSplinePropertyPath)
{
	ResetSelection(false);
	if (!InSplinePropertyPath.IsValid())
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Component;
	SplinePropertyPath = InSplinePropertyPath;
	SelectedSectionIndex = INDEX_NONE;
	SelectedLaneIndex = LANE_INDEX_NONE;
	SelectedKeyIndex = INDEX_NONE;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedSection(int32 InSelectedSectionIndex)
{
	if (InSelectedSectionIndex < 0 || !SplinePropertyPath.IsValid() || uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Component))
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Section;
	SelectedSectionIndex = InSelectedSectionIndex;
	SelectedLaneIndex = LANE_INDEX_NONE;
	SelectedKeyIndex = INDEX_NONE;
	bSectionBoundarySelected = false;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedLane(int32 InSelectedLaneIndex)
{
	if (!SplinePropertyPath.IsValid() || SelectedSectionIndex == INDEX_NONE || uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Section))
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Lane;
	SelectedLaneIndex = InSelectedLaneIndex;
	SelectedKeyIndex = INDEX_NONE;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSelectedKeyIndex(int32 KeyIndex)
{
	if (!SplinePropertyPath.IsValid() || SelectedSectionIndex == INDEX_NONE || uint8(State) < uint8(EMetaLandRoadSectionSelectionState::Section))
	{
		return;
	}

	State = EMetaLandRoadSectionSelectionState::Key;
	SelectedKeyIndex = KeyIndex;
}

EMetaLandRoadSectionSelectionState UMetaLandRoadSectionComponentVisualizerSelectionState::GetStateVerified() const
{
	UMetaLandRoadSplineComponent* Component = GetSelectedSpline();

	if (State > EMetaLandRoadSectionSelectionState::None)
	{
		if (!IsValid(Component))
		{
			return EMetaLandRoadSectionSelectionState::None;
		}

		if (State > EMetaLandRoadSectionSelectionState::Component)
		{
			if (SelectedSectionIndex < 0 || SelectedSectionIndex >= Component->GetLaneSectionsNum())
			{
				return EMetaLandRoadSectionSelectionState::Component;
			}

			if (State == EMetaLandRoadSectionSelectionState::Section && SelectedLaneIndex != LANE_INDEX_NONE)
			{
				return EMetaLandRoadSectionSelectionState::Component;
			}

			if (State >= EMetaLandRoadSectionSelectionState::Lane)
			{
				const FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SelectedSectionIndex);
				if (SelectedLaneIndex != LANE_INDEX_NONE && !Section.CheckLaneIndex(SelectedLaneIndex))
				{
					return EMetaLandRoadSectionSelectionState::Component;
				}

				if (State >= EMetaLandRoadSectionSelectionState::Key && SelectedKeyIndex != INDEX_NONE)
				{
					const FMetaLandRoadLane& Lane = Section.GetLaneByIndex(SelectedLaneIndex);
					if (!Lane.Width.Keys.IsValidIndex(SelectedKeyIndex))
					{
						return EMetaLandRoadSectionSelectionState::Lane;
					}
				}
			}
		}
	}

	return State;
}

const FComponentPropertyPath UMetaLandRoadSectionComponentVisualizerSelectionState::GetSplinePropertyPath() const
{
	return SplinePropertyPath;
}

UMetaLandRoadSplineComponent* UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedSpline() const
{
	return SplinePropertyPath.IsValid()
		? Cast<UMetaLandRoadSplineComponent>(SplinePropertyPath.GetComponent())
		: nullptr;
}

int32 UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedSectionIndex() const
{
	return SelectedSectionIndex;
}

int32 UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedLaneIndex() const
{
	return SelectedLaneIndex;
}

int32 UMetaLandRoadSectionComponentVisualizerSelectionState::GetSelectedKeyIndex() const
{
	return SelectedKeyIndex;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSectionBoundarySelected() const
{
	return bSectionBoundarySelected;
}

void UMetaLandRoadSectionComponentVisualizerSelectionState::SetSectionBoundarySelected(bool bInSelected)
{
	bSectionBoundarySelected = bInSelected;
}

FVector UMetaLandRoadSectionComponentVisualizerSelectionState::GetCachedPosition() const
{
	return CachedPosition;
}

FQuat UMetaLandRoadSectionComponentVisualizerSelectionState::GetCachedRotation() const
{
	return CachedRotation;
}

float UMetaLandRoadSectionComponentVisualizerSelectionState::GetCachedSplineKey() const
{
	return CachedSplineKey;
}

float UMetaLandRoadSectionComponentVisualizerSelectionState::GetLastPickSplineKey() const
{
	return LastPickSplineKey;
}

EMetaLandRoadSectionSelectionState UMetaLandRoadSectionComponentVisualizerSelectionState::GetState() const
{
	return State;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSelected(
	const UMetaLandRoadSplineComponent* Spline,
	int SectionIndex) const
{
	return Spline == GetSelectedSpline() && SectionIndex == SelectedSectionIndex;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSelected(
	const UMetaLandRoadSplineComponent* Spline,
	int SectionIndex,
	int LaneIndex) const
{
	return Spline == GetSelectedSpline()
		&& SectionIndex == SelectedSectionIndex
		&& LaneIndex == SelectedLaneIndex;
}

bool UMetaLandRoadSectionComponentVisualizerSelectionState::IsSelected(
	const UMetaLandRoadSplineComponent* Spline,
	int SectionIndex,
	int LaneIndex,
	int WidthIndex) const
{
	return Spline == GetSelectedSpline()
		&& SectionIndex == SelectedSectionIndex
		&& LaneIndex == SelectedLaneIndex
		&& WidthIndex == SelectedKeyIndex;
}

class FMetaLandRoadSectionComponentVisualizerCommands : public TCommands<FMetaLandRoadSectionComponentVisualizerCommands>
{
public:
	FMetaLandRoadSectionComponentVisualizerCommands()
		: TCommands<FMetaLandRoadSectionComponentVisualizerCommands>(
			TEXT("MetaLandRoadSectionComponentVisualizer"),
			LOCTEXT("MetaLandRoadSectionComponentVisualizer", "MetaLandRoad Section Component Visualizer"),
			NAME_None,
			FMetaLandRoadStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(AddLaneToLeft, "Add Lane to Left", "Add a new road lane to the left side of the currently selected lane.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddLaneToRight, "Add Lane to Right", "Add a new road lane to the right side of the currently selected lane.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteLane, "Delete Lane", "Delete selected lane.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddWidthKey, "Add Width Key", "Add a width key on the selected lane boundary (Alt+T).", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteWidthKey, "Delete Width Key", "Delete the selected width key.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SetKeyToCurve, "Curve", "Set spline point to Curve type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToCurveCustomTangent, "Curve Custom Tangent", "Set spline point to Curve Custom Tangent type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToCurveClamped, "Curve Clamped", "Set spline point to Curve Clamped type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToLinear, "Linear", "Set spline point to Linear type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToConstant, "Constant", "Set spline point to Constant type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToArc, "Arc", "The segment from this point to the next one will try to maintain a circular arc.", EUserInterfaceActionType::RadioButton, FInputChord());
	}

public:
	TSharedPtr<FUICommandInfo> AddLaneToLeft;
	TSharedPtr<FUICommandInfo> AddLaneToRight;
	TSharedPtr<FUICommandInfo> DeleteLane;
	TSharedPtr<FUICommandInfo> AddWidthKey;
	TSharedPtr<FUICommandInfo> DeleteWidthKey;
	TSharedPtr<FUICommandInfo> SetKeyToCurve;
	TSharedPtr<FUICommandInfo> SetKeyToCurveCustomTangent;
	TSharedPtr<FUICommandInfo> SetKeyToCurveClamped;
	TSharedPtr<FUICommandInfo> SetKeyToLinear;
	TSharedPtr<FUICommandInfo> SetKeyToConstant;
	TSharedPtr<FUICommandInfo> SetKeyToArc;
};

FMoKuEditSplinesComponentVisualizer::FMoKuEditSplinesComponentVisualizer()
{
	FMetaLandRoadSectionComponentVisualizerCommands::Register();
	SectionComponentVisualizerActions = MakeShareable(new FUICommandList);
	RoadSectionSelectionState = NewObject<UMetaLandRoadSectionComponentVisualizerSelectionState>(
		GetTransientPackage(),
		TEXT("MetaLandRoadSectionComponentVisualizerSelectionState"),
		RF_Transactional);
}

FMoKuEditSplinesComponentVisualizer::~FMoKuEditSplinesComponentVisualizer() = default;

FString FMoKuEditSplinesComponentVisualizer::GetReferencerName() const
{
	return GetReferencerNameStatic();
}

UMetaLandRoadSectionComponentVisualizerSelectionState* FMoKuEditSplinesComponentVisualizer::GetSelectionState() const
{
	return RoadSectionSelectionState;
}

void FMoKuEditSplinesComponentVisualizer::RefreshRoadMeshForSpline(UMetaLandRoadSplineComponent* SplineComp) const
{
	if (!SplineComp)
	{
		return;
	}

	SplineComp->EnsureAllCurvePointTypesForSmoothCorners();

	if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		RoadActor->RefreshDynamicRoadMesh();
	}
	SplineComp->RefreshLaneVisualization();
}

void FMoKuEditSplinesComponentVisualizer::RefreshRoadMeshAndRedraw(UMetaLandRoadSplineComponent* SplineComp, bool bForceRedraw) const
{
	RefreshRoadMeshForSpline(SplineComp);
	if (GEditor && bForceRedraw)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMoKuEditSplinesComponentVisualizer::EnsureDefaultProfileIfNeeded(UMetaLandRoadSplineComponent* SplineComp) const
{
	if (SplineComp && SplineComp->GetLaneSectionsNum() == 0)
	{
		MetaLandRoadProfile::EnsureDefaultRoadProfile(SplineComp);
	}
}

void FMoKuEditSplinesComponentVisualizer::ClearSectionHighlight()
{
	if (SelectionState)
	{
		SelectionState->ClearSelectedKeys();
	}
	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ResetSelection(true);
	}
	HoveredSectionIndex = INDEX_NONE;
	HoveredBoundarySectionIndex = INDEX_NONE;
	HoveredSplineKey = -1.0f;
	CachedLaneHoveredSectionIndex = INDEX_NONE;
	CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	CachedLaneHoveredWidthIndex = INDEX_NONE;
}

void FMoKuEditSplinesComponentVisualizer::SyncSectionLineEditModeFlag(bool bEnabled)
{
	bSectionLineEditMode = bEnabled;
	if (bEnabled)
	{
		CachedNativeHoveredKey = INDEX_NONE;
		CachedNativeHoveredSegment = INDEX_NONE;
	}
	else
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		if (RoadSectionSelectionState)
		{
			RoadSectionSelectionState->ResetSelection(true);
		}
	}
}

bool FMoKuEditSplinesComponentVisualizer::SelectSectionBoundaryForDrag(int32 SectionIndex)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();

	if (!SplineComp || SectionIndex <= 0 || SectionIndex >= SplineComp->GetLaneSectionsNum())
	{
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MetaLandRoadSectionVis", "SelectSectionBoundary", "Select Section Boundary"));

	FComponentPropertyPath SplinePropertyPath(SplineComp);
	if (SelectionState && SplinePropertyPath.IsValid())
	{
		SelectionState->ClearSelectedKeys();
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
	}

	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		if (SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(SectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
	}

	ActivateRoadVisualization();

	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
		GCurrentLevelEditingViewportClient->SetRequiredCursorOverride(false);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::SelectMainLineForPathDrag(int32 SectionIndex, float SplineKey)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				SplineComp = Road->GetSplineComponent();
				break;
			}
		}
	}
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
	if (NumPoints < 1)
	{
		return false;
	}

	float UseKey = SplineKey;
	if (UseKey < 0.0f)
	{
		const double MidS = 0.5 * (SplineComp->GetLaneSection(SectionIndex).SOffset
			+ (SectionIndex + 1 < SplineComp->GetLaneSectionsNum()
				? SplineComp->GetLaneSection(SectionIndex + 1).SOffset
				: SplineComp->GetSplineLength()));
		UseKey = SplineComp->GetInputKeyValueAtDistanceAlongSpline(MidS);
	}

	int32 BestKey = FMath::Clamp(FMath::RoundToInt(UseKey), 0, NumPoints - 1);
	float BestDistSq = TNumericLimits<float>::Max();
	const FVector PickWorld = SplineComp->GetLocationAtSplineInputKey(UseKey, ESplineCoordinateSpace::World);
	for (int32 KeyIdx = 0; KeyIdx < NumPoints; ++KeyIdx)
	{
		const FVector KeyWorld = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
		const float DistSq = FVector::DistSquared(PickWorld, KeyWorld);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestKey = KeyIdx;
		}
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("MetaLandRoadSectionVis", "SelectMainLinePathDrag", "Select Road Path Point"));

	FComponentPropertyPath SplinePropertyPath(SplineComp);
	if (SelectionState && SplinePropertyPath.IsValid())
	{
		SelectionState->Modify();
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		SelectionState->ClearSelectedKeys();
		SelectionState->ClearSelectedSegmentIndex();
		SelectionState->ClearSelectedTangentHandle();
		SelectionState->ModifySelectedKeys().Add(BestKey);
		SelectionState->SetLastKeyIndexSelected(BestKey);
		SelectionState->SetCachedRotation(
			SplineComp->GetQuaternionAtSplinePoint(BestKey, ESplineCoordinateSpace::World));
	}

	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		if (SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(false);
		RoadSectionSelectionState->RecordPickSplineKey(UseKey);
		FVector WidgetPos = SplineComp->GetLocationAtSplinePoint(BestKey, ESplineCoordinateSpace::World);
		WidgetPos.Z += SplineComp->LaneVisualizationHeightOffset + 8.0f;
		RoadSectionSelectionState->SetCachedData(
			WidgetPos,
			SplineComp->GetQuaternionAtSplinePoint(BestKey, ESplineCoordinateSpace::World),
			static_cast<float>(BestKey));
	}

	ActivateRoadVisualization();
	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::ApplySectionBoundaryWorldDrag(int32 SectionIndex, const FVector& WorldPoint)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && RoadSectionSelectionState)
	{
		SplineComp = RoadSectionSelectionState->GetSelectedSpline();
	}
	if (!SplineComp && GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				SplineComp = Road->GetSplineComponent();
				break;
			}
		}
	}
	if (!SplineComp || SectionIndex <= 0 || SectionIndex >= SplineComp->GetLaneSectionsNum())
	{
		return false;
	}

	static constexpr double MinSectionLength = 100.0;
	if (SplineComp->GetSplineLength() <= MinSectionLength * 2.0)
	{
		return false;
	}

	const float ClosestKey = SplineComp->FindInputKeyClosestToWorldLocation(WorldPoint);
	double ClosestS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);
	ClosestS = FMath::Clamp(ClosestS, 0.0, SplineComp->GetSplineLength());

	static constexpr double SameBoundaryTolerance = 0.1;
	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	const double OldS = Section.SOffset;
	ClosestS = MetaLandRoad::ClampSectionBoundaryS(
		SplineComp, SectionIndex, ClosestS, MinSectionLength, SameBoundaryTolerance);

	if (FMath::IsNearlyEqual(OldS, ClosestS, SameBoundaryTolerance))
	{
		if (RoadSectionSelectionState)
		{
			RoadSectionSelectionState->SetCachedDataAtSectionStart(SectionIndex);
		}
		return true;
	}

	for (FMetaLandRoadLaneSection& LaneSection : SplineComp->GetLaneSectionsMutable())
	{
		if (FMath::IsNearlyEqual(LaneSection.SOffset, OldS, SameBoundaryTolerance))
		{
			LaneSection.SOffset = ClosestS;
		}
	}

	SplineComp->GetLaneSectionsMutable().StableSort(
		[](const FMetaLandRoadLaneSection& A, const FMetaLandRoadLaneSection& B)
		{
			if (!FMath::IsNearlyEqual(A.SOffset, B.SOffset, 0.1))
			{
				return A.SOffset < B.SOffset;
			}
			return static_cast<uint8>(A.Side) < static_cast<uint8>(B.Side);
		});

	// Re-map selection to the moved boundary after sort.
	if (RoadSectionSelectionState)
	{
		const int32 NewSectionIndex = CompVisUtils::FindBestFit(
			SplineComp->GetLaneSections(),
			[ClosestS](const FMetaLandRoadLaneSection& It)
			{
				return FMath::Abs(It.SOffset - ClosestS);
			});
		if (NewSectionIndex != INDEX_NONE)
		{
			SectionIndex = NewSectionIndex;
		}
	}

	SplineComp->bAutoMaintainMidpointSectionSplit = false;
	SplineComp->UpdateLaneSectionBounds();
	SplineComp->UpdateRoadLayout();
	SplineComp->MarkRenderStateDirty();

	if (RoadSectionSelectionState)
	{
		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (RoadSectionSelectionState->GetSelectedSpline() != SplineComp && SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(SectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

FString FMoKuEditSplinesComponentVisualizer::GetReferencerNameStatic()
{
	return TEXT("FMoKuEditSplinesComponentVisualizer");
}

bool FMoKuEditSplinesComponentVisualizer::IsSectionLineEditMode() const
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
		&& FMetaLandRoadModule::Get().IsSectionMode();
}

void FMoKuEditSplinesComponentVisualizer::SetSectionLineEditMode(bool bEnabled)
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		FMetaLandRoadModule::Get().SetRoadComponentVisMode(bEnabled);
	}
	else
	{
		SyncSectionLineEditModeFlag(bEnabled);
	}
}

void FMoKuEditSplinesComponentVisualizer::ToggleSectionLineEditMode()
{
	SetSectionLineEditMode(!IsSectionLineEditMode());
}

void FMoKuEditSplinesComponentVisualizer::OnRegister()
{
	FSplineComponentVisualizer::OnRegister();

	const FMetaLandRoadSectionComponentVisualizerCommands& Commands = FMetaLandRoadSectionComponentVisualizerCommands::Get();

	// Alt+E toggle: LevelEditor global action only (mapping here caused double-toggle with ProcessCommandBindings).

	SectionComponentVisualizerActions->MapAction(
		Commands.AddLaneToLeft,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddLane, true),
		FCanExecuteAction::CreateLambda([this]()
		{
			return (bSectionLineEditMode || FMetaLandRoadModule::Get().IsLaneEditMode())
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddLaneToRight,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddLane, false),
		FCanExecuteAction::CreateLambda([this]()
		{
			return (bSectionLineEditMode || FMetaLandRoadModule::Get().IsLaneEditMode())
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteLane,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteLane),
		FCanExecuteAction::CreateLambda([this]()
		{
			return (bSectionLineEditMode || FMetaLandRoadModule::Get().IsLaneEditMode())
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Lane;
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddWidthKey,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddWidthKey),
		FCanExecuteAction::CreateLambda([this]()
		{
			return FMetaLandRoadModule::Get().IsLaneEditMode()
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteWidthKey,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteWidthKey),
		FCanExecuteAction::CreateLambda([this]()
		{
			return FMetaLandRoadModule::Get().IsLaneEditMode()
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key;
		}));

	auto MapRoadSplinePointTypeOnList = [this](
		const TSharedPtr<FUICommandInfo>& Command,
		EMetaLandRoadSplinePointType Mode,
		TSharedPtr<FUICommandList> CommandList)
	{
		if (!CommandList.IsValid())
		{
			return;
		}

		CommandList->MapAction(
			Command,
			FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnSetRoadSplinePointType, Mode),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::IsRoadSplinePointTypeSet, Mode));
	};

	MapRoadSplinePointTypeOnList(Commands.SetKeyToCurve, EMetaLandRoadSplinePointType::Curve, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToCurveCustomTangent, EMetaLandRoadSplinePointType::CurveCustomTangent, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToCurveClamped, EMetaLandRoadSplinePointType::CurveClamped, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToLinear, EMetaLandRoadSplinePointType::Linear, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToConstant, EMetaLandRoadSplinePointType::Constant, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToArc, EMetaLandRoadSplinePointType::Arc, SectionComponentVisualizerActions);

	const FInputBindingManager& InputBindingManager = FInputBindingManager::Get();
	const auto RemapEngineSplinePointType = [this, &InputBindingManager, &MapRoadSplinePointTypeOnList](
		const FName CommandName,
		EMetaLandRoadSplinePointType Mode)
	{
		const TSharedPtr<FUICommandInfo> EngineCommand = InputBindingManager.FindCommandInContext(
			TEXT("SplineComponentVisualizer"),
			CommandName);
		if (EngineCommand.IsValid())
		{
			MapRoadSplinePointTypeOnList(EngineCommand, Mode, SectionComponentVisualizerActions);
		}
	};

	RemapEngineSplinePointType(TEXT("SetKeyToCurve"), EMetaLandRoadSplinePointType::Curve);
	RemapEngineSplinePointType(TEXT("SetKeyToLinear"), EMetaLandRoadSplinePointType::Linear);
	RemapEngineSplinePointType(TEXT("SetKeyToConstant"), EMetaLandRoadSplinePointType::Constant);
}

void FMoKuEditSplinesComponentVisualizer::AddReferencedObjects(FReferenceCollector& Collector)
{
	FSplineComponentVisualizer::AddReferencedObjects(Collector);
	if (RoadSectionSelectionState)
	{
		Collector.AddReferencedObject(RoadSectionSelectionState);
	}
}

bool FMoKuEditSplinesComponentVisualizer::ShouldShowForSelectedSubcomponents(const UActorComponent* Component)
{
	if (const UMoKuEditSplinesComponent* MoKuSpline = Cast<const UMoKuEditSplinesComponent>(Component))
	{
		return MetaLandRoadSectionComponentVisualizerPrivate::IsMainRoadSpline(MoKuSpline);
	}

	return FSplineComponentVisualizer::ShouldShowForSelectedSubcomponents(Component);
}

void FMoKuEditSplinesComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (!Component || !PDI)
	{
		return;
	}

	if (const UMoKuEditSplinesComponent* MoKuSpline = Cast<const UMoKuEditSplinesComponent>(Component))
	{
		if (!MetaLandRoadSectionComponentVisualizerPrivate::IsMainRoadSpline(MoKuSpline))
		{
			return;
		}
	}

	const UMetaLandRoadSplineComponent* SplineComp = Cast<const UMetaLandRoadSplineComponent>(Component);
	if (!SplineComp)
	{
		FSplineComponentVisualizer::DrawVisualization(Component, View, PDI);
		return;
	}

	// Standard: road mesh selection only — no spline/section/native overlays.
	if (FMetaLandRoadModule::Get().IsStandardMode())
	{
		return;
	}

	const bool bIsOwnerSelected = MetaLandRoadSectionComponentVisualizerPrivate::IsSplineOwnerSelected(SplineComp);
	const bool bIsComponentSelected = CompVisUtils::IsSelectedInViewport(SplineComp);
	const UMetaLandRoadSplineComponent* EditedSpline = GetEditedMetaLandRoadSpline();
	const bool bIsEditedSpline = EditedSpline == SplineComp;
	// W→E: ActiveNativeEditSpline is kept until after Apply; use it so pink main draws even if selection lags.
	const bool bActiveNativeMatch =
		FMetaLandRoadModule::Get().GetActiveNativeEditSpline() == SplineComp;
	const bool bShouldDrawRoad =
		bIsOwnerSelected || bIsComponentSelected || bIsEditedSpline || bActiveNativeMatch;

	if (bShouldDrawRoad && SelectionState)
	{
		FComponentPropertyPath SplinePropertyPath(const_cast<UMetaLandRoadSplineComponent*>(SplineComp));
		if (SplinePropertyPath.IsValid()
			&& SelectionState->GetSplinePropertyPath().GetComponent() != SplineComp)
		{
			SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		}

		if (RoadSectionSelectionState
			&& RoadSectionSelectionState->GetSelectedSpline() != SplineComp
			&& SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
	}

	// Standard only: engine spline draw. Alt+E must NOT call parent (draws key rings).
	// Alt+W uses DrawNativeEdit* overlays below.
	if (bShouldDrawRoad
		&& !IsSectionLineEditMode()
		&& !FMetaLandRoadModule::Get().IsLaneEditMode()
		&& !FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
	{
		FSplineComponentVisualizer::DrawVisualization(Component, View, PDI);
	}

	if (!bShouldDrawRoad)
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	// Alt+W — inherit engine spline visualizer (hit proxies, screen-scaled thickness, context menu).
	if (!IsSectionLineEditMode()
		&& !FMetaLandRoadModule::Get().IsLaneEditMode()
		&& FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
	{
		const UMetaLandRoadSplineComponent* ActiveNative =
			FMetaLandRoadModule::Get().GetActiveNativeEditSpline();
		if (!ActiveNative || ActiveNative != SplineComp)
		{
			return;
		}

		if (SelectionState)
		{
			const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->SyncEngineSelectionStateForSpline(SplineComp);
		}

		const int32 SelectedSegmentIndex = SelectionState ? SelectionState->GetSelectedSegmentIndex() : INDEX_NONE;
		const TSet<int32>& SelectedKeys = GetSelectedKeys();
		// Point and segment selection are mutually exclusive in Alt+W.
		const int32 EffectiveSelectedSegment =
			SelectedKeys.Num() > 0 ? INDEX_NONE : SelectedSegmentIndex;
		const int32 HoveredSegment = CachedNativeHoveredKey == INDEX_NONE ? CachedNativeHoveredSegment : INDEX_NONE;

		// Custom spline draw + engine HSpline*Proxy hit tests (no FSplineComponentVisualizer::DrawVisualization).
		DrawNativeEditSplineOverlay(
			SplineComp,
			PDI,
			CachedNativeHoveredKey,
			HoveredSegment,
			EffectiveSelectedSegment,
			SelectedKeys);

		DrawNativeEditRingOverlay(
			View,
			CachedNativeHoveredKey,
			SelectedKeys,
			EffectiveSelectedSegment,
			SplineComp,
			PDI);

		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	// Alt+T — lane boundaries only (no main center line).
	if (FMetaLandRoadModule::Get().IsLaneEditMode())
	{
		if (!bShouldDrawRoad)
		{
			CachedLaneHoveredSectionIndex = INDEX_NONE;
			CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
			CachedLaneHoveredWidthIndex = INDEX_NONE;
			return;
		}

		FMetaLandRoadLaneEditHighlight Highlight;
		Highlight.HoveredSectionIndex = CachedLaneHoveredSectionIndex;
		Highlight.HoveredLaneIndex = CachedLaneHoveredLaneIndex;
		Highlight.HoveredWidthIndex = CachedLaneHoveredWidthIndex;
		if (RoadSectionSelectionState)
		{
			Highlight.SelectedSectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
			Highlight.SelectedLaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
			if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
			{
				Highlight.SelectedWidthIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
			}
		}

		MetaLandRoadLaneVisualization::DrawLaneEditVisualization(SplineComp, PDI, Highlight);

		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	if (!IsSectionLineEditMode())
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	// Alt+E — section center lines / boundary handles.
	// Hover comes ONLY from TickEditorViewport / FMetaLandRoadSectionHover::Tick.
	// Never call Viewport->GetHitProxy here: hit-proxy render re-enters DrawVisualization → stack overflow.
	HoveredSectionIndex = INDEX_NONE;
	HoveredBoundarySectionIndex = INDEX_NONE;
	HoveredSplineKey = -1.0f;
	if (FMetaLandRoadSectionHover::GetLastHoveredBoundarySection() != INDEX_NONE)
	{
		HoveredBoundarySectionIndex = FMetaLandRoadSectionHover::GetLastHoveredBoundarySection();
	}
	else if (FMetaLandRoadSectionHover::GetLastHoveredSection() != INDEX_NONE)
	{
		HoveredSectionIndex = FMetaLandRoadSectionHover::GetLastHoveredSection();
		HoveredSplineKey = FMetaLandRoadSectionHover::GetLastHoveredSplineKey();
	}

	MetaLandRoadSectionComponentVisualizerPrivate::DrawSectionEditLines(
		const_cast<UMetaLandRoadSplineComponent*>(SplineComp),
		PDI,
		RoadSectionSelectionState,
		HoveredSectionIndex);

	// Alt+E: no lane overlay / side accent — only pink main + orange section crosses below.

	if (SplineComp->GetLaneSectionsNum() >= 2)
	{
		MetaLandRoadSectionComponentVisualizerPrivate::DrawSectionBoundaryHandles(
			SplineComp,
			PDI,
			RoadSectionSelectionState,
			HoveredBoundarySectionIndex);
	}
}

void FMoKuEditSplinesComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	// Alt+W only — Alt+E uses 3D PDI lines, no HUD chrome.
	if (!Canvas || !View || !Component
		|| (Cast<const UMoKuEditSplinesComponent>(Component)
			&& !MetaLandRoadSectionComponentVisualizerPrivate::IsMainRoadSpline(Component))
		|| !FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
	{
		return;
	}

	FSplineComponentVisualizer::DrawVisualizationHUD(Component, Viewport, View, Canvas);
}

const UMetaLandRoadSplineComponent* FMoKuEditSplinesComponentVisualizer::UpdateSelectedComponentAndSectionAndLane(HComponentVisProxy* VisProxy)
{
	check(RoadSectionSelectionState);

	const UMetaLandRoadSplineComponent* NewSplineComp = CastChecked<const UMetaLandRoadSplineComponent>(VisProxy->Component.Get());
	AActor* OldSplineOwningActor = RoadSectionSelectionState->GetSplinePropertyPath().GetParentOwningActor();
	FComponentPropertyPath NewSplinePropertyPath(NewSplineComp);
	RoadSectionSelectionState->SetSelectedSpline(NewSplinePropertyPath);

	if (OldSplineOwningActor != NewSplinePropertyPath.GetParentOwningActor())
	{
		RoadSectionSelectionState->ResetSelection(true);
	}

	if (NewSplinePropertyPath.IsValid())
	{
		if (RoadSectionSelectionState->GetSelectedSpline() != NewSplineComp)
		{
			RoadSectionSelectionState->ResetCachedData();
		}
	}
	else
	{
		RoadSectionSelectionState->ResetSelection(false);
		return nullptr;
	}

	CompVisUtils::DeselectAllExcept(NewSplineComp);

	if (SelectionState)
	{
		SelectionState->SetSplinePropertyPath(NewSplinePropertyPath);
	}

		if (VisProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType()))
		{
			HMetaLandRoadSectionVisProxy* SectionProxy = static_cast<HMetaLandRoadSectionVisProxy*>(VisProxy);
			if (SectionProxy->SectionIndex < 0 || SectionProxy->SectionIndex >= NewSplineComp->GetLaneSectionsNum())
			{
				return NewSplineComp;
			}
			RoadSectionSelectionState->SetSelectedSection(SectionProxy->SectionIndex);

			if (VisProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType()))
			{
				HMetaLandRoadLaneVisProxy* LaneProxy = static_cast<HMetaLandRoadLaneVisProxy*>(VisProxy);
				// Alt+E: section pick only — do not enter lane selection (avoids lane highlight).
				if (!IsSectionLineEditMode()
					&& LaneProxy->LaneIndex != LANE_INDEX_NONE)
				{
					const FMetaLandRoadLaneSection& Section = NewSplineComp->GetLaneSection(SectionProxy->SectionIndex);
					if (Section.CheckLaneIndex(LaneProxy->LaneIndex))
					{
						RoadSectionSelectionState->SetSelectedLane(LaneProxy->LaneIndex);
					}
				}
			}
		}

	return NewSplineComp;
}

bool FMoKuEditSplinesComponentVisualizer::TryCompleteNativeEditPointSelection(
	HComponentVisProxy* VisProxy,
	FEditorViewportClient* InViewportClient,
	const USplineComponent* SplineComp)
{
	if (!SelectionState || !VisProxy || !SplineComp)
	{
		return false;
	}

	if (SelectionState->GetLastKeyIndexSelected() == INDEX_NONE)
	{
		SelectionState->SetSplinePropertyPath(FComponentPropertyPath());
		return false;
	}

	SelectionState->ClearSelectedSegmentIndex();
	SelectionState->SetCachedRotation(
		SplineComp->GetQuaternionAtSplinePoint(
			SelectionState->GetLastKeyIndexSelected(), ESplineCoordinateSpace::World));

	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ResetSelection(true);
	}

	if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
			const_cast<UActorComponent*>(VisProxy->Component.Get())))
	{
		SetActiveForSpline(MetaSpline);
		EnsureNativeEditTranslateWidget(InViewportClient);
	}

	return true;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleNativeEditClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay()
		|| !VisProxy
		|| !VisProxy->Component.IsValid()
		|| !SelectionState
		|| (Click.GetKey() != EKeys::LeftMouseButton && Click.GetKey() != EKeys::RightMouseButton))
	{
		return false;
	}

	// Control-point rings beat spine segments — same priority as hover picking.
	if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
			const_cast<UActorComponent*>(VisProxy->Component.Get())))
	{
		if (InViewportClient && InViewportClient->Viewport)
		{
			FIntPoint MousePos;
			InViewportClient->Viewport->GetMousePos(MousePos);
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				InViewportClient->Viewport,
				InViewportClient->GetScene(),
				InViewportClient->EngineShowFlags));
			if (FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily))
			{
				const int32 KeyAtMouse = PickHoveredControlPointKey(MetaSpline, View, MousePos, 24.0f);
				if (KeyAtMouse != INDEX_NONE
					&& !VisProxy->IsA(HMetaLandRoadSplineKeyVisProxy::StaticGetType()))
				{
					const FScopedTransaction Transaction(LOCTEXT("SelectSplinePointNative", "Select Spline Point"));
					SelectionState->Modify();

					if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
					{
						const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
						if (Click.GetKey() != EKeys::RightMouseButton || !SelectedKeys.Contains(KeyAtMouse))
						{
							ChangeSelectionState(KeyAtMouse, InViewportClient->IsCtrlPressed());
						}
						SelectionState->ClearSelectedSegmentIndex();
						SelectionState->ClearSelectedTangentHandle();
						return TryCompleteNativeEditPointSelection(VisProxy, InViewportClient, SplineComp);
					}

					return false;
				}
			}
		}
	}

	if (VisProxy->IsA(HMetaLandRoadSplineKeyVisProxy::StaticGetType()))
	{
		const HMetaLandRoadSplineKeyVisProxy* KeyProxy =
			static_cast<const HMetaLandRoadSplineKeyVisProxy*>(VisProxy);
		if (KeyProxy->KeyIndex == INDEX_NONE)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SelectSplinePointNative", "Select Spline Point"));
		SelectionState->Modify();

		if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
		{
			const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
			if (Click.GetKey() != EKeys::RightMouseButton || !SelectedKeys.Contains(KeyProxy->KeyIndex))
			{
				ChangeSelectionState(KeyProxy->KeyIndex, InViewportClient->IsCtrlPressed());
			}
			SelectionState->ClearSelectedSegmentIndex();
			SelectionState->ClearSelectedTangentHandle();
			return TryCompleteNativeEditPointSelection(VisProxy, InViewportClient, SplineComp);
		}

		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadSplineSegmentVisProxy::StaticGetType()))
	{
		const HMetaLandRoadSplineSegmentVisProxy* SegProxy =
			static_cast<const HMetaLandRoadSplineSegmentVisProxy*>(VisProxy);
		if (SegProxy->SegmentIndex == INDEX_NONE)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SelectSplineSegmentNative", "Select Spline Segment"));
		SelectionState->Modify();

		if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
		{
			SelectionState->ClearSelectedKeys();
			SelectionState->ClearSelectedTangentHandle();
			SelectionState->SetLastKeyIndexSelected(INDEX_NONE);
			SelectionState->SetSelectedSegmentIndex(SegProxy->SegmentIndex);

			constexpr int32 NumSubdivisions = 16;
			const int32 SelectedSegmentIndex = SegProxy->SegmentIndex;
			float SubsegmentStartKey = static_cast<float>(SelectedSegmentIndex);
			FVector SubsegmentStart = SplineComp->GetLocationAtSplineInputKey(
				SubsegmentStartKey, ESplineCoordinateSpace::World);
			double ClosestDistance = TNumericLimits<double>::Max();
			FVector BestLocation = SubsegmentStart;
			for (int32 Step = 1; Step < NumSubdivisions; ++Step)
			{
				const float SubsegmentEndKey = static_cast<float>(SelectedSegmentIndex)
					+ static_cast<float>(Step) / static_cast<float>(NumSubdivisions);
				const FVector SubsegmentEnd = SplineComp->GetLocationAtSplineInputKey(
					SubsegmentEndKey, ESplineCoordinateSpace::World);
				FVector SplineClosest;
				FVector RayClosest;
				FMath::SegmentDistToSegmentSafe(
					SubsegmentStart,
					SubsegmentEnd,
					Click.GetOrigin(),
					Click.GetOrigin() + Click.GetDirection() * 50000.0f,
					SplineClosest,
					RayClosest);
				const double Distance = FVector::DistSquared(SplineClosest, RayClosest);
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					BestLocation = SplineClosest;
				}
				SubsegmentStartKey = SubsegmentEndKey;
				SubsegmentStart = SubsegmentEnd;
			}

			SelectionState->SetSelectedSplinePosition(BestLocation);

			if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
					const_cast<UActorComponent*>(VisProxy->Component.Get())))
			{
				SetActiveForSpline(MetaSpline);
				EnsureNativeEditTranslateWidget(InViewportClient);
			}
			return true;
		}

		return false;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleEngineSplineEditClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !MetaLandRoadSectionComponentVisualizerPrivate::IsEngineSplineEditProxy(VisProxy))
	{
		return false;
	}

	const bool bHandled = FSplineComponentVisualizer::VisProxyHandleClick(InViewportClient, VisProxy, Click);
	if (bHandled)
	{
		if (FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
		{
			if (UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(
					const_cast<UActorComponent*>(VisProxy->Component.Get())))
			{
				SetActiveForSpline(SplineComp);
			}
		}

		if (RoadSectionSelectionState)
		{
			const FScopedTransaction Transaction(LOCTEXT("SelectSplinePoint", "Select Spline Point"));
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->ResetSelection(true);
		}
	}

	return bHandled;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleMetaLandRoadProxyClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->Component.IsValid()
		|| !MetaLandRoadSectionComponentVisualizerPrivate::IsMetaLandRoadVisProxy(VisProxy))
	{
		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType()))
	{
		const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionLane", "Select Road Lane"));
		RoadSectionSelectionState->Modify();
		HMetaLandRoadLaneVisProxy* Proxy = static_cast<HMetaLandRoadLaneVisProxy*>(VisProxy);
		if (const UMetaLandRoadSplineComponent* SplineComp = UpdateSelectedComponentAndSectionAndLane(VisProxy))
		{
			if (SelectionState)
			{
				SelectionState->ClearSelectedKeys();
			}

			const bool bBoundaryKey = VisProxy->IsA(HMetaLandRoadSectionKeyVisProxy::StaticGetType());
			if (bBoundaryKey)
			{
				RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
				RoadSectionSelectionState->SetCachedDataAtSectionStart(Proxy->SectionIndex);
				RoadSectionSelectionState->SetSectionBoundarySelected(true);
			}
			else if (IsSectionLineEditMode() || Proxy->LaneIndex == LANE_INDEX_NONE)
			{
				const float Key = FMetaLandRoadSectionHover::GetLastHoveredSplineKey() >= 0.0f
					? FMetaLandRoadSectionHover::GetLastHoveredSplineKey()
					: SplineComp->FindInputKeyClosestToWorldLocation(
						Click.GetOrigin() + Click.GetDirection() * 5000.0f);
				if (IsSectionLineEditMode())
				{
					SelectMainLineForPathDrag(Proxy->SectionIndex, Key);
				}
				else
				{
					RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
					RoadSectionSelectionState->RecordPickSplineKey(Key);
				}
			}
			else
			{
				RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
				RoadSectionSelectionState->SetSelectedLane(Proxy->LaneIndex);
				RoadSectionSelectionState->SetSectionBoundarySelected(false);

				const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
					SplineComp->GetLaneRange(Proxy->SectionIndex, Proxy->LaneIndex);
				if (Range.EndS > Range.StartS + KINDA_SMALL_NUMBER)
				{
					const float Key = SplineComp->ClosestKeyToSegment2(
						static_cast<float>(Range.StartS),
						static_cast<float>(Range.EndS),
						Click.GetOrigin(),
						Click.GetOrigin() + Click.GetDirection() * 50000.0f);
					RoadSectionSelectionState->RecordPickSplineKey(Key);
				}
				else
				{
					RoadSectionSelectionState->RecordPickSplineKey(
						SplineComp->GetInputKeyValueAtDistanceAlongSpline(Range.StartS));
				}
			}

			ActivateRoadVisualization();
			GEditor->RedrawLevelEditingViewports(true);
			return true;
		}

		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType())
		|| VisProxy->IsA(HMetaLandRoadSplineVisProxy::StaticGetType()))
	{
		const FScopedTransaction Transaction(LOCTEXT("UnselectRoad", "Unselect Road"));
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ResetSelection(false);
		GEditor->RedrawLevelEditingViewports(true);
		return true;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (VisProxyHandleNativeEditClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	// Alt+E: never route engine key/segment proxies (Add/Delete/Duplicate Point = Alt+W only).
	if ((IsSectionLineEditMode() || FMetaLandRoadModule::Get().IsLaneEditMode())
		&& VisProxy
		&& MetaLandRoadSectionComponentVisualizerPrivate::IsEngineSplineEditProxy(VisProxy))
	{
		return false;
	}

	if (VisProxyHandleEngineSplineEditClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	if (VisProxyHandleLaneWidthClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	if (VisProxyHandleMetaLandRoadProxyClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	return FSplineComponentVisualizer::VisProxyHandleClick(InViewportClient, VisProxy, Click);
}

UMetaLandRoadSplineComponent* FMoKuEditSplinesComponentVisualizer::GetEditedMetaLandRoadSpline() const
{
	// NativeEdit, or briefly during W→E Apply while ActiveNativeEditSpline is still set.
	if (UMetaLandRoadSplineComponent* ActiveNative = FMetaLandRoadModule::Get().GetActiveNativeEditSpline())
	{
		if (FMetaLandRoadModule::Get().IsNativeEditMode()
			|| FMetaLandRoadModule::Get().IsSectionMode()
			|| FMetaLandRoadModule::Get().IsLaneEditMode())
		{
			return ActiveNative;
		}
	}

	// Prefer explicit road-section selection. Parent GetEditedSplineComponent() may return a
	// side-helper spline; treating that as authoritative previously made this return null and
	// broke both the translate gizmo and HandleInputDelta for section boundaries.
	auto PickValidRoadSpline = [](UMetaLandRoadSplineComponent* Candidate) -> UMetaLandRoadSplineComponent*
	{
		if (!Candidate || !MetaLandRoadSectionComponentVisualizerPrivate::IsMainRoadSpline(Candidate))
		{
			return nullptr;
		}
		return Candidate;
	};

	if (UMetaLandRoadSplineComponent* FromRoadSel = RoadSectionSelectionState
		? PickValidRoadSpline(RoadSectionSelectionState->GetSelectedSpline())
		: nullptr)
	{
		return FromRoadSel;
	}

	if (UMetaLandRoadSplineComponent* FromParent = PickValidRoadSpline(
		Cast<UMetaLandRoadSplineComponent>(FSplineComponentVisualizer::GetEditedSplineComponent())))
	{
		if (CompVisUtils::IsSelectedInViewport(FromParent)
			|| MetaLandRoadSectionComponentVisualizerPrivate::IsSplineOwnerSelected(FromParent))
		{
			return FromParent;
		}
	}

	if (UMetaLandRoadSplineComponent* FromEditor = PickValidRoadSpline(
		MetaLandRoadSectionComponentVisualizerPrivate::FindSelectedRoadSplineFromEditor()))
	{
		return FromEditor;
	}

	return nullptr;
}

UActorComponent* FMoKuEditSplinesComponentVisualizer::GetEditedComponent() const
{
	// Prefer the engine SelectionState path (native Add Point / key edit). Fall back to road selection.
	if (UActorComponent* Edited = FSplineComponentVisualizer::GetEditedComponent())
	{
		return Edited;
	}
	return GetEditedMetaLandRoadSpline();
}

bool FMoKuEditSplinesComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	if (FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
	{
		return FSplineComponentVisualizer::GetWidgetLocation(ViewportClient, OutLocation);
	}

	// Boundary cross → SOffset gizmo (Alt+E only).
	if (IsSectionLineEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->IsSectionBoundarySelected()
		&& RoadSectionSelectionState->GetSelectedSpline()
		&& RoadSectionSelectionState->GetSelectedSectionIndex() > 0
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE)
	{
		OutLocation = RoadSectionSelectionState->GetCachedPosition();
		return true;
	}

	// Alt+E main-line path drag / native key drag.
	if (GetSelectedKeys().Num() > 0 && SelectionState && SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		if (!FSplineComponentVisualizer::GetWidgetLocation(ViewportClient, OutLocation))
		{
			return false;
		}
		if (IsSectionLineEditMode())
		{
			if (const UMetaLandRoadSplineComponent* RoadSpline = GetEditedMetaLandRoadSpline())
			{
				OutLocation.Z += RoadSpline->LaneVisualizationHeightOffset + 8.0f;
			}
		}
		return true;
	}

	if (FMetaLandRoadModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() == EMetaLandRoadSectionSelectionState::Key)
	{
		OutLocation = RoadSectionSelectionState->GetCachedPosition();
		return true;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const
{
	if (FMetaLandRoadModule::Get().IsNativeEditMode() && SelectionState)
	{
		if (SelectionState->GetLastKeyIndexSelected() != INDEX_NONE
			|| SelectionState->GetSelectedSegmentIndex() != INDEX_NONE)
		{
			return FSplineComponentVisualizer::GetCustomInputCoordinateSystem(ViewportClient, OutMatrix);
		}
	}

	if (GetSelectedKeys().Num() > 0 && SelectionState && SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		return FSplineComponentVisualizer::GetCustomInputCoordinateSystem(ViewportClient, OutMatrix);
	}

	if (FMetaLandRoadModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() == EMetaLandRoadSectionSelectionState::Key)
	{
		OutMatrix = FRotationMatrix::Make(RoadSectionSelectionState->GetCachedRotation());
		return true;
	}

	if (ViewportClient->GetWidgetCoordSystemSpace() == COORD_Local || ViewportClient->GetWidgetMode() == UE::Widget::WM_Rotate)
	{
		if (RoadSectionSelectionState
			&& RoadSectionSelectionState->IsSectionBoundarySelected()
			&& MetaLandRoadSectionComponentVisualizerPrivate::IsRoadSectionEditingActive(RoadSectionSelectionState)
			&& GetEditedMetaLandRoadSpline())
		{
			OutMatrix = FRotationMatrix::Make(RoadSectionSelectionState->GetCachedRotation());
			return true;
		}
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::IsVisualizingArchetype() const
{
	return FSplineComponentVisualizer::IsVisualizingArchetype();
}

bool FMoKuEditSplinesComponentVisualizer::HandleInputDelta(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FVector& DeltaTranslate,
	FRotator& DeltaRotate,
	FVector& DeltaScale)
{
	check(RoadSectionSelectionState);

	// Alt+W: engine spline key/segment move + road mesh refresh after drag.
	if (FMetaLandRoadModule::Get().IsNativeEditMode()
		&& !IsSectionLineEditMode()
		&& SelectionState)
	{
		const bool bHandled = FSplineComponentVisualizer::HandleInputDelta(
			ViewportClient, Viewport, DeltaTranslate, DeltaRotate, DeltaScale);
		if (bHandled)
		{
			if (UMetaLandRoadSplineComponent* RoadSpline = GetEditedMetaLandRoadSpline())
			{
				RoadSpline->EnsureAllCurvePointTypesForSmoothCorners();
				RoadSpline->UpdateRoadLayout();
				RefreshRoadMeshForSpline(RoadSpline);
				MetaLandRoadLaneVisualization::RefreshLaneVisualization(RoadSpline);
			}
		}
		return bHandled;
	}

	const EMetaLandRoadSectionSelectionState State = RoadSectionSelectionState->GetStateVerified();

	if (FMetaLandRoadModule::Get().IsLaneEditMode()
		&& State == EMetaLandRoadSectionSelectionState::Key)
	{
		UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
		if (!SplineComp)
		{
			return false;
		}

		SplineComp->Modify();
		RoadSectionSelectionState->Modify();

		const FVector WidgetLocationWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaTranslate;
		const float ClosestKey = SplineComp->FindInputKeyClosestToWorldLocation(WidgetLocationWorld);
		const float ClosestS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);

		const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
		const int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
		const int32 KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();

		FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
		FMetaLandRoadLane& Lane = Section.GetLaneByIndex(LaneIndex);
		FRichCurveKey& Key = Lane.Width.Keys[KeyIndex];
		const double FillSOffset = Section.SOffset + Key.Time;

		const FTransform KeyTransform = SplineComp->GetTransformAtSplineInputKey(
			SplineComp->GetInputKeyValueAtDistanceAlongSpline(FillSOffset),
			ESplineCoordinateSpace::World);
		const FVector WidgetLocationLocal = KeyTransform.InverseTransformPositionNoScale(WidgetLocationWorld);

		const double TargetROffset = WidgetLocationLocal.Y;
		const double CurrentROffset =
			Section.EvalLaneROffset(LaneIndex, FillSOffset, 1.0) + SplineComp->EvalROffset(FillSOffset);

		const double SOffset = ClosestS - Section.SOffset;
		Key.Value += static_cast<float>((TargetROffset - CurrentROffset) * (LaneIndex >= LANE_INDEX_NONE ? 1.0 : -1.0));
		Key.Time = static_cast<float>(SOffset);

		Lane.Width.Keys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });

		const int32 NewKeyIndex = FindBestFitWidthKeyIndex(Lane.Width, SOffset);
		RoadSectionSelectionState->SetSelectedKeyIndex(NewKeyIndex);
		RoadSectionSelectionState->SetCachedDataAtLane(
			SectionIndex,
			LaneIndex,
			Section.SOffset + Lane.Width.Keys[NewKeyIndex].Time,
			1.0);

		SplineComp->UpdateRoadLayout();
		RefreshRoadMeshForSpline(SplineComp);
		GEditor->RedrawLevelEditingViewports(true);
		return true;
	}

	const bool bDragSectionBoundary =
		RoadSectionSelectionState->IsSectionBoundarySelected()
		&& RoadSectionSelectionState->GetSelectedSectionIndex() > 0
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE
		&& (State == EMetaLandRoadSectionSelectionState::Section
			|| State == EMetaLandRoadSectionSelectionState::Lane);

	if (bDragSectionBoundary)
	{
		const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
		FVector WidgetLocationWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaTranslate;

		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			if (SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
			{
				const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(
					SplineComp->GetLaneSection(SectionIndex).SOffset);
				const FVector Tangent = SplineComp->GetDirectionAtSplineInputKey(
					Key, ESplineCoordinateSpace::World).GetSafeNormal();
				const FVector DeltaAlong = Tangent * FVector::DotProduct(DeltaTranslate, Tangent);
				WidgetLocationWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaAlong;
			}
		}

		if (ApplySectionBoundaryWorldDrag(SectionIndex, WidgetLocationWorld))
		{
			if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
			{
				RefreshRoadMeshForSpline(SplineComp);
			}
			return true;
		}
		return false;
	}

	// Alt+E main-line path drag / native key drag.
	if (GetSelectedKeys().Num() > 0 && SelectionState && SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		const bool bHandled = FSplineComponentVisualizer::HandleInputDelta(
			ViewportClient, Viewport, DeltaTranslate, DeltaRotate, DeltaScale);
		if (bHandled)
		{
			if (UMetaLandRoadSplineComponent* RoadSpline = GetEditedMetaLandRoadSpline())
			{
				RefreshRoadMeshForSpline(RoadSpline);
				if (IsSectionLineEditMode())
				{
					MetaLandRoadLaneVisualization::RefreshLaneVisualization(RoadSpline);
				}
			}
		}
		return bHandled;
	}

	return false;
}

void FMoKuEditSplinesComponentVisualizer::TrackingStarted(FEditorViewportClient* InViewportClient)
{
	if (FMetaLandRoadModule::Get().IsNativeEditMode()
		&& SelectionState
		&& SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		EnsureNativeEditTranslateWidget(InViewportClient);
	}

	if (FMetaLandRoadModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() == EMetaLandRoadSectionSelectionState::Key)
	{
		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			SplineComp->Modify();
			RoadSectionSelectionState->Modify();
		}
	}

	if (RoadSectionSelectionState
		&& RoadSectionSelectionState->IsSectionBoundarySelected()
		&& RoadSectionSelectionState->GetSelectedSectionIndex() > 0
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE)
	{
		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			SplineComp->Modify();
			RoadSectionSelectionState->Modify();
		}
	}

	FSplineComponentVisualizer::TrackingStarted(InViewportClient);
}

bool FMoKuEditSplinesComponentVisualizer::HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	// Alt+W / Alt+E mode toggles are owned by LevelEditor global actions only (avoid double-toggle).

	if (FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay() && !IsSectionLineEditMode())
	{
		if (Event == IE_Pressed)
		{
			if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
			{
				SetActiveForSpline(SplineComp);
				if (SelectionState)
				{
					SyncEngineSelectionStateForSpline(SplineComp);
				}
			}
		}

		return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (SplineComp != nullptr)
	{
		if (RoadSectionSelectionState->GetSelectedSectionIndex() != INDEX_NONE
			&& RoadSectionSelectionState->GetSelectedSectionIndex() >= SplineComp->GetLaneSectionsNum())
		{
			EndEditing();
			return false;
		}
	}

	if (Event == IE_Pressed)
	{
		// Section split/lane commands only — ToggleSectionLineEdit is LevelEditor-only (no double-toggle).
		if (SectionComponentVisualizerActions->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false))
		{
			return true;
		}

		// Ctrl+LMB: add width key on lane boundary (Alt+T Width mode).
		if (FMetaLandRoadModule::Get().IsLaneEditMode()
			&& Key == EKeys::LeftMouseButton
			&& FSlateApplication::Get().GetModifierKeys().IsControlDown())
		{
			OnAddWidthKey();
			return true;
		}

		// LMB: boundary select / click-empty clears highlight.
		if (IsSectionLineEditMode() && Key == EKeys::LeftMouseButton && Viewport && ViewportClient
			&& !FSlateApplication::Get().GetModifierKeys().IsAltDown()
			&& !FSlateApplication::Get().GetModifierKeys().IsControlDown())
		{
			if (SplineComp)
			{
				FIntPoint MousePos;
				Viewport->GetMousePos(MousePos);

				FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
					Viewport,
					ViewportClient->GetScene(),
					ViewportClient->EngineShowFlags));
				FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
				if (View)
				{
					int32 BoundarySection = INDEX_NONE;
					if (SplineComp->GetLaneSectionsNum() >= 2
						&& MetaLandRoadSectionComponentVisualizerPrivate::PickSectionBoundaryByScreenDistance(
							SplineComp, *View, MousePos, 48.0f, BoundarySection))
					{
						if (SelectSectionBoundaryForDrag(BoundarySection))
						{
							return true;
						}
					}

					HHitProxy* HitProxy = Viewport->GetHitProxy(MousePos.X, MousePos.Y);
					const bool bHitOurProxy = HitProxy
						&& (HitProxy->IsA(HMetaLandRoadSectionKeyVisProxy::StaticGetType())
							|| HitProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType())
							|| HitProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType()));
					int32 NearMainSection = INDEX_NONE;
					float NearMainKey = -1.0f;
					const bool bNearMain = FMetaLandRoadSectionHover::PickSectionAtMouse(
						SplineComp, *View, MousePos, NearMainSection, NearMainKey, 56.0f);
					if (bNearMain && BoundarySection == INDEX_NONE)
					{
						if (SelectMainLineForPathDrag(NearMainSection, NearMainKey))
						{
							return true;
						}
					}
					if (!bHitOurProxy && BoundarySection == INDEX_NONE && !bNearMain)
					{
						ClearSectionHighlight();
						FMetaLandRoadSectionHover::ClearHoverOverlay(SplineComp);
						FMetaLandRoadSectionHover::GetLastHoveredBoundarySection() = INDEX_NONE;
						FMetaLandRoadSectionHover::GetLastHoveredSection() = INDEX_NONE;
						FMetaLandRoadSectionHover::GetLastHoveredSplineKey() = -1.0f;
						GEditor->RedrawLevelEditingViewports(true);
						return true;
					}
				}
			}
		}
	}

	// Alt+E: custom input only.
	if (IsSectionLineEditMode())
	{
		return false;
	}

	if (FMetaLandRoadModule::Get().IsLaneEditMode())
	{
		return false;
	}

	return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleRoadSplinePickClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click,
	bool bShowContextMenu)
{
	if (!InViewportClient)
	{
		return false;
	}

	// Alt+W native RMB goes through ComponentVisManager hit proxies — not this screen-pick path.
	if (FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay() && !IsSectionLineEditMode())
	{
		return false;
	}

	if (!IsSectionLineEditMode())
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();

	if (!SplineComp && HitProxy)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			const HActor* ActorProxy = static_cast<const HActor*>(HitProxy);
			if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActorProxy->Actor))
			{
				SplineComp = RoadActor->GetSplineComponent();
			}
			else if (AActor* HitActor = ActorProxy->Actor)
			{
				SplineComp = HitActor->FindComponentByClass<UMetaLandRoadSplineComponent>();
			}
		}
		else if (const HComponentVisProxy* ComponentProxy = HitProxyCast<HComponentVisProxy>(HitProxy))
		{
			if (const UActorComponent* HitComponent = ComponentProxy->Component.Get())
			{
				if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(HitComponent->GetOwner()))
				{
					SplineComp = RoadActor->GetSplineComponent();
				}
			}
		}
	}

	if (!SplineComp || !InViewportClient)
	{
		return false;
	}

	EnsureDefaultProfileIfNeeded(SplineComp);

	if (SplineComp->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	// In section-line mode, engine key proxies keep native edit; section line uses our proxies.
	if (HitProxy && MetaLandRoadSectionComponentVisualizerPrivate::IsEngineSplineEditProxy(HitProxy)
		&& Click.GetKey() != EKeys::RightMouseButton)
	{
		return false;
	}

	if (HitProxy && HitProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType()))
	{
		HMetaLandRoadLaneVisProxy* LaneProxy = static_cast<HMetaLandRoadLaneVisProxy*>(HitProxy);
		if (LaneProxy->Component.Get() == SplineComp)
		{
			if (VisProxyHandleClick(InViewportClient, LaneProxy, Click))
			{
				return FinishPickContextMenu(InViewportClient, bShowContextMenu);
			}
		}
	}

	if (FViewport* Viewport = InViewportClient->Viewport)
	{
		const FIntPoint ClickPos = Click.GetClickPos();
		if (HMetaLandRoadSectionKeyVisProxy* SectionKeyProxy = CompVisUtils::GetHitProxy<HMetaLandRoadSectionKeyVisProxy>(Viewport, ClickPos.X, ClickPos.Y, 48))
		{
			if (SectionKeyProxy->Component.Get() == SplineComp)
			{
				if (VisProxyHandleClick(InViewportClient, SectionKeyProxy, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}

		if (HMetaLandRoadLaneVisProxy* LaneProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneVisProxy>(Viewport, ClickPos.X, ClickPos.Y, 40))
		{
			if (LaneProxy->Component.Get() == SplineComp)
			{
				if (VisProxyHandleClick(InViewportClient, LaneProxy, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewportClient->Viewport,
		InViewportClient->GetScene(),
		InViewportClient->EngineShowFlags));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	int32 BoundarySectionIndex = INDEX_NONE;
	if (MetaLandRoadSectionComponentVisualizerPrivate::PickSectionBoundaryByScreenDistance(
		SplineComp,
		*View,
		Click.GetClickPos(),
		40.0f,
		BoundarySectionIndex))
	{
		const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionBoundary", "Select Road Section Boundary"));
		RoadSectionSelectionState->Modify();

		HMetaLandRoadSectionKeyVisProxy SectionKeyProxy(SplineComp, BoundarySectionIndex, HPP_UI);
		if (!UpdateSelectedComponentAndSectionAndLane(&SectionKeyProxy))
		{
			return false;
		}

		RoadSectionSelectionState->SetSelectedSection(BoundarySectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(BoundarySectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
		ActivateRoadVisualization();
		GEditor->RedrawLevelEditingViewports(true);
		// Boundary pick: select/drag only — no Add Section menu.
		return true;
	}

	int32 SectionIndex = INDEX_NONE;
	float SplineKey = 0.0f;
	if (!FMetaLandRoadSectionHover::PickSectionAtMouse(
		SplineComp,
		*View,
		Click.GetClickPos(),
		SectionIndex,
		SplineKey,
		56.0f))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionLane", "Select Road Lane"));
	RoadSectionSelectionState->Modify();

	HMetaLandRoadLaneVisProxy LaneProxy(SplineComp, SectionIndex, LANE_INDEX_NONE, HPP_UI);
	if (!UpdateSelectedComponentAndSectionAndLane(&LaneProxy))
	{
		return false;
	}

	RoadSectionSelectionState->SetSelectedSection(SectionIndex);
	RoadSectionSelectionState->RecordPickSplineKey(SplineKey);
	RoadSectionSelectionState->SetSectionBoundarySelected(false);
	HoveredSectionIndex = SectionIndex;
	HoveredBoundarySectionIndex = INDEX_NONE;
	HoveredSplineKey = SplineKey;

	if (SelectionState)
	{
		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (SplinePropertyPath.IsValid())
		{
			SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		}
	}

	ActivateRoadVisualization();
	GEditor->RedrawLevelEditingViewports(true);
	return FinishPickContextMenu(InViewportClient, bShowContextMenu);
}

void FMoKuEditSplinesComponentVisualizer::ActivateRoadVisualization()
{
	if (!GUnrealEd || !GCurrentLevelEditingViewportClient)
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();

	if (SplineComp)
	{
		if (bSectionLineEditMode)
		{
			EnsureDefaultProfileIfNeeded(SplineComp);
		}

		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (SplinePropertyPath.IsValid() && SelectionState)
		{
			SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		}
		if (RoadSectionSelectionState && RoadSectionSelectionState->GetSelectedSpline() != SplineComp && SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
	}

	FEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	TSharedPtr<FComponentVisualizer> ComponentVis = StaticCastSharedPtr<FComponentVisualizer>(SharedThis(this).ToSharedPtr());
	GUnrealEd->ComponentVisManager.SetActiveComponentVis(ViewportClient, ComponentVis);
}

bool FMoKuEditSplinesComponentVisualizer::HasFocusOnSelectionBoundingBox(FBox& OutBoundingBox)
{
	return false;
}

bool FMoKuEditSplinesComponentVisualizer::HandleSnapTo(
	const bool bInAlign,
	const bool bInUseLineTrace,
	const bool bInUseBounds,
	const bool bInUsePivot,
	AActor* InDestination)
{
	return false;
}

void FMoKuEditSplinesComponentVisualizer::TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove)
{
	if (bInDidMove)
	{
		UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
		if (SplineComp
			&& SelectionState
			&& SelectionState->GetSelectedKeys().Num() > 0
			&& RoadSectionSelectionState
			&& !RoadSectionSelectionState->IsSectionBoundarySelected())
		{
			// Finished Alt+E main-line path drag — keep mesh/lines in sync.
			RefreshRoadMeshForSpline(SplineComp);
			MetaLandRoadLaneVisualization::RefreshLaneVisualization(SplineComp);
		}
		else if (SplineComp)
		{
			if (GetSelectionState()->GetState() == EMetaLandRoadSectionSelectionState::Section)
			{
				const EMetaLandRoadSectionSelectionState StateVerified = RoadSectionSelectionState->GetStateVerified();
				double SOffset = 0.0;
				bool bValid = false;
				if (StateVerified >= EMetaLandRoadSectionSelectionState::Section)
				{
					SOffset = SplineComp->GetLaneSection(RoadSectionSelectionState->GetSelectedSectionIndex()).SOffset;
					bValid = true;
				}

				SplineComp->TrimLaneSections();
				RoadSectionSelectionState->FixState();

				if (bValid)
				{
					const int32 BestSectionIndex = CompVisUtils::FindBestFit(SplineComp->GetLaneSections(), [SOffset](const FMetaLandRoadLaneSection& It)
					{
						return FMath::Abs(SOffset - It.SOffset);
					});
					RoadSectionSelectionState->SetSelectedSection(BestSectionIndex);
					RoadSectionSelectionState->SetCachedDataAtSectionStart(BestSectionIndex);
				}
			}
			else
			{
				SplineComp->TrimLaneSections();
			}

			SplineComp->GetRoadLayoutMutable().UpdateLayoutVersion();
			SplineComp->UpdateRoadLayout();
			RefreshRoadMeshAndRedraw(SplineComp);
			SplineComp->MarkRenderStateDirty();
		}
	}

	FSplineComponentVisualizer::TrackingStopped(InViewportClient, bInDidMove);
}

void FMoKuEditSplinesComponentVisualizer::EndEditing()
{
	if (!GIsTransacting && IsValid(RoadSectionSelectionState))
	{
		RoadSectionSelectionState->ResetSelection(false);
	}
	FSplineComponentVisualizer::EndEditing();
}

void FMoKuEditSplinesComponentVisualizer::OnSplitSectionSide(EMetaLandRoadLaneSectionSide Side)
{
	TrySplitSectionSide(Side, -1.0f);
}

int32 FMoKuEditSplinesComponentVisualizer::TrySplitSectionSide(EMetaLandRoadLaneSectionSide Side, float OverrideSplitKey)
{
	if (!bSectionLineEditMode)
	{
		SetSectionLineEditMode(true);
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return INDEX_NONE;
	}

	EnsureDefaultProfileIfNeeded(SplineComp);

	// Ensure a section is selected so the new span can highlight after split.
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		FComponentPropertyPath Path(SplineComp);
		if (Path.IsValid() && RoadSectionSelectionState->GetSelectedSpline() != SplineComp)
		{
			RoadSectionSelectionState->SetSelectedSpline(Path);
		}
		RoadSectionSelectionState->SetSelectedSection(0);
	}

	float SplitKey = OverrideSplitKey;
	if (SplitKey < 0.0f)
	{
		const float HoverKey = HoveredSplineKey >= 0.0f
			? HoveredSplineKey
			: FMetaLandRoadSectionHover::GetLastHoveredSplineKey();
		SplitKey = MetaLandRoadSectionComponentVisualizerPrivate::ResolveSplitSplineKey(
			SplineComp,
			RoadSectionSelectionState,
			HoverKey);
	}

	if (SplitKey < 0.0f)
	{
		return INDEX_NONE;
	}

	const FScopedTransaction Transaction(LOCTEXT("SplitSection", "Split Section"));
	SplineComp->Modify();

	int32 NewSectionIndex = INDEX_NONE;
	switch (Side)
	{
	case EMetaLandRoadLaneSectionSide::Left:
		NewSectionIndex = SplineComp->EditorAddLeftSection(SplitKey);
		break;
	case EMetaLandRoadLaneSectionSide::Right:
		NewSectionIndex = SplineComp->EditorAddRightSection(SplitKey);
		break;
	default:
		NewSectionIndex = SplineComp->SplitSection(SplitKey, EMetaLandRoadLaneSectionSide::Both);
		break;
	}

	if (NewSectionIndex != INDEX_NONE)
	{
		if (SelectionState)
		{
			SelectionState->ClearSelectedKeys();
		}
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->SetSelectedSection(NewSectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(NewSectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
		ActivateRoadVisualization();
		RefreshRoadMeshAndRedraw(SplineComp);
	}

	return NewSectionIndex;
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteSection()
{
	if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section)
	{
		const FScopedTransaction Transaction(LOCTEXT("DeleteSection", "Delete Section"));

		UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
		SplineComp->Modify();
		SplineComp->GetLaneSectionsMutable().RemoveAt(RoadSectionSelectionState->GetSelectedSectionIndex());
		SplineComp->UpdateRoadLayout();
		SplineComp->UpdateLaneSectionBounds();
		SplineComp->TrimLaneSections();
		RefreshRoadMeshAndRedraw(SplineComp);
		SplineComp->MarkRenderStateDirty();

		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->SetSelectedLane(LANE_INDEX_NONE);
	}
}

void FMoKuEditSplinesComponentVisualizer::OnAddLane(bool bOnLeft)
{
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddLane", "Add Lane"));

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	SplineComp->Modify();

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
， 
		LaneIndex = CachedLaneHoveredLaneIndex;
	}
	if (LaneIndex == LANE_INDEX_NONE)
	{
		return;
	}

	FMetaLandRoadLaneSection& SelectedSection = SplineComp->GetLaneSection(SectionIndex);
	FMetaLandRoadLane& SelectedLane = SelectedSection.GetLaneByIndex(LaneIndex);

	if (SelectedLane.Width.GetNumKeys() == 0)
	{
		SelectedLane.Width.AddKey(0.0f, SelectedLane.Width.GetDefaultValue());
	}

	const double S0 = SplineComp->GetDistanceAlongSplineAtSplineInputKey(RoadSectionSelectionState->GetCachedSplineKey());
	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range = SplineComp->GetLaneRange(SectionIndex, LaneIndex);
	const double S = S0 - Range.StartS;

	if (S0 < Range.StartS || S0 > Range.EndS)
	{
		return;
	}

	if (LaneWidthCurveContainsKeyAtS(SelectedLane.Width, S))
	{
		return;
	}

	int32 KeyIndex = 0;
	for (int32 Index = 1; Index < SelectedLane.Width.GetNumKeys(); ++Index)
	{
		if (S > SelectedLane.Width.Keys[Index].Time)
		{
			KeyIndex = Index;
		}
		else
		{
			break;
		}
	}

	FRichCurveKey NewKey = SelectedLane.Width.Keys[KeyIndex];
	NewKey.Time = static_cast<float>(S);
	NewKey.InterpMode = ERichCurveInterpMode::RCIM_Cubic;
	NewKey.TangentMode = ERichCurveTangentMode::RCTM_Auto;
	SelectedLane.Width.Keys.Insert(NewKey, KeyIndex + 1);
	SelectedLane.Width.AutoSetTangents();

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->SetSelectedLane(LaneIndex);
	RoadSectionSelectionState->SetSelectedKeyIndex(KeyIndex + 1);
	const double KeyS = SelectedSection.SOffset + SelectedLane.Width.Keys[KeyIndex + 1].Time;
	RoadSectionSelectionState->SetCachedDataAtLane(SectionIndex, LaneIndex, KeyS, 1.0);

	SplineComp->UpdateRoadLayout();
	RefreshRoadMeshAndRedraw(SplineComp);
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteWidthKey()
{
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Key)
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteWidthKey", "Delete Width Key"));
	SplineComp->Modify();

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	const int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
	const int32 KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
	FMetaLandRoadLane& SelectedLane = SplineComp->GetLaneSection(SectionIndex).GetLaneByIndex(LaneIndex);

	if (SelectedLane.Width.Keys.Num() > 1)
	{
		SelectedLane.Width.Keys.RemoveAt(KeyIndex);
		SelectedLane.Width.AutoSetTangents();
	}

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->SetSelectedKeyIndex(INDEX_NONE);

	SplineComp->UpdateRoadLayout();
	RefreshRoadMeshAndRedraw(SplineComp);
}

void FMoKuEditSplinesComponentVisualizer::GenerateLaneWidthContextMenuSections(FMenuBuilder& InMenuBuilder) const
{
	if (!FMetaLandRoadModule::Get().IsLaneEditMode() || !RoadSectionSelectionState)
	{
		return;
	}

	const EMetaLandRoadSectionSelectionState State = RoadSectionSelectionState->GetStateVerified();
	if (State < EMetaLandRoadSectionSelectionState::Section)
	{
		return;
	}

	const FMetaLandRoadSectionComponentVisualizerCommands& Commands = FMetaLandRoadSectionComponentVisualizerCommands::Get();
	InMenuBuilder.BeginSection("MetaLandRoadLaneEdit", LOCTEXT("ContextMenuRoadLane", "Lane"));
	InMenuBuilder.AddMenuEntry(Commands.AddLaneToLeft);
	InMenuBuilder.AddMenuEntry(Commands.AddLaneToRight);
	InMenuBuilder.AddMenuEntry(Commands.DeleteLane);
	InMenuBuilder.EndSection();
	InMenuBuilder.BeginSection("MetaLandRoadLaneWidth", LOCTEXT("ContextMenuRoadWidth", "Width"));
	InMenuBuilder.AddMenuEntry(Commands.AddWidthKey);
	InMenuBuilder.AddMenuEntry(Commands.DeleteWidthKey);
	InMenuBuilder.EndSection();
}

bool FMoKuEditSplinesComponentVisualizer::HasMainLineSectionMenuContext() const
{
	if (!bSectionLineEditMode)
	{
		return false;
	}

	// Hovering the center main line (not a boundary cross).
	if (HoveredSectionIndex != INDEX_NONE && HoveredBoundarySectionIndex == INDEX_NONE)
	{
		return true;
	}

	// Selected section span (not boundary-only SOffset pick).
	if (RoadSectionSelectionState
		&& RoadSectionSelectionState->GetSelectedSectionIndex() != INDEX_NONE
		&& !RoadSectionSelectionState->IsSectionBoundarySelected())
	{
		return true;
	}

	return false;
}

TSharedPtr<SWidget> FMoKuEditSplinesComponentVisualizer::GenerateContextMenu() const
{
	if (FMetaLandRoadModule::Get().IsLaneEditMode())
	{
		FMenuBuilder LaneMenu(true, SectionComponentVisualizerActions);
		GenerateLaneWidthContextMenuSections(LaneMenu);
		return LaneMenu.MakeWidget();
	}

	if (IsSectionLineEditMode())
	{
		if (!HasMainLineSectionMenuContext())
		{
			return nullptr;
		}

		FMenuBuilder SectionMenu(true, SectionComponentVisualizerActions);
		GenerateContextMenuSections(SectionMenu);
		return SectionMenu.MakeWidget();
	}

	// Alt+W: identical to engine FSplineComponentVisualizer (Add/Delete/Duplicate Point…).
	return FSplineComponentVisualizer::GenerateContextMenu();
}

void FMoKuEditSplinesComponentVisualizer::GenerateContextMenuSections(FMenuBuilder& InMenuBuilder) const
{
	if (!bSectionLineEditMode)
	{
		// Must forward — FSplineComponentVisualizer::GenerateContextMenu() calls this virtually.
		FSplineComponentVisualizer::GenerateContextMenuSections(InMenuBuilder);
		return;
	}

	if (!HasMainLineSectionMenuContext())
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}

	check(RoadSectionSelectionState);
	if (SplineComp->GetLaneSectionsNum() == 0)
	{
		return;
	}
	auto MakeSectionAction = [this](TFunction<void()> ExecuteFn, TFunction<bool()> CanExecuteFn)
	{
		return FUIAction(
			FExecuteAction::CreateLambda([ExecuteFn]() { ExecuteFn(); }),
			FCanExecuteAction::CreateLambda([CanExecuteFn]() { return CanExecuteFn(); }));
	};

	const auto CanEditSection = [this]()
	{
		return this->bSectionLineEditMode && this->HasMainLineSectionMenuContext();
	};

	InMenuBuilder.BeginSection("MetaLandRoadSection", LOCTEXT("ContextMenuRoadSection", "Road Section"));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("SplitFullSection", "Add Section (Both)"),
		LOCTEXT("SplitFullSectionTooltip", "Split left and right faces together at the pick on the center line."),
		FSlateIcon(),
		MakeSectionAction(
			[this]() { const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->TrySplitSectionSide(EMetaLandRoadLaneSectionSide::Both); },
			CanEditSection));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("SplitLeftSection", "Add Left Section"),
		LOCTEXT("SplitLeftSectionTooltip", "Subdivide the left face only at the pick on the center line."),
		FSlateIcon(FMetaLandRoadStyle::GetStyleSetName(), "MetaLandRoadEditorModeCommands.InteractiveTool"),
		MakeSectionAction(
			[this]() { const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->TrySplitSectionSide(EMetaLandRoadLaneSectionSide::Left); },
			CanEditSection));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("SplitRightSection", "Add Right Section"),
		LOCTEXT("SplitRightSectionTooltip", "Subdivide the right face only at the pick on the center line."),
		FSlateIcon(FMetaLandRoadStyle::GetStyleSetName(), "MetaLandRoadEditorModeCommands.InteractiveTool"),
		MakeSectionAction(
			[this]() { const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->TrySplitSectionSide(EMetaLandRoadLaneSectionSide::Right); },
			CanEditSection));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteSection", "Delete Section"),
		LOCTEXT("DeleteSectionTooltip", "Delete current road section."),
		FSlateIcon(),
		MakeSectionAction(
			[this]() { const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->OnDeleteSection(); },
			CanEditSection));
	InMenuBuilder.EndSection();
	// Alt+E: section ops only — no Lane / Add Point (points = Alt+W).
}

void FMoKuEditSplinesComponentVisualizer::OnSetRoadSplinePointType(EMetaLandRoadSplinePointType Mode)
{
	const FScopedTransaction Transaction(LOCTEXT("SetRoadSplinePointType", "Set Spline Point Type"));

	UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(GetEditedSplineComponent());
	if (!SplineComp)
	{
		return;
	}

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	int32 LastEditedPointIndex = INDEX_NONE;
	const TSet<int32>& SelectedKeys = GetSelectedKeys();
	for (int32 SelectedKeyIndex : SelectedKeys)
	{
		if (SelectedKeyIndex >= 0 && SelectedKeyIndex < SplineComp->GetNumberOfSplinePoints())
		{
			SplineComp->SetRoadSplinePointType(SelectedKeyIndex, Mode, false);
			LastEditedPointIndex = SelectedKeyIndex;
		}
	}

	SplineComp->UpdateSpline(LastEditedPointIndex);
	SplineComp->TrimLaneSections();
	SplineComp->bSplineHasBeenEdited = true;
	GEditor->RedrawLevelEditingViewports(true);
}

bool FMoKuEditSplinesComponentVisualizer::IsRoadSplinePointTypeSet(EMetaLandRoadSplinePointType Mode) const
{
	if (!IsKeySelectionValid())
	{
		return false;
	}

	const UMetaLandRoadSplineComponent* SplineComp = Cast<const UMetaLandRoadSplineComponent>(GetEditedSplineComponent());
	if (!SplineComp)
	{
		return false;
	}

	for (int32 SelectedKeyIndex : GetSelectedKeys())
	{
		if (SelectedKeyIndex >= 0
			&& SelectedKeyIndex < SplineComp->GetNumberOfSplinePoints()
			&& SplineComp->GetRoadSplinePointType(SelectedKeyIndex) == Mode)
		{
			return true;
		}
	}

	return false;
}

void FMoKuEditSplinesComponentVisualizer::GenerateRoadSplinePointTypeSubMenu(FMenuBuilder& MenuBuilder) const
{
	const FMetaLandRoadSectionComponentVisualizerCommands& Commands = FMetaLandRoadSectionComponentVisualizerCommands::Get();

	MenuBuilder.AddMenuEntry(Commands.SetKeyToCurve);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToCurveCustomTangent);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToCurveClamped);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToLinear);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToConstant);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToArc);
}

FTSTicker::FDelegateHandle FMoKuEditSplinesComponentVisualizer::EditorViewportTickerHandle;

static FVector RaiseNativeEditPoint(const UMetaLandRoadSplineComponent* RoadSpline, const FVector& WorldPos)
{
	FVector Raised = WorldPos;
	Raised.Z += FMoKuEditSplinesComponentVisualizer::GetNativeEditHeightOffset(RoadSpline);
	return Raised;
}

float FMoKuEditSplinesComponentVisualizer::GetNativeEditHeightOffset(const UMetaLandRoadSplineComponent* RoadSpline)
{
	// Alt+W uses engine FSplineComponentVisualizer hit proxies at the real spline height — no extra lift.
	(void)RoadSpline;
	return 0.0f;
}

float FMoKuEditSplinesComponentVisualizer::GetNativeEditRingWorldRadius(
	const FSceneView* View,
	const FVector& CenterWorld,
	const FVector& RingAxisX,
	const FVector& RingAxisY,
	float DesiredPixelRadius)
{
	if (!View || DesiredPixelRadius <= 0.0f)
	{
		return 18.0f;
	}

	FVector2D CenterPx;
	if (!View->WorldToPixel(CenterWorld, CenterPx))
	{
		return 18.0f;
	}

	const float ProbeCm = 10.0f;
	const FVector AxisX = RingAxisX.GetSafeNormal();
	const FVector AxisY = RingAxisY.GetSafeNormal();
	FVector2D ProbePx;
	const float PixelsPerCmX = View->WorldToPixel(CenterWorld + AxisX * ProbeCm, ProbePx)
		? FVector2D::Distance(CenterPx, ProbePx) / ProbeCm : 0.0f;
	const float PixelsPerCmY = View->WorldToPixel(CenterWorld + AxisY * ProbeCm, ProbePx)
		? FVector2D::Distance(CenterPx, ProbePx) / ProbeCm : 0.0f;
	const float PixelsPerCm = FMath::Max(FMath::Max(PixelsPerCmX, PixelsPerCmY), KINDA_SMALL_NUMBER);
	return DesiredPixelRadius / PixelsPerCm;
}

void FMoKuEditSplinesComponentVisualizer::EnsureNativeEditTranslateWidget(FEditorViewportClient* ViewportClient)
{
	FEditorViewportClient* Client = ViewportClient ? ViewportClient : GCurrentLevelEditingViewportClient;
	if (Client)
	{
		Client->SetWidgetMode(UE::Widget::WM_Translate);
	}
}

int32 FMoKuEditSplinesComponentVisualizer::PickHoveredControlPointKey(
	const UMetaLandRoadSplineComponent* RoadSpline,
	const FSceneView* View,
	const FIntPoint& MousePos,
	float DesiredHoverPixelRadius)
{
	if (!RoadSpline || !View || MousePos.X == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const int32 NumPoints = RoadSpline->GetNumberOfSplinePoints();
	float BestDistSq = TNumericLimits<float>::Max();
	int32 BestKey = INDEX_NONE;
	const FVector2D Mouse(static_cast<float>(MousePos.X), static_cast<float>(MousePos.Y));
	const float HoverPixelRadius = FMath::Max(DesiredHoverPixelRadius, 22.0f);

	for (int32 KeyIdx = 0; KeyIdx < NumPoints; ++KeyIdx)
	{
		const FVector KeyWorld = RaiseNativeEditPoint(
			RoadSpline,
			RoadSpline->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World));
		FVector2D Pixel;
		if (!View->WorldToPixel(KeyWorld, Pixel))
		{
			continue;
		}
		const float DistSq = FVector2D::DistSquared(Mouse, Pixel);
		if (DistSq <= HoverPixelRadius * HoverPixelRadius && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestKey = KeyIdx;
		}
	}
	return BestKey;
}

int32 FMoKuEditSplinesComponentVisualizer::PickHoveredSplineSegment(
	const UMetaLandRoadSplineComponent* RoadSpline,
	const FSceneView* View,
	const FIntPoint& MousePos,
	float MaxPixelDistance)
{
	if (!RoadSpline || !View || MousePos.X == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const int32 NumSegments = RoadSpline->GetNumberOfSplineSegments();
	if (NumSegments <= 0)
	{
		return INDEX_NONE;
	}

	const FVector2D Mouse(static_cast<float>(MousePos.X), static_cast<float>(MousePos.Y));
	const float BestDistSqLimit = MaxPixelDistance * MaxPixelDistance;
	float BestDistSq = BestDistSqLimit;
	int32 BestSegment = INDEX_NONE;
	constexpr int32 NumSubdivisions = 24;

	for (int32 SegIdx = 0; SegIdx < NumSegments; ++SegIdx)
	{
		FVector PrevPos = RaiseNativeEditPoint(
			RoadSpline,
			RoadSpline->GetLocationAtSplineInputKey(static_cast<float>(SegIdx), ESplineCoordinateSpace::World));
		for (int32 Step = 1; Step <= NumSubdivisions; ++Step)
		{
			const float Key = static_cast<float>(SegIdx) + static_cast<float>(Step) / static_cast<float>(NumSubdivisions);
			FVector CurPos = RaiseNativeEditPoint(
				RoadSpline,
				RoadSpline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World));

			FVector2D ScreenA;
			FVector2D ScreenB;
			if (View->WorldToPixel(PrevPos, ScreenA) && View->WorldToPixel(CurPos, ScreenB))
			{
				const FVector2D Closest = FMath::ClosestPointOnSegment2D(Mouse, ScreenA, ScreenB);
				const float DistSq = FVector2D::DistSquared(Mouse, Closest);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestSegment = SegIdx;
				}
			}
			PrevPos = CurPos;
		}
	}

	return BestSegment;
}

static void GetNativeEditRingAxes(
	const UMetaLandRoadSplineComponent* RoadSpline,
	int32 KeyIdx,
	FVector& OutAxisX,
	FVector& OutAxisY)
{
	// Flat ring on the spline plane (Right x Tangent), not camera-facing.
	OutAxisX = RoadSpline->GetRightVectorAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World).GetSafeNormal();
	OutAxisY = RoadSpline->GetDirectionAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World).GetSafeNormal();
}

static void DrawNativeEditControlPointRingPDI(
	FPrimitiveDrawInterface* PDI,
	const FVector& Center,
	const FVector& RingAxisX,
	const FVector& RingAxisY,
	float WorldRadius,
	bool bSelected,
	bool bHovered)
{
	if (!PDI || WorldRadius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FLinearColor Pink = FLinearColor(FMetaLandRoadEditorColors::SplineColor);
	const FLinearColor Yellow = FLinearColor::Yellow;
	const bool bHighlight = bSelected || bHovered;
	const FLinearColor RingColor = bHighlight ? Yellow : Pink;
	const FLinearColor GlowColor = bHighlight
		? FLinearColor(1.0f, 0.9f, 0.0f, 0.55f)
		: FLinearColor(Pink.R, Pink.G, Pink.B, 0.35f);
	const float RingThickness = bHighlight ? 3.0f : 2.0f;
	const float GlowRadiusScale = bHighlight ? 1.2f : 1.08f;

	const FVector XAxis = RingAxisX.GetSafeNormal();
	const FVector YAxis = RingAxisY.GetSafeNormal();

	DrawCircle(
		PDI,
		Center,
		XAxis,
		YAxis,
		GlowColor,
		WorldRadius * GlowRadiusScale,
		32,
		SDPG_Foreground,
		bHighlight ? 2.0f : 1.25f,
		0.0f,
		true);
	DrawCircle(
		PDI,
		Center,
		XAxis,
		YAxis,
		RingColor,
		WorldRadius,
		32,
		SDPG_Foreground,
		RingThickness,
		0.0f,
		true);
}

void FMoKuEditSplinesComponentVisualizer::DrawNativeEditSplineOverlay(
	const UMetaLandRoadSplineComponent* RoadSpline,
	FPrimitiveDrawInterface* PDI,
	int32 HoveredKey,
	int32 HoveredSegment,
	int32 SelectedSegment,
	const TSet<int32>& SelectedKeys)
{
	if (!RoadSpline || !PDI)
	{
		return;
	}

	const int32 NumPoints = RoadSpline->GetNumberOfSplinePoints();
	if (NumPoints < 1)
	{
		return;
	}

	const int32 NumSegments = RoadSpline->GetNumberOfSplineSegments();
	UMetaLandRoadSplineComponent* MutableSpline = const_cast<UMetaLandRoadSplineComponent*>(RoadSpline);

	const FLinearColor Pink = FLinearColor(FMetaLandRoadEditorColors::SplineColor);
	const FLinearColor Yellow = FLinearColor::Yellow;
	const float NormalThickness = 4.0f;
	const float HighlightThickness = 6.0f;
	constexpr int32 NumSteps = 16;

	for (int32 SegIdx = 0; SegIdx < NumSegments; ++SegIdx)
	{
		const int32 EndKey = (RoadSpline->IsClosedLoop() && SegIdx == NumSegments - 1)
			? 0
			: SegIdx + 1;
		const bool bSegHovered = HoveredKey == INDEX_NONE && HoveredSegment == SegIdx;
		const bool bSegSelected = SelectedSegment == SegIdx;
		// Keys (rings) own endpoint feedback — do not yellow the spine when a point is selected/hovered.
		const bool bHighlight = bSegHovered || bSegSelected;
		const FLinearColor SegColor = bHighlight ? Yellow : Pink;
		const float SegThickness = bHighlight ? HighlightThickness : NormalThickness;

		if (PDI->IsHitTesting())
		{
			PDI->SetHitProxy(new HMetaLandRoadSplineSegmentVisProxy(MutableSpline, SegIdx, HPP_Foreground));
		}

		FVector OldPos = RoadSpline->GetLocationAtSplineInputKey(static_cast<float>(SegIdx), ESplineCoordinateSpace::World);
		for (int32 StepIdx = 1; StepIdx <= NumSteps; ++StepIdx)
		{
			const float Key = static_cast<float>(SegIdx) + static_cast<float>(StepIdx) / static_cast<float>(NumSteps);
			const FVector NewPos = RoadSpline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
			PDI->DrawLine(OldPos, NewPos, SegColor, SDPG_Foreground, SegThickness, 0.0f, true);
			OldPos = NewPos;
		}

		if (PDI->IsHitTesting())
		{
			PDI->SetHitProxy(nullptr);
		}
	}

	// Invisible pick discs at keys — rings are drawn separately (raised to match ring overlay).
	const float KeyHitSize = 28.0f + GetDefault<ULevelEditorViewportSettings>()->SelectedSplinePointSizeAdjustment;
	for (int32 KeyIdx = 0; KeyIdx < NumPoints; ++KeyIdx)
	{
		const FVector KeyPos = RaiseNativeEditPoint(
			RoadSpline,
			RoadSpline->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World));
		if (PDI->IsHitTesting())
		{
			PDI->SetHitProxy(new HMetaLandRoadSplineKeyVisProxy(MutableSpline, KeyIdx, HPP_Foreground));
			PDI->DrawPoint(KeyPos, FLinearColor(FMetaLandRoadEditorColors::SplineColor), KeyHitSize, SDPG_Foreground);
			PDI->SetHitProxy(nullptr);
		}
	}
}

void FMoKuEditSplinesComponentVisualizer::DrawNativeEditRingOverlay(
	const FSceneView* View,
	int32 HoveredKey,
	const TSet<int32>& SelectedKeys,
	int32 SelectedSegment,
	const UMetaLandRoadSplineComponent* RoadSpline,
	FPrimitiveDrawInterface* PDI)
{
	if (!RoadSpline || !PDI)
	{
		return;
	}

	const bool bSegmentSelected = SelectedSegment != INDEX_NONE;

	// Alt+W: draw every key ring immediately; hover / select only change highlight.
	const int32 NumPoints = RoadSpline->GetNumberOfSplinePoints();
	for (int32 KeyIdx = 0; KeyIdx < NumPoints; ++KeyIdx)
	{
		const bool bSelected = !bSegmentSelected && SelectedKeys.Contains(KeyIdx);
		const bool bHovered = !bSegmentSelected && HoveredKey == KeyIdx;

		const FVector KeyPos = RaiseNativeEditPoint(
			RoadSpline,
			RoadSpline->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World));

		FVector RingAxisX = FVector::ForwardVector;
		FVector RingAxisY = FVector::RightVector;
		GetNativeEditRingAxes(RoadSpline, KeyIdx, RingAxisX, RingAxisY);

		const float DesiredPx = bSelected ? 24.0f : (bHovered ? 22.0f : 18.0f);
		const float RingRadius = View
			? GetNativeEditRingWorldRadius(View, KeyPos, RingAxisX, RingAxisY, DesiredPx)
			: 24.0f;
		DrawNativeEditControlPointRingPDI(PDI, KeyPos, RingAxisX, RingAxisY, RingRadius, bSelected, bHovered);
	}
}

bool FMoKuEditSplinesComponentVisualizer::FinishPickContextMenu(
	FEditorViewportClient* ViewportClient,
	bool bShowContextMenu) const
{
	if (!bShowContextMenu)
	{
		return true;
	}
	if (!ViewportClient)
	{
		return false;
	}

	// Add Section menu only when the main center line is highlighted / selected.
	if (IsSectionLineEditMode() && !HasMainLineSectionMenuContext())
	{
		return true;
	}

	const TSharedPtr<SWidget> MenuWidget = GenerateContextMenu();
	const TSharedPtr<SEditorViewport> ViewportWidget = ViewportClient->GetEditorViewportWidget();
	if (!MenuWidget.IsValid() || !ViewportWidget.IsValid())
	{
		return false;
	}

	FSlateApplication::Get().PushMenu(
		ViewportWidget.ToSharedRef(),
		FWidgetPath(),
		MenuWidget.ToSharedRef(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	return true;
}

void FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		StopEditorViewportTick();
		return;
	}

	if (FMetaLandRoadModule::Get().IsSectionMode() || FMetaLandRoadModule::Get().IsLaneEditMode() || FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
	{
		StartEditorViewportTick();
		return;
	}

	StopEditorViewportTick();
}

void FMoKuEditSplinesComponentVisualizer::StartEditorViewportTick()
{
	if (EditorViewportTickerHandle.IsValid() || !GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		return;
	}
	EditorViewportTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(
			GMoKuEditSplinesComponentVisualizer.ToSharedRef(),
			&FMoKuEditSplinesComponentVisualizer::TickEditorViewport),
		0.033f);
}

void FMoKuEditSplinesComponentVisualizer::StopEditorViewportTick()
{
	if (EditorViewportTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorViewportTickerHandle);
		EditorViewportTickerHandle.Reset();
	}
}

bool FMoKuEditSplinesComponentVisualizer::TickEditorViewport(float /*DeltaTime*/)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		return true;
	}

	if (FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay())
	{
		TickNativeEditHover();
		return true;
	}

	if (FMetaLandRoadModule::Get().IsLaneEditMode())
	{
		TickLaneEditHover();
		return true;
	}

	if (FMetaLandRoadModule::Get().IsSectionMode())
	{
		return FMetaLandRoadSectionHover::Tick(0.0f);
	}

	return true;
}

void FMoKuEditSplinesComponentVisualizer::TickLaneEditHover()
{
	const int32 PrevSection = CachedLaneHoveredSectionIndex;
	const int32 PrevLane = CachedLaneHoveredLaneIndex;
	const int32 PrevWidth = CachedLaneHoveredWidthIndex;

	CachedLaneHoveredSectionIndex = INDEX_NONE;
	CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	CachedLaneHoveredWidthIndex = INDEX_NONE;

	if (!GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		if (GEditor && (PrevSection != INDEX_NONE || PrevLane != LANE_INDEX_NONE || PrevWidth != INDEX_NONE))
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	FViewport* Viewport = GCurrentLevelEditingViewportClient->Viewport;
	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);

	if (HMetaLandRoadLaneWidthKeyVisProxy* WidthKeyProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneWidthKeyVisProxy>(
		Viewport, MousePos.X, MousePos.Y, 48))
	{
		if (WidthKeyProxy->LaneIndex != LANE_INDEX_NONE
			&& Cast<UMetaLandRoadSplineComponent>(WidthKeyProxy->Component.Get()))
		{
			CachedLaneHoveredSectionIndex = WidthKeyProxy->SectionIndex;
			CachedLaneHoveredLaneIndex = WidthKeyProxy->LaneIndex;
			CachedLaneHoveredWidthIndex = WidthKeyProxy->WidthIndex;
		}
	}
	else if (HMetaLandRoadLaneWidthSegmentVisProxy* WidthSegProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneWidthSegmentVisProxy>(
		Viewport, MousePos.X, MousePos.Y, 48))
	{
		if (WidthSegProxy->LaneIndex != LANE_INDEX_NONE
			&& Cast<UMetaLandRoadSplineComponent>(WidthSegProxy->Component.Get()))
		{
			CachedLaneHoveredSectionIndex = WidthSegProxy->SectionIndex;
			CachedLaneHoveredLaneIndex = WidthSegProxy->LaneIndex;
			CachedLaneHoveredWidthIndex = WidthSegProxy->WidthIndex;
		}
	}

	if (GEditor && (PrevSection != CachedLaneHoveredSectionIndex
		|| PrevLane != CachedLaneHoveredLaneIndex
		|| PrevWidth != CachedLaneHoveredWidthIndex))
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void FMoKuEditSplinesComponentVisualizer::UpdateNativeHoverCacheFromViewport()
{
	if (!FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay() || IsSectionLineEditMode())
	{
		CachedNativeHoveredKey = INDEX_NONE;
		CachedNativeHoveredSegment = INDEX_NONE;
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = FMetaLandRoadModule::Get().GetActiveNativeEditSpline();
	if (!SplineComp || !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		CachedNativeHoveredKey = INDEX_NONE;
		CachedNativeHoveredSegment = INDEX_NONE;
		return;
	}

	FViewport* Viewport = GCurrentLevelEditingViewportClient->Viewport;
	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GCurrentLevelEditingViewportClient->GetScene(),
		GCurrentLevelEditingViewportClient->EngineShowFlags));
	FSceneView* View = GCurrentLevelEditingViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return;
	}

	CachedNativeHoveredKey = PickHoveredControlPointKey(SplineComp, View, MousePos);
	CachedNativeHoveredSegment = INDEX_NONE;
	if (CachedNativeHoveredKey == INDEX_NONE)
	{
		CachedNativeHoveredSegment = PickHoveredSplineSegment(SplineComp, View, MousePos, 48.0f);
	}
}

void FMoKuEditSplinesComponentVisualizer::TickNativeEditHover()
{
	const int32 PrevKey = CachedNativeHoveredKey;
	const int32 PrevSegment = CachedNativeHoveredSegment;
	UpdateNativeHoverCacheFromViewport();

	if (GEditor && (PrevKey != CachedNativeHoveredKey || PrevSegment != CachedNativeHoveredSegment))
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void FMoKuEditSplinesComponentVisualizer::SetActiveForSpline(UMetaLandRoadSplineComponent* SplineComp)
{
	if (!SplineComp || !GUnrealEd || !GCurrentLevelEditingViewportClient)
	{
		return;
	}

	FComponentPropertyPath Path(SplineComp);
	if (Path.IsValid() && SelectionState)
	{
		SelectionState->SetSplinePropertyPath(Path);
	}

	if (GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		TSharedPtr<FComponentVisualizer> Vis =
			StaticCastSharedPtr<FComponentVisualizer>(GMoKuEditSplinesComponentVisualizer);
		GUnrealEd->ComponentVisManager.SetActiveComponentVis(GCurrentLevelEditingViewportClient, Vis);
	}
}

void FMoKuEditSplinesComponentVisualizer::SyncEngineSelectionStateForSpline(
	const UMetaLandRoadSplineComponent* SplineComp)
{
	if (!SelectionState || !SplineComp)
	{
		return;
	}

	FComponentPropertyPath SplinePropertyPath(const_cast<UMetaLandRoadSplineComponent*>(SplineComp));
	if (SplinePropertyPath.IsValid()
		&& SelectionState->GetSplinePropertyPath().GetComponent() != SplineComp)
	{
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
	}
}

void FMoKuEditSplinesComponentVisualizer::ClearNativeKeySelection()
{
	if (!SelectionState)
	{
		return;
	}
	SelectionState->ClearSelectedKeys();
	SelectionState->ClearSelectedSegmentIndex();
	SelectionState->ClearSelectedTangentHandle();
	SelectionState->SetLastKeyIndexSelected(INDEX_NONE);
	CachedNativeHoveredKey = INDEX_NONE;
	CachedNativeHoveredSegment = INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
