#include "MetaLandRoadPropLibrary.h"
#include "Engine/DataTable.h"

const FName FMetaLandRoadPropLibrary::TagMilitary(TEXT("Military"));
const FName FMetaLandRoadPropLibrary::TagTrenches(TEXT("Trenches"));
const FName FMetaLandRoadPropLibrary::TagWall(TEXT("Wall"));
const FName FMetaLandRoadPropLibrary::TagEquipment(TEXT("Equipment"));
const FName FMetaLandRoadPropLibrary::TagMetal(TEXT("Metal"));
const FName FMetaLandRoadPropLibrary::TagWood(TEXT("Wood"));
const FName FMetaLandRoadPropLibrary::TagDuckboard(TEXT("Duckboard"));
const FName FMetaLandRoadPropLibrary::TagCorrugated(TEXT("Corrugated"));
const FName FMetaLandRoadPropLibrary::TagMegascans(TEXT("Megascans"));

bool FMetaLandRoadPropAssetRow::HasTag(FName Tag) const
{
	return Tag.IsNone() ? false : Tags.Contains(Tag);
}

bool FMetaLandRoadPropAssetRow::HasAllTags(const TArray<FName>& Required) const
{
	for (const FName& Tag : Required)
	{
		if (!HasTag(Tag))
		{
			return false;
		}
	}
	return true;
}

UMetaLandRoadPropSettings::UMetaLandRoadPropSettings()
{
	PropAssetTable = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/MetaLandRoad/PropSetting/DT_PropSettingAssets.DT_PropSettingAssets")));
}

const UDataTable* UMetaLandRoadPropSettings::GetPropAssetTable()
{
	const UMetaLandRoadPropSettings* Settings = GetDefault<UMetaLandRoadPropSettings>();
	if (!Settings)
	{
		return nullptr;
	}

	const UDataTable* Table = Settings->PropAssetTable.LoadSynchronous();
	if (!Table || Table->GetRowStruct() != FMetaLandRoadPropAssetRow::StaticStruct())
	{
		return nullptr;
	}
	return Table;
}

void FMetaLandRoadPropLibrary::GetEnabledRows(TArray<const FMetaLandRoadPropAssetRow*>& OutRows)
{
	OutRows.Reset();
	const UDataTable* Table = UMetaLandRoadPropSettings::GetPropAssetTable();
	if (!Table)
	{
		return;
	}

	for (const FName& RowName : Table->GetRowNames())
	{
		const FMetaLandRoadPropAssetRow* Row = Table->FindRow<FMetaLandRoadPropAssetRow>(RowName, TEXT("PropLibrary"), false);
		if (Row && Row->bEnabled)
		{
			OutRows.Add(Row);
		}
	}
}

void FMetaLandRoadPropLibrary::FindRowsByTag(FName Tag, TArray<const FMetaLandRoadPropAssetRow*>& OutRows)
{
	OutRows.Reset();
	TArray<const FMetaLandRoadPropAssetRow*> Enabled;
	GetEnabledRows(Enabled);
	for (const FMetaLandRoadPropAssetRow* Row : Enabled)
	{
		if (Row && Row->HasTag(Tag))
		{
			OutRows.Add(Row);
		}
	}
}

void FMetaLandRoadPropLibrary::FindRowsByAllTags(const TArray<FName>& Tags, TArray<const FMetaLandRoadPropAssetRow*>& OutRows)
{
	OutRows.Reset();
	TArray<const FMetaLandRoadPropAssetRow*> Enabled;
	GetEnabledRows(Enabled);
	for (const FMetaLandRoadPropAssetRow* Row : Enabled)
	{
		if (Row && Row->HasAllTags(Tags))
		{
			OutRows.Add(Row);
		}
	}
}

const FMetaLandRoadPropAssetRow* FMetaLandRoadPropLibrary::FindRowByMesh(const TSoftObjectPtr<UStaticMesh>& Mesh)
{
	if (Mesh.IsNull())
	{
		return nullptr;
	}

	const FSoftObjectPath Wanted = Mesh.ToSoftObjectPath();
	TArray<const FMetaLandRoadPropAssetRow*> Enabled;
	GetEnabledRows(Enabled);
	for (const FMetaLandRoadPropAssetRow* Row : Enabled)
	{
		if (Row && Row->Mesh.ToSoftObjectPath() == Wanted)
		{
			return Row;
		}
	}
	return nullptr;
}

const FMetaLandRoadPropAssetRow* FMetaLandRoadPropLibrary::FindDefaultFenceRow()
{
	TArray<const FMetaLandRoadPropAssetRow*> Walls;
	FindRowsByTag(TagWall, Walls);
	for (const FMetaLandRoadPropAssetRow* Row : Walls)
	{
		if (Row && !Row->Mesh.IsNull())
		{
			return Row;
		}
	}
	TArray<const FMetaLandRoadPropAssetRow*> Enabled;
	GetEnabledRows(Enabled);
	return Enabled.Num() > 0 ? Enabled[0] : nullptr;
}
