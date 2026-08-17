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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings, meta = (EditCondition = "FalloffMode == ERoadBrushFalloffMode::Angle"))
	float FalloffAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings, meta = (ClampMin = "0.1", EditCondition = "FalloffMode == ERoadBrushFalloffMode::Width"))
	float FalloffWidth;

	/** Shifts the height soft-rim relative to the mesh silhouette (cm). Fully editable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	float EdgeOffset;

	/**
	 * Extra height bias on landscape stamp (cm), added on top of road mesh depth.
	 * Terrain always follows the road mesh; raise ZOffset for taller embankment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FalloffSettings)
	float ZOffset;



	FRoadFalloffSettings()
		: FalloffMode(ERoadBrushFalloffMode::Width)
		, FalloffAngle(35.0f)
		// Distance falloff (cm). Artist-owned — EnsureDefault will not overwrite this.
		, FalloffWidth(800.0f)
		, EdgeOffset(0)
		, ZOffset(-5.0f)
	{
	}

};


USTRUCT(BlueprintType)
struct FRoadBrushEffectBlurring
{
	GENERATED_BODY()

	FRoadBrushEffectBlurring()
		// Blur softens DF into speckled noise on weight/height — off by default.
		: bBlurShape(false)
		, Radius(1)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	bool bBlurShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	int32 Radius;
};

/** Landmass-style curl warp on the distance-field edge (breaks straight knife edges). */
USTRUCT(BlueprintType)
struct FRoadBrushEffectCurlNoise
{
	GENERATED_BODY()

	FRoadBrushEffectCurlNoise()
		// Off by default — Curl makes embankment / pavement edges look sawtoothed.
		: Curl1Amount(0.0f)
		, Curl2Amount(0.0f)
		, Curl1Tiling(24.0f)
		, Curl2Tiling(12.0f)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (DisplayName = "Curl 1 Strength"))
	float Curl1Amount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (DisplayName = "Curl 2 Strength"))
	float Curl2Amount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (DisplayName = "Curl 1 Tiling"))
	float Curl1Tiling;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (DisplayName = "Curl 2 Tiling"))
	float Curl2Tiling;
};

/** Texture displacement along the brush falloff (Landmass Displacement). */
USTRUCT(BlueprintType)
struct FRoadBrushEffectDisplacement
{
	GENERATED_BODY()

	FRoadBrushEffectDisplacement()
		: DisplacementHeight(0.0f)
		, DisplacementTiling(2.0f)
		, Texture(nullptr)
		, Midpoint(0.5f)
		, Channel(1.0f, 0.0f, 0.0f, 0.0f)
		, WeightmapInfluence(0.0f)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float DisplacementHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float DisplacementTiling;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	TSoftObjectPtr<UTexture2D> Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	float Midpoint;

	/** Which texture channels drive displacement (Landmass Channel = R by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	FLinearColor Channel;

	/** How much displacement also modulates the Road weightmap edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WeightmapInfluence;
};

/** Inner/outer smooth blend distances (cm) for MeshBrush falloff materials. */
USTRUCT(BlueprintType)
struct FRoadBrushEffectSmoothBlending
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (ClampMin = "0.01"))
	float InnerSmoothDistance = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects, meta = (ClampMin = "0.01"))
	float OuterSmoothDistance = 128.0f;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	FRoadBrushEffectCurlNoise CurlNoise;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	FRoadBrushEffectDisplacement Displacement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
	FRoadBrushEffectSmoothBlending SmoothBlending;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BrushEffects)
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
		// Pavement Road: narrow soft rim (AA). Wide Falloff → muddy gray across full width.
		: FalloffWidth(180.f)
		, EdgeOffset(0)
		, ModulationTexture(nullptr)
		, TextureTiling(4.0f)
		// Keep 0 until a ModulationTexture is assigned — non-zero + None hardens MeshBrush_Weightmap edges.
		, TextureInfluence(0.0f)
		, Midpoint(0.5f)
		, FinalOpacity(1.0f)
	{
	}

	/** Soft blend width (cm). Road≈300–500 (pavement AA); Dirty≈Height Falloff (side slopes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings, meta = (ClampMin = "0.1", ClampMax = "2500.0"))
	float FalloffWidth;

	/** Shifts the weight edge (cm). Keep near 0; large values hide the soft rim under the mesh → knife edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings, meta = (ClampMin = "-2000.0", ClampMax = "2000.0"))
	float EdgeOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	TSoftObjectPtr<UTexture2D> ModulationTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings, meta = (ClampMin = "0.1", ClampMax = "64.0"))
	float TextureTiling;

	/** 0–1 only. Values like 1000+ destroy soft edges (sawtooth) and material blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TextureInfluence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float Midpoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadBodyWeightmapSettings)
	float FinalOpacity;


};

