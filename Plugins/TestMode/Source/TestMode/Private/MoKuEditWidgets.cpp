// Fill out your copyright notice in the Description page of Project Settings.


#include "MoKuEditWidgets.h"
#include "SlateOptMacros.h"
#include "AssetThumbnail.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "Misc/Optional.h"

TSharedRef<SWidget> FMoKuAssetViewItemHelper::CreateTileItemContents(SMoKuAssetTileItem* const InTileItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	return CreateTileItemContentsImp(InTileItem, InThumbnail, OutItemShadowBorder);
}


template<typename T>
TSharedRef<SWidget> FMoKuAssetViewItemHelper::CreateTileItemContentsImp(T* const InTileItemconst, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	TSharedRef<SOverlay> ItemContentsOverlay = SNew(SOverlay);

	OutItemShadowBorder = FName("ContentBrowser.ThumbnailShadow");

	ItemContentsOverlay->AddSlot()
		[

			InThumbnail
			
		];

	ItemContentsOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.MaxDesiredWidth(InTileItemconst, &T::GetStateIconImageSize)
			.MaxDesiredHeight(InTileItemconst, &T::GetStateIconImageSize)
			[
				SNew(SImage)
				.Image(InTileItemconst, &T::GetSCCStateImage)
			]
		];

	return ItemContentsOverlay;
}



BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAssetItemWidgets::Construct(const FArguments& InArgs)
{
	AssetItem = InArgs._AssetItem;
	//ThumbnailEditMode = InArgs._ThumbnailEditMode;
	IsSelected = InArgs._IsSelected;

}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION



BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SMoKuAssetTileItem::Construct(const FArguments& InArgs)
{
	SAssetItemWidgets::Construct(SAssetItemWidgets::FArguments()
		.AssetItem(InArgs._AssetItem)
		.IsSelected(InArgs._IsSelected)
	);


	AssetThumbnail = InArgs._AssetThumbnail;
	ItemWidth = InArgs._ItemWidth;
	ThumbnailPadding = InArgs._ThumbnailPadding;


	TSharedPtr<SWidget> Thumbnail;
	if (AssetItem.IsValid() && AssetThumbnail.IsValid())
	{

		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.bForceGenericThumbnail = InArgs._AlwaysUseGenericThumbnail;
		ThumbnailConfig.bAllowHintText = InArgs._AllowThumbnailHintLabel;
		ThumbnailConfig.ThumbnailLabel = InArgs._ThumbnailLabel;
		ThumbnailConfig.HighlightedText = InArgs._HighlightText;
		ThumbnailConfig.HintColorAndOpacity = InArgs._ThumbnailHintColorAndOpacity;

		Thumbnail = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}
	else
	{
		Thumbnail = SNew(SImage).Image(FAppStyle::GetDefaultBrush());
	}





	FName ItemShadowBorderName;
	TSharedRef<SWidget> ItemContents = FMoKuAssetViewItemHelper::CreateTileItemContents(this, Thumbnail.ToSharedRef(), ItemShadowBorderName);

	ChildSlot
	[
		SNew(SBorder)
		//.Padding(FMargin(0.0f, 0.0f, 5.0f, 5.0f))
		.BorderImage(this, &SMoKuAssetTileItem::GetBorderImage)
		[

			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				// The remainder of the space is reserved for the name.
				SNew(SBox)
				.Padding(ThumbnailPadding - 4.0f)
				.WidthOverride(this,  &SMoKuAssetTileItem::GetThumbnailBoxSize)
				.HeightOverride(this, &SMoKuAssetTileItem::GetThumbnailBoxSize)
				[
					// Drop shadow border
					SNew(SBorder)
					.Padding(1.0f)
					.BorderImage(FAppStyle::GetBrush(ItemShadowBorderName))
					[
						ItemContents
					]
				]
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(2.f, 0.0))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.FillHeight(1.f)
			[
				SAssignNew(AssetNameWidget, SInlineEditableTextBlock)
				.Font(this, &SMoKuAssetTileItem::GetThumbnailFont)
				.Text(GetNameText())
				.HighlightText(InArgs._HighlightText)
				.Justification(ETextJustify::Center)
				.IsReadOnly(true)
				.IsSelected(InArgs._IsSelectedExclusively)
				.MultiLine(true)

			]
		]
	];

}


END_SLATE_FUNCTION_BUILD_OPTIMIZATION




FOptionalSize SMoKuAssetTileItem::GetStateIconImageSize() const
{
	FOptionalSize IconSize = FOptionalSize(ItemWidth.Get());
	return IconSize.Get() > 12 ? IconSize : 12;
}


const FSlateBrush* SMoKuAssetTileItem::GetSCCStateImage() const
{
	return ThumbnailEditMode.Get() ? FAppStyle::GetNoBrush() : SCCStateBrush;
}


const FSlateBrush* SMoKuAssetTileItem::GetBorderImage() const
{
	const bool bIsSelected = IsSelected.IsBound() ? IsSelected.Execute() : false;
	const bool bIsHoveredOrDraggedOver = IsHovered();
	if (bIsSelected && bIsHoveredOrDraggedOver)
	{
		static const FName SelectedHover("ContentBrowser.AssetTileItem.SelectedHoverBorder");
		return FAppStyle::Get().GetBrush(SelectedHover);
	}
	else if (bIsSelected)
	{
		static const FName Selected("ContentBrowser.AssetTileItem.SelectedBorder");
		return FAppStyle::Get().GetBrush(Selected);
	}
	else if (bIsHoveredOrDraggedOver)
	{
		static const FName Hovered("ContentBrowser.AssetTileItem.HoverBorder");
		return FAppStyle::Get().GetBrush(Hovered);
	}

	return FStyleDefaults::GetNoBrush();
}

FText SMoKuAssetTileItem::GetNameText() const
{
	if (AssetItem)
	{
		FName Name = AssetItem->AssetName;
		return FText::FromName(Name);
	}

	return FText();
}

FOptionalSize SMoKuAssetTileItem::GetThumbnailBoxSize() const
{
	return FOptionalSize(ItemWidth.Get());
}



FSlateFontInfo SMoKuAssetTileItem::GetThumbnailFont() const
{
	FOptionalSize ThumbSize = GetThumbnailBoxSize();
	if (ThumbSize.IsSet())
	{
		float Size = ThumbSize.Get();
		if (Size < 50)
		{
			const static FName SmallFontName("ContentBrowser.AssetTileViewNameFontVerySmall");
			return FAppStyle::GetFontStyle(SmallFontName);
		}
		else if (Size < 85)
		{
			const static FName SmallFontName("ContentBrowser.AssetTileViewNameFontSmall");
			return FAppStyle::GetFontStyle(SmallFontName);
		}
	}

	const static FName RegularFont("ContentBrowser.AssetTileViewNameFont");
	return FAppStyle::GetFontStyle(RegularFont);
}

void SMoKuAssetTileItem::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const float PrevSizeX = LastGeometry.Size.X;

	LastGeometry = AllottedGeometry;

	if (PrevSizeX != AllottedGeometry.Size.X && AssetNameWidget.IsValid())
	{
		AssetNameWidget->SetWrapTextAt(GetNameTextWrapWidth());
	}
}

float SMoKuAssetTileItem:: GetNameTextWrapWidth() const
{ 
	return LastGeometry.GetLocalSize().X - 2.f; 
}


void SMoKuRecommendAssetView::Construct(const FArguments& InArgs)
{

	static TOptional<FLinearColor> BasicShapeColorOverride;

	ThumbnailHintFadeInSequence.JumpToStart();

	ThumbnailHintFadeInSequence.AddCurve(0, 0.5f, ECurveEaseFunction::Linear);

	int32 SortOrder = 0;
	//bFillEmptySpaceInTileView = InArgs._FillEmptySpaceInTileView;
	FillScale = 1.0f;

	FDisplayMetrics DisplayMetrics;

	FSlateApplication::Get().GetCachedDisplayMetrics(DisplayMetrics);

	const FVector2D DisplaySize(
		DisplayMetrics.PrimaryDisplayWorkAreaRect.Right - DisplayMetrics.PrimaryDisplayWorkAreaRect.Left,
		DisplayMetrics.PrimaryDisplayWorkAreaRect.Bottom - DisplayMetrics.PrimaryDisplayWorkAreaRect.Top);

	const float ThumbnailScaleRangeScalar = (DisplaySize.Y / 1080);

	TileViewThumbnailResolution = 256;
	TileViewThumbnailSize = 48;
	TileViewThumbnailPadding = 2;

	MinThumbnailScale = 0.2f * ThumbnailScaleRangeScalar;
	MaxThumbnailScale = 1.5f * ThumbnailScaleRangeScalar;

	HighlightedText = InArgs._HighlightedText; 
	EditorGizmo = InArgs._EditorGizmo;
	//AllowThumbnailHintLabel = InArgs._AllowThumbnailHintLabel;
	ChildSlot
	[
		SNew(SBorder)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(2)
			.AutoHeight()
			[
				SAssignNew(ViewContainer, SBorder)
				.Padding(0)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				[
					SAssignNew(TileView, STileView<TSharedPtr<FAssetData>>)
					.ListItemsSource(&FilterItems)
					.ItemHeight(this, &SMoKuRecommendAssetView::GetTileViewItemHeight)
					.ItemWidth(this, &SMoKuRecommendAssetView::GetTileViewItemWidth)
					.OnGenerateTile(this, &SMoKuRecommendAssetView::MakeTileViewWidget)
					.OnMouseButtonClick(this, &SMoKuRecommendAssetView::OnListMouseButtonClick)

				]
			]
		]

	];

	RefreshVisualView();
}


void SMoKuRecommendAssetView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CalculateFillScale(AllottedGeometry);
	CalculateThumbnailHintColorAndOpacity();
}



TSharedRef<ITableRow> SMoKuRecommendAssetView::MakeTileViewWidget(TSharedPtr<FAssetData> InAssetItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!ensure(InAssetItem.IsValid()))
	{
		return SNew(STableRow<TSharedPtr<FAssetData>>, OwnerTable);
	}

	TSharedPtr< STableRow<TSharedPtr<FAssetData>>> TableRowWidget;
	SAssignNew(TableRowWidget, STableRow<TSharedPtr<FAssetData>>, OwnerTable)
		.Style(FAppStyle::Get(), "ContentBrowser.AssetListView.TileTableRow");

	TSharedRef<SMoKuAssetThumbnail> AssetLibarayThumbnail = SNew(SMoKuAssetThumbnail, *(InAssetItem.Get()));

	TSharedPtr<FAssetThumbnail> Thumbnail = AssetLibarayThumbnail->GetAssetThumbnail();

	TSharedRef<SMoKuAssetTileItem> Item =
		SNew(SMoKuAssetTileItem)
		.AssetThumbnail(Thumbnail)
		.AssetItem(InAssetItem)
		.ThumbnailPadding(0)
		.ItemWidth(this, &SMoKuRecommendAssetView::GetTileViewItemWidth)
		.ThumbnailHintColorAndOpacity(this, &SMoKuRecommendAssetView::GetThumbnailHintColorAndOpacity)
		.HighlightText(HighlightedText)
		.IsSelected(FIsSelected::CreateSP(TableRowWidget.Get(), &STableRow<TSharedPtr<FAssetData>>::IsSelected))
		.IsSelectedExclusively(FIsSelected::CreateSP(TableRowWidget.Get(), &STableRow<TSharedPtr<FAssetData>>::IsSelectedExclusively));

	TableRowWidget->SetContent(Item);

	return TableRowWidget.ToSharedRef();
}



float SMoKuRecommendAssetView::GetTileViewItemWidth() const
{
	return GetTileViewItemBaseWidth() * FillScale*0.8;
}

float SMoKuRecommendAssetView::GetTileViewItemBaseWidth() const
{

	return(TileViewThumbnailSize + TileViewThumbnailPadding * 2) * FMath::Lerp(MinThumbnailScale, MaxThumbnailScale,1.0f);

}

void SMoKuRecommendAssetView::CalculateFillScale(const FGeometry& AllottedGeometry)
{
	float ItemWidth = GetTileViewItemBaseWidth();

	const float ScrollbarWidth = 16 + 1;
	float TotalWidth = AllottedGeometry.GetLocalSize().X - (ScrollbarWidth / AllottedGeometry.Scale);
	float Coverage = TotalWidth / ItemWidth;
	int32 Items = (int)(TotalWidth / ItemWidth);

	if (Items > 0)
	{
		float GapSpace = ItemWidth * (Coverage - Items);
		float ExpandAmount = GapSpace / (float)Items;
		FillScale = (ItemWidth + ExpandAmount) / ItemWidth;
		FillScale = FMath::Max(1.0f, FillScale);
	}
	else
	{
		FillScale = 1.0f;
	}

}


float SMoKuRecommendAssetView::GetTileViewItemHeight() const
{
	return TileViewNameHeight + GetTileViewItemBaseHeight() * FillScale*1.2;
}

float SMoKuRecommendAssetView::GetTileViewItemBaseHeight() const
{

	return (TileViewThumbnailSize + TileViewThumbnailPadding * 2) * FMath::Lerp(MinThumbnailScale, MaxThumbnailScale,1.0f);

}

void SMoKuRecommendAssetView::CalculateThumbnailHintColorAndOpacity()
{

	if (HighlightedText.Get().IsEmpty())
	{
		if (ThumbnailHintFadeInSequence.IsPlaying())
		{
			if (ThumbnailHintFadeInSequence.IsForward())
			{
				ThumbnailHintFadeInSequence.Reverse();
			}
		}
		else if (ThumbnailHintFadeInSequence.IsAtEnd())
		{
			ThumbnailHintFadeInSequence.PlayReverse(this->AsShared());
		}
	}
	else
	{
		if (ThumbnailHintFadeInSequence.IsPlaying())
		{
			if (ThumbnailHintFadeInSequence.IsInReverse())
			{
				ThumbnailHintFadeInSequence.Reverse();
			}
		}
		else if (ThumbnailHintFadeInSequence.IsAtStart())
		{
			ThumbnailHintFadeInSequence.Play(this->AsShared());
		}
		const float Opacity = ThumbnailHintFadeInSequence.GetLerp();
		ThumbnailHintColorAndOpacity = FLinearColor(1.0, 1.0, 1.0, Opacity);
	}

}
FLinearColor SMoKuRecommendAssetView::GetThumbnailHintColorAndOpacity() const
{
	//We update this color in tick instead of here as an optimization
	return ThumbnailHintColorAndOpacity;
}

void SMoKuRecommendAssetView::OnListMouseButtonClick(TSharedPtr<FAssetData> AssetItem)
{
	FString ReplacePath = AssetItem->ToSoftObjectPath().ToString();
	UStaticMesh* ReplaceMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *ReplacePath));
	EditorGizmo.Get()->OwnerGizmoParams.OwnerMesh = ReplaceMesh;
	RefreshVisualViewImp();


	AMoKuEditSplineActor* CurActor = Cast<AMoKuEditSplineActor>(EditorGizmo.Get()->OwnerGizmoParams.OwnerActor);
	TOptional<int32> Index = EditorGizmo.Get()->OwnerGizmoParams.Index;
	TOptional<TMap<int,int>> ExtraMeshIndex = EditorGizmo.Get()->OwnerGizmoParams.ExtraAssetIndex;
	if(Index.IsSet())
	{
		UStaticMeshComponent* SplineMesh = CurActor->GetSplineComponent()->LocalMeshComponents[Index.GetValue()];
		TArray<FMoKuSplineInterpPoint> ProceduralPoints = CurActor->GetSplineComponent()->GetProceduralPoints();

		ProceduralPoints[Index.GetValue()].Mesh = ReplaceMesh;
		SplineMesh->SetStaticMesh(ReplaceMesh);
		CurActor->GetSplineComponent()->SetProceduralPoints(ProceduralPoints);
	}
	if(ExtraMeshIndex.IsSet())
	{
		TArray<int>Keys;
		TMap<int32,int32>ExtraIndexMap =  ExtraMeshIndex.GetValue();
		ExtraMeshIndex.GetValue().GetKeys(Keys);

		
		TArray<UStaticMeshComponent*> Comps = CurActor->ExtraLocalMeshComponents[Keys[0]];
		TObjectPtr<UStaticMeshComponent> Comp = Comps[ExtraIndexMap[Keys[0]]];
		Comp->SetStaticMesh(ReplaceMesh);
	}
}

void SMoKuRecommendAssetView::RefreshVisualView()
{
	RefreshVisualViewImp();
}

//测试代码 直接引用的路径 而且没有匹配模式且非并发操作
void SMoKuRecommendAssetView::RefreshVisualViewImp()
{
	
	FString DataTablePath = "/Game/AssetTest/Recommend_Asset_1.Recommend_Asset_1";
	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *DataTablePath);
	TArray<TSharedPtr<FAssetData>>AssetVisibleView;
	AssetVisibleView.Reset();
	if(DataTable)
	{ 
	TArray<FString> TestAssetPath = DataTableUtils::GetColumnDataAsString(DataTable, FName(TEXT("assetpath")), EDataTableExportFlags::None);
	//for (auto& s : TestAssetPath)	UE_LOG(LogTemp, Warning, TEXT("s==%s"), *s)

	UMoKuEditorGizmoBase* MoKuEditorGizmoBase = EditorGizmo.Get();
	if (MoKuEditorGizmoBase->OwnerGizmoParams.OwnerMesh)
	{

		FString AssetPath = MoKuEditorGizmoBase->OwnerGizmoParams.OwnerMesh.GetPathName();
		int Index = TestAssetPath.IndexOfByKey(AssetPath);

		if (Index != INDEX_NONE)
		{
			TArray<FString> ReferencePath = DataTableUtils::GetColumnDataAsString(DataTable, FName(TEXT("staticasset")), EDataTableExportFlags::None);
			const UScriptStruct* DataTableStruct = DataTable->GetRowStruct();
			TArray<FName> RowNames = DataTable->GetRowNames();
			const uint8* RowData = DataTable->FindRowUnchecked(RowNames[Index]);

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

			for (TFieldIterator<FProperty> It(DataTableStruct); It; ++It)
			{
				FProperty* Property = *It;
				FName PropertyName = Property->GetFName();
				if (Property->IsA(FArrayProperty::StaticClass()))
				{
					FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
					FProperty* InnerProperty = ArrayProperty->Inner;
					if (InnerProperty->IsA(FObjectProperty::StaticClass()))
					{
						const void* ArrayData = ArrayProperty->ContainerPtrToValuePtr<void>(RowData);
						FScriptArrayHelper Helper(ArrayProperty, ArrayData);
						for (int32 Idx = 0; Idx < Helper.Num(); ++Idx)
						{
							FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InnerProperty);
							UObject* Obj = ObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Idx));
							UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
							if (Mesh)
							{
								 //Will deprected  UE5.6
								FAssetData MeshAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*Mesh->GetPathName());
								TSharedPtr<FAssetData> Info = MakeShared<FAssetData>(MeshAssetData);
								AssetVisibleView.Add(Info);
							}
						}
					}
					SetFilterItems(AssetVisibleView);
					RefreshListView();
				}
			}
		}

		}
	}

}
void SMoKuRecommendAssetView::RefreshListView()
{
	TileView->RequestListRefresh();
}
