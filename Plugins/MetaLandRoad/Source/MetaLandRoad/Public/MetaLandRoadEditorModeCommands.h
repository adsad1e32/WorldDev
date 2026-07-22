// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Framework/Commands/Commands.h"
#include "Tools/InteractiveToolsCommands.h"
/**
 * This class contains info about the full set of commands used in this editor mode.
 */
class FMetaLandRoadEditorModeCommands : public TCommands<FMetaLandRoadEditorModeCommands>
{
public:
	FMetaLandRoadEditorModeCommands();

	//static void RegisterCommandBindings(TSharedPtr<FUICommandList> UICommandList, TFunction<void(FMetaLandRoadEditorModeCommands)> OnCommandExecuted);
	//static void UnRegisterCommandBindings(TSharedPtr<FUICommandList> UICommandList);
protected:
	struct FMetaLandRoadToolCommand
	{
		FString ToolUIName;
		TSharedPtr<FUICommandInfo> ToolCommand;
	};
	TArray<FMetaLandRoadToolCommand> RegisteredTools;



public:
	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
	TSharedPtr<FUICommandInfo> FindToolByName(FString Name, bool& bFound) const;
	virtual void RegisterCommands() override;
	static TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetCommands()
	{
		return FMetaLandRoadEditorModeCommands::Get().Commands;
	}

	TSharedPtr<FUICommandInfo> SimpleTool;
	TSharedPtr<FUICommandInfo> InteractiveTool;
	TSharedPtr<FUICommandInfo> ShapesTool;

};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
