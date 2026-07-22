// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Widgets/Views/STileView.h"
#include "MoKuEditorGizmoBase.h"

class SMoKuAssetTileItem;
struct FSlateFontInfo;



class SMoKuAssetThumbnail : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMoKuAssetThumbnail)
		: _Width(80)
		, _Height(80)
		, _AlwaysUseGenericThumbnail(false)
		, _AssetTypeColorOverride()
		{}

		SLATE_ARGUMENT(uint32, Width)

		SLATE_ARGUMENT(uint32, Height)

		SLATE_ARGUMENT(FName, ClassThumbnailBrushOverride)

		SLATE_ARGUMENT(bool, AlwaysUseGenericThumbnail)

		SLATE_ARGUMENT(TOptional<FLinearColor>, AssetTypeColorOverride)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FAssetData& InAsset)
	{
		Asset = InAsset;

		//FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		TSharedPtr<FAssetThumbnailPool> ThumbnailPool = UThumbnailManager::Get().GetSharedThumbnailPool();

		Thumbnail = MakeShareable(new FAssetThumbnail(Asset, InArgs._Width, InArgs._Height, ThumbnailPool));

		FAssetThumbnailConfig Config;
		Config.bForceGenericThumbnail = InArgs._AlwaysUseGenericThumbnail;
		Config.ClassThumbnailBrushOverride = InArgs._ClassThumbnailBrushOverride;
		Config.AssetTypeColorOverride = InArgs._AssetTypeColorOverride;
		ChildSlot
		[
			Thumbnail->MakeThumbnailWidget(Config)
		];
	}

	TSharedPtr<FAssetThumbnail> GetAssetThumbnail() const
	{

		return Thumbnail;
	}

private:

	FAssetData Asset;
	TSharedPtr< FAssetThumbnail > Thumbnail;
};


struct FMoKuAssetViewItemHelper
{
public:

	static TSharedRef<SWidget> CreateTileItemContents(SMoKuAssetTileItem* const InTileItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder);

private:
	template <typename T>
	static TSharedRef<SWidget> CreateTileItemContentsImp(T* const InTileItemconst, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder);
};


class SAssetItemWidgets : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetItemWidgets)
	{}

	SLATE_ARGUMENT(TSharedPtr<FAssetData>, AssetItem)
	SLATE_ARGUMENT(FIsSelected, IsSelected)
	SLATE_END_ARGS()


	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);



protected:

	TSharedPtr<FAssetData> AssetItem;
	FIsSelected IsSelected;

};


class SMoKuAssetTileItem : public SAssetItemWidgets
{

	friend FMoKuAssetViewItemHelper;
	SLATE_BEGIN_ARGS(SMoKuAssetTileItem)
		: _ThumbnailEditMode(false)
		,_ItemWidth(16)
		, _AllowThumbnailHintLabel(true)
		, _ThumbnailLabel(EThumbnailLabel::AssetName)
		, _ThumbnailHintColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
		, _ThumbnailPadding(0)
		, _AlwaysUseGenericThumbnail(false)

		{}
		SLATE_ATTRIBUTE(bool, ThumbnailEditMode)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_ATTRIBUTE(float, ItemWidth)
		SLATE_ARGUMENT(bool, AllowThumbnailHintLabel)
		SLATE_ARGUMENT(EThumbnailLabel::Type, ThumbnailLabel)
		SLATE_ATTRIBUTE(FLinearColor, ThumbnailHintColorAndOpacity)
		SLATE_ARGUMENT(float, ThumbnailPadding)
		SLATE_ARGUMENT(bool, AlwaysUseGenericThumbnail)
		SLATE_ARGUMENT(TSharedPtr<FAssetData>, AssetItem)
		SLATE_ARGUMENT(FIsSelected, IsSelected)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnail>, AssetThumbnail)
		SLATE_ARGUMENT(FIsSelected, IsSelectedExclusively)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SMoKuAssetTileItem(){};
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);

protected:

	virtual float GetNameTextWrapWidth() const;



private:

	TSharedPtr<FAssetThumbnail> AssetThumbnail;
	TAttribute<float> ItemWidth;
	float ThumbnailPadding;
	
protected:
	TAttribute<bool> ThumbnailEditMode;
	const FSlateBrush* SCCStateBrush;
	TSharedPtr<SInlineEditableTextBlock> AssetNameWidget;
	FGeometry LastGeometry;

private:

	FOptionalSize GetStateIconImageSize() const;
	const FSlateBrush* GetSCCStateImage() const;
	const FSlateBrush* GetBorderImage()const;
	FText GetNameText() const;
	FOptionalSize GetThumbnailBoxSize() const;
	FSlateFontInfo GetThumbnailFont() const;
};



class SMoKuRecommendAssetView : public SCompoundWidget
{
	friend class SAssetViewItemToolTip;

public:
	SLATE_BEGIN_ARGS(SMoKuRecommendAssetView)
		: _ThumbnailScale(1)
		, _FillEmptySpaceInTileView(true)
		, _AllowThumbnailHintLabel(true)
		, _EditorGizmo()
		{}
		//SLATE_ARGUMENT(TSharedPtr<SDockTab>, ParentTab)
		SLATE_ATTRIBUTE(float, ThumbnailScale)
		SLATE_ATTRIBUTE(FText, HighlightedText)
		SLATE_ARGUMENT(bool, FillEmptySpaceInTileView)
		SLATE_ARGUMENT(bool, AllowThumbnailHintLabel)
		SLATE_ARGUMENT(TWeakObjectPtr<UMoKuEditorGizmoBase>,EditorGizmo)



	SLATE_END_ARGS()
	virtual void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void RefreshVisualView();

private:

	float FillScale = 1;
	FLinearColor ThumbnailHintColorAndOpacity;
	TAttribute< FText > HighlightedText;
	FCurveSequence ThumbnailHintFadeInSequence;
	int32 TileViewThumbnailResolution;
	int32 TileViewThumbnailSize;
	int32 TileViewThumbnailPadding;
	int32 TileViewNameHeight;
	float MinThumbnailScale;
	float MaxThumbnailScale;
	TSharedPtr<SBorder> ViewContainer;
	TArray<TSharedPtr<FAssetData>> FilterItems;
	TSharedPtr<STileView<TSharedPtr<FAssetData>>> TileView;
	TWeakObjectPtr<UMoKuEditorGizmoBase> EditorGizmo;

private:

	TSharedRef<ITableRow> MakeTileViewWidget(TSharedPtr<FAssetData> InAssetItem, const TSharedRef<STableViewBase>& OwnerTable);
	float GetTileViewItemWidth() const;
	float GetTileViewItemBaseWidth() const;
	float GetTileViewItemHeight() const;
	float GetTileViewItemBaseHeight() const;
	void CalculateFillScale(const FGeometry& AllottedGeometry);
	FLinearColor GetThumbnailHintColorAndOpacity() const;
	void CalculateThumbnailHintColorAndOpacity();
	void OnListMouseButtonClick(TSharedPtr<FAssetData> InAssetItem);

	//²âÊÔº¯Êý
	void RefreshVisualViewImp();
	void RefreshListView();
	void SetFilterItems(TArray<TSharedPtr<FAssetData>> InFilterItems) { FilterItems = InFilterItems; }

};












