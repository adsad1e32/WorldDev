#include "MetaLandRoadSectionEditVisualization.h"

#include "MetaLandRoadSectionHover.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadVisProxy.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "SceneManagement.h"
#include "Selection.h"

static float GetSplineVisualHeightOffset(const UMetaLandRoadSplineComponent* SplineComp)
{
	return SplineComp ? SplineComp->LaneVisualizationHeightOffset + 8.0f : 23.0f;
}

static FVector RaiseSplineVisualPoint(const UMetaLandRoadSplineComponent* SplineComp, const FVector& WorldLocation)
{
	return WorldLocation + FVector(0.0f, 0.0f, GetSplineVisualHeightOffset(SplineComp));
}

static bool ClipSectionDrawRangeToMesh(
	const UMetaLandRoadSplineComponent* SplineComp,
	double InS0,
	double InS1,
	double& OutS0,
	double& OutS1)
{
	OutS0 = InS0;
	OutS1 = InS1;
	if (InS1 <= InS0 + KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp ? SplineComp->GetOwner() : nullptr);
	if (!Road)
	{
		return true;
	}
	TArray<TPair<double, double>> Spans;
	Road->GetJunctionMeshGenerationSpans(Spans);
	if (Spans.Num() == 0)
	{
		return true;
	}
	double BestLen = -1.0;
	bool bFound = false;
	for (const TPair<double, double>& Span : Spans)
	{
		const double Clip0 = FMath::Max(InS0, Span.Key);
		const double Clip1 = FMath::Min(InS1, Span.Value);
		if (Clip1 <= Clip0 + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const double Len = Clip1 - Clip0;
		if (Len > BestLen)
		{
			BestLen = Len;
			OutS0 = Clip0;
			OutS1 = Clip1;
			bFound = true;
		}
	}
	return bFound;
}

/** Snap section boundary S onto asphalt tip when it sits in a junction trim gap. */
static bool ResolveSectionBoundaryDrawS(
	const UMetaLandRoadSplineComponent* SplineComp,
	double RawS,
	double& OutDrawS)
{
	OutDrawS = RawS;
	if (!SplineComp)
	{
		return false;
	}
	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	if (!Road)
	{
		return true;
	}
	TArray<TPair<double, double>> Spans;
	Road->GetJunctionMeshGenerationSpans(Spans);
	if (Spans.Num() == 0)
	{
		return true;
	}

	for (const TPair<double, double>& Span : Spans)
	{
		if (RawS >= Span.Key - 1.0 && RawS <= Span.Value + 1.0)
		{
			OutDrawS = FMath::Clamp(RawS, Span.Key, Span.Value);
			return true;
		}
	}

	// Inside a hub gap: do NOT snap to tip. Legacy Both markers in the vacancy used
	// to pile extra orange mouths on a simple T (箭头1). Hide gap-interior markers.
	return false;
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

	double S = SplineComp->GetLaneSection(SectionIndex).SOffset;
	if (!ResolveSectionBoundaryDrawS(SplineComp, S, S))
	{
		return false;
	}
	const FMetaLandRoadLaneSection& SectionAtBoundary = SplineComp->GetLaneSection(SectionIndex);

	FVector EdgeL;
	FVector EdgeR;
	FVector EdgeC;
	if (SplineComp->GetRoadEdgePointsAtS(S, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
	{
		OutLeft = RaiseSplineVisualPoint(SplineComp, EdgeL);
		OutRight = RaiseSplineVisualPoint(SplineComp, EdgeR);
		OutCenter = RaiseSplineVisualPoint(SplineComp, EdgeC);
		switch (SectionAtBoundary.Side)
		{
		case EMetaLandRoadLaneSectionSide::Left:
			OutRight = OutCenter;
			break;
		case EMetaLandRoadLaneSectionSide::Right:
			OutLeft = OutCenter;
			break;
		default:
			break;
		}
		return true;
	}

	const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(S);
	OutCenter = RaiseSplineVisualPoint(
		SplineComp,
		SplineComp->EvalLanePosition(SectionIndex, LANE_INDEX_NONE, S, 0.0, ESplineCoordinateSpace::World));

	const FMetaLandRoadLayout& Layout = SplineComp->GetRoadLayout();
	const FVector RightVector = SplineComp->GetRightVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
	const double FallbackHalfWidth = MetaLandRoad::DefaultRoadLaneWidth * 2.0;

	OutLeft = OutCenter;
	OutRight = OutCenter;

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
	double StartS = Section.SOffset;
	double EndS = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
	if (!ClipSectionDrawRangeToMesh(SplineComp, StartS, EndS, StartS, EndS))
	{
		return;
	}
	if (EndS <= StartS + KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Geometric spline center (ROffset≡0) — not EvalROffset, which drifts after multi-lane center.
	TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
	SplineComp->BuildLinearApproximation(
		Points,
		[&](double /*S*/)
		{
			return 0.0;
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

	if (bWithHitProxy && PDI->IsHitTesting())
	{
		DrawSpan(FLinearColor::White, FMath::Max(LineThickness * 2.5f, 48.0f), true);
	}
	DrawSpan(Color, LineThickness, bWithHitProxy && PDI->IsHitTesting());
}

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

	double StartS = 0.0;
	double EndS = SplineComp->GetSplineLength();
	if (!ClipSectionDrawRangeToMesh(SplineComp, StartS, EndS, StartS, EndS))
	{
		return;
	}
	if (EndS <= StartS + KINDA_SMALL_NUMBER)
	{
		return;
	}

	TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
	SplineComp->BuildLinearApproximation(
		Points,
		[&](double /*S*/)
		{
			return 0.0;
		},
		StartS,
		EndS,
		MetaLandRoad::DefaultReparamStepsPerSegment,
		MetaLandRoad::DefaultMinNumStepsPerSection,
		ESplineCoordinateSpace::World);

	for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
	{
		const FVector A = RaiseSplineVisualPoint(SplineComp, Points[PointIndex - 1].Position);
		const FVector B = RaiseSplineVisualPoint(SplineComp, Points[PointIndex].Position);
		PDI->DrawLine(A, B, Color, SDPG_Foreground, LineThickness, 0.0f, true);
	}
}

UMetaLandRoadSplineComponent* MetaLandRoadSectionEditVisualization::FindSelectedRoadSplineFromEditor()
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

bool MetaLandRoadSectionEditVisualization::IsRoadSectionEditingActive(
	const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState)
{
	return SelectionState
		&& SelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
}

bool MetaLandRoadSectionEditVisualization::IsMainRoadSpline(const UActorComponent* Component)
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

bool MetaLandRoadSectionEditVisualization::IsEngineSplineEditProxy(const HHitProxy* HitProxy)
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

bool MetaLandRoadSectionEditVisualization::IsMetaLandRoadVisProxy(const HHitProxy* HitProxy)
{
	if (!HitProxy)
	{
		return false;
	}

	return HitProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType())
		|| HitProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType())
		|| HitProxy->IsA(HMetaLandRoadSplineVisProxy::StaticGetType());
}

bool MetaLandRoadSectionEditVisualization::IsSplineOwnerSelected(const UMetaLandRoadSplineComponent* SplineComp)
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

void MetaLandRoadSectionEditVisualization::DrawSectionBoundaryHandles(
	const UMetaLandRoadSplineComponent* SplineComp,
	FPrimitiveDrawInterface* PDI,
	const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
	int32 InHoveredSectionIndex)
{
	if (!SplineComp || !PDI || SplineComp->GetLaneSectionsNum() < 2)
	{
		return;
	}

	const bool bHitTesting = PDI->IsHitTesting();

	double LastDrawS = -TNumericLimits<double>::Max();
	for (int32 SectionIndex = 1; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
	{
		double DrawS = SplineComp->GetLaneSection(SectionIndex).SOffset;
		if (!ResolveSectionBoundaryDrawS(SplineComp, DrawS, DrawS))
		{
			continue;
		}
		// Co-located L|R markers + tip-snap collapse to one orange cross.
		if (FMath::IsNearlyEqual(DrawS, LastDrawS, 5.0))
		{
			continue;
		}
		LastDrawS = DrawS;

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

		const FMetaLandRoadLaneSection& SectionAtBoundary = SplineComp->GetLaneSection(SectionIndex);
		auto DrawCross = [&](const FVector& A, const FVector& B)
		{
			if (bHitTesting)
			{
				PDI->SetHitProxy(new HMetaLandRoadSectionKeyVisProxy(SplineComp, SectionIndex, HPP_Foreground));
				PDI->DrawLine(A, B, FColor::White, SDPG_Foreground, 12.0f, 0.0f, true);
				PDI->SetHitProxy(nullptr);
			}
			else
			{
				PDI->DrawLine(A, B, CrossColor, SDPG_Foreground, LineThickness, 0.0f, true);
			}
		};

		switch (SectionAtBoundary.Side)
		{
		case EMetaLandRoadLaneSectionSide::Left:
			DrawCross(LeftLoc, CenterLoc);
			break;
		case EMetaLandRoadLaneSectionSide::Right:
			DrawCross(CenterLoc, RightLoc);
			break;
		default:
			DrawCross(LeftLoc, RightLoc);
			break;
		}
	}
}

bool MetaLandRoadSectionEditVisualization::PickSectionBoundaryByScreenDistance(
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
	const FMetaLandRoadLayout& Layout = SplineComp->GetRoadLayout();
	const float ZDelta = GetSplineVisualHeightOffset(SplineComp) - SplineComp->LaneVisualizationHeightOffset;

	auto TestWorldSegment = [&](const FVector& AWorld, const FVector& BWorld, int32 SectionIndex) -> bool
	{
		FVector2D AScreen;
		FVector2D BScreen;
		if (!View.WorldToPixel(AWorld, AScreen) || !View.WorldToPixel(BWorld, BScreen))
		{
			return false;
		}

		const float DistSq = FMath::Square(MetaLandRoad::DistancePointToSegment2D(ClickPos2D, AScreen, BScreen));
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutSectionIndex = SectionIndex;
			return true;
		}
		return false;
	};

	for (int32 SectionIndex = 1; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
	{
		FVector LeftLoc;
		FVector RightLoc;
		FVector CenterLoc;
		if (!GetSectionBoundaryCrossEndpoints(SplineComp, SectionIndex, LeftLoc, RightLoc, CenterLoc))
		{
			continue;
		}

		const FMetaLandRoadLaneSection& SectionAtBoundary = Layout.Sections[SectionIndex];
		for (int32 HeightIdx = 0; HeightIdx < 2; ++HeightIdx)
		{
			const float ExtraZ = (HeightIdx == 0) ? 0.0f : ZDelta;
			auto Lift = [ExtraZ](FVector P) { P.Z += ExtraZ; return P; };
			const FVector Left = Lift(LeftLoc);
			const FVector Right = Lift(RightLoc);
			const FVector Center = Lift(CenterLoc);

			switch (SectionAtBoundary.Side)
			{
			case EMetaLandRoadLaneSectionSide::Left:
				TestWorldSegment(Left, Center, SectionIndex);
				break;
			case EMetaLandRoadLaneSectionSide::Right:
				TestWorldSegment(Center, Right, SectionIndex);
				break;
			default:
				TestWorldSegment(Left, Right, SectionIndex);
				break;
			}
		}
	}

	return OutSectionIndex != INDEX_NONE;
}

float MetaLandRoadSectionEditVisualization::ResolveSplitSplineKey(
	const UMetaLandRoadSplineComponent* SplineComp,
	UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
	float HoverSplineKey)
{
	if (!SplineComp || !SelectionState)
	{
		return -1.0f;
	}

	static constexpr double MinSplitMargin = 10.0;
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

void MetaLandRoadSectionEditVisualization::DrawSectionEditLines(
	UMetaLandRoadSplineComponent* SplineComp,
	FPrimitiveDrawInterface* PDI,
	const UMetaLandRoadSectionComponentVisualizerSelectionState* SelectionState,
	int32 HoveredSectionIndex)
{
	if (!SplineComp || !PDI)
	{
		return;
	}

	const FLinearColor Pink(1.0f, 0.25f, 0.75f, 1.0f);
	const FLinearColor Yellow(1.0f, 1.0f, 0.0f, 1.0f);
	const float NormalThickness = 4.0f;
	const float HighlightThickness = 6.0f;

	const int32 SelectedSectionIndex = (SelectionState
		&& SelectionState->GetSelectedSpline() == SplineComp
		&& !SelectionState->IsSectionBoundarySelected()
		&& SelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE
		&& SelectionState->GetState() >= EMetaLandRoadSectionSelectionState::Section)
		? SelectionState->GetSelectedSectionIndex()
		: INDEX_NONE;

	if (SplineComp->GetLaneSectionsNum() == 0)
	{
		const bool bHighlight = HoveredSectionIndex != INDEX_NONE || SelectedSectionIndex != INDEX_NONE;
		DrawFullCenterEditLine(
			SplineComp,
			PDI,
			bHighlight ? Yellow : Pink,
			bHighlight ? HighlightThickness : NormalThickness);
		return;
	}

	// Draw every Both span (not only the longest) so Add Section boundaries stay visible.
	int32 DrawnBothCount = 0;
	for (int32 SectionIndex = 0; SectionIndex < SplineComp->GetLaneSectionsNum(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
		if (Section.Side != EMetaLandRoadLaneSectionSide::Both)
		{
			continue;
		}

		double StartS = Section.SOffset;
		double EndS = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
		if (!ClipSectionDrawRangeToMesh(SplineComp, StartS, EndS, StartS, EndS))
		{
			continue;
		}
		if (EndS <= StartS + KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const bool bHighlight =
			HoveredSectionIndex == SectionIndex || SelectedSectionIndex == SectionIndex;
		DrawSectionCenterLineSpan(
			SplineComp,
			PDI,
			SectionIndex,
			bHighlight ? Yellow : Pink,
			bHighlight ? HighlightThickness : NormalThickness,
			true);
		++DrawnBothCount;
	}

	if (DrawnBothCount == 0)
	{
		// Left|Right-only markers: still need a pickable geometric center for Add Section.
		const bool bHighlight = HoveredSectionIndex != INDEX_NONE || SelectedSectionIndex != INDEX_NONE;
		DrawFullCenterEditLine(
			SplineComp,
			PDI,
			bHighlight ? Yellow : Pink,
			bHighlight ? HighlightThickness : NormalThickness);
	}
}
