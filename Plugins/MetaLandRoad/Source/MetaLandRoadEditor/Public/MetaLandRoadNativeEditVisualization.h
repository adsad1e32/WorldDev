#pragma once

#include "Math/IntPoint.h"
#include "MetaLandRoadLaneVisualization.h"
#include "SceneView.h"

class FPrimitiveDrawInterface;
class UMetaLandRoadSplineComponent;

namespace MetaLandRoadNativeEditVisualization
{
	/** Screen pick radius for control-point rings (click + hover). */
	inline constexpr float NativeEditKeyPickRadiusPx = 54.0f;
	/** Display scale for Alt+W / Alt+T control-point rings. */
	inline constexpr float NativeEditRingDisplayScale = 1.5f;

	METALANDROADEDITOR_API float GetNativeEditHeightOffset(const UMetaLandRoadSplineComponent* RoadSpline);

	METALANDROADEDITOR_API float GetNativeEditRingWorldRadius(
		const FSceneView* View,
		const FVector& CenterWorld,
		const FVector& RingAxisX,
		const FVector& RingAxisY,
		float DesiredPixelRadius = 48.0f);

	METALANDROADEDITOR_API int32 PickHoveredControlPointKey(
		const UMetaLandRoadSplineComponent* RoadSpline,
		const FSceneView* View,
		const FIntPoint& MousePos,
		float DesiredHoverPixelRadius = 22.0f);

	METALANDROADEDITOR_API int32 PickHoveredSplineSegment(
		const UMetaLandRoadSplineComponent* RoadSpline,
		const FSceneView* View,
		const FIntPoint& MousePos,
		float MaxPixelDistance = 20.0f);

	/** Solid camera-scaled box (same radius helper as Alt+W / Alt+T rings). */
	METALANDROADEDITOR_API void DrawNativeEditSolidBoxPDI(
		FPrimitiveDrawInterface* PDI,
		const FSceneView* View,
		const FVector& CenterWorld,
		bool bSelected,
		bool bHovered);

	METALANDROADEDITOR_API void DrawNativeEditSplineOverlay(
		const UMetaLandRoadSplineComponent* RoadSpline,
		FPrimitiveDrawInterface* PDI,
		int32 HoveredKey,
		int32 HoveredSegment,
		int32 SelectedSegment,
		const TSet<int32>& SelectedKeys);

	METALANDROADEDITOR_API void DrawNativeEditRingOverlay(
		const FSceneView* View,
		int32 HoveredKey,
		const TSet<int32>& SelectedKeys,
		int32 SelectedSegment,
		const UMetaLandRoadSplineComponent* RoadSpline,
		FPrimitiveDrawInterface* PDI);

	/** Alt+T width-key rings (shared ring styling with Alt+W). */
	METALANDROADEDITOR_API void DrawLaneWidthKeyRingOverlay(
		const FSceneView* View,
		const UMetaLandRoadSplineComponent* RoadSpline,
		FPrimitiveDrawInterface* PDI,
		const FMetaLandRoadLaneEditHighlight& Highlight,
		int32 HoveredSectionIndex,
		int32 HoveredLaneIndex,
		int32 HoveredWidthIndex);
}
