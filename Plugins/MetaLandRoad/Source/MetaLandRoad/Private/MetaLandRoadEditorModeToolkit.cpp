// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaLandRoadEditorModeToolkit.h"
#include "MetaLandRoadEditorMode.h"
#include "Engine/Selection.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorDelegates.h"
#include "UObject/StructOnScope.h"
#include "EditorModeManager.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "MoKuEditSplineActor.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadEditorModeCommands.h"
#include "MetaLandRoadModule.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadLaneAttribute.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SSeparator.h"
#include "Styling/AppStyle.h"
#include "Templates/Function.h"


#define LOCTEXT_NAMESPACE "MetaLandRoadEditorModeToolkit"

FMetaLandRoadEditorModeToolkit::FMetaLandRoadEditorModeToolkit()
{
}

void FMetaLandRoadEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{

	//TestEditMode=(UMetaLandRoadEditorMode*)InOwningMode.Get();

	//SAssignNew(SplineEditWidget, SSplineEditPalette);
	TSharedPtr<SVerticalBox> ToolkitWidgetVBox = SNew(SVerticalBox);
	SAssignNew(ToolkitWidget, SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Fill)
		[
			ToolkitWidgetVBox->AsShared()
		];
	FMetaLandRoadEditorModeCommands::Register();
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	EnsureLandscapeKeyDetailsView();
	FMetaLandRoadModule::Get().LandscapeKeyEditorRefresh.AddSP(
		this, &FMetaLandRoadEditorModeToolkit::RefreshLandscapeKeyDetails);
	ToolkitWidgetVBox->AddSlot().HAlign(HAlign_Fill).AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		BuildLandscapeAttributePanel()
	];
	ToolkitWidgetVBox->AddSlot().HAlign(HAlign_Fill).FillHeight(1.f)
	[
		DetailsView->AsShared()
	];
	//FMetaLandRoadEditorModeCommands& Commands = FMetaLandRoadEditorModeCommands::Get();
	//TArray<TSharedPtr<FUICommandInfo>> CreatePaletteItems
	//({
	//	Commands.InteractiveTool,
	//	Commands.ShapesTool,
	//});
}

void FMetaLandRoadEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FMetaLandRoadEditorModeToolkit::GetToolkitFName() const
{
	return FName("MetaLandRoadEditorMode");
}

FText FMetaLandRoadEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "MetaLandRoadEditorMode Toolkit");
}



TSharedPtr<SWidget> FMetaLandRoadEditorModeToolkit::GetInlineContent()const
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Fill)
		[
			ToolkitWidget.ToSharedRef()
		];
}



void  FMetaLandRoadEditorModeToolkit::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{

	//�����Ż��ռ�
	UpdateActiveToolProperties();
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveTool(EToolSide::Left);
	ActiveToolName = CurTool->GetToolInfo().ToolDisplayName;

	//CurTool->OnPropertySetsModified.AddSP(this, &FMetaLandRoadEditorModeToolkit::UpdateActiveToolProperties);
	FString ActiveToolIdentifier = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveToolName(EToolSide::Left);
	if (ActiveToolIdentifier == SPLINETOOLNAME)
	{
		SelectionDelegateHandle = USelection::SelectionChangedEvent.AddRaw(this, &FMetaLandRoadEditorModeToolkit::RefreshDetailsWidget);
		UObject* SelectObject = nullptr;
		RefreshDetailsWidget(SelectObject);
	}
	else
	{
		CurTool->OnPropertySetsModified.AddSP(this, &FMetaLandRoadEditorModeToolkit::UpdateActiveToolProperties);
	}
}

void  FMetaLandRoadEditorModeToolkit::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{

	DetailsView->SetObject(nullptr);
	ActiveToolName = FText::GetEmpty();

	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveTool(EToolSide::Left);
	if (CurTool)
	{
		CurTool->OnPropertySetsModified.RemoveAll(this);
		CurTool->OnPropertyModifiedDirectlyByTool.RemoveAll(this);
	}
	//CurTool->OnPropertySetsModified.RemoveAll(this);
	USelection::SelectionChangedEvent.Remove(SelectionDelegateHandle);
	SelectionDelegateHandle.Reset();
}


void FMetaLandRoadEditorModeToolkit::OnToolPaletteChanged(FName PaletteName)
{
	UE_LOG(LogTemp, Warning, TEXT("PaletteName==%s"), *PaletteName.ToString())
}

void FMetaLandRoadEditorModeToolkit::UpdateActiveToolProperties()
{
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveTool(EToolSide::Left);
	if (CurTool == nullptr)
	{
		return;
	}
	TArray<UObject*>ToolProperties = CurTool->GetToolProperties(true);
	DetailsView->SetObjects(ToolProperties,true);
}



void FMetaLandRoadEditorModeToolkit::RefreshDetailsWidget(UObject* SelectedObject)
{
	UInteractiveTool* CurTool = this->OwningEditorMode->GetToolManager()->GetActiveTool(EToolSide::Left);
	UEditorInteractiveToolsContext* ToolsContext = GetScriptableEditorMode()->GetInteractiveToolsContext();
	if (!CurTool)
	{
		return;
	}
	TArray<UObject*>ToolProperties = CurTool->GetToolProperties(true);
	USelection* SelectedItem = GEditor->GetSelectedActors();
	int SelectCount = GEditor->GetSelectedActorCount();
	TArray<UObject*> SelectedObjects;
	SelectedItem->GetSelectedObjects(SelectedObjects);
	for (UObject* Object : SelectedObjects)
	{
		if (Object->IsA<AMoKuEditSplineActor>())
		{		
			AMoKuEditSplineActor* SelecObject = Cast<AMoKuEditSplineActor>(Object);	
			ToolProperties.Add(Object);
		}

	}
	const bool bForceRefresh = true;
	DetailsView->SetObjects(ToolProperties, bForceRefresh);

}





UMetaLandRoadSplineComponent* FMetaLandRoadEditorModeToolkit::FindSelectedRoadSpline() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
		{
			if (UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent())
			{
				return Spline;
			}
		}
	}
	return nullptr;
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeAttributeClicked()
{
	OnLandscapeShowClicked();
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeShowClicked()
{
	FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
	UMetaLandRoadSplineComponent* Spline = FindSelectedRoadSpline();
	if (!Spline)
	{
		Spline = Module.GetActiveNativeEditSpline();
	}

	Module.EnterLandscapeAttributeOverlay();

	if (Spline)
	{
		Spline->EnsureAllCurvePointTypesForSmoothCorners(false);
		MetaLandRoadProfile::EnsureLandscapeAttributeOnSections(Spline);
		Spline->UpdateRoadLayout();
		if (TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis = Module.GetRoadComponentVisualizer())
		{
			RoadVis->SetActiveForSpline(Spline);
			RoadVis->SelectLandscapeAttributeKey(0, 0);
		}
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			Road->NotifyLandscapeBrushChanged(true, false, true);
		}
	}
	Module.SetLandscapeAttributeOverlayVisible(true);
	RefreshLandscapeKeyDetails();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeHideClicked()
{
	FMetaLandRoadModule::Get().HideLandscapeAttributeDisplay();
}

void FMetaLandRoadEditorModeToolkit::EnsureLandscapeKeyDetailsView()
{
	if (LandscapeKeyDetailsView.IsValid())
	{
		return;
	}

	FPropertyEditorModule& PropModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowScrollBar = false;

	FStructureDetailsViewArgs StructArgs;
	StructArgs.bShowObjects = true;
	StructArgs.bShowAssets = true;
	StructArgs.bShowClasses = true;
	StructArgs.bShowInterfaces = true;

	LandscapeKeyEditStruct = MakeShared<FStructOnScope>(FMetaLandRoadLandscapeKeyDetails::StaticStruct());
	LandscapeKeyDetailsView = PropModule.CreateStructureDetailView(
		DetailsArgs,
		StructArgs,
		LandscapeKeyEditStruct,
		LOCTEXT("LandscapeKeyStruct", "Landscape Key"));
	LandscapeKeyDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		this, &FMetaLandRoadEditorModeToolkit::OnLandscapeKeyDetailsFinished);
}

void FMetaLandRoadEditorModeToolkit::RefreshLandscapeKeyDetails()
{
	if (!LandscapeKeyDetailsView.IsValid() || !LandscapeKeyEditStruct.IsValid())
	{
		return;
	}

	bRefreshingLandscapeKeyDetails = true;
	FMetaLandRoadLandscapeKeyDetails* EditData = reinterpret_cast<FMetaLandRoadLandscapeKeyDetails*>(
		LandscapeKeyEditStruct->GetStructMemory());
	if (EditData)
	{
		FMetaLandRoadLandscapeAttributeValue Value;
		double S = 0.0;
		if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
			FMetaLandRoadModule::Get().GetRoadComponentVisualizer())
		{
			if (RoadVis->GetSelectedLandscapeKeyValue(Value, S))
			{
				EditData->SOffset = S;
				EditData->Landscape = Value;
				EditData->Landscape.MigrateLegacyFields();
				const int32 KeyIndex = RoadVis->GetSelectedLandscapeKeyDisplayIndex();
				const FText Title = RoadVis->GetSelectedLandscapeKeyTitle();
				const bool bIsEndKey = Title.ToString().Contains(TEXT("End"));
				EditData->bCanEditSOffset = KeyIndex != 0 && !bIsEndKey;
				if (KeyIndex == 0)
				{
					EditData->KeyRole = FName(TEXT("Begin"));
				}
				else if (bIsEndKey)
				{
					EditData->KeyRole = FName(TEXT("End"));
				}
				else
				{
					EditData->KeyRole = FName(*FString::Printf(TEXT("Key %d"), KeyIndex + 1));
				}
			}
		}
	}

	LandscapeKeyDetailsView->SetStructureData(LandscapeKeyEditStruct);
	bRefreshingLandscapeKeyDetails = false;
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeKeyDetailsFinished(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (bRefreshingLandscapeKeyDetails || !LandscapeKeyEditStruct.IsValid())
	{
		return;
	}

	const FMetaLandRoadLandscapeKeyDetails* EditData = reinterpret_cast<const FMetaLandRoadLandscapeKeyDetails*>(
		LandscapeKeyEditStruct->GetStructMemory());
	if (!EditData)
	{
		return;
	}

	const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadModule::Get().GetRoadComponentVisualizer();
	if (!RoadVis.IsValid())
	{
		return;
	}

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(FMetaLandRoadLandscapeKeyDetails, SOffset))
	{
		RoadVis->SetSelectedLandscapeKeySOffset(EditData->SOffset);
		return;
	}

	FMetaLandRoadLandscapeAttributeValue Value = EditData->Landscape;
	Value.SyncFalloffFromMetaRoadFields();
	RoadVis->SetSelectedLandscapeKeyValue(Value);
}

TSharedRef<SWidget> FMetaLandRoadEditorModeToolkit::BuildLandscapeAttributePanel()
{
	EnsureLandscapeKeyDetailsView();

	auto MatchesSearch = [this](const FText& DisplayName) -> bool
	{
		if (AttributeSearchText.IsEmpty())
		{
			return true;
		}
		return DisplayName.ToString().Contains(AttributeSearchText, ESearchCase::IgnoreCase);
	};

	auto MakeAttributeRow = [this](FName TypeId, const FText& DisplayName, const FName IconName, bool bPinned) -> TSharedRef<SWidget>
	{
		const bool bIsLandscape = TypeId == FMetaLandRoadAttributeRegistry::TypeLandscape;
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([bIsLandscape]()
			{
				return (bIsLandscape && FMetaLandRoadModule::Get().IsLandscapeAttributeSelected())
					? FLinearColor(0.05f, 0.28f, 0.55f, 1.0f)
					: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			})
			.Padding(FMargin(4.0f, 3.0f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(0.0f))
				.IsEnabled(bIsLandscape)
				.OnClicked_Lambda([this, bIsLandscape]()
				{
					if (bIsLandscape)
					{
						OnLandscapeAttributeClicked();
					}
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(bPinned ? "Icons.Pin" : "Icons.Unpin"))
						.ColorAndOpacity(bPinned
							? FSlateColor(FLinearColor(1.0f, 0.85f, 0.2f))
							: FSlateColor::UseSubduedForeground())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(IconName))
						.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(DisplayName)
						.ColorAndOpacity_Lambda([bPinned, bIsLandscape]()
						{
							if (bPinned || (bIsLandscape && FMetaLandRoadModule::Get().IsLandscapeAttributeSelected()))
							{
								return FSlateColor(FLinearColor(1.0f, 0.92f, 0.2f));
							}
							return FSlateColor::UseForeground();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::GetEmpty())
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchAttributes", "Search attributes..."))
				.OnTextChanged_Lambda([this](const FText& NewText)
				{
					AttributeSearchText = NewText.ToString();
					if (ToolkitWidget.IsValid())
					{
						ToolkitWidget->Invalidate(EInvalidateWidgetReason::Paint);
					}
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 22.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Pin"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AttributeCol", "Attribute"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TypeCol", "Type"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch]()
				{
					return MatchesSearch(LOCTEXT("MarkAttr", "Mark")) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(FMetaLandRoadAttributeRegistry::TypeMark, LOCTEXT("MarkAttr", "Mark"), "ClassIcon.Actor", false)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch]()
				{
					return MatchesSearch(LOCTEXT("SpeedAttr", "Speed")) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(FMetaLandRoadAttributeRegistry::TypeSpeed, LOCTEXT("SpeedAttr", "Speed"), "ClassIcon.Actor", false)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch]()
				{
					return MatchesSearch(LOCTEXT("CurbCutAttr", "Curb Cut")) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(FMetaLandRoadAttributeRegistry::TypeCurbCut, LOCTEXT("CurbCutAttr", "Curb Cut"), "GraphEditor.Curve_16x", false)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch]()
				{
					return MatchesSearch(LOCTEXT("LandscapeAttr", "Landscape")) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(FMetaLandRoadAttributeRegistry::TypeLandscape, LOCTEXT("LandscapeAttr", "Landscape"), "ClassIcon.Landscape", true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch]()
				{
					return MatchesSearch(LOCTEXT("PolygonAttr", "Polygon")) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(FMetaLandRoadAttributeRegistry::TypePolygon, LOCTEXT("PolygonAttr", "Polygon"), "Icons.Transform", false)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch]()
				{
					return MatchesSearch(LOCTEXT("SidewalkHeightAttr", "Sidewalk Height")) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(FMetaLandRoadAttributeRegistry::TypeSidewalkHeight, LOCTEXT("SidewalkHeightAttr", "Sidewalk Height"), "ClassIcon.StaticMeshActor", false)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([]()
				{
					return FMetaLandRoadModule::Get().IsLandscapeAttributeSelected()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ShowOverlay", "Show"))
					.OnClicked_Lambda([this]()
					{
						OnLandscapeShowClicked();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("HideOverlay", "Hide"))
					.OnClicked_Lambda([this]()
					{
						OnLandscapeHideClicked();
						return FReply::Handled();
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Visibility_Lambda([]()
				{
					return FMetaLandRoadModule::Get().IsLandscapeAttributeSelected()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.Text_Lambda([]()
				{
					if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
						FMetaLandRoadModule::Get().GetRoadComponentVisualizer())
					{
						return RoadVis->GetSelectedLandscapeKeyTitle();
					}
					return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
				})
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([]()
				{
					return FMetaLandRoadModule::Get().IsLandscapeAttributeSelected()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					LandscapeKeyDetailsView.IsValid()
						? LandscapeKeyDetailsView->GetWidget().ToSharedRef()
						: SNullWidget::NullWidget
				]
			]
		];
}

void FMetaLandRoadEditorModeToolkit::BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder)
{
	if (HasToolkitBuilder())
	{
		return;
	}
	const FMetaLandRoadEditorModeCommands& Commands = FMetaLandRoadEditorModeCommands::Get();
	ToolbarBuilder.AddToolBarButton(Commands.InteractiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.ShapesTool);
	
}


#undef LOCTEXT_NAMESPACE
