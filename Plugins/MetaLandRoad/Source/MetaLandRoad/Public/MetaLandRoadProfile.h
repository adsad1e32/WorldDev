#pragma once

#include "CoreMinimal.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadProfile.generated.h"

class UMetaLandRoadSplineComponent;

/** Lightweight lane description used when creating a new road (MetaRoad FRoadLaneProfile equivalent). */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	double Width = MetaLandRoad::DefaultRoadLaneWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	EMetaLandRoadLaneDirection Direction = EMetaLandRoadLaneDirection::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	EMetaLandRoadDriveableLaneType DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;
};

/** Lane section profile applied on first road placement (MetaRoad FRoadLaneSectionProfile equivalent). */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneSectionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	TArray<FMetaLandRoadLaneProfile> Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadProfile)
	TArray<FMetaLandRoadLaneProfile> Right;
};

namespace MetaLandRoadProfile
{
	METALANDROAD_API FMetaLandRoadLaneSectionProfile GetDefaultRoadProfile();

	METALANDROAD_API void SetRoadProfile(UMetaLandRoadSplineComponent* TargetSpline, const FMetaLandRoadLaneSectionProfile& RoadProfile);

	/** Optional: when enabled and exactly 2 sections exist, keep the second section at 50% spline length. */
	METALANDROAD_API void ApplyDefaultLongitudinalSectionSplit(UMetaLandRoadSplineComponent* TargetSpline);

	/** Apply default lanes when the spline has no sections yet. */
	METALANDROAD_API void EnsureDefaultRoadProfile(UMetaLandRoadSplineComponent* TargetSpline);

	/** Refresh layout bounds after spline edits. */
	METALANDROAD_API void SyncRoadLayout(UMetaLandRoadSplineComponent* TargetSpline);
}
