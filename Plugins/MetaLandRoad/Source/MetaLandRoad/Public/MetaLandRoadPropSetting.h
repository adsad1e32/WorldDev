#pragma once

#include "CoreMinimal.h"
#include "MetaLandRoadPropLibrary.h"

#include "MetaLandRoadPropSetting.generated.h"

class UMetaLandRoadSplineComponent;

UENUM(BlueprintType)
enum class EMetaLandRoadPropEdgeRef : uint8
{
	Free,
	SectionLeft,
	SectionRight,
	LaneLeft,
	LaneRight,
};

USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadPropRailKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	double SOffset = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	EMetaLandRoadPropLayout Layout = EMetaLandRoadPropLayout::Off;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting", meta = (ClampMin = "0.0"))
	float SpacingCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	float ZOffsetCm = 0.0f;
};

USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadPropOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	int32 QuantizedS = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	bool bDeleted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	float DeltaS = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	float DeltaAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	float DeltaYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	TSoftObjectPtr<UStaticMesh> MeshOverride;
};

USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadPropRail
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	FGuid RailId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	EMetaLandRoadPropEdgeRef EdgeRef = EMetaLandRoadPropEdgeRef::Free;

	/** Used when EdgeRef is Free. Locked edges ignore this and use 0 or 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Alpha = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	TArray<FMetaLandRoadPropRailKey> Keys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSetting")
	TArray<FMetaLandRoadPropOverride> Overrides;

	float ResolveAlpha() const;
	void EnsureSpanKeys(double EndS);
	int32 AddSpanKey(double SOffset, double DuplicateTolerance = 1.0);
	bool RemoveSpanKey(int32 KeyIndex);
	bool SetSpanKeyS(int32 KeyIndex, double NewS, double MaxS);
	int32 FindSpanKey(double SOffset, double Tolerance = 1.0) const;
};

USTRUCT()
struct METALANDROAD_API FMetaLandRoadPropSettingDetails
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Rail")
	FName RailName;

	UPROPERTY(EditAnywhere, Category = "Rail")
	EMetaLandRoadPropEdgeRef EdgeRef = EMetaLandRoadPropEdgeRef::Free;

	UPROPERTY(EditAnywhere, Category = "Rail", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Alpha = 0.5f;

	UPROPERTY(VisibleAnywhere, Category = "Span", meta = (Hidden))
	double SOffset = 0.0;

	UPROPERTY(EditAnywhere, Category = "Span")
	EMetaLandRoadPropLayout Layout = EMetaLandRoadPropLayout::Off;

	UPROPERTY(EditAnywhere, Category = "Span", meta = (ClampMin = "0.0",
		ToolTip = "Distance between instance starts. 0 = pack using the mesh length along the rail."))
	float SpacingCm = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Span")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, Category = "Span")
	float ZOffsetCm = 0.0f;
};

struct METALANDROAD_API FMetaLandRoadPropSettingUtil
{
	static bool EvalRailWorld(
		const UMetaLandRoadSplineComponent* Spline,
		double WorldS,
		const FMetaLandRoadPropRail& Rail,
		FVector& OutWorld);

	static int32 QuantizeS(double WorldS);
};
