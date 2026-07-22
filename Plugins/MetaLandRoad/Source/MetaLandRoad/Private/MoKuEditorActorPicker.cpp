// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoKuEditorActorPicker.h"

#include "ComponentVisualizer.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "HitProxies.h"
#include "InputCoreTypes.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/SWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogMoKuActorPicker, Log, All);

class FMoKuEditorActorPickerInputProcessor : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override
	{
		if (!FMoKuEditorActorPicker::CanUsePickInput())
		{
			return false;
		}

		if (KeyEvent.GetKey() == EKeys::P && !KeyEvent.IsControlDown() && !KeyEvent.IsAltDown() && !KeyEvent.IsShiftDown())
		{
			FMoKuEditorActorPicker::Get().TogglePickMode();
			return true;
		}

		//if (KeyEvent.GetKey() == EKeys::Escape && FMoKuEditorActorPicker::Get().IsPickModeActive())
		//{
		//	FMoKuEditorActorPicker::Get().SetPickModeActive(false);
		//	return true;
		//}

		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
		{
			return false;
		}

		if (!FMoKuEditorActorPicker::Get().IsPickModeActive())
		{
			return false;
		}

		if (!FMoKuEditorActorPicker::CanUsePickInput())
		{
			return false;
		}

		FViewport* Viewport = GEditor->GetActiveViewport();
		if (!Viewport || !Viewport->HasFocus())
		{
			return false;
		}

		FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
		if (!ViewportClient)
		{
			return false;
		}

		AActor* PickedActor = FMoKuEditorActorPicker::PickActorFromViewport(Viewport, ViewportClient);
		if (PickedActor)
		{
			FMoKuEditorActorPicker::Get().BroadcastActorPicked(PickedActor);
		}

		// Consume RMB in pick mode so selection / context menu does not run.
		return true;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("MoKuEditorActorPicker");
	}
};

FMoKuEditorActorPicker& FMoKuEditorActorPicker::Get()
{
	static FMoKuEditorActorPicker Instance;
	return Instance;
}

bool FMoKuEditorActorPicker::CanUsePickInput()
{
	if (!GEditor || !GLevelEditorModeTools().IsDefaultModeActive())
	{
		return false;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	// Do not steal P / Esc while typing in a text field.
	if (const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget())
	{
		if (FocusedWidget->GetType().IsEqual(TEXT("SEditableText")) ||
			FocusedWidget->GetType().IsEqual(TEXT("SMultiLineEditableText")) ||
			FocusedWidget->GetType().IsEqual(TEXT("SEditableTextBox")))
		{
			return false;
		}
	}

	return true;
}

void FMoKuEditorActorPicker::Register()
{
	if (bIsRegistered)
	{
		return;
	}

	if (!FSlateApplication::IsInitialized())
	{
		if (!PostEngineInitHandle.IsValid())
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMoKuEditorActorPicker::Register);
		}
		return;
	}

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	RegisterInputProcessor();
}

void FMoKuEditorActorPicker::RegisterInputProcessor()
{
	if (bIsRegistered || !FSlateApplication::IsInitialized())
	{
		return;
	}

	InputProcessor = MakeShared<FMoKuEditorActorPickerInputProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, EInputPreProcessorType::PreEditor);
	bIsRegistered = true;

	UE_LOG(LogMoKuActorPicker, Log, TEXT("Registered. Press P to toggle pick mode, then right-click actors (Ctrl+LMB select is unaffected)."));
}

void FMoKuEditorActorPicker::Unregister()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	SetPickModeActive(false);

	if (!bIsRegistered || !InputProcessor.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	InputProcessor.Reset();
	bIsRegistered = false;
}

void FMoKuEditorActorPicker::TogglePickMode()
{
	SetPickModeActive(!bPickModeActive);
}

void FMoKuEditorActorPicker::SetPickModeActive(bool bActive)
{
	if (bPickModeActive == bActive)
	{
		return;
	}

	bPickModeActive = bActive;

	if (bPickModeActive)
	{
		UE_LOG(LogMoKuActorPicker, Log, TEXT("Pick mode ON: right-click actors in level viewport (P or Esc to exit)."));
	}
	else
	{
		UE_LOG(LogMoKuActorPicker, Log, TEXT("Pick mode OFF."));
	}
}

AActor* FMoKuEditorActorPicker::PickActorFromViewport(FViewport* Viewport, FEditorViewportClient* ViewportClient)
{
	if (!Viewport || !ViewportClient)
	{
		return nullptr;
	}

	const int32 MouseX = Viewport->GetMouseX();
	const int32 MouseY = Viewport->GetMouseY();

	HHitProxy* HitProxy = Viewport->GetHitProxy(MouseX, MouseY);
	if (!HitProxy)
	{
		return nullptr;
	}

	if (const HActor* ActorHit = HitProxyCast<HActor>(HitProxy))
	{
		return ActorHit->Actor.Get();
	}

	if (const HComponentVisProxy* VisHit = HitProxyCast<HComponentVisProxy>(HitProxy))
	{
		if (const UActorComponent* Component = VisHit->Component.Get())
		{
			return Component->GetOwner();
		}
	}

	return nullptr;
}

void FMoKuEditorActorPicker::BroadcastActorPicked(AActor* PickedActor)
{
	if (!IsValid(PickedActor))
	{
		return;
	}

	UE_LOG(LogMoKuActorPicker, Log, TEXT("OnActorPicked: Label=%s, Class=%s, Path=%s"),
		*PickedActor->GetActorLabel(),
		*PickedActor->GetClass()->GetName(),
		*PickedActor->GetPathName());

	ActorPickedDelegate.Broadcast(PickedActor);
}
