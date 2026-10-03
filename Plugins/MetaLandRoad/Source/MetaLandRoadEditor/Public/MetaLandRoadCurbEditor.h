#pragma once

#include "CoreMinimal.h"

class AMoKuEditIntersectionActor;
class AMoKuEditSplineActor;
class FPrimitiveDrawInterface;
class FSceneView;
class UMetaLandRoadCurbProfile;
class UMetaLandRoadSplineComponent;
struct FMetaLandRoadSidewalkEditDetails;

struct FSidewalkEdgePick
{
	UMetaLandRoadSplineComponent* Spline = nullptr;
	int32 Sign = 0;
	double S = -1.0;
	AMoKuEditIntersectionActor* Junction = nullptr;
	int32 CornerIndex = INDEX_NONE;

	bool HitEdge() const { return Spline != nullptr && Sign != 0; }
	bool HitCorner() const { return Junction != nullptr && CornerIndex != INDEX_NONE; }
	bool IsHit() const { return HitEdge() || HitCorner(); }
};

namespace MetaLandRoadCurbEditor
{
	UMetaLandRoadCurbProfile* FindProfileForSpline(UMetaLandRoadSplineComponent* Spline);
	void DrawSelectedRoadPreview(FPrimitiveDrawInterface* PDI);
	bool EnableSidewalksOnSpline(UMetaLandRoadSplineComponent* Spline);
	bool GetSidewalkDetails(UMetaLandRoadSplineComponent* Spline, FMetaLandRoadSidewalkEditDetails& OutDetails);
	bool SetSidewalkDetails(UMetaLandRoadSplineComponent* Spline, const FMetaLandRoadSidewalkEditDetails& InDetails);
	bool AddCurbKeyAtS(UMetaLandRoadSplineComponent* Spline, double SOffset);
	bool RemoveSelectedCurbKey(UMetaLandRoadSplineComponent* Spline);
	bool ApplySidewalkPick(const FSidewalkEdgePick& Pick);
	void DrawSidewalkKeys(FPrimitiveDrawInterface* PDI, UMetaLandRoadSplineComponent* Spline);
	void DrawSidewalkEdges(FPrimitiveDrawInterface* PDI, UMetaLandRoadSplineComponent* Spline);
	bool PickSidewalkOverlay(const FSceneView* View, int32 MouseX, int32 MouseY, FSidewalkEdgePick& OutPick);
	bool PickSidewalkEdge(
		UMetaLandRoadSplineComponent* Spline,
		const FSceneView* View,
		int32 MouseX,
		int32 MouseY,
		int32& OutSign,
		double& OutS);
}
