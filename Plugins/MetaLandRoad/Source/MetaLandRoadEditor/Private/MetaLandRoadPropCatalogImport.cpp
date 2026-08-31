#include "MetaLandRoadPropCatalogImport.h"
#include "MetaLandRoadPropLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MetaLandRoadPropCatalog
{
	static const TCHAR* TablePackagePath = TEXT("/MetaLandRoad/PropSetting/DT_PropSettingAssets");
	static const TCHAR* TableAssetName = TEXT("DT_PropSettingAssets");
	static const TCHAR* ScanRoot = TEXT("/Game/Fab/Megascans/3D");

	static bool IsNoiseToken(const FString& Token)
	{
		if (Token.Len() <= 1)
		{
			return true;
		}
		if (Token.IsNumeric())
		{
			return true;
		}
		if (Token.StartsWith(TEXT("yd")) && Token.Len() >= 6)
		{
			return true;
		}
		if (Token.Equals(TEXT("High"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("Low"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("3D"), ESearchCase::IgnoreCase)
			|| Token.Equals(TEXT("SM"), ESearchCase::IgnoreCase)
			|| Token.StartsWith(TEXT("tier"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	}

	static void InferFromPackage(const FString& PackagePath, FMetaLandRoadPropAssetRow& OutRow)
	{
		TArray<FString> Parts;
		PackagePath.ParseIntoArray(Parts, TEXT("/"), true);

		TSet<FName> TagSet;
		TagSet.Add(FMetaLandRoadPropLibrary::TagMegascans);

		FString FolderLabel;
		for (const FString& Part : Parts)
		{
			if (Part.Equals(TEXT("Game"), ESearchCase::IgnoreCase)
				|| Part.Equals(TEXT("Fab"), ESearchCase::IgnoreCase)
				|| Part.Equals(TEXT("Megascans"), ESearchCase::IgnoreCase)
				|| Part.Equals(TEXT("StaticMeshes"), ESearchCase::IgnoreCase)
				|| Part.Equals(TEXT("Materials"), ESearchCase::IgnoreCase)
				|| Part.Equals(TEXT("Textures"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			TArray<FString> Tokens;
			Part.ParseIntoArray(Tokens, TEXT("_"), true);
			if (Tokens.Num() >= 3 && FolderLabel.IsEmpty())
			{
				FolderLabel = Part;
			}

			for (FString& Token : Tokens)
			{
				if (IsNoiseToken(Token))
				{
					continue;
				}
				TagSet.Add(FName(*Token));
			}
		}

		OutRow.Tags = TagSet.Array();
		OutRow.Tags.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		if (TagSet.Contains(FMetaLandRoadPropLibrary::TagWall))
		{
			OutRow.Category = FMetaLandRoadPropLibrary::TagWall;
			OutRow.DrawKind = EMetaLandRoadPropDrawKind::DeformStrip;
			OutRow.SuggestedLayout = EMetaLandRoadPropLayout::DeformStrip;
			OutRow.DefaultSpacingCm = 200.0f;
		}
		else
		{
			OutRow.Category = FMetaLandRoadPropLibrary::TagEquipment;
			OutRow.DrawKind = EMetaLandRoadPropDrawKind::RigidInstance;
			OutRow.SuggestedLayout = EMetaLandRoadPropLayout::EvenSpacing;
			OutRow.DefaultSpacingCm = 400.0f;
		}

		if (TagSet.Contains(FMetaLandRoadPropLibrary::TagMetal))
		{
			OutRow.Material = FMetaLandRoadPropLibrary::TagMetal;
		}
		else if (TagSet.Contains(FMetaLandRoadPropLibrary::TagWood))
		{
			OutRow.Material = FMetaLandRoadPropLibrary::TagWood;
		}

		if (FolderLabel.IsEmpty())
		{
			FolderLabel = FPaths::GetBaseFilename(PackagePath);
		}
		OutRow.DisplayName = FText::FromString(FolderLabel.Replace(TEXT("_"), TEXT(" ")));
		OutRow.bEnabled = true;
	}

	static FName MakeRowName(const FAssetData& Asset)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!AssetName.IsEmpty())
		{
			return FName(*AssetName);
		}
		return FName(*FPaths::GetBaseFilename(Asset.PackageName.ToString()));
	}

	static UDataTable* LoadOrCreateTable(UPackage*& OutPackage)
	{
		OutPackage = CreatePackage(TablePackagePath);
		if (!OutPackage)
		{
			return nullptr;
		}

		UDataTable* Table = FindObject<UDataTable>(OutPackage, TableAssetName);
		if (!Table)
		{
			Table = NewObject<UDataTable>(OutPackage, TableAssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		Table->RowStruct = FMetaLandRoadPropAssetRow::StaticStruct();
		return Table;
	}

	static bool SaveTable(UPackage* Package, UDataTable* Table)
	{
		if (!Package || !Table)
		{
			return false;
		}

		FAssetRegistryModule::AssetCreated(Table);
		Package->MarkPackageDirty();
		Table->MarkPackageDirty();

		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(
			TablePackagePath, Filename, FPackageName::GetAssetPackageExtension()))
		{
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Table, *Filename, SaveArgs);
	}
}

int32 MetaLandRoadRebuildPropAssetTable(bool bOverwriteInferred)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(true);

	FARFilter Filter;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(MetaLandRoadPropCatalog::ScanRoot));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	UPackage* Package = nullptr;
	UDataTable* Table = MetaLandRoadPropCatalog::LoadOrCreateTable(Package);
	if (!Table)
	{
		UE_LOG(LogTemp, Error, TEXT("[PropSetting] Failed to create %s"), MetaLandRoadPropCatalog::TablePackagePath);
		return 0;
	}

	int32 Added = 0;
	int32 Updated = 0;
	for (const FAssetData& Asset : Assets)
	{
		const FName RowName = MetaLandRoadPropCatalog::MakeRowName(Asset);
		FMetaLandRoadPropAssetRow Inferred;
		Inferred.Mesh = TSoftObjectPtr<UStaticMesh>(Asset.ToSoftObjectPath());
		MetaLandRoadPropCatalog::InferFromPackage(Asset.PackageName.ToString(), Inferred);

		if (const FMetaLandRoadPropAssetRow* Existing = Table->FindRow<FMetaLandRoadPropAssetRow>(RowName, TEXT("Rebuild"), false))
		{
			if (!bOverwriteInferred && Existing->Tags.Num() > 0)
			{
				continue;
			}
			Table->RemoveRow(RowName);
			Table->AddRow(RowName, Inferred);
			++Updated;
			continue;
		}

		Table->AddRow(RowName, Inferred);
		++Added;
	}

	if (!MetaLandRoadPropCatalog::SaveTable(Package, Table))
	{
		UE_LOG(LogTemp, Error, TEXT("[PropSetting] Save failed for %s"), MetaLandRoadPropCatalog::TablePackagePath);
		return 0;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[PropSetting] DT_PropSettingAssets: scanned %d meshes, added %d, updated %d -> %s"),
		Assets.Num(), Added, Updated, MetaLandRoadPropCatalog::TablePackagePath);
	return Added + Updated;
}

static FAutoConsoleCommand CMetaLandRoadRebuildPropAssetTable(
	TEXT("MetaLandRoad.RebuildPropAssetTable"),
	TEXT("Scan /Game/Fab/Megascans/3D StaticMeshes into /MetaLandRoad/PropSetting/DT_PropSettingAssets. Arg Force = overwrite inferred tags."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		const bool bForce = Args.Contains(TEXT("Force"));
		MetaLandRoadRebuildPropAssetTable(bForce);
	}));
