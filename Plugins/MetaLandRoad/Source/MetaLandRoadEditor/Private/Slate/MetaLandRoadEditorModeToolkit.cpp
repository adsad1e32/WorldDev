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
#include "MoKuEditIntersectionActor.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadEditorModeCommands.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadPropSetting.h"
#include "MetaLandRoadCurbEditor.h"
#include "MetaLandRoadTypes.h"
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

static AMoKuEditSplineActor* GetToolkitEditedRoad()
{
	const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer();
	if (!RoadVis.IsValid())
	{
		return nullptr;
	}
	UMetaLandRoadSplineComponent* Spline = RoadVis->GetEditedMetaLandRoadSpline();
	return Spline ? Cast<AMoKuEditSplineActor>(Spline->GetOwner()) : nullptr;
}

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
	FMetaLandRoadEditorModule::Get().LandscapeKeyEditorRefresh.AddSP(
		this, &FMetaLandRoadEditorModeToolkit::RefreshLandscapeKeyDetails);
	FMetaLandRoadEditorModule::Get().PropSettingEditorRefresh.AddSP(
		this, &FMetaLandRoadEditorModeToolkit::RefreshPropSettingDetails);
	FMetaLandRoadEditorModule::Get().SidewalkEditorRefresh.AddSP(
		this, &FMetaLandRoadEditorModeToolkit::RefreshSidewalkDetails);
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
	if (!CurTool)
	{
		return;
	}
	ActiveToolName = CurTool->GetToolInfo().ToolDisplayName;
	CurTool->OnPropertySetsModified.AddSP(this, &FMetaLandRoadEditorModeToolkit::UpdateActiveToolProperties);
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
}


void FMetaLandRoadEditorModeToolkit::OnToolPaletteChanged(FName PaletteName)
{
	(void)PaletteName;
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



UMetaLandRoadSplineComponent* FMetaLandRoadEditorModeToolkit::FindSelectedRoadSpline() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawSidewalkOverlay())
	{
		if (UMetaLandRoadSplineComponent* Focus = FMetaLandRoadEditorModule::Get().GetSidewalkFocusSpline())
		{
			AActor* FocusOwner = Focus->GetOwner();
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (*It == FocusOwner)
				{
					return Focus;
				}
				if (AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(*It))
				{
					if (Junction->JunctionData && Junction->JunctionData->LinkedRoadActors.Contains(
						Cast<AMoKuEditSplineActor>(FocusOwner)))
					{
						return Focus;
					}
				}
			}
		}
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
		if (AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(*It))
		{
			if (Junction->JunctionData)
			{
				for (AMoKuEditSplineActor* Linked : Junction->JunctionData->LinkedRoadActors)
				{
					if (Linked && Linked->GetSplineComponent())
					{
						return Linked->GetSplineComponent();
					}
				}
			}
		}
	}
	return nullptr;
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeAttributeClicked()
{
	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	Module.EnterLandscapeAttributeOverlay();
	Module.DismissLandscapeOverlayKeepAttribute();
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeShowClicked()
{
	AActor* Selected = nullptr;
	if (GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			AActor* Candidate = Cast<AActor>(*It);
			if (Cast<AMoKuEditSplineActor>(Candidate) || Cast<AMoKuEditIntersectionActor>(Candidate))
			{
				Selected = Candidate;
				break;
			}
		}
	}

	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	if (Selected)
	{
		Module.ShowLandscapeOverlayForActor(Selected);
		RefreshLandscapeKeyDetails();
		return;
	}

	if (UMetaLandRoadSplineComponent* Spline = Module.GetActiveNativeEditSpline())
	{
		Module.ShowLandscapeOverlayForActor(Spline->GetOwner());
		RefreshLandscapeKeyDetails();
	}
}

void FMetaLandRoadEditorModeToolkit::OnLandscapeHideClicked()
{
	FMetaLandRoadEditorModule::Get().DismissLandscapeOverlayKeepAttribute();
}

void FMetaLandRoadEditorModeToolkit::OnPropSettingAttributeClicked()
{
	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	Module.EnterPropSettingOverlay();
	OnPropSettingShowClicked();
}

void FMetaLandRoadEditorModeToolkit::OnPropSettingShowClicked()
{
	AActor* Selected = nullptr;
	if (GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				Selected = Road;
				break;
			}
		}
	}

	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	if (Selected)
	{
		Module.ShowPropSettingOverlayForActor(Selected);
		RefreshPropSettingDetails();
		return;
	}
	if (UMetaLandRoadSplineComponent* Spline = Module.GetActiveNativeEditSpline())
	{
		Module.ShowPropSettingOverlayForActor(Spline->GetOwner());
		RefreshPropSettingDetails();
	}
}

void FMetaLandRoadEditorModeToolkit::OnPropSettingHideClicked()
{
	FMetaLandRoadEditorModule::Get().DismissPropSettingOverlay();
}

void FMetaLandRoadEditorModeToolkit::OnPropSettingAddRailClicked()
{
	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	if (!Module.ShouldDrawPropSettingOverlay())
	{
		OnPropSettingShowClicked();
	}
	if (Module.GetPropPlaceMode() == FMetaLandRoadEditorModule::EPropPlaceMode::AddRail)
	{
		Module.SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
		return;
	}
	Module.SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::AddRail);
}

void FMetaLandRoadEditorModeToolkit::OnPropSettingAddSpanClicked()
{
	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	if (!Module.ShouldDrawPropSettingOverlay())
	{
		OnPropSettingShowClicked();
	}
	if (Module.GetPropPlaceMode() == FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan)
	{
		Module.SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
		return;
	}
	Module.SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan);
}

void FMetaLandRoadEditorModeToolkit::EnsurePropSettingDetailsView()
{
	if (PropSettingDetailsView.IsValid())
	{
		return;
	}

	FPropertyEditorModule& PropModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowScrollBar = false;
	DetailsArgs.NotifyHook = this;

	FStructureDetailsViewArgs StructArgs;
	StructArgs.bShowObjects = true;
	StructArgs.bShowAssets = true;
	StructArgs.bShowClasses = true;
	StructArgs.bShowInterfaces = true;

	PropSettingEditStruct = MakeShared<FStructOnScope>(FMetaLandRoadPropSettingDetails::StaticStruct());
	PropSettingDetailsView = PropModule.CreateStructureDetailView(
		DetailsArgs,
		StructArgs,
		PropSettingEditStruct,
		LOCTEXT("PropSettingStruct", "PropSetting"));
	PropSettingDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		this, &FMetaLandRoadEditorModeToolkit::OnPropSettingDetailsFinished);
	if (IDetailsView* InnerView = PropSettingDetailsView->GetDetailsView())
	{
		InnerView->OnFinishedChangingProperties().AddSP(
			this, &FMetaLandRoadEditorModeToolkit::OnPropSettingDetailsFinished);
		InnerView->SetIsPropertyVisibleDelegate(
			FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
			{
				return PropertyAndParent.Property.GetFName()
					!= GET_MEMBER_NAME_CHECKED(FMetaLandRoadPropSettingDetails, SOffset);
			}));
	}
}

void FMetaLandRoadEditorModeToolkit::RefreshPropSettingDetails()
{
	EnsurePropSettingDetailsView();
	bRefreshingPropSettingDetails = true;
	if (FMetaLandRoadPropSettingDetails* EditData = PropSettingEditStruct.IsValid()
		? reinterpret_cast<FMetaLandRoadPropSettingDetails*>(PropSettingEditStruct->GetStructMemory())
		: nullptr)
	{
		*EditData = FMetaLandRoadPropSettingDetails();
		if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
			FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
		{
			RoadVis->GetSelectedPropSettingDetails(*EditData);
		}
	}
	if (PropSettingDetailsView.IsValid())
	{
		PropSettingDetailsView->SetStructureData(PropSettingEditStruct);
	}
	bRefreshingPropSettingDetails = false;
}

void FMetaLandRoadEditorModeToolkit::NotifyPostChange(
	const FPropertyChangedEvent& PropertyChangedEvent,
	FProperty* PropertyThatChanged)
{
	(void)PropertyThatChanged;
	if (FMetaLandRoadEditorModule::Get().IsPropSettingSelected())
	{
		OnPropSettingDetailsFinished(PropertyChangedEvent);
	}
	if (FMetaLandRoadEditorModule::Get().IsSidewalkSelected())
	{
		OnSidewalkDetailsFinished(PropertyChangedEvent);
	}
}

void FMetaLandRoadEditorModeToolkit::OnPropSettingDetailsFinished(const FPropertyChangedEvent& PropertyChangedEvent)
{
	(void)PropertyChangedEvent;
	if (bRefreshingPropSettingDetails || !PropSettingEditStruct.IsValid())
	{
		return;
	}
	const FMetaLandRoadPropSettingDetails* EditData =
		reinterpret_cast<const FMetaLandRoadPropSettingDetails*>(PropSettingEditStruct->GetStructMemory());
	if (!EditData)
	{
		return;
	}
	if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
	{
		RoadVis->SetSelectedPropSettingDetails(*EditData);
	}
}

void FMetaLandRoadEditorModeToolkit::OnSidewalkAttributeClicked()
{
	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	Module.EnterSidewalkOverlay();
	OnSidewalkShowClicked();
}

void FMetaLandRoadEditorModeToolkit::OnSidewalkShowClicked()
{
	AActor* Selected = nullptr;
	if (GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				Selected = Road;
				break;
			}
			if (AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(*It))
			{
				Selected = Junction;
				break;
			}
		}
	}

	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	if (Selected)
	{
		Module.ShowSidewalkOverlayForActor(Selected);
		return;
	}

	if (UMetaLandRoadSplineComponent* Spline = Module.GetActiveNativeEditSpline())
	{
		Module.ShowSidewalkOverlayForActor(Spline->GetOwner());
	}
}

void FMetaLandRoadEditorModeToolkit::OnSidewalkHideClicked()
{
	FMetaLandRoadEditorModule::Get().DismissSidewalkOverlay();
}

void FMetaLandRoadEditorModeToolkit::OnSidewalkAddKeyClicked()
{
	if (UMetaLandRoadSplineComponent* Spline = FindSelectedRoadSpline())
	{
		const double S = Spline->GetSplineLength() * 0.5;
		MetaLandRoadCurbEditor::AddCurbKeyAtS(Spline, S);
		RefreshSidewalkDetails();
	}
}

void FMetaLandRoadEditorModeToolkit::OnSidewalkDeleteKeyClicked()
{
	if (UMetaLandRoadSplineComponent* Spline = FindSelectedRoadSpline())
	{
		MetaLandRoadCurbEditor::RemoveSelectedCurbKey(Spline);
		RefreshSidewalkDetails();
	}
}

void FMetaLandRoadEditorModeToolkit::EnsureSidewalkDetailsView()
{
	if (SidewalkDetailsView.IsValid())
	{
		return;
	}

	FPropertyEditorModule& PropModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowScrollBar = false;
	FStructureDetailsViewArgs StructArgs;
	SidewalkEditStruct = MakeShared<FStructOnScope>(FMetaLandRoadSidewalkEditDetails::StaticStruct());
	SidewalkDetailsView = PropModule.CreateStructureDetailView(
		DetailsArgs,
		StructArgs,
		SidewalkEditStruct,
		LOCTEXT("SidewalkStruct", "Sidewalk"));
	SidewalkDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		this, &FMetaLandRoadEditorModeToolkit::OnSidewalkDetailsFinished);
}

void FMetaLandRoadEditorModeToolkit::RefreshSidewalkDetails()
{
	EnsureSidewalkDetailsView();
	bRefreshingSidewalkDetails = true;
	if (FMetaLandRoadSidewalkEditDetails* EditData = SidewalkEditStruct.IsValid()
		? reinterpret_cast<FMetaLandRoadSidewalkEditDetails*>(SidewalkEditStruct->GetStructMemory())
		: nullptr)
	{
		*EditData = FMetaLandRoadSidewalkEditDetails();
		if (UMetaLandRoadSplineComponent* Spline = FindSelectedRoadSpline())
		{
			MetaLandRoadCurbEditor::GetSidewalkDetails(Spline, *EditData);
		}
	}
	if (SidewalkDetailsView.IsValid())
	{
		SidewalkDetailsView->SetStructureData(SidewalkEditStruct);
	}
	bRefreshingSidewalkDetails = false;
}

void FMetaLandRoadEditorModeToolkit::OnSidewalkDetailsFinished(const FPropertyChangedEvent& PropertyChangedEvent)
{
	(void)PropertyChangedEvent;
	if (bRefreshingSidewalkDetails || !SidewalkEditStruct.IsValid())
	{
		return;
	}
	const FMetaLandRoadSidewalkEditDetails* EditData =
		reinterpret_cast<const FMetaLandRoadSidewalkEditDetails*>(SidewalkEditStruct->GetStructMemory());
	if (!EditData)
	{
		return;
	}
	if (!GEditor)
	{
		return;
	}
	if (UMetaLandRoadSplineComponent* Spline = FindSelectedRoadSpline())
	{
		MetaLandRoadCurbEditor::SetSidewalkDetails(Spline, *EditData);
	}
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

void FMetaLandRoadEditorModeToolkit::EnsureJunctionLandscapeDetailsView()
{
	if (JunctionLandscapeDetailsView.IsValid())
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

	JunctionLandscapeEditStruct = MakeShared<FStructOnScope>(FMetaLandRoadJunctionLandscapeDetails::StaticStruct());
	JunctionLandscapeDetailsView = PropModule.CreateStructureDetailView(
		DetailsArgs,
		StructArgs,
		JunctionLandscapeEditStruct,
		LOCTEXT("JunctionLandscapeStruct", "Junction Landscape"));
	JunctionLandscapeDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		this, &FMetaLandRoadEditorModeToolkit::OnJunctionLandscapeDetailsFinished);
}

void FMetaLandRoadEditorModeToolkit::RefreshLandscapeKeyDetails()
{
	EnsureLandscapeKeyDetailsView();
	EnsureJunctionLandscapeDetailsView();
	bRefreshingLandscapeKeyDetails = true;

	if (AMoKuEditIntersectionActor* Junction = FMetaLandRoadEditorModule::Get().GetLandscapeContextJunction())
	{
		if (JunctionLandscapeEditStruct.IsValid())
		{
			if (FMetaLandRoadJunctionLandscapeDetails* EditData =
				reinterpret_cast<FMetaLandRoadJunctionLandscapeDetails*>(
					JunctionLandscapeEditStruct->GetStructMemory()))
			{
				EditData->Landscape.SideOffset = Junction->JunctionSampleEdgeOffset;
				EditData->Landscape.SideFalloff = Junction->JunctionSampleFalloffWidth;
				EditData->Landscape.HeightOffset = Junction->RoadHeightmapSettings.FalloffSettings.ZOffset;
				EditData->Landscape.LayerFactor = 1.0f;
				EditData->Landscape.SyncFalloffFromMetaRoadFields();
				EditData->RoadWeightFalloffWidth = Junction->GetRoadWeightFalloffWidth();
			}
			if (JunctionLandscapeDetailsView.IsValid())
			{
				JunctionLandscapeDetailsView->SetStructureData(JunctionLandscapeEditStruct);
			}
		}
		bRefreshingLandscapeKeyDetails = false;
		return;
	}

	if (!LandscapeKeyDetailsView.IsValid() || !LandscapeKeyEditStruct.IsValid())
	{
		bRefreshingLandscapeKeyDetails = false;
		return;
	}

	FMetaLandRoadLandscapeKeyDetails* EditData = reinterpret_cast<FMetaLandRoadLandscapeKeyDetails*>(
		LandscapeKeyEditStruct->GetStructMemory());
	if (EditData)
	{
		FMetaLandRoadLandscapeAttributeValue Value;
		double S = 0.0;
		if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
			FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
		{
			if (RoadVis->GetSelectedLandscapeKeyValue(Value, S))
			{
				EditData->SOffset = S;
				EditData->Landscape = Value;
				if (EditData->Landscape.RoadWeightFalloffWidth < 1.0f)
				{
					if (AMoKuEditSplineActor* Road = GetToolkitEditedRoad())
					{
						EditData->Landscape.RoadWeightFalloffWidth = Road->GetRoadWeightFalloffWidth();
					}
				}
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
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer();
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

void FMetaLandRoadEditorModeToolkit::OnJunctionLandscapeDetailsFinished(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (bRefreshingLandscapeKeyDetails || !JunctionLandscapeEditStruct.IsValid())
	{
		return;
	}

	AMoKuEditIntersectionActor* Junction = FMetaLandRoadEditorModule::Get().GetLandscapeContextJunction();
	const FMetaLandRoadJunctionLandscapeDetails* EditData =
		reinterpret_cast<const FMetaLandRoadJunctionLandscapeDetails*>(
			JunctionLandscapeEditStruct->GetStructMemory());
	if (!Junction || !EditData)
	{
		return;
	}

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(FMetaLandRoadJunctionLandscapeDetails, RoadWeightFalloffWidth)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FMetaLandRoadJunctionLandscapeDetails, RoadWeightFalloffWidth))
	{
		Junction->Modify();
		Junction->RoadWeightFalloffWidth = FMath::Clamp(EditData->RoadWeightFalloffWidth, 1.0f, 2500.0f);
		Junction->NotifyLandscapeBrushChanged(true, true, true);
		return;
	}

	Junction->ApplyJunctionLandscapeValue(EditData->Landscape);
}

TSharedRef<SWidget> FMetaLandRoadEditorModeToolkit::BuildLandscapeAttributePanel()
{
	EnsureLandscapeKeyDetailsView();
	EnsureJunctionLandscapeDetailsView();
	EnsurePropSettingDetailsView();
	EnsureSidewalkDetailsView();

	auto MatchesSearch = [this](const FText& DisplayName) -> bool
	{
		if (AttributeSearchText.IsEmpty())
		{
			return true;
		}
		return DisplayName.ToString().Contains(AttributeSearchText, ESearchCase::IgnoreCase);
	};

	auto MakeAttributeRow = [this](const FMetaLandRoadAttributeTypeDesc& Desc, bool bPinned) -> TSharedRef<SWidget>
	{
		const FName TypeId = Desc.TypeId;
		const FText DisplayName = Desc.DisplayName;
		const FName IconName = Desc.IconName.IsNone() ? FName(TEXT("ClassIcon.Actor")) : Desc.IconName;
		const FText CategoryText = FText::FromName(Desc.Category);
		const bool bIsLandscape = TypeId == FMetaLandRoadAttributeRegistry::TypeLandscape;
		const bool bIsPropSetting = TypeId == FMetaLandRoadAttributeRegistry::TypePropSetting;
		const bool bIsSidewalk = TypeId == FMetaLandRoadAttributeRegistry::TypeSidewalkHeight;
		const bool bRowEnabled = bIsLandscape || bIsPropSetting || bIsSidewalk;
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([bIsLandscape, bIsPropSetting, bIsSidewalk]()
			{
				if (bIsLandscape && FMetaLandRoadEditorModule::Get().IsLandscapeAttributeSelected())
				{
					return FLinearColor(0.05f, 0.28f, 0.55f, 1.0f);
				}
				if (bIsPropSetting && FMetaLandRoadEditorModule::Get().IsPropSettingSelected())
				{
					return FLinearColor(0.18f, 0.32f, 0.12f, 1.0f);
				}
				if (bIsSidewalk && FMetaLandRoadEditorModule::Get().IsSidewalkSelected())
				{
					return FLinearColor(0.28f, 0.18f, 0.08f, 1.0f);
				}
				return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			})
			.Padding(FMargin(4.0f, 3.0f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(0.0f))
				.IsEnabled(bRowEnabled)
				.OnClicked_Lambda([this, bIsLandscape, bIsPropSetting, bIsSidewalk]()
				{
					if (bIsLandscape)
					{
						OnLandscapeAttributeClicked();
					}
					else if (bIsPropSetting)
					{
						OnPropSettingAttributeClicked();
					}
					else if (bIsSidewalk)
					{
						OnSidewalkAttributeClicked();
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
						.ColorAndOpacity_Lambda([bPinned, bIsLandscape, bIsPropSetting, bIsSidewalk]()
						{
							if (bPinned
								|| (bIsLandscape && FMetaLandRoadEditorModule::Get().IsLandscapeAttributeSelected())
								|| (bIsPropSetting && FMetaLandRoadEditorModule::Get().IsPropSettingSelected())
								|| (bIsSidewalk && FMetaLandRoadEditorModule::Get().IsSidewalkSelected()))
							{
								return FSlateColor(FLinearColor(1.0f, 0.92f, 0.2f));
							}
							return FSlateColor::UseForeground();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(CategoryText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			];
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
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
		];

	for (const FMetaLandRoadAttributeTypeDesc& Desc : FMetaLandRoadAttributeRegistry::GetTypes())
	{
		const FText DisplayName = Desc.DisplayName;
		const bool bPinned = Desc.TypeId == FMetaLandRoadAttributeRegistry::TypeLandscape
			|| Desc.TypeId == FMetaLandRoadAttributeRegistry::TypePropSetting
			|| Desc.TypeId == FMetaLandRoadAttributeRegistry::TypeSidewalkHeight;
		Panel->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([MatchesSearch, DisplayName]()
				{
					return MatchesSearch(DisplayName) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					MakeAttributeRow(Desc, bPinned)
				]
			];
	}

	Panel->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([]()
				{
					return FMetaLandRoadEditorModule::Get().IsLandscapeAttributeSelected()
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
			];

	Panel->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().IsPropSettingSelected()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("PropShowOverlay", "Show"))
				.OnClicked_Lambda([this]()
				{
					OnPropSettingShowClicked();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("PropHideOverlay", "Hide"))
				.OnClicked_Lambda([this]()
				{
					OnPropSettingHideClicked();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text_Lambda([]()
				{
					return FMetaLandRoadEditorModule::Get().GetPropPlaceMode()
						== FMetaLandRoadEditorModule::EPropPlaceMode::AddRail
						? LOCTEXT("PropAddRailOn", "Add Rail 纵向…")
						: LOCTEXT("PropAddRail", "Add Rail 纵向");
				})
				.OnClicked_Lambda([this]()
				{
					OnPropSettingAddRailClicked();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text_Lambda([]()
				{
					return FMetaLandRoadEditorModule::Get().GetPropPlaceMode()
						== FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan
						? LOCTEXT("PropAddSpanOn", "Add Span…")
						: LOCTEXT("PropAddSpan", "Add Span");
				})
				.OnClicked_Lambda([this]()
				{
					OnPropSettingAddSpanClicked();
					return FReply::Handled();
				})
			]
		];

	Panel->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().IsSidewalkSelected()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SidewalkShowOverlay", "Show"))
				.OnClicked_Lambda([this]()
				{
					OnSidewalkShowClicked();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SidewalkHideOverlay", "Hide"))
				.OnClicked_Lambda([this]()
				{
					OnSidewalkHideClicked();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SidewalkAddKey", "Add Key"))
				.OnClicked_Lambda([this]()
				{
					OnSidewalkAddKeyClicked();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("SidewalkDeleteKey", "Delete Key"))
				.OnClicked_Lambda([this]()
				{
					OnSidewalkDeleteKeyClicked();
					return FReply::Handled();
				})
			]
		];

	Panel->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().ShouldShowLandscapeDetailsPanel()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.Text_Lambda([]()
			{
				if (FMetaLandRoadEditorModule::Get().GetLandscapeContextJunction())
				{
					return LOCTEXT("JunctionLandscapeTitle", "Junction Landscape");
				}
				if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
					FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
				{
					return RoadVis->GetSelectedLandscapeKeyTitle();
				}
				return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
			})
		];

	Panel->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().ShouldShowLandscapeDetailsPanel()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([]()
					{
						return FMetaLandRoadEditorModule::Get().GetLandscapeContextJunction()
							? EVisibility::Collapsed
							: EVisibility::Visible;
					})
					[
						LandscapeKeyDetailsView.IsValid()
							? LandscapeKeyDetailsView->GetWidget().ToSharedRef()
							: SNullWidget::NullWidget
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([]()
					{
						return FMetaLandRoadEditorModule::Get().GetLandscapeContextJunction()
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
					[
						JunctionLandscapeDetailsView.IsValid()
							? JunctionLandscapeDetailsView->GetWidget().ToSharedRef()
							: SNullWidget::NullWidget
					]
				]
			]
		];

	Panel->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().ShouldShowPropSettingDetailsPanel()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.Text_Lambda([]()
			{
				if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
					FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer())
				{
					return RoadVis->GetSelectedPropSettingTitle();
				}
				return LOCTEXT("PropSettingTitle", "PropSetting");
			})
		];

	Panel->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().ShouldShowPropSettingDetailsPanel()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				PropSettingDetailsView.IsValid()
					? PropSettingDetailsView->GetWidget().ToSharedRef()
					: SNullWidget::NullWidget
			]
		];

	Panel->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().ShouldShowSidewalkDetailsPanel()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.Text_Lambda([]()
			{
				const int32 Sign = FMetaLandRoadEditorModule::Get().GetSidewalkEdgeSign();
				if (Sign > 0)
				{
					return LOCTEXT("RightEdgeTitle", "Right Edge");
				}
				if (Sign < 0)
				{
					return LOCTEXT("LeftEdgeTitle", "Left Edge");
				}
				return LOCTEXT("PickEdgeTitle", "Hover an edge line, click to edit it");
			})
		];

	Panel->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([]()
			{
				return FMetaLandRoadEditorModule::Get().ShouldShowSidewalkDetailsPanel()
					&& FMetaLandRoadEditorModule::Get().GetSidewalkEdgeSign() != 0
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			[
				SidewalkDetailsView.IsValid()
					? SidewalkDetailsView->GetWidget().ToSharedRef()
					: SNullWidget::NullWidget
			]
		];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			Panel
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
