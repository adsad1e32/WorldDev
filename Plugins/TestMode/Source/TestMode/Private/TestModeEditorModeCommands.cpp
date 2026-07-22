// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestModeEditorModeCommands.h"
#include "TestModeEditorMode.h"
#include "EditorStyleSet.h"
#include "TestModeStyle.h"

#define LOCTEXT_NAMESPACE "TestModeEditorModeCommands"

FTestModeEditorModeCommands::FTestModeEditorModeCommands(): 
	TCommands<FTestModeEditorModeCommands>("TestModeEditorModeCommands",
		NSLOCTEXT("TestModeEditorMode", "TestModeEditorModeCommands", "WorldBuilder Editor Mode"),
		NAME_None,
		FTestModeStyle::Get()->GetStyleSetName())
{
}

void FTestModeEditorModeCommands::RegisterCommands()
{

#define REGISTER_TESTMODE_TOOL_COMMAND(ToolCommandInfo, ToolName, ToolTip,HotKey) \
		UI_COMMAND(ToolCommandInfo, ToolName, ToolTip, EUserInterfaceActionType::ToggleButton, FInputChord(HotKey)); \
		RegisteredTools.Add(FTestModeToolCommand{ ToolName, ToolCommandInfo });

#define REGISTER_TESTMODE_TOOL_COMMAND_RADIO(ToolCommandInfo, ToolName, ToolTip,HotKey) \
		UI_COMMAND(ToolCommandInfo, ToolName, ToolTip, EUserInterfaceActionType::RadioButton, FInputChord(HotKey)); \
		RegisteredTools.Add(FTestModeToolCommand{ ToolName, ToolCommandInfo });


	REGISTER_TESTMODE_TOOL_COMMAND(InteractiveTool, "Spline", "Ctrl + Click to Generate mesh along with the Spline",EKeys::G);
	REGISTER_TESTMODE_TOOL_COMMAND(ShapesTool, "Shapes", "Generate Lots mesh between Spline and Junction", EKeys::B);
	TArray<TSharedPtr<FUICommandInfo>> RoadCreatorCommands = {
	InteractiveTool
	};
	TArray<TSharedPtr<FUICommandInfo>> ShapesCreatorCommands = {
	ShapesTool
	};
	Commands.Add(FName(UTestModeEditorMode::InteractiveToolName), RoadCreatorCommands);
	Commands.Add(FName(UTestModeEditorMode::ShapesGenerationName), ShapesCreatorCommands);


#undef REGISTER_TESTMODE_TOOL_COMMAND

}


TSharedPtr<FUICommandInfo> FTestModeEditorModeCommands::FindToolByName(FString Name, bool& bFound) const
{
	bFound = false;
	for (const FTestModeToolCommand& Command : RegisteredTools)
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
