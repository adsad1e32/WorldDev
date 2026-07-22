#pragma once

#include "SplineRoadBasicSettings.generated.h"

class UTexture2D;

/** The blend mode changes how the brush material is applied to the terrain. */
UENUM(BlueprintType)
enum class ERoadBrushBlendType : uint8
{
	/** Alpha Blend will affect the heightmap both upwards and downwards. */
	AlphaBlend,
	/** Limits the brush to only lowering the terrain. */
	Min,
	/** Limits the brush to only raising the terrain. */
	Max,
	/** Performs an additive blend, using a flat Z=0 terrain as the input. Useful when you want to preserve underlying detail or ramps. */
	Additive,
};

UENUM(BlueprintType)
enum class ERoadBrushFalloffMode : uint8
{
	Angle,
	Width,
};

USTRUCT(BlueprintType)
struct FRoadFalloffSettings
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	ERoadBrushFalloffMode FalloffMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings, meta = (EditCondition = "FalloffMode == EWaterBrushFalloffMode::Angle"))
	float FalloffAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings, meta = (ClampMin = "0.1", EditCondition = "FalloffMode == EWaterBrushFalloffMode::Width"))
	float FalloffWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	float EdgeOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	float ZOffset;



	FRoadFalloffSettings()
		:FalloffMode(ERoadBrushFalloffMode::Angle)
		, FalloffAngle(45.0f)
		, FalloffWidth(1024.0f)
		, EdgeOffset(0)
		, ZOffset(0)
	{
	}

};


USTRUCT(BlueprintType)
struct FRoadBrushEffectBlurring
{
	GENERATED_BODY()

	FRoadBrushEffectBlurring()
		: bBlurShape(true)
		, Radius(2)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	bool bBlurShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	int32 Radius;
};

USTRUCT(BlueprintType)
struct FRoadBrushEffectTerracing
{
	GENERATED_BODY()

	FRoadBrushEffectTerracing()
		: TerraceAlpha(0.0f)
		, TerraceSpacing(256.0f)
		, TerraceSmoothness(0.0f)
		, MaskLength(0.0f)
		, MaskStartOffset(0.0f)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float TerraceAlpha;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float TerraceSpacing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float TerraceSmoothness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float MaskLength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float MaskStartOffset;
};


USTRUCT(BlueprintType)
struct FRoadBrushEffects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	FRoadBrushEffectBlurring Blurring;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	//FWaterBrushEffectCurlNoise CurlNoise;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	//FWaterBrushEffectDisplacement Displacement;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	//FWaterBrushEffectSmoothBlending SmoothBlending;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	FRoadBrushEffectTerracing Terracing;
};




USTRUCT(BlueprintType)
struct FSplineRoadHeightmapSettings
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TerrainCarvingSettings)
	ERoadBrushBlendType BlendMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TerrainCarvingSettings)
	FRoadFalloffSettings FalloffSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TerrainCarvingSettings)
	FRoadBrushEffects Effects;
	FSplineRoadHeightmapSettings()
	: BlendMode(ERoadBrushBlendType::AlphaBlend)
	{

	}


}; 

USTRUCT(BlueprintType)

struct FRoadBodyWeightmapSettings
{
	GENERATED_BODY()

	FRoadBodyWeightmapSettings()
		: FalloffWidth(512.f)
		, EdgeOffset(0)
		, ModulationTexture(nullptr)
		, TextureTiling(4.0f)
		, TextureInfluence(.5f)
		, Midpoint(0.5f)
		, FinalOpacity(1.0f)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings, meta = (ClampMin = "0.1"))
	float FalloffWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float EdgeOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	TSoftObjectPtr<UTexture2D> ModulationTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float TextureTiling;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float TextureInfluence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float Midpoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float FinalOpacity;


};

