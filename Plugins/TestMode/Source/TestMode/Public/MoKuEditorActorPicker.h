// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FEditorViewportClient;
class FViewport;

/** Fired when the user picks an actor (no selection change). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMoKuEditorActorPicked, AActor* /*PickedActor*/);

/**
 * Editor-only actor picker (default Select Mode):
 * - Press P to toggle pick mode on/off (does not use Ctrl, so Ctrl+LMB multi-select still works).
 * - While pick mode is on: right-click in the level viewport to pick actors (repeatable).
 * - Press Esc or P again to exit pick mode.
 */
class TESTMODE_API FMoKuEditorActorPicker
{
public:
	static FMoKuEditorActorPicker& Get();

	void Register();
	void Unregister();

	FOnMoKuEditorActorPicked& OnActorPicked() { return ActorPickedDelegate; }

	bool IsPickModeActive() const { return bPickModeActive; }

	static AActor* PickActorFromViewport(FViewport* Viewport, FEditorViewportClient* ViewportClient);

private:
	friend class FMoKuEditorActorPickerInputProcessor;

	void RegisterInputProcessor();
	void SetPickModeActive(bool bActive);
	void TogglePickMode();
	void BroadcastActorPicked(AActor* PickedActor);

	static bool CanUsePickInput();

	FOnMoKuEditorActorPicked ActorPickedDelegate;
	TSharedPtr<class FMoKuEditorActorPickerInputProcessor> InputProcessor;
	FDelegateHandle PostEngineInitHandle;
	bool bPickModeActive = false;
	bool bIsRegistered = false;
};
