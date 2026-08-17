#include "MetaLandRoadNativeEditVisualization.h"

#include "MetaLandRoadEditorColors.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadVisProxy.h"
#include "SceneManagement.h"
#include "Settings/LevelEditorViewportSettings.h"

static FVector RaiseNativeEditPoint(const UMetaLandRoadSplineComponent* RoadSpline, const FVector& WorldPos)
{
	FVector Raised = WorldPos;
	Raised.Z += MetaLandRoadNativeEditVisualization::GetNativeEditHeightOffset(RoadSpline);
	return Raised;
}

static void GetNativeEditRingGroundAxes(FVector& OutAxisX, FVector& OutAxisY)
{
	OutAxisX = FVector(1.0f, 0.0f, 0.0f);
	OutAxisY = FVector(0.0f, 1.0f, 0.0f);
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

float MetaLandRoadNativeEditVisualization::GetNativeEditHeightOffset(const UMetaLandRoadSplineComponent* RoadSpline)
{
	(void)RoadSpline;
	return 0.0f;
}

float MetaLandRoadNativeEditVisualization::GetNativeEditRingWorldRadius(
	const FSceneView* View,
	const FVector& CenterWorld,
	const FVector& RingAxisX,
	const FVector& RingAxisY,
	float DesiredPixelRadius)
{
	if (!View || DesiredPixelRadius <= 0.0f)
	{
		return 60.0f;
	}

	FVector2D CenterPx;
	if (!View->WorldToPixel(CenterWorld, CenterPx))
	{
		return 60.0f;
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
	constexpr float MinWorldRadiusCm = 48.0f;
	constexpr float MaxWorldRadiusCm = 144.0f;
	return FMath::Clamp(DesiredPixelRadius / PixelsPerCm, MinWorldRadiusCm, MaxWorldRadiusCm);
}

int32 MetaLandRoadNativeEditVisualization::PickHoveredControlPointKey(
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
	const float MinPixelRadius = FMath::Max(DesiredHoverPixelRadius, NativeEditKeyPickRadiusPx);
	FVector RingAxisX = FVector::ZeroVector;
	FVector RingAxisY = FVector::ZeroVector;
	GetNativeEditRingGroundAxes(RingAxisX, RingAxisY);

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

		const float DesiredPx = 24.0f * NativeEditRingDisplayScale;
		const float WorldRadius = GetNativeEditRingWorldRadius(
			View, KeyWorld, RingAxisX, RingAxisY, DesiredPx);
		FVector2D EdgePx;
		const float RingPixelRadius = View->WorldToPixel(KeyWorld + RingAxisX * WorldRadius, EdgePx)
			? FVector2D::Distance(Pixel, EdgePx)
			: DesiredPx;
		const float HoverPixelRadius = FMath::Max(MinPixelRadius, RingPixelRadius + 8.0f);

		const float DistSq = FVector2D::DistSquared(Mouse, Pixel);
		if (DistSq <= HoverPixelRadius * HoverPixelRadius && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestKey = KeyIdx;
		}
	}
	return BestKey;
}

int32 MetaLandRoadNativeEditVisualization::PickHoveredSplineSegment(
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
		const int32 I1 = SegIdx + 1;
		const float Key0 = RoadSpline->GetSplinePointInputKey(SegIdx);
		const float Key1 = RoadSpline->GetSplinePointInputKey(I1);
		FVector PrevPos = RaiseNativeEditPoint(
			RoadSpline,
			RoadSpline->GetLocationAtSplinePoint(SegIdx, ESplineCoordinateSpace::World));
		for (int32 Step = 1; Step <= NumSubdivisions; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / static_cast<float>(NumSubdivisions);
			const FVector WorldPos = (Step == NumSubdivisions)
				? RoadSpline->GetLocationAtSplinePoint(I1, ESplineCoordinateSpace::World)
				: RoadSpline->GetLocationAtSplineInputKey(
					FMath::Lerp(Key0, Key1, Alpha), ESplineCoordinateSpace::World);
			FVector CurPos = RaiseNativeEditPoint(RoadSpline, WorldPos);

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

void MetaLandRoadNativeEditVisualization::DrawNativeEditSplineOverlay(
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
	// Center line highlights only on hover, so RMB Add Point has a clear target.
	const bool bHighlightMain = HoveredSegment != INDEX_NONE;
	(void)HoveredKey;
	(void)SelectedKeys;
	(void)SelectedSegment;
	if (!PDI->IsHitTesting())
	{
		RoadSpline->DrawMainCenterLinePDI(
			PDI,
			bHighlightMain ? Yellow : Pink,
			bHighlightMain ? HighlightThickness : NormalThickness);
	}

	for (int32 SegIdx = 0; SegIdx < NumSegments; ++SegIdx)
	{
		if (!PDI->IsHitTesting())
		{
			continue;
		}

		PDI->SetHitProxy(new HMetaLandRoadSplineSegmentVisProxy(MutableSpline, SegIdx, HPP_Foreground));
		const int32 I1 = SegIdx + 1;
		const float Key0 = RoadSpline->GetSplinePointInputKey(SegIdx);
		const float Key1 = RoadSpline->GetSplinePointInputKey(I1);
		const FVector Raise(0.0f, 0.0f, RoadSpline->GetMainCenterLineHeightOffset());
		FVector OldPos = RoadSpline->GetLocationAtSplinePoint(SegIdx, ESplineCoordinateSpace::World) + Raise;
		constexpr int32 HitSteps = 16;
		for (int32 StepIdx = 1; StepIdx <= HitSteps; ++StepIdx)
		{
			const float Alpha = static_cast<float>(StepIdx) / static_cast<float>(HitSteps);
			const FVector NewPos = ((StepIdx == HitSteps)
				? RoadSpline->GetLocationAtSplinePoint(I1, ESplineCoordinateSpace::World)
				: RoadSpline->GetLocationAtSplineInputKey(
					FMath::Lerp(Key0, Key1, Alpha), ESplineCoordinateSpace::World))
				+ Raise;
			PDI->DrawLine(OldPos, NewPos, FLinearColor::White, SDPG_Foreground, 48.0f, 0.0f, true);
			OldPos = NewPos;
		}
		PDI->SetHitProxy(nullptr);
	}

	const float KeyHitSize = 72.0f + GetDefault<ULevelEditorViewportSettings>()->SelectedSplinePointSizeAdjustment;
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

void MetaLandRoadNativeEditVisualization::DrawNativeEditRingOverlay(
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
	const int32 NumPoints = RoadSpline->GetNumberOfSplinePoints();
	for (int32 KeyIdx = 0; KeyIdx < NumPoints; ++KeyIdx)
	{
		const bool bSelected = !bSegmentSelected && SelectedKeys.Contains(KeyIdx);
		const bool bHovered = !bSegmentSelected && HoveredKey == KeyIdx;

		const FVector KeyPos = RaiseNativeEditPoint(
			RoadSpline,
			RoadSpline->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World));

		FVector RingAxisX = FVector::RightVector;
		FVector RingAxisY = FVector::ForwardVector;
		GetNativeEditRingGroundAxes(RingAxisX, RingAxisY);

		const float DesiredPx = (bSelected ? 32.0f : (bHovered ? 28.0f : 24.0f))
			* NativeEditRingDisplayScale;
		const float RingRadius = View
			? GetNativeEditRingWorldRadius(View, KeyPos, RingAxisX, RingAxisY, DesiredPx)
			: 60.0f;
		DrawNativeEditControlPointRingPDI(PDI, KeyPos, RingAxisX, RingAxisY, RingRadius, bSelected, bHovered);
	}
}

void MetaLandRoadNativeEditVisualization::DrawLaneWidthKeyRingOverlay(
	const FSceneView* View,
	const UMetaLandRoadSplineComponent* RoadSpline,
	FPrimitiveDrawInterface* PDI,
	const FMetaLandRoadLaneEditHighlight& Highlight,
	int32 HoveredSectionIndex,
	int32 HoveredLaneIndex,
	int32 HoveredWidthIndex)
{
	if (!RoadSpline || !PDI || PDI->IsHitTesting())
	{
		return;
	}

	if (Highlight.HasLaneFaceSelection())
	{
		return;
	}

	const bool bUseHoverLane =
		HoveredSectionIndex != INDEX_NONE && HoveredLaneIndex != LANE_INDEX_NONE;
	const bool bWidthKeySelected = Highlight.SelectedWidthIndex != INDEX_NONE;
	const bool bShowAllKeys =
		bWidthKeySelected
		|| (Highlight.bHoveredLaneBoundaryLine && bUseHoverLane)
		|| (Highlight.bHoveredWidthIsKey && bUseHoverLane);
	if (!bShowAllKeys)
	{
		return;
	}

	int32 SpanSectionIndex = INDEX_NONE;
	int32 LaneIndex = LANE_INDEX_NONE;
	if (bWidthKeySelected)
	{
		SpanSectionIndex = Highlight.SelectedSectionIndex;
		LaneIndex = Highlight.SelectedLaneIndex;
	}
	else
	{
		SpanSectionIndex = HoveredSectionIndex;
		LaneIndex = HoveredLaneIndex;
	}

	if (SpanSectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
	{
		return;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		RoadSpline, SpanSectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE
		|| !RoadSpline->GetLaneSection(LaneSectionIndex).CheckLaneIndex(LaneIndex))
	{
		return;
	}

	const FMetaLandRoadLaneSection& SpanSection = RoadSpline->GetLaneSection(SpanSectionIndex);
	const double SpanS0 = SpanSection.SOffset;
	const double SpanS1 = MetaLandRoad::ResolveSectionEndS(RoadSpline, SpanSectionIndex);
	const FMetaLandRoadLane& Lane = RoadSpline->GetLaneSection(LaneSectionIndex).GetLaneByIndex(LaneIndex);
	const double KeyDrawS0 = FMath::Max(SpanS0, Lane.GetStartOffset());
	const double KeyDrawS1 = FMath::Min(SpanS1, Lane.GetEndOffset());
	if (KeyDrawS1 <= KeyDrawS0 + KINDA_SMALL_NUMBER)
	{
		return;
	}

	auto DrawRingForLaneKey = [&](int32 KeyIndex, double KeyS, bool bSelected, bool bHovered)
	{
		FVector KeyPos = RoadSpline->EvalLanePosition(
			LaneSectionIndex, LaneIndex, KeyS, 1.0, ESplineCoordinateSpace::World);
		KeyPos.Z += RoadSpline->LaneVisualizationHeightOffset;

		FVector RingAxisX = FVector::RightVector;
		FVector RingAxisY = FVector::ForwardVector;
		GetNativeEditRingGroundAxes(RingAxisX, RingAxisY);

		const float DesiredPx = (bSelected ? 32.0f : (bHovered ? 28.0f : 24.0f))
			* NativeEditRingDisplayScale;
		const float RingRadius = View
			? GetNativeEditRingWorldRadius(View, KeyPos, RingAxisX, RingAxisY, DesiredPx)
			: 60.0f;

		DrawNativeEditControlPointRingPDI(PDI, KeyPos, RingAxisX, RingAxisY, RingRadius, bSelected, bHovered);
	};

	for (int32 KeyIndex = 0; KeyIndex < Lane.Width.GetNumKeys(); ++KeyIndex)
	{
		const double KeyS = Lane.GetStartOffset() + Lane.Width.Keys[KeyIndex].Time;
		if (KeyS < KeyDrawS0 - KINDA_SMALL_NUMBER || KeyS > KeyDrawS1 + KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const bool bSelected =
			bWidthKeySelected
			&& Highlight.SelectedSectionIndex == SpanSectionIndex
			&& Highlight.SelectedLaneIndex == LaneIndex
			&& Highlight.SelectedWidthIndex == KeyIndex;
		const bool bHovered =
			!bSelected
			&& Highlight.bHoveredWidthIsKey
			&& HoveredSectionIndex == SpanSectionIndex
			&& HoveredLaneIndex == LaneIndex
			&& HoveredWidthIndex == KeyIndex;
		DrawRingForLaneKey(KeyIndex, KeyS, bSelected, bHovered);
	}
}
