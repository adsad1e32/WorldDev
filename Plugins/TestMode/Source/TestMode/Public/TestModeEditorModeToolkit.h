// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "TestModeEditorMode.h"


/**
 * This FModeToolkit just creates a basic UI panel that allows various InteractiveTools to
 * be initialized, and a DetailsView used to show properties of the active Tool.
 */
class FTestModeEditorModeToolkit : public FModeToolkit
{
public:
	FTestModeEditorModeToolkit();

	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual void GetToolPaletteNames(TArray<FName>& PaletteNames) const override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget>GetInlineContent() const override;



	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;

	virtual void OnToolPaletteChanged(FName PaletteName) override;
	virtual void BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);

private:
	TSharedPtr< class SSplineEditPalette > SplineEditWidget;
	UTestModeEditorMode* TestEditMode;
	FText ActiveToolName;


	FDelegateHandle SelectionDelegateHandle;


protected:
	//TSharedPtr<IDetailsView> ModeDetailsView;
	void UpdateActiveToolProperties();
	void RefreshDetailsWidget(UObject* SelectedObject);
};
