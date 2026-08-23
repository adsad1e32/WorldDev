#pragma once

#include "CoreMinimal.h"
#include "SplineRoadBasicSettings.h"
#include "StructUtils/InstancedStruct.h"

#include "MetaLandRoadLaneAttribute.generated.h"

/** How a typed attribute evaluates between keys along S. */
UENUM(BlueprintType)
enum class EMetaLandRoadAttributeEvalMode : uint8
{
	/** Hold the last key until the next one (Speed, Mark, Polygon, Curb Cut). */
	Stepped,
	/** Blend neighboring keys (Landscape, Sidewalk Height). */
	Interpolated,
};

/** Where an attribute track may be pinned. */
UENUM(BlueprintType)
enum class EMetaLandRoadAttributeHost : uint8
{
	/** Per driving / sidewalk lane. */
	Lane,
	/** Section center-line (lane index 0 / island ring). */
	SectionCenter,
};

/** Descriptor for one attribute type. Same track API; different Value struct. */
struct METALANDROAD_API FMetaLandRoadAttributeTypeDesc
{
	FName TypeId;
	FText DisplayName;
	FName Category;
	FName IconName;
	const UScriptStruct* ValueStruct = nullptr;
	EMetaLandRoadAttributeEvalMode EvalMode = EMetaLandRoadAttributeEvalMode::Stepped;
	bool bAllowOnLane = true;
	bool bAllowOnSectionCenter = false;
};

/** Base value type for lane attributes along SOffset. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	virtual ~FMetaLandRoadLaneAttributeValue() = default;

	/** Blend this toward Other. Default keeps this (stepped / non-numeric types). */
	virtual void InterpolateWith(const FMetaLandRoadLaneAttributeValue& Other, float Alpha);
};

/** Landscape deform along S. Keys use Perlin smootherstep; 3+ keys mix clamped Catmull-Rom. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLandscapeAttributeValue : public FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Landscape, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LayerFactor = 1.0f;

	/** Extra fade beyond the solid road bed (cm). MetaRoad Side Falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Landscape, meta = (ClampMin = "0.0", DisplayName = "Side Falloff"))
	float SideFalloff = 2000.0f;

	/** Extra solid width beyond the mesh edge (cm). MetaRoad Side Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Landscape, meta = (DisplayName = "Side Offset"))
	float SideOffset = 0.0f;

	/** Extra bias on the captured mesh Z (cm). 0 = landscape meets the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Landscape, meta = (DisplayName = "Height Offset"))
	float HeightOffset = 0.0f;

	/** Kept in sync with Side* for leftover JumpFlood / actor falloff. */
	UPROPERTY()
	FRoadFalloffSettings FalloffSettings;

	FMetaLandRoadLandscapeAttributeValue();
	void MigrateLegacyFields();
	void SyncLegacyFromFalloff();
	void SyncFalloffFromMetaRoadFields();
	void PostSerialize(const FArchive& Ar);

	virtual void InterpolateWith(const FMetaLandRoadLaneAttributeValue& Other, float Alpha) override;
	void InterpolateCatmullRom(
		const FMetaLandRoadLandscapeAttributeValue& P0,
		const FMetaLandRoadLandscapeAttributeValue& P1,
		const FMetaLandRoadLandscapeAttributeValue& P2,
		const FMetaLandRoadLandscapeAttributeValue& P3,
		float T);

	static FMetaLandRoadAttributeTypeDesc GetTypeDesc();
};

/** Details-template edit proxy so new Landscape UPROPERTY fields appear automatically. */
USTRUCT()
struct METALANDROAD_API FMetaLandRoadLandscapeKeyDetails
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Key", meta = (EditCondition = "bCanEditSOffset"))
	double SOffset = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Key")
	FName KeyRole;

	UPROPERTY(EditAnywhere, Category = "Landscape", meta = (ShowOnlyInnerProperties))
	FMetaLandRoadLandscapeAttributeValue Landscape;

	UPROPERTY(Transient)
	bool bCanEditSOffset = true;
};

/** Mode panel when a junction is selected (no road key / SOffset). */
USTRUCT()
struct METALANDROAD_API FMetaLandRoadJunctionLandscapeDetails
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Landscape", meta = (ShowOnlyInnerProperties))
	FMetaLandRoadLandscapeAttributeValue Landscape;
};

/** Stepped speed-limit metadata. Unit is caller-defined. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadSpeedAttributeValue : public FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Speed)
	float MaxSpeed = 15.0f;

	static FMetaLandRoadAttributeTypeDesc GetTypeDesc();
};

/** Stepped road-mark profile name. Empty = no mark on that stretch. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadMarkAttributeValue : public FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mark)
	FName Profile;

	static FMetaLandRoadAttributeTypeDesc GetTypeDesc();
};

/** Stepped curb-cut payload at the key (cut center). */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadCurbCutAttributeValue : public FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut)
	bool bIsCross = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "10.0", ClampMax = "1000.0"))
	float WidthA = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "10.0", ClampMax = "1000.0"))
	float FalloffA = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "10.0", ClampMax = "1000.0"))
	float WidthB = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float FalloffB = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float Alpha = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float Depth = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut, meta = (ClampMin = "1", ClampMax = "10"))
	int32 MeshDensity = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CurbCut)
	bool bSmooth = false;

	static FMetaLandRoadAttributeTypeDesc GetTypeDesc();
};

/** Stepped 2D overlay. Profile is a name until polygon assets exist. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadPolygonAttributeValue : public FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Polygon)
	FName Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Polygon, meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float Alpha = 0.5f;

	static FMetaLandRoadAttributeTypeDesc GetTypeDesc();
};

/** Sidewalk vertex Z offset along S. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadSidewalkHeightAttributeValue : public FMetaLandRoadLaneAttributeValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sidewalk, meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float Height = 0.0f;

	virtual void InterpolateWith(const FMetaLandRoadLaneAttributeValue& Other, float Alpha) override;

	static FMetaLandRoadAttributeTypeDesc GetTypeDesc();
};

USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneAttributeKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneAttribute)
	double SOffset = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneAttribute)
	TInstancedStruct<FMetaLandRoadLaneAttributeValue> Value;

	bool operator<(const FMetaLandRoadLaneAttributeKey& Other) const;
};

/** Built-in + future custom types. Register once; Pin/AddKey/Eval stay the same. */
class METALANDROAD_API FMetaLandRoadAttributeRegistry
{
public:
	static const FName TypeMark;
	static const FName TypeSpeed;
	static const FName TypeCurbCut;
	static const FName TypeLandscape;
	static const FName TypePolygon;
	static const FName TypeSidewalkHeight;

	static void RegisterBuiltInTypes();
	static bool RegisterType(const FMetaLandRoadAttributeTypeDesc& Desc);
	static const FMetaLandRoadAttributeTypeDesc* FindType(FName TypeId);
	static const TArray<FMetaLandRoadAttributeTypeDesc>& GetTypes();
	static TInstancedStruct<FMetaLandRoadLaneAttributeValue> MakeDefaultValue(FName TypeId);
	static bool CanPinOn(FName TypeId, EMetaLandRoadAttributeHost Host);
};

/** One typed track of keys along S. Map key on lane/section is TypeId. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadLaneAttribute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = LaneAttribute)
	FName TypeId;

	UPROPERTY(EditAnywhere, Category = LaneAttribute)
	TArray<FMetaLandRoadLaneAttributeKey> Keys;

	void Trim(double S0, double S1);
	void SortKeys();
	void EnsureFirstKeyAtZero();
	void EnsureBeginAndEndKeys(double EndS);

	int32 AddKey(double SOffset, double DuplicateTolerance = 1.0);
	bool RemoveKey(int32 KeyIndex);
	bool SetKeySOffset(int32 KeyIndex, double NewSOffset, double MaxSOffset = -1.0);
	int32 FindKeyIndex(double SOffset, double Tolerance = 1.0) const;
	bool CanRemoveKey(int32 KeyIndex) const;
	bool IsBeginKey(int32 KeyIndex) const;
	bool IsEndKey(int32 KeyIndex) const;
	void ApplyFalloffToKey(int32 KeyIndex, const FRoadFalloffSettings& Falloff);
	void ApplyFalloffToAllKeys(const FRoadFalloffSettings& Falloff);
	bool GetKeyFalloff(int32 KeyIndex, FRoadFalloffSettings& OutFalloff) const;

	bool EvalAt(double SOffset, TInstancedStruct<FMetaLandRoadLaneAttributeValue>& OutValue) const;
	bool EvalAsLandscape(double SOffset, FMetaLandRoadLandscapeAttributeValue& OutValue) const;

	static FMetaLandRoadLaneAttribute* PinOnMap(
		TMap<FName, FMetaLandRoadLaneAttribute>& Map,
		FName InTypeId,
		EMetaLandRoadAttributeHost Host);
	static bool UnpinFromMap(TMap<FName, FMetaLandRoadLaneAttribute>& Map, FName InTypeId);
	static FMetaLandRoadLaneAttribute* FindOnMap(TMap<FName, FMetaLandRoadLaneAttribute>& Map, FName InTypeId);
	static const FMetaLandRoadLaneAttribute* FindOnMap(const TMap<FName, FMetaLandRoadLaneAttribute>& Map, FName InTypeId);
};
