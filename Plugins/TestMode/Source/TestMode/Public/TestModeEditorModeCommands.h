// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Framework/Commands/Commands.h"
#include "Tools/InteractiveToolsCommands.h"
/**
 * This class contains info about the full set of commands used in this editor mode.
 */
class FTestModeEditorModeCommands : public TCommands<FTestModeEditorModeCommands>
{
public:
	FTestModeEditorModeCommands();

	//static void RegisterCommandBindings(TSharedPtr<FUICommandList> UICommandList, TFunction<void(FTestModeEditorModeCommands)> OnCommandExecuted);
	//static void UnRegisterCommandBindings(TSharedPtr<FUICommandList> UICommandList);
protected:
	struct FTestModeToolCommand
	{
		FString ToolUIName;
		TSharedPtr<FUICommandInfo> ToolCommand;
	};
	TArray<FTestModeToolCommand> RegisteredTools;



public:
	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
	TSharedPtr<FUICommandInfo> FindToolByName(FString Name, bool& bFound) const;
	virtual void RegisterCommands() override;
	static TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetCommands()
	{
		return FTestModeEditorModeCommands::Get().Commands;
	}

	TSharedPtr<FUICommandInfo> SimpleTool;
	TSharedPtr<FUICommandInfo> InteractiveTool;
	TSharedPtr<FUICommandInfo> ShapesTool;

};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
