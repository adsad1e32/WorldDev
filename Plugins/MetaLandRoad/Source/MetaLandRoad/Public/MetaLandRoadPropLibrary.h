#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/StaticMesh.h"

#include "MetaLandRoadPropLibrary.generated.h"

class UStaticMesh;
class UDataTable;

/** How a catalog mesh should be drawn. Layout enum is how to sample; this is how to instance. */
UENUM(BlueprintType)
enum class EMetaLandRoadPropDrawKind : uint8
{
	RigidInstance UMETA(DisplayName = "Rigid Instance"),
	DeformStrip UMETA(DisplayName = "Deform Strip"),
};

/** Span layout algorithm. Keep this list short; add values, not graphs. */
UENUM(BlueprintType)
enum class EMetaLandRoadPropLayout : uint8
{
	Off UMETA(DisplayName = "Off"),
	EvenSpacing UMETA(DisplayName = "Even Spacing"),
	DeformStrip UMETA(DisplayName = "Deform Strip"),
};

/** One PropSetting catalog entry. DataTable row. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadPropAssetRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	FText DisplayName;

	/** Wall, Equipment, Metal, Wood, Duckboard, Corrugated, … */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	TArray<FName> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	FName Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	FName Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	EMetaLandRoadPropDrawKind DrawKind = EMetaLandRoadPropDrawKind::RigidInstance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	EMetaLandRoadPropLayout SuggestedLayout = EMetaLandRoadPropLayout::EvenSpacing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop", meta = (ClampMin = "0.0"))
	float DefaultSpacingCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop")
	bool bEnabled = true;

	bool HasTag(FName Tag) const;
	bool HasAllTags(const TArray<FName>& Required) const;
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MetaLandRoad PropSetting"))
class METALANDROAD_API UMetaLandRoadPropSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetaLandRoadPropSettings();

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	UPROPERTY(EditAnywhere, config, Category = "PropSetting", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	TSoftObjectPtr<UDataTable> PropAssetTable;

	static const UDataTable* GetPropAssetTable();
};

struct METALANDROAD_API FMetaLandRoadPropLibrary
{
	static const FName TagMilitary;
	static const FName TagTrenches;
	static const FName TagWall;
	static const FName TagEquipment;
	static const FName TagMetal;
	static const FName TagWood;
	static const FName TagDuckboard;
	static const FName TagCorrugated;
	static const FName TagMegascans;

	static void GetEnabledRows(TArray<const FMetaLandRoadPropAssetRow*>& OutRows);
	static void FindRowsByTag(FName Tag, TArray<const FMetaLandRoadPropAssetRow*>& OutRows);
	static void FindRowsByAllTags(const TArray<FName>& Tags, TArray<const FMetaLandRoadPropAssetRow*>& OutRows);
	static const FMetaLandRoadPropAssetRow* FindRowByMesh(const TSoftObjectPtr<UStaticMesh>& Mesh);
	static const FMetaLandRoadPropAssetRow* FindDefaultFenceRow();
};
