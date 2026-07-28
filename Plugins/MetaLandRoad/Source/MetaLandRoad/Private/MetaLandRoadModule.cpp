// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaLandRoadModule.h"
#include "MetaLandRoadSectionHover.h"
#include "MetaLandRoadProfile.h"
#include "MoKuEditorActorPicker.h"
#include "MetaLandRoadEditorModeCommands.h"
#include "PropertyEditorModule.h"
#include "MoKuSplineActorCustomization.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditSplinesComponent.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadStyle.h"
#include "MetaLandRoadTypes.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "WorldEditorSettings.h"
#include "WorldBrushEditorManagerFactory.h"
#include "MoKuEditBaseActor.h"
#include "EditSplineRoadInfo.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "UnrealEdGlobals.h"
#include "MetaLandRoadSplineComponent.h"
#include "Editor.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "EditorViewportClient.h"
#include "Selection.h"
#include "UObject/UObjectIterator.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"
#include "ComponentVisualizer.h"
#include "LevelEditor.h"
#include "Framework/Commands/UICommandList.h"
#include "Components/DynamicMeshComponent.h"
#define LOCTEXT_NAMESPACE "MetaLandRoadModule"

//DEFINE_LOG_CATEGORY(LogRoadEditor);

TSharedPtr<FMoKuEditSplinesComponentVisualizer> GMoKuEditSplinesComponentVisualizer;

#if WITH_EDITOR
static UMetaLandRoadSplineComponent* MetaLandRoadDebugFindSelectedSpline()
{
	if (!GEditor)
	{
		return nullptr;
	}

	for (FSelectionIterator It(GEditor->GetSelectedComponentIterator()); It; ++It)
	{
		if (UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(*It))
		{
			return SplineComp;
		}
	}

	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (UMetaLandRoadSplineComponent* SplineComp = Actor->FindComponentByClass<UMetaLandRoadSplineComponent>())
			{
				return SplineComp;
			}
		}
	}

	return nullptr;
}

static FAutoConsoleCommand CMetaLandRoadToggleSectionLine(
	TEXT("MetaLandRoad.ToggleSectionLineEdit"),
	TEXT("Toggle native spline visualizer vs section-line visualizer (same as Alt+E)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
		{
			FMetaLandRoadModule::Get().ToggleRoadComponentVisMode();
		}
	}));
#endif

void FMetaLandRoadModule::RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer)
{
	if (GUnrealEd)
	{
		GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
	}

	RegisteredComponentClassNames.AddUnique(ComponentClassName);
}

bool FMetaLandRoadModule::OnDeferredComponentVisRegisterTick(float /*DeltaTime*/)
{
	if (!bComponentVisualizersRegistered)
	{
		EnsureComponentVisualizersRegistered();
	}
	return !bComponentVisualizersRegistered;
}

void FMetaLandRoadModule::EnsureComponentVisualizersRegistered()
{
	if (!GUnrealEd)
	{
		if (!PostEngineInitHandle.IsValid())
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(
				this,
				&FMetaLandRoadModule::EnsureComponentVisualizersRegistered);
		}
		if (!ComponentVisRegisterTickerHandle.IsValid())
		{
			ComponentVisRegisterTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FMetaLandRoadModule::OnDeferredComponentVisRegisterTick),
				0.25f);
		}
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] GUnrealEd null — defer ComponentVisualizer registration"));
		return;
	}

	if (bComponentVisualizersRegistered)
	{
		return;
	}

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (ComponentVisRegisterTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ComponentVisRegisterTickerHandle);
		ComponentVisRegisterTickerHandle.Reset();
	}

	const bool bCreatedVisualizer = !GMoKuEditSplinesComponentVisualizer.IsValid();
	if (bCreatedVisualizer)
	{
		GMoKuEditSplinesComponentVisualizer = MakeShareable(new FMoKuEditSplinesComponentVisualizer);
	}

	const TSharedPtr<FComponentVisualizer> VisualizerPtr =
		StaticCastSharedPtr<FComponentVisualizer>(GMoKuEditSplinesComponentVisualizer);

	RegisterComponentVisualizer(
		UMoKuEditSplinesComponent::StaticClass()->GetFName(),
		VisualizerPtr);
	RegisterComponentVisualizer(
		UMetaLandRoadSplineComponent::StaticClass()->GetFName(),
		VisualizerPtr);

	if (bCreatedVisualizer && GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		GMoKuEditSplinesComponentVisualizer->OnRegister();
	}

	bComponentVisualizersRegistered = true;

	CompVisUtils::RefreshComponentVisualizersForSelection();
}

void FMetaLandRoadModule::TryRegisterComponentVisualizers()
{
	EnsureComponentVisualizersRegistered();
}

void FMetaLandRoadModule::StartupModule()
{
	FMetaLandRoadStyle::Initialize();
	FMetaLandRoadEditorModeCommands::Register();
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(AMoKuEditSplineActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FMoKuSplineConnectAssetMeshDetail::MakeInstance));
	GEngine->OnLevelActorAdded().AddRaw(this, &FMetaLandRoadModule::OnLevelActorAddedToWorld);
	EnsureComponentVisualizersRegistered();

	if (!bComponentVisualizersRegistered && !LevelEditorModuleChangedHandle.IsValid())
	{
		LevelEditorModuleChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(
			this,
			&FMetaLandRoadModule::OnModulesChangedForHotkey);
	}

	if (GEditor)
	{
		EditorSelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(
			this,
			&FMetaLandRoadModule::OnEditorSelectionChanged);
	}

	RegisterSectionLineEditHotkey();

	if (GEditor)
	{
		GEditor->ActorFactories.Add(NewObject<UWorldBrushEditorManagerFactory>());
	}

	FMoKuEditorActorPicker::Get().Register();
	ActorPickerHandle = FMoKuEditorActorPicker::Get().OnActorPicked().AddRaw(this, &FMetaLandRoadModule::OnEditorActorPicked);
}

void FMetaLandRoadModule::OnEditorActorPicked(AActor* PickedActor)
{
	if (!IsValid(PickedActor) || !GEditor)
	{
		return;
	}

	if (UMetaLandRoadSplineComponent* PickedSpline = PickedActor->FindComponentByClass<UMetaLandRoadSplineComponent>())
	{
		CompVisUtils::SelectRoadSplineExclusive(PickedActor, PickedSpline, true);

		if (FMetaLandRoadModule::Get().ShouldShowNativeEditOverlay()
			&& GMoKuEditSplinesComponentVisualizer.IsValid())
		{
			GMoKuEditSplinesComponentVisualizer->SetActiveForSpline(PickedSpline);
		}
	}
	else
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(PickedActor, true, true);
	}

	UE_LOG(LogTemp, Log, TEXT("[MetaLandRoad] OnActorPicked: %s (%s)"),
		*PickedActor->GetActorLabel(),
		*PickedActor->GetClass()->GetName());
}

void FMetaLandRoadModule::ApplyEditorVisModeSideEffects(bool bShowSectionEdit)
{
	if (!bComponentVisualizersRegistered)
	{
		EnsureComponentVisualizersRegistered();
	}

	// Do not early-return here: selection sync and subcomponent settings are required for
	// UUnrealEdEngine::DrawComponentVisualizers even when the global visualizer ptr is stale.
	if (UEditorPerProjectUserSettings* Settings = GetMutableDefault<UEditorPerProjectUserSettings>())
	{
		const bool bWantsSubcomponentVis = IsNativeEditMode() || IsSectionMode() || IsLaneEditMode();
		if (bWantsSubcomponentVis)
		{
			if (!SavedShowSelectionSubcomponents.IsSet())
			{
				SavedShowSelectionSubcomponents = Settings->bShowSelectionSubcomponents;
			}
			Settings->bShowSelectionSubcomponents = true;
		}
		else if (SavedShowSelectionSubcomponents.IsSet())
		{
			Settings->bShowSelectionSubcomponents = SavedShowSelectionSubcomponents.GetValue();
			SavedShowSelectionSubcomponents.Reset();
		}
	}

	UMetaLandRoadSplineComponent* RoadSpline = ActiveNativeEditSpline.Get();
	if (!RoadSpline)
	{
		RoadSpline = MetaLandRoadDebugFindSelectedSpline();
	}
	if (!RoadSpline && GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				RoadSpline = Road->GetSplineComponent();
				break;
			}
		}
	}

	if (RoadSpline)
	{
		if (IsStandardMode())
		{
			if (GEditor && RoadSpline->GetOwner())
			{
				GEditor->GetSelectedComponents()->DeselectAll();
				CompVisUtils::EnsurePasteSafeActorSelection(RoadSpline->GetOwner());
			}
		}
		else if (IsNativeEditMode() || IsSectionMode() || IsLaneEditMode())
		{
			// Persist for CompVis draw gating (first Standard→Alt+E often has no prior ActiveNative).
			ActiveNativeEditSpline = RoadSpline;

			CompVisUtils::SelectRoadSplineExclusive(RoadSpline->GetOwner(), RoadSpline, true);
			CompVisUtils::RefreshComponentVisualizersForSelection();

		}
	}

	if (!GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		EnsureComponentVisualizersRegistered();
	}

	if (GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		GMoKuEditSplinesComponentVisualizer->SyncSectionLineEditModeFlag(bShowSectionEdit);

		if (RoadSpline)
		{
			if (bShowSectionEdit)
			{
				MetaLandRoadProfile::EnsureDefaultRoadProfile(RoadSpline);
				RoadSpline->GetRoadLayoutMutable().UpdateBounds(RoadSpline->GetSplineLength());
				RoadSpline->RefreshLaneVisualization();
			}
			else if (IsLaneEditMode())
			{
				MetaLandRoadProfile::EnsureDefaultRoadProfile(RoadSpline);
				RoadSpline->GetRoadLayoutMutable().UpdateBounds(RoadSpline->GetSplineLength());
			}

			if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(RoadSpline->GetOwner()))
			{
				if (RoadActor->DynamicMeshComponent)
				{
					RoadActor->DynamicMeshComponent->bSelectable = IsStandardMode();
					// Alt+W: let viewport hit tests reach spline segment/key proxies underneath the road mesh.
					RoadActor->DynamicMeshComponent->SetCollisionResponseToChannel(
						ECC_Visibility,
						IsNativeEditMode() ? ECR_Ignore : ECR_Block);
				}
			}

			if (bShowSectionEdit)
			{
				// CompVis overlays do not draw in Game View.
				if (GCurrentLevelEditingViewportClient
					&& GCurrentLevelEditingViewportClient->IsInGameView())
				{
					GCurrentLevelEditingViewportClient->SetGameView(false);
				}
				GMoKuEditSplinesComponentVisualizer->ActivateRoadVisualization();
			}
			else if (IsLaneEditMode())
			{
				if (GCurrentLevelEditingViewportClient
					&& GCurrentLevelEditingViewportClient->IsInGameView())
				{
					GCurrentLevelEditingViewportClient->SetGameView(false);
				}
				GMoKuEditSplinesComponentVisualizer->ActivateRoadVisualization();
			}
			else if (ShouldShowNativeEditOverlay())
			{
				GMoKuEditSplinesComponentVisualizer->SetActiveForSpline(RoadSpline);
			}
		}
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

	// First Standard→Alt+E / Alt+T: selection/CompVis list can lag one frame — re-sync next tick.
	if ((bShowSectionEdit || IsLaneEditMode()) && RoadSpline)
	{
		TWeakObjectPtr<UMetaLandRoadSplineComponent> WeakSpline = RoadSpline;
		const bool bLaneMode = IsLaneEditMode();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakSpline, bLaneMode](float)
			{
				UMetaLandRoadSplineComponent* Spline = WeakSpline.Get();
				if (!Spline || !GEditor || !GMoKuEditSplinesComponentVisualizer.IsValid())
				{
					return false;
				}
				if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
				{
					return false;
				}
				if (bLaneMode)
				{
					if (!FMetaLandRoadModule::Get().IsLaneEditMode())
					{
						return false;
					}
				}
				else if (!FMetaLandRoadModule::Get().IsSectionMode())
				{
					return false;
				}

				CompVisUtils::SelectRoadSplineExclusive(Spline->GetOwner(), Spline, false);
				CompVisUtils::RefreshComponentVisualizersForSelection();
				GMoKuEditSplinesComponentVisualizer->ActivateRoadVisualization();
				GEditor->RedrawLevelEditingViewports(true);
				return false;
			}),
			0.0f);
	}
}

void FMetaLandRoadModule::SyncSelectedRoadActorToSpline()
{
	if (!GEditor || bSyncingRoadActorSelection)
	{
		return;
	}

	// Standard mode: actor/mesh selection only — no spline sub-selection or overlays.
	if (IsStandardMode())
	{
		return;
	}

	for (FSelectionIterator It(GEditor->GetSelectedComponentIterator()); It; ++It)
	{
		UActorComponent* SelectedComp = Cast<UActorComponent>(*It);
		if (!SelectedComp)
		{
			continue;
		}

		AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SelectedComp->GetOwner());
		if (!Road || SelectedComp != Road->DynamicMeshComponent)
		{
			continue;
		}

		UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent();
		if (!Spline)
		{
			return;
		}

		if (GEditor->GetSelectedActors()->IsSelected(Road)
			&& CompVisUtils::IsComponentInActorElementSelection(Spline)
			&& CompVisUtils::IsSelectedInViewport(Spline))
		{
			return;
		}

		TGuardValue<bool> SyncGuard(bSyncingRoadActorSelection, true);
		CompVisUtils::SelectRoadSplineExclusive(Road, Spline, true);
		return;
	}

	AMoKuEditSplineActor* SelectedRoad = nullptr;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
		{
			if (SelectedRoad)
			{
				return;
			}
			SelectedRoad = Road;
		}
		else if (Cast<AActor>(*It))
		{
			return;
		}
	}

	if (!SelectedRoad)
	{
		return;
	}

	UMoKuEditSplinesComponent* Spline = SelectedRoad->GetSplineComponent();
	if (!Spline)
	{
		return;
	}

	if (CompVisUtils::IsComponentInActorElementSelection(Spline)
		&& CompVisUtils::IsSelectedInViewport(Spline))
	{
		return;
	}

	TGuardValue<bool> SyncGuard(bSyncingRoadActorSelection, true);
	CompVisUtils::SelectRoadSplineExclusive(SelectedRoad, Spline, true);
}

void FMetaLandRoadModule::OnEditorSelectionChanged(UObject* /*NewSelection*/)
{
	SyncSelectedRoadActorToSpline();
}

void FMetaLandRoadModule::SetEditorVisMode(EMetaLandRoadEditorVisMode NewMode)
{
	if (!GUnrealEd)
	{
		return;
	}

	if (EditorVisMode == NewMode)
	{
		return;
	}

	const EMetaLandRoadEditorVisMode PreviousMode = EditorVisMode;

	// Keep ActiveNativeEditSpline through Apply so W→E still finds the road for selection/profile.
	// Clear native key selection only; reset ActiveNativeEditSpline after Apply when leaving NativeEdit.
	if (PreviousMode == EMetaLandRoadEditorVisMode::NativeEdit
		&& GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		GMoKuEditSplinesComponentVisualizer->ClearNativeKeySelection();
	}

	if (PreviousMode == EMetaLandRoadEditorVisMode::Section && NewMode != EMetaLandRoadEditorVisMode::Section && GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TObjectIterator<UMetaLandRoadSplineComponent> It; It; ++It)
			{
				if (It->GetWorld() == World)
				{
					FMetaLandRoadSectionHover::ClearHoverOverlay(*It);
					MetaLandRoadLaneVisualization::RefreshLaneVisualization(*It);
				}
			}
		}
		if (GMoKuEditSplinesComponentVisualizer.IsValid())
		{
			GMoKuEditSplinesComponentVisualizer->ClearSectionHighlight();
		}
	}

	if (PreviousMode == EMetaLandRoadEditorVisMode::Lane && NewMode != EMetaLandRoadEditorVisMode::Lane)
	{
		if (GMoKuEditSplinesComponentVisualizer.IsValid())
		{
			GMoKuEditSplinesComponentVisualizer->ClearSectionHighlight();
		}
		if (GEditor)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				for (TObjectIterator<UMetaLandRoadSplineComponent> It; It; ++It)
				{
					if (It->GetWorld() == World)
					{
						MetaLandRoadLaneVisualization::RefreshLaneVisualization(*It);
					}
				}
			}
		}
	}

	EditorVisMode = NewMode;
	ApplyEditorVisModeSideEffects(IsSectionMode());

	// Keep ActiveNativeEditSpline in NativeEdit + Section + Lane so first Alt+E (and W→E) always has a draw target.
	// Clear only when returning to Standard.
	if (NewMode == EMetaLandRoadEditorVisMode::Standard)
	{
		ActiveNativeEditSpline.Reset();
	}

	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();

	// Avoid a second world-wide RefreshLaneVisualization here — Apply already refreshed the
	// active road when entering Section; extra refresh+redraw during mode switch worsens re-entry risk.
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

}

void FMetaLandRoadModule::ToggleRoadComponentVisMode()
{
	if (IsSectionMode())
	{
		SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
	}
	else
	{
		SetEditorVisMode(EMetaLandRoadEditorVisMode::Section);
	}
}

void FMetaLandRoadModule::SetRoadComponentVisMode(bool bSectionLineMode)
{
	if (bSectionLineMode)
	{
		SetEditorVisMode(EMetaLandRoadEditorVisMode::Section);
	}
	else if (IsSectionMode())
	{
		SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
	}
}

void FMetaLandRoadModule::RegisterSectionLineEditHotkey()
{
	if (bSectionLineHotkeyMapped)
	{
		return;
	}

	if (!FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		if (!LevelEditorModuleChangedHandle.IsValid())
		{
			LevelEditorModuleChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(
				this,
				&FMetaLandRoadModule::OnModulesChangedForHotkey);
		}
		return;
	}

	if (LevelEditorModuleChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(LevelEditorModuleChangedHandle);
		LevelEditorModuleChangedHandle.Reset();
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<FUICommandList> LevelEditorActions = LevelEditorModule.GetGlobalLevelEditorActions();
	if (!LevelEditorActions.IsValid())
	{
		return;
	}

	const FMetaLandRoadEditorModeCommands& Commands = FMetaLandRoadEditorModeCommands::Get();
	if (!Commands.ToggleSectionLineEdit.IsValid())
	{
		return;
	}

	LevelEditorActions->MapAction(
		Commands.ToggleSectionLineEdit,
		FExecuteAction::CreateLambda([]()
		{
			if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
			{
				FMetaLandRoadModule::Get().ToggleRoadComponentVisMode();
			}
		}),
		FCanExecuteAction::CreateLambda([]()
		{
			return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"));
		}),
		FIsActionChecked::CreateLambda([]()
		{
			return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
				&& FMetaLandRoadModule::Get().IsSectionMode();
		}));

	if (Commands.EnterNativeSplineEdit.IsValid())
	{
		LevelEditorActions->MapAction(
			Commands.EnterNativeSplineEdit,
			FExecuteAction::CreateLambda([]()
			{
				if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
				{
					FMetaLandRoadModule::Get().ToggleNativeSplineEditMode();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"));
			}),
			FIsActionChecked::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
					&& FMetaLandRoadModule::Get().IsNativeEditMode();
			}));
	}

	if (Commands.ToggleLaneEdit.IsValid())
	{
		LevelEditorActions->MapAction(
			Commands.ToggleLaneEdit,
			FExecuteAction::CreateLambda([]()
			{
				if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
				{
					FMetaLandRoadModule::Get().ToggleLaneEditMode();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"));
			}),
			FIsActionChecked::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
					&& FMetaLandRoadModule::Get().IsLaneEditMode();
			}));
	}

	bSectionLineHotkeyMapped = true;
	UE_LOG(LogTemp, Log, TEXT("[MetaLandRoad] Alt+E / Alt+W / Alt+T bound on LevelEditor actions."));
}

void FMetaLandRoadModule::OnModulesChangedForHotkey(FName ModuleName, EModuleChangeReason Reason)
{
	if (Reason == EModuleChangeReason::ModuleLoaded)
	{
		if (ModuleName == TEXT("UnrealEd") || ModuleName == TEXT("LevelEditor"))
		{
			EnsureComponentVisualizersRegistered();
		}

		if (ModuleName == TEXT("LevelEditor"))
		{
			RegisterSectionLineEditHotkey();
		}
	}
}

void FMetaLandRoadModule::UnregisterSectionLineEditHotkey()
{
	if (LevelEditorModuleChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(LevelEditorModuleChangedHandle);
		LevelEditorModuleChangedHandle.Reset();
	}

	if (!bSectionLineHotkeyMapped || !FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		bSectionLineHotkeyMapped = false;
		return;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	if (const TSharedPtr<FUICommandList> LevelEditorActions = LevelEditorModule.GetGlobalLevelEditorActions())
	{
		const FMetaLandRoadEditorModeCommands& Commands = FMetaLandRoadEditorModeCommands::Get();
		if (Commands.ToggleSectionLineEdit.IsValid())
		{
			LevelEditorActions->UnmapAction(Commands.ToggleSectionLineEdit);
		}
		if (Commands.EnterNativeSplineEdit.IsValid())
		{
			LevelEditorActions->UnmapAction(Commands.EnterNativeSplineEdit);
		}
		if (Commands.ToggleLaneEdit.IsValid())
		{
			LevelEditorActions->UnmapAction(Commands.ToggleLaneEdit);
		}
	}

	bSectionLineHotkeyMapped = false;
}

UMetaLandRoadSplineComponent* FMetaLandRoadModule::FindSelectedOrOwnedRoadSpline() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	for (FSelectionIterator It(GEditor->GetSelectedComponentIterator()); It; ++It)
	{
		if (UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(*It))
		{
			return Spline;
		}
		if (UActorComponent* Comp = Cast<UActorComponent>(*It))
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Comp->GetOwner()))
			{
				return Road->GetSplineComponent();
			}
		}
	}

	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
		{
			return Road->GetSplineComponent();
		}
	}

	if (GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		return GMoKuEditSplinesComponentVisualizer->GetEditedMetaLandRoadSpline();
	}

	if (IsNativeEditMode() || IsLaneEditMode())
	{
		return ActiveNativeEditSpline.Get();
	}
	return nullptr;
}

void FMetaLandRoadModule::ExitNativeSplineEditMode()
{
	if (!IsNativeEditMode())
	{
		return;
	}

	if (GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		GMoKuEditSplinesComponentVisualizer->ClearNativeKeySelection();
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

	ActiveNativeEditSpline.Reset();
	SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+W native spline edit OFF"));
}

void FMetaLandRoadModule::ToggleNativeSplineEditMode()
{
	if (IsNativeEditMode())
	{
		ExitNativeSplineEditMode();
		return;
	}
	EnterNativeSplineEditMode();
}

void FMetaLandRoadModule::EnterNativeSplineEditMode()
{
	UMetaLandRoadSplineComponent* RoadSpline = FindSelectedOrOwnedRoadSpline();
	if (!RoadSpline)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+W: no road spline selected"));
		return;
	}

	ActiveNativeEditSpline = RoadSpline;

	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsInGameView())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MetaLandRoad] Alt+W: GameView was ON — ComponentVisualizer does not draw in GameView; exiting GameView now (or press G)."));
		GCurrentLevelEditingViewportClient->SetGameView(false);
	}

	SetEditorVisMode(EMetaLandRoadEditorVisMode::NativeEdit);

	RoadSpline->EnsureAllCurvePointTypesForSmoothCorners();
	if (GEditor)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			FColor::Yellow,
			TEXT("Alt+W ON — hover ring (yellow) / click to drag. Alt+W again to exit"));
		GEditor->RedrawLevelEditingViewports(true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+W native spline edit ON Spline=%s Points=%d"),
		*RoadSpline->GetName(),
		RoadSpline->GetNumberOfSplinePoints());
}

void FMetaLandRoadModule::ExitLaneEditMode()
{
	if (!IsLaneEditMode())
	{
		return;
	}

	if (GMoKuEditSplinesComponentVisualizer.IsValid())
	{
		GMoKuEditSplinesComponentVisualizer->ClearSectionHighlight();
	}

	ActiveNativeEditSpline.Reset();
	SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+T lane edit OFF"));
}

void FMetaLandRoadModule::ToggleLaneEditMode()
{
	if (IsLaneEditMode())
	{
		ExitLaneEditMode();
		return;
	}
	EnterLaneEditMode();
}

void FMetaLandRoadModule::EnterLaneEditMode()
{
	UMetaLandRoadSplineComponent* RoadSpline = FindSelectedOrOwnedRoadSpline();
	if (!RoadSpline)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+T: no road spline selected"));
		return;
	}

	ActiveNativeEditSpline = RoadSpline;

	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsInGameView())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MetaLandRoad] Alt+T: GameView was ON — ComponentVisualizer does not draw in GameView; exiting GameView now (or press G)."));
		GCurrentLevelEditingViewportClient->SetGameView(false);
	}

	SetEditorVisMode(EMetaLandRoadEditorVisMode::Lane);

	if (GEditor)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			FColor::Green,
			TEXT("Alt+T ON — green left / red right lanes. Click to select. Alt+T again to exit."));
		GEditor->RedrawLevelEditingViewports(true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+T lane edit ON Spline=%s Sections=%d"),
		*RoadSpline->GetName(),
		RoadSpline->GetLaneSectionsNum());
}

void FMetaLandRoadModule::ShutdownModule()
{
	if (EditorSelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(EditorSelectionChangedHandle);
		EditorSelectionChangedHandle.Reset();
	}

	UnregisterSectionLineEditHotkey();

	if (GEngine)
	{
		//if (PostEngineInitHandle.IsValid())
		//{
		//	FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		//	PostEngineInitHandle.Reset();
		//}
		//if (ComponentVisRegisterTickerHandle.IsValid())
		//{
		//	FTSTicker::GetCoreTicker().RemoveTicker(ComponentVisRegisterTickerHandle);
		//	ComponentVisRegisterTickerHandle.Reset();
		//}
		GEngine->OnLevelActorAdded().RemoveAll(this);
		for (const FName ClassName : RegisteredComponentClassNames)
		{
			if (GUnrealEd)
			{
				GUnrealEd->UnregisterComponentVisualizer(ClassName);
			}
		}
	}
	bComponentVisualizersRegistered = false;

	if (ActorPickerHandle.IsValid())
	{
		FMoKuEditorActorPicker::Get().OnActorPicked().Remove(ActorPickerHandle);
		ActorPickerHandle.Reset();
	}
	FMoKuEditorActorPicker::Get().Unregister();

	FMetaLandRoadEditorModeCommands::Unregister();
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.UnregisterCustomClassLayout(AMoKuEditSplineActor::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	FMetaLandRoadStyle::Shutdown();

}


void FMetaLandRoadModule::OnLevelActorAddedToWorld(AActor* Actor)
{
	if (!Actor)return;
	UWorld* ActorWorld = Actor->GetWorld();

	//check(RoadEditorSettings != nullptr);
	IRoadBrushActorInterface* WorldEditBlueprintBrush = Cast<IRoadBrushActorInterface>(Actor);
	if (WorldEditBlueprintBrush)
	{

		if (ActorWorld && ActorWorld->IsEditorWorld()&&WorldEditBlueprintBrush)
		{
			FBox RoadZoneBounds(ForceInit);
			TArray<ALandscape*> FoundLandscapes;
			bool bFoundIntersectingLandscape = false;

			const bool bNonColliding = true;
			const bool bIncludeChildActors = false;
			const FBox ActorBounds = Actor->GetComponentsBoundingBox(bNonColliding, bIncludeChildActors);

			ALandscape* FallbackLandscape = nullptr;
			for (ALandscape* Landscape : TActorRange<ALandscape>(ActorWorld))
			{
				check(Landscape);
				if (Landscape->GetLandscapeGuid().IsValid())
				{
					if (FallbackLandscape == nullptr)
					{
						FallbackLandscape = Landscape;
					}

					const FBox LandscapeBounds = Landscape->GetCompleteBounds();
					if (LandscapeBounds.Intersect(ActorBounds))
					{
						FoundLandscapes.Add(Landscape);
						// Make sure the water zone's bounds is large enough to fit all landscapes that intersect with this water body :
						RoadZoneBounds += LandscapeBounds;
						bFoundIntersectingLandscape = true;
					}
				}
			}
		
			if (!bFoundIntersectingLandscape && FallbackLandscape != nullptr)
			{
				FoundLandscapes.Add(FallbackLandscape);
				const FBox LandscapeBounds = FallbackLandscape->GetCompleteBounds();
				RoadZoneBounds += LandscapeBounds;
			}
				//WorldEditBlueprintBrush->SetTargetLandscape(Landscape);
			const FBox LandscapeBounds = RoadZoneBounds;
			const UWorldEditorSettings* RoadEditorSettings = GetDefault<UWorldEditorSettings>();

			if ((WorldEditBlueprintBrush != nullptr) && WorldEditBlueprintBrush->AffectsLandscape() && !FoundLandscapes.IsEmpty())
			{

				TSubclassOf<AWorldLandscapeBlueprintBrush> WorldEditBrushClass = RoadEditorSettings->GetWorldEditorManagerClass();
				if (UClass* WorldEditBrushClassPtr = WorldEditBrushClass.Get())
				{
					for (ALandscape* FoundLandscape : FoundLandscapes)
					{
						check(IsValid(FoundLandscape));

						bool bHasEditorManager = false;
						FoundLandscape->ForEachLayerConst([&bHasEditorManager](const FLandscapeLayer& Layer)
						{
								for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
								{
									bHasEditorManager |= Cast<AWorldLandscapeBlueprintBrush>(Brush.GetBrush()) != nullptr;
									if (bHasEditorManager)
									{
										// Stop iterating the moment we've found the brush manager : 
										return false;
									}
								}

								return true;
						});
						if (!bHasEditorManager)
						{
							UActorFactory* WorldEditBrushActorFactory = GEditor->FindActorFactoryForActorClass(WorldEditBrushClassPtr);
							// The manager is added from the BP brush tool only and is being spawned on a valid edit layer
							FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { FoundLandscape->GetActorLabel(), WorldEditBrushClassPtr->GetName() });
							FName BrushActorName = MakeUniqueObjectName(FoundLandscape->GetOuter(), WorldEditBrushClassPtr, FName(BrushActorString));
							FActorSpawnParameters SpawnParams;
							SpawnParams.Name = BrushActorName;
							SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
							AWorldLandscapeBlueprintBrush* NewBrush = (WorldEditBrushActorFactory != nullptr)
								? Cast<AWorldLandscapeBlueprintBrush>(WorldEditBrushActorFactory->CreateActor(WorldEditBrushClassPtr, FoundLandscape->GetLevel(), FTransform(RoadZoneBounds.GetCenter()), SpawnParams))
								: ActorWorld->SpawnActor<AWorldLandscapeBlueprintBrush>(WorldEditBrushClass, SpawnParams);
							//AWorldSceneManagement* NewBrush = ActorWorld->SpawnActor<AWorldSceneManagement>(WorldEditBrushClass, SpawnParams);

							if (NewBrush)
							{
								/*	if (!WaterBrushActorFactory)
									{
										UE_LOG(LogWaterEditor, Warning, TEXT("WaterManager Actor Factory could not be found! The newly spawned %s may have incorrect defaults!"), *NewBrush->GetActorLabel());
									}*/

								bHasEditorManager = true;
								NewBrush->SetActorLabel(BrushActorString);
								NewBrush->SetTargetLandscape(FoundLandscape);

								TObjectPtr<AMoKuEditBaseActor> MoKuEditBaseActor  = Cast<AMoKuEditBaseActor>(Actor);
								MoKuEditBaseActor-> SceneManagement = Cast<AWorldSceneManagement>(NewBrush);

							}
						}

					}
				}

			}
	
		}

	}

}

IMPLEMENT_MODULE(FMetaLandRoadModule, MetaLandRoad)


#undef LOCTEXT_NAMESPACE
