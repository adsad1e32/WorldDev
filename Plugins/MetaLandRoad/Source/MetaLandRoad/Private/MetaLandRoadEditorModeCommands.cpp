// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaLandRoadEditorModeCommands.h"
#include "MetaLandRoadEditorMode.h"
#include "EditorStyleSet.h"
#include "MetaLandRoadStyle.h"

#define LOCTEXT_NAMESPACE "MetaLandRoadEditorModeCommands"

FMetaLandRoadEditorModeCommands::FMetaLandRoadEditorModeCommands(): 
	TCommands<FMetaLandRoadEditorModeCommands>("MetaLandRoadEditorModeCommands",
		NSLOCTEXT("MetaLandRoadEditorMode", "MetaLandRoadEditorModeCommands", "WorldBuilder Editor Mode"),
		NAME_None,
		FMetaLandRoadStyle::Get()->GetStyleSetName())
{
}

void FMetaLandRoadEditorModeCommands::RegisterCommands()
{

#define REGISTER_TESTMODE_TOOL_COMMAND(ToolCommandInfo, ToolName, ToolTip,HotKey) \
		UI_COMMAND(ToolCommandInfo, ToolName, ToolTip, EUserInterfaceActionType::ToggleButton, FInputChord(HotKey)); \
		RegisteredTools.Add(FMetaLandRoadToolCommand{ ToolName, ToolCommandInfo });

#define REGISTER_TESTMODE_TOOL_COMMAND_RADIO(ToolCommandInfo, ToolName, ToolTip,HotKey) \
		UI_COMMAND(ToolCommandInfo, ToolName, ToolTip, EUserInterfaceActionType::RadioButton, FInputChord(HotKey)); \
		RegisteredTools.Add(FMetaLandRoadToolCommand{ ToolName, ToolCommandInfo });


	REGISTER_TESTMODE_TOOL_COMMAND(InteractiveTool, "Spline", "Ctrl + Click to Generate mesh along with the Spline",EKeys::G);
	REGISTER_TESTMODE_TOOL_COMMAND(ShapesTool, "Shapes", "Generate Lots mesh between Spline and Junction", EKeys::B);
	TArray<TSharedPtr<FUICommandInfo>> RoadCreatorCommands = {
	InteractiveTool
	};
	TArray<TSharedPtr<FUICommandInfo>> ShapesCreatorCommands = {
	ShapesTool
	};
	Commands.Add(FName(UMetaLandRoadEditorMode::InteractiveToolName), RoadCreatorCommands);
	Commands.Add(FName(UMetaLandRoadEditorMode::ShapesGenerationName), ShapesCreatorCommands);


#undef REGISTER_TESTMODE_TOOL_COMMAND

}


TSharedPtr<FUICommandInfo> FMetaLandRoadEditorModeCommands::FindToolByName(FString Name, bool& bFound) const
{
	bFound = false;
	for (const FMetaLandRoadToolCommand& Command : RegisteredTools)
	{
		if (Command.ToolUIName.Equals(Name, ESearchCase::IgnoreCase)
			|| (Command.ToolCommand.IsValid() && Command.ToolCommand->GetLabel().ToString().Equals(Name, ESearchCase::IgnoreCase)))
		{
			bFound = true;
			return Command.ToolCommand;
		}
	}
	return TSharedPtr<FUICommandInfo>();
}

#undef LOCTEXT_NAMESPACE
