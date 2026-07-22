// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaLandRoadEditorModeToolkit.h"
#include "MetaLandRoadEditorMode.h"
#include "Engine/Selection.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "MoKuEditSplineActor.h"
#include "MetaLandRoadEditorModeCommands.h"


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
