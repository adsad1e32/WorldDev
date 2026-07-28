#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

#include "MetaLandRoadLaneAttribute.generated.h"

/** Base value type for lane attributes along SOffset. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	virtual ~FMetaLandRoadLaneAttributeValue() = default;
};

USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneAttributeKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneAttribute)
	double SOffset = 0.0;

	UPROPERTY(EditAnywhere, Category = LaneAttribute)
	TInstancedStruct<FMetaLandRoadLaneAttributeValue> Value;

	bool operator<(const FMetaLandRoadLaneAttributeKey& Other) const
	{
		return SOffset < Other.SOffset;
	}
};

/** Arbitrary metadata assigned along a lane, keyed by SOffset. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneAttribute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = LaneAttribute)
	TArray<FMetaLandRoadLaneAttributeKey> Keys;

	void Trim(double S0, double S1);
};
