// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestModeEditorModeToolkit.h"
#include "TestModeEditorMode.h"
#include "Engine/Selection.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "MoKuEditSplineActor.h"
#include "TestModeEditorModeCommands.h"


#define LOCTEXT_NAMESPACE "TestModeEditorModeToolkit"

FTestModeEditorModeToolkit::FTestModeEditorModeToolkit()
{
}

void FTestModeEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{

	//TestEditMode=(UTestModeEditorMode*)InOwningMode.Get();

	//SAssignNew(SplineEditWidget, SSplineEditPalette);
	TSharedPtr<SVerticalBox> ToolkitWidgetVBox = SNew(SVerticalBox);
	SAssignNew(ToolkitWidget, SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Fill)
		[
			ToolkitWidgetVBox->AsShared()
		];
	FTestModeEditorModeCommands::Register();
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	ToolkitWidgetVBox->AddSlot().HAlign(HAlign_Fill).FillHeight(1.f)
	[
		DetailsView->AsShared()
	];
	//FTestModeEditorModeCommands& Commands = FTestModeEditorModeCommands::Get();
	//TArray<TSharedPtr<FUICommandInfo>> CreatePaletteItems
	//({
	//	Commands.InteractiveTool,
	//	Commands.ShapesTool,
	//});
}

void FTestModeEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FTestModeEditorModeToolkit::GetToolkitFName() const
{
	return FName("TestModeEditorMode");
}

FText FTestModeEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "TestModeEditorMode Toolkit");
}



TSharedPtr<SWidget> FTestModeEditorModeToolkit::GetInlineContent()const
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Fill)
		[
			ToolkitWidget.ToSharedRef()
		];
}



void  FTestModeEditorModeToolkit::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{

	//还有优化空间
	UpdateActiveToolProperties();
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveTool(EToolSide::Left);
	ActiveToolName = CurTool->GetToolInfo().ToolDisplayName;

	//CurTool->OnPropertySetsModified.AddSP(this, &FTestModeEditorModeToolkit::UpdateActiveToolProperties);
	FString ActiveToolIdentifier = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveToolName(EToolSide::Left);
	if (ActiveToolIdentifier == SPLINETOOLNAME)
	{
		SelectionDelegateHandle = USelection::SelectionChangedEvent.AddRaw(this, &FTestModeEditorModeToolkit::RefreshDetailsWidget);
		UObject* SelectObject = nullptr;
		RefreshDetailsWidget(SelectObject);
	}
	else
	{
		CurTool->OnPropertySetsModified.AddSP(this, &FTestModeEditorModeToolkit::UpdateActiveToolProperties);
	}
}

void  FTestModeEditorModeToolkit::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
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


void FTestModeEditorModeToolkit::OnToolPaletteChanged(FName PaletteName)
{
	UE_LOG(LogTemp, Warning, TEXT("PaletteName==%s"), *PaletteName.ToString())
}

void FTestModeEditorModeToolkit::UpdateActiveToolProperties()
{
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager(EToolsContextScope::EdMode)->GetActiveTool(EToolSide::Left);
	if (CurTool == nullptr)
	{
		return;
	}
	TArray<UObject*>ToolProperties = CurTool->GetToolProperties(true);
	DetailsView->SetObjects(ToolProperties,true);
}



void FTestModeEditorModeToolkit::RefreshDetailsWidget(UObject* SelectedObject)
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





void FTestModeEditorModeToolkit::BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder)
{
	if (HasToolkitBuilder())
	{
		return;
	}
	const FTestModeEditorModeCommands& Commands = FTestModeEditorModeCommands::Get();
	ToolbarBuilder.AddToolBarButton(Commands.InteractiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.ShapesTool);
	
}


#undef LOCTEXT_NAMESPACE
