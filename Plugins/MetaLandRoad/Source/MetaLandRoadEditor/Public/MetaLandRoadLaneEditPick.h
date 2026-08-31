#pragma once

#include "Math/IntPoint.h"
#include "MetaLandRoadTypes.h"
#include "SceneView.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

class FEditorViewportClient;
class FViewport;
class UMetaLandRoadSplineComponent;

/** Screen-space pick result for lane width segment / key / boundary. */
struct FMetaLandRoadLaneWidthSegmentPickResult
{
	int32 SectionIndex = INDEX_NONE;
	int32 LaneIndex = LANE_INDEX_NONE;
	int32 WidthIndex = INDEX_NONE;
};

namespace MetaLandRoadLaneEditPick
{
	/** Alt+T hover pick radius (px) for width-key rings. */
	inline constexpr int32 LaneWidthKeyHoverRadiusPx = 28;
	inline constexpr int32 LaneMouthRingPickRadiusPx = 36;
	inline constexpr int32 LaneWidthSegmentHoverRadiusPx = 2;
	inline constexpr int32 LaneFaceClickRadiusPx = 10;
	inline constexpr int32 LaneWidthSegmentClickRadiusPx = 4;
	inline constexpr int32 LaneBoundarySegmentHoverRadiusPx = 18;
	inline constexpr int32 SectionBoundaryHoverRadiusPx = 22;

	METALANDROADEDITOR_API bool ShouldDeferInputToTranslateWidget(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		int32 MouseX,
		int32 MouseY);

	METALANDROADEDITOR_API bool IsLaneValidOnSpanSection(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SpanSectionIndex,
		int32 LaneIndex);

	METALANDROADEDITOR_API FVector RaiseLaneVizPoint(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FVector& WorldLocation);

	METALANDROADEDITOR_API bool PickLaneWidthSegmentAtMouse(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView& View,
		const FIntPoint& MousePos,
		float MaxScreenDistance,
		FMetaLandRoadLaneWidthSegmentPickResult& OutResult);

	METALANDROADEDITOR_API bool PickLaneBoundaryOnSectionAtMouse(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView& View,
		const FIntPoint& MousePos,
		float MaxScreenDistance,
		FMetaLandRoadLaneWidthSegmentPickResult& OutResult);

	METALANDROADEDITOR_API bool PickLaneWidthKeyAtMouse(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView& View,
		const FIntPoint& MousePos,
		int32 PrimarySectionIndex,
		int32 PrimaryLaneIndex,
		int32 PrimaryWidthIndex,
		const TArray<FMetaLandRoadSelectedLaneRefU>& MultiSelectedLanes,
		int32 HoveredSectionIndex,
		int32 HoveredLaneIndex,
		float MaxScreenDistance,
		FMetaLandRoadLaneWidthSegmentPickResult& OutResult);

	METALANDROADEDITOR_API EMetaLandRoadMouthSide ResolveMouthSideFromMouse(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SpanSectionIndex,
		int32 LaneIndex,
		const FSceneView& View,
		const FIntPoint& MousePos);

	/** Outward draw direction at a lane mouth  - local spline tangent at tip S (not lane chord). */
	METALANDROADEDITOR_API bool ResolveMouthOutwardTangent(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadDrawAnchor& Anchor,
		FVector& OutOutwardTangent);

	/**
	 * Clamp lane mouth S to the visible mesh tip (junction trim), so spawn/highlight
	 * stick to the cut edge instead of overlapping into a trimmed gap.
	 */
	METALANDROADEDITOR_API double ClampMouthAnchorSToVisibleMesh(
		const UMetaLandRoadSplineComponent* SplineComp,
		double PreferredAnchorS,
		EMetaLandRoadMouthSide MouthSide);

	METALANDROADEDITOR_API bool IsBundleCompatibleWithAnchor(
		const FMetaLandRoadDrawAnchor& ExistingAnchor,
		int32 NewLaneIndex);

	METALANDROADEDITOR_API bool TryExpandAnchorBundle(
		FMetaLandRoadDrawAnchor& InOutAnchor,
		int32 NewLaneIndex);

	/**
	 * Hover auto-match: contiguous window on the seed face, then nearest lanes from
	 * the opposite face until DesiredLaneCount (so 3-lane source snap is not stuck at 2).
	 * Explicit Shift bundles still own the final subset when locked.
	 * PreferWorldPos biases the window / opposite-face picks toward the cursor.
	 */
	METALANDROADEDITOR_API bool ExpandAnchorBundleToCount(
		const UMetaLandRoadSplineComponent* SplineComp,
		FMetaLandRoadDrawAnchor& InOutAnchor,
		int32 DesiredLaneCount,
		const FVector* PreferWorldPos = nullptr);

	/** Same road tip: match mouth side + S (span SectionIndex may differ per lane). */
	METALANDROADEDITOR_API bool IsSamePhysicalMouth(
		const FMetaLandRoadDrawAnchor& Anchor,
		const UMetaLandRoadSplineComponent* Spline,
		EMetaLandRoadMouthSide MouthSide,
		double AnchorS,
		double SToleranceCm = 150.0);

	/** Whole-road mouth: every live lane on both faces at this S. */
	METALANDROADEDITOR_API bool TrySelectAllLanesOnMouthSide(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SpanSectionIndex,
		FMetaLandRoadDrawAnchor& InOutAnchor);

	/** Screen-pick lane face + mouth (World Builder anchor pick). */
	METALANDROADEDITOR_API bool PickLaneAnchorAtMouse(
		UWorld* World,
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		const FIntPoint& MousePos,
		bool bShiftDown,
		bool bSelectAllLanesOnSide,
		const FMetaLandRoadDrawAnchor* ExistingAnchor,
		FMetaLandRoadDrawAnchor& OutAnchor,
		UMetaLandRoadSplineComponent*& OutSpline);

	/** Mouth lateral edges + outward tangent for lane-width draw guidance. */
	METALANDROADEDITOR_API bool ComputeAnchorMouthRailFrame(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadDrawAnchor& Anchor,
		FVector& OutTipLeft,
		FVector& OutTipRight,
		FVector& OutTipCenter,
		FVector& OutOutwardTangent);
}
