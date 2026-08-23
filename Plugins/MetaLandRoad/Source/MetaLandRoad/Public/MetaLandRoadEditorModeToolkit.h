// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "MetaLandRoadEditorMode.h"
#include "UObject/UnrealType.h"

class UMetaLandRoadSplineComponent;


/**
 * This FModeToolkit just creates a basic UI panel that allows various InteractiveTools to
 * be initialized, and a DetailsView used to show properties of the active Tool.
 */
class FMetaLandRoadEditorModeToolkit : public FModeToolkit
{
public:
	FMetaLandRoadEditorModeToolkit();

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
	UMetaLandRoadEditorMode* TestEditMode;
	FText ActiveToolName;


	FDelegateHandle SelectionDelegateHandle;
	FString AttributeSearchText;


protected:
	//TSharedPtr<IDetailsView> ModeDetailsView;
	void UpdateActiveToolProperties();
	void RefreshDetailsWidget(UObject* SelectedObject);
	TSharedRef<SWidget> BuildLandscapeAttributePanel();
	void OnLandscapeAttributeClicked();
	void OnLandscapeShowClicked();
	void OnLandscapeHideClicked();
	UMetaLandRoadSplineComponent* FindSelectedRoadSpline() const;
	void EnsureLandscapeKeyDetailsView();
	void EnsureJunctionLandscapeDetailsView();
	void RefreshLandscapeKeyDetails();
	void OnLandscapeKeyDetailsFinished(const FPropertyChangedEvent& PropertyChangedEvent);
	void OnJunctionLandscapeDetailsFinished(const FPropertyChangedEvent& PropertyChangedEvent);

	TSharedPtr<class IStructureDetailsView> LandscapeKeyDetailsView;
	TSharedPtr<class FStructOnScope> LandscapeKeyEditStruct;
	TSharedPtr<class IStructureDetailsView> JunctionLandscapeDetailsView;
	TSharedPtr<class FStructOnScope> JunctionLandscapeEditStruct;
	bool bRefreshingLandscapeKeyDetails = false;
};
