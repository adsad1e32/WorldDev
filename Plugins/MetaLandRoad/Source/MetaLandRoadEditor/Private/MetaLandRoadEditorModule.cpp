// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadEditorHooks.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadSectionHover.h"
#include "MetaLandRoadProfile.h"
#include "MoKuEditorActorPicker.h"
#include "MetaLandRoadEditorModeCommands.h"
#include "MetaLandRoadEditorMode.h"
#include "MoKuEditorUtils.h"
#include "HAL/IConsoleManager.h"
#include "PropertyEditorModule.h"
#include "MoKuSplineActorCustomization.h"
#include "MetaLandRoadCurbProfile.h"
#include "MetaLandRoadCurbEditor.h"
#include "MoKuEditSplineActor.h"
#include "MetaLandRoadPropInstances.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplinesComponent.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadStyle.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadLaneAttribute.h"
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
#include "Tools/MetaLandRoadInteractiveTool.h"
#include "InteractiveToolManager.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "EditorViewportClient.h"
#include "Selection.h"
#include "UObject/UObjectIterator.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"
#include "ComponentVisualizer.h"
#include "LevelEditor.h"
#include "ILevelEditor.h"
#include "EditorModeManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Components/DynamicMeshComponent.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "MetaLandRoadPropCatalogImport.h"
#define LOCTEXT_NAMESPACE "MetaLandRoadModule"

//DEFINE_LOG_CATEGORY(LogRoadEditor);

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

static void RestoreAllRoadActorsViewportSelectionInWorld(UWorld* World)
{
	if (!World)
	{
		return;
	}

	for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
	{
		AMoKuEditSplineActor* Road = *It;
		if (!IsValid(Road) || !Road->DynamicMeshComponent)
		{
			continue;
		}
		Road->DynamicMeshComponent->bSelectable = true;
		Road->DynamicMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}
}

static FAutoConsoleCommand CMetaLandRoadToggleSectionLine(
	TEXT("MetaLandRoad.ToggleSectionLineEdit"),
	TEXT("Toggle native spline visualizer vs section-line visualizer (same as Alt+E)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor")))
		{
			FMetaLandRoadEditorModule::Get().ToggleRoadComponentVisMode();
		}
	}));
#endif

#if WITH_EDITOR
static FAutoConsoleCommand CMetaLandRoadDebugEvalMouth(
	TEXT("MetaLandRoad.DebugEvalMouth"),
	TEXT("Log EvalMouthTransform for World Builder EditTarget anchor (Lane Anchor pick)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		if (!GEditor)
		{
			return;
		}

		UMetaLandRoadEditorMode* RoadMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(
			UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId);
		if (!RoadMode)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LaneAnchor] DebugEvalMouth: not in World Builder mode"));
			return;
		}

		const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
		if (!Target.bHasPendingAnchor || !Target.PendingAnchor.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[LaneAnchor] DebugEvalMouth: no EditTarget anchor — LMB a lane first"));
			return;
		}

		UMetaLandRoadSplineComponent* Spline = Target.Spline.Get();
		if (!Spline)
		{
			Spline = Target.PendingAnchor.SourceSpline.Get();
		}

		if (!Spline)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LaneAnchor] DebugEvalMouth: no spline on EditTarget"));
			return;
		}

		MetaLandRoadLogDrawAnchor(TEXT("DebugEvalMouth"), Target.PendingAnchor);
		FTransform MouthTransform = FTransform::Identity;
		if (Spline->EvalMouthTransform(Target.PendingAnchor, MouthTransform))
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[LaneAnchor] EvalMouth  Loc=%s Rot=%s"),
				*MouthTransform.GetLocation().ToString(),
				*MouthTransform.Rotator().ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[LaneAnchor] DebugEvalMouth: EvalMouthTransform failed"));
		}
	}));
#endif

void FMetaLandRoadEditorModule::RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer)
{
	if (GUnrealEd)
	{
		GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
	}

	RegisteredComponentClassNames.AddUnique(ComponentClassName);
}

bool FMetaLandRoadEditorModule::OnDeferredComponentVisRegisterTick(float /*DeltaTime*/)
{
	if (!bComponentVisualizersRegistered)
	{
		EnsureComponentVisualizersRegistered();
	}
	return !bComponentVisualizersRegistered;
}

void FMetaLandRoadEditorModule::EnsureComponentVisualizersRegistered()
{
	if (!GUnrealEd)
	{
		if (!PostEngineInitHandle.IsValid())
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(
				this,
				&FMetaLandRoadEditorModule::EnsureComponentVisualizersRegistered);
		}
		if (!ComponentVisRegisterTickerHandle.IsValid())
		{
			ComponentVisRegisterTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FMetaLandRoadEditorModule::OnDeferredComponentVisRegisterTick),
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

	const bool bCreatedVisualizer = !RoadComponentVisualizer.IsValid();
	if (bCreatedVisualizer)
	{
		RoadComponentVisualizer = MakeShareable(new FMoKuEditSplinesComponentVisualizer);
	}

	const TSharedPtr<FComponentVisualizer> VisualizerPtr =
		StaticCastSharedPtr<FComponentVisualizer>(RoadComponentVisualizer);

	RegisterComponentVisualizer(
		UMoKuEditSplinesComponent::StaticClass()->GetFName(),
		VisualizerPtr);
	RegisterComponentVisualizer(
		UMetaLandRoadSplineComponent::StaticClass()->GetFName(),
		VisualizerPtr);

	if (bCreatedVisualizer && RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->OnRegister();
	}

	bComponentVisualizersRegistered = true;

	CompVisUtils::RefreshComponentVisualizersForSelection();
}

TSharedPtr<FMoKuEditSplinesComponentVisualizer> FMetaLandRoadEditorModule::GetRoadComponentVisualizer() const
{
	return RoadComponentVisualizer;
}

void FMetaLandRoadEditorModule::BindRuntimeHooks()
{
	FMetaLandRoadEditorHooks::RefreshLaneVisualization = [](UMetaLandRoadSplineComponent* Spline)
	{
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(Spline);
	};
	FMetaLandRoadEditorHooks::RecookJunction = [](AMoKuEditIntersectionActor* Junction, bool bRefreshLinkedRoads)
	{
		UMetaLandRoadEditorMode::RecookJunctionActorStatic(Junction, bRefreshLinkedRoads);
	};
	FMetaLandRoadEditorHooks::TryGetSelectedLandscapeKey =
		[](const UMetaLandRoadSplineComponent* Spline, int32& OutSection, int32& OutKey) -> bool
	{
		const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
			FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer();
		if (!RoadVis.IsValid()
			|| !RoadVis->HasSelectedLandscapeAttributeKey()
			|| RoadVis->GetEditedMetaLandRoadSpline() != Spline)
		{
			return false;
		}
		OutSection = RoadVis->GetSelectedSectionIndex();
		OutKey = RoadVis->GetSelectedLandscapeKeyDisplayIndex();
		return OutKey != INDEX_NONE;
	};
	FMetaLandRoadEditorHooks::NotifySplineComponentSet = [](AMoKuEditSplineActor* Road)
	{
		if (!Road)
		{
			return;
		}
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		{
			TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();
			if (!LevelEditorPtr.IsValid())
			{
				return;
			}
			UMetaLandRoadEditorMode* EditMode = Cast<UMetaLandRoadEditorMode>(
				LevelEditorPtr->GetEditorModeManager().GetActiveScriptableMode("EM_MetaLandRoadEditorMode"));
			if (!EditMode)
			{
				return;
			}
			UMetaLandRoadInteractiveTool* CurTool = Cast<UMetaLandRoadInteractiveTool>(
				EditMode->GetToolManager()->GetActiveTool(EToolSide::Left));
			EditMode->CreateActiveGizmos(Road);
			if (CurTool)
			{
				TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FSocketGizmoTransformInfo>> GizmoInfo;
				GizmoInfo.Add(Road, Road->GizmoTransformInfos);
				CurTool->UpdateGizmoInfo(GizmoInfo);
			}
		}
	};
}

void FMetaLandRoadEditorModule::UnbindRuntimeHooks()
{
	FMetaLandRoadEditorHooks::RefreshLaneVisualization = nullptr;
	FMetaLandRoadEditorHooks::RecookJunction = nullptr;
	FMetaLandRoadEditorHooks::TryGetSelectedLandscapeKey = nullptr;
	FMetaLandRoadEditorHooks::NotifySplineComponentSet = nullptr;
}

void FMetaLandRoadEditorModule::TryRegisterComponentVisualizers()
{
	EnsureComponentVisualizersRegistered();
}

void FMetaLandRoadEditorModule::StartupModule()
{
	if (GEngine)
	{
		RegisterEditorFeatures();
	}
	else if (!EditorFeaturesInitHandle.IsValid())
	{
		EditorFeaturesInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(
			this,
			&FMetaLandRoadEditorModule::RegisterEditorFeatures);
	}
}

void FMetaLandRoadEditorModule::RegisterEditorFeatures()
{
	if (bEditorFeaturesRegistered)
	{
		return;
	}
	bEditorFeaturesRegistered = true;

	if (EditorFeaturesInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(EditorFeaturesInitHandle);
		EditorFeaturesInitHandle.Reset();
	}

	BindRuntimeHooks();
	FMetaLandRoadStyle::Initialize();
	FMetaLandRoadAttributeRegistry::RegisterBuiltInTypes();
	FMetaLandRoadEditorModeCommands::Register();
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(AMoKuEditSplineActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FMoKuSplineConnectAssetMeshDetail::MakeInstance));
	PropertyEditorModule.NotifyCustomizationModuleChanged();
	GEngine->OnLevelActorAdded().AddRaw(this, &FMetaLandRoadEditorModule::OnLevelActorAddedToWorld);
	EnsureComponentVisualizersRegistered();

	if (!bComponentVisualizersRegistered && !LevelEditorModuleChangedHandle.IsValid())
	{
		LevelEditorModuleChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(
			this,
			&FMetaLandRoadEditorModule::OnModulesChangedForHotkey);
	}

	if (GEditor)
	{
		EditorSelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(
			this,
			&FMetaLandRoadEditorModule::OnEditorSelectionChanged);
	}

	RegisterSectionLineEditHotkey();

	if (GEditor)
	{
		GEditor->ActorFactories.Add(NewObject<UWorldBrushEditorManagerFactory>());
	}

	FMoKuEditorActorPicker::Get().Register();
	ActorPickerHandle = FMoKuEditorActorPicker::Get().OnActorPicked().AddRaw(this, &FMetaLandRoadEditorModule::OnEditorActorPicked);
}

void FMetaLandRoadEditorModule::OnEditorActorPicked(AActor* PickedActor)
{
	if (!IsValid(PickedActor) || !GEditor)
	{
		return;
	}

	if (UMetaLandRoadSplineComponent* PickedSpline = PickedActor->FindComponentByClass<UMetaLandRoadSplineComponent>())
	{
		CompVisUtils::SelectRoadSplineExclusive(PickedActor, PickedSpline, true);

		if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
		{
			FMetaLandRoadEditorModule::Get().SyncNativeEditTargetToSelection(PickedSpline);
		}
		else if (RoadComponentVisualizer.IsValid())
		{
			RoadComponentVisualizer->SetActiveForSpline(PickedSpline);
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

void FMetaLandRoadEditorModule::ApplyEditorVisModeSideEffects(bool bShowSectionEdit)
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

	if (!RoadComponentVisualizer.IsValid())
	{
		EnsureComponentVisualizersRegistered();
	}

	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->SyncSectionLineEditModeFlag(bShowSectionEdit);

		if (RoadSpline)
		{
			if (bShowSectionEdit)
			{
				MetaLandRoadProfile::EnsureDefaultRoadProfile(RoadSpline);
				MetaLandRoadProfile::EnsureBothFacesOnRoad(RoadSpline);
				RoadSpline->GetRoadLayoutMutable().UpdateBounds(RoadSpline->GetSplineLength());
				RoadSpline->RefreshLaneVisualization();
			}
			else if (IsLaneEditMode())
			{
				MetaLandRoadProfile::EnsureDefaultRoadProfile(RoadSpline);
				MetaLandRoadProfile::EnsureBothFacesOnRoad(RoadSpline);
				RoadSpline->GetRoadLayoutMutable().UpdateBounds(RoadSpline->GetSplineLength());
			}

			if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(RoadSpline->GetOwner()))
			{
				if (RoadActor->DynamicMeshComponent)
				{
					const bool bAllowMeshPick = IsStandardMode();
					RoadActor->DynamicMeshComponent->bSelectable = bAllowMeshPick;
					RoadActor->DynamicMeshComponent->SetCollisionResponseToChannel(
						ECC_Visibility,
						IsNativeEditMode() ? ECR_Ignore : ECR_Block);
				}
			}

			if (IsStandardMode())
			{
				if (UWorld* World = RoadSpline->GetWorld())
				{
					RestoreAllRoadActorsViewportSelectionInWorld(World);
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
				RoadComponentVisualizer->SetActiveForSpline(RoadSpline);
				RoadComponentVisualizer->ActivateRoadVisualization();
			}
			else if (IsLaneEditMode())
			{
				if (GCurrentLevelEditingViewportClient
					&& GCurrentLevelEditingViewportClient->IsInGameView())
				{
					GCurrentLevelEditingViewportClient->SetGameView(false);
				}
				RoadComponentVisualizer->SetActiveForSpline(RoadSpline);
				RoadComponentVisualizer->EnsureLaneEditMenuSelection();
				RoadComponentVisualizer->ActivateRoadVisualization();
			}
			else if (ShouldShowNativeEditOverlay())
			{
				if (GCurrentLevelEditingViewportClient
					&& GCurrentLevelEditingViewportClient->IsInGameView())
				{
					GCurrentLevelEditingViewportClient->SetGameView(false);
				}
				RoadComponentVisualizer->SetActiveForSpline(RoadSpline);
				RoadComponentVisualizer->ActivateRoadVisualization();
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
				if (!Spline || !GEditor)
				{
					return false;
				}
				const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
					FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer();
				if (!RoadVis.IsValid())
				{
					return false;
				}
				if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
				{
					return false;
				}
				if (bLaneMode)
				{
					if (!FMetaLandRoadEditorModule::Get().IsLaneEditMode())
					{
						return false;
					}
				}
				else if (!FMetaLandRoadEditorModule::Get().IsSectionMode())
				{
					return false;
				}

				CompVisUtils::SelectRoadSplineExclusive(Spline->GetOwner(), Spline, false);
				CompVisUtils::RefreshComponentVisualizersForSelection();
				RoadVis->ActivateRoadVisualization();
				GEditor->RedrawLevelEditingViewports(true);
				return false;
			}),
			0.0f);
	}
}

void FMetaLandRoadEditorModule::SyncSelectedRoadActorToSpline()
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

void FMetaLandRoadEditorModule::OnEditorSelectionChanged(UObject* /*NewSelection*/)
{
	SyncSelectedRoadActorToSpline();

	if (IsNativeEditMode())
	{
		if (UMetaLandRoadSplineComponent* SelectedSpline = FindSelectedOrOwnedRoadSpline())
		{
			SyncNativeEditTargetToSelection(SelectedSpline);
		}
	}
}

void FMetaLandRoadEditorModule::SyncNativeEditTargetToSelection(UMetaLandRoadSplineComponent* SplineComp)
{
	if (!IsNativeEditMode() || !SplineComp)
	{
		return;
	}

	if (UMetaLandRoadSplineComponent* Prev = ActiveNativeEditSpline.Get())
	{
		if (Prev != SplineComp)
		{
			if (AMoKuEditSplineActor* PrevRoad = Cast<AMoKuEditSplineActor>(Prev->GetOwner()))
			{
				if (PrevRoad->DynamicMeshComponent)
				{
					PrevRoad->DynamicMeshComponent->bSelectable = true;
					PrevRoad->DynamicMeshComponent->SetCollisionResponseToChannel(
						ECC_Visibility, ECR_Block);
				}
			}
			if (RoadComponentVisualizer.IsValid())
			{
				RoadComponentVisualizer->ClearNativeKeySelection();
			}
		}
	}

	ActiveNativeEditSpline = SplineComp;

	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		if (Road->DynamicMeshComponent)
		{
			Road->DynamicMeshComponent->bSelectable = false;
			Road->DynamicMeshComponent->SetCollisionResponseToChannel(
				ECC_Visibility, ECR_Ignore);
		}
	}

	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->SetActiveForSpline(SplineComp);
	}
}

void FMetaLandRoadEditorModule::SetEditorVisMode(EMetaLandRoadEditorVisMode NewMode)
{
	if (!GUnrealEd)
	{
		return;
	}

	// Interactive Tool owns the viewport — Alt+W/E/T only while no draw tool is active.
	if (NewMode != EMetaLandRoadEditorVisMode::Standard && IsInteractiveSplineDrawToolActive())
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
		&& RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearNativeKeySelection();
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
		if (RoadComponentVisualizer.IsValid())
		{
			RoadComponentVisualizer->ClearSectionHighlight();
		}
	}

	if (PreviousMode == EMetaLandRoadEditorVisMode::Lane && NewMode != EMetaLandRoadEditorVisMode::Lane)
	{
		if (RoadComponentVisualizer.IsValid())
		{
			RoadComponentVisualizer->ClearSectionHighlight();
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

	if (NewMode != EMetaLandRoadEditorVisMode::Standard)
	{
		SetLandscapeAttributeOverlayVisible(false);
	}

	EditorVisMode = NewMode;
	ApplyEditorVisModeSideEffects(IsSectionMode());

	// Keep ActiveNativeEditSpline in NativeEdit + Section + Lane so first Alt+E (and W→E) always has a draw target.
	// Clear only when returning to Standard.
	if (NewMode == EMetaLandRoadEditorVisMode::Standard)
	{
		ActiveNativeEditSpline.Reset();
		ClearInteractiveSessionLeftovers();
	}

	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();

	// Avoid a second world-wide RefreshLaneVisualization here — Apply already refreshed the
	// active road when entering Section; extra refresh+redraw during mode switch worsens re-entry risk.
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

}

void FMetaLandRoadEditorModule::SetLandscapeAttributeSelected(bool bSelected)
{
	bLandscapeAttributeSelected = bSelected;
	if (!bSelected)
	{
		SetLandscapeAttributeOverlayVisible(false);
	}
	if (bSelected)
	{
		DismissPropSettingOverlay();
		DismissSidewalkOverlay();
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::SetPropSettingSelected(bool bSelected)
{
	bPropSettingSelected = bSelected;
	if (!bSelected)
	{
		bShowPropSettingOverlay = false;
		PropPlaceMode = EPropPlaceMode::Select;
		if (RoadComponentVisualizer.IsValid())
		{
			RoadComponentVisualizer->ClearPropSettingSelection();
		}
	}
	if (bSelected)
	{
		HideLandscapeAttributeDisplay();
		bShowSidewalkOverlay = false;
		bSidewalkSelected = false;
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::EnterPropSettingOverlay()
{
	if (!IsStandardMode())
	{
		ExitEditorVisModesForInteractiveTool();
	}
	SetPropSettingSelected(true);
}

void FMetaLandRoadEditorModule::DismissPropSettingOverlay()
{
	bShowPropSettingOverlay = false;
	bPropSettingSelected = false;
	PropPlaceMode = EPropPlaceMode::Select;
	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearPropSettingSelection();
	}
	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();
	BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::ShowPropSettingOverlayForActor(AActor* Actor)
{
	AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Actor);
	if (!Road)
	{
		return;
	}

	UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent();
	if (!Spline)
	{
		return;
	}

	EnterPropSettingOverlay();
	SetLandscapeAttributeOverlayVisible(false);
	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->SetActiveForSpline(Spline);
	}
	bShowPropSettingOverlay = true;
	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();
	BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

bool FMetaLandRoadEditorModule::ShouldDrawPropSettingOverlay() const
{
	return bShowPropSettingOverlay
		&& bPropSettingSelected
		&& IsStandardMode()
		&& !IsLiveSplineDrawSession();
}

bool FMetaLandRoadEditorModule::ShouldShowPropSettingDetailsPanel() const
{
	if (!bPropSettingSelected || !bShowPropSettingOverlay)
	{
		return false;
	}
	if (!GEditor)
	{
		return false;
	}
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (Cast<AMoKuEditSplineActor>(*It))
		{
			return true;
		}
	}
	return false;
}

void FMetaLandRoadEditorModule::SetPropPlaceMode(EPropPlaceMode Mode)
{
	PropPlaceMode = Mode;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::SetSidewalkSelected(bool bSelected)
{
	bSidewalkSelected = bSelected;
	if (!bSelected)
	{
		bShowSidewalkOverlay = false;
	}
	if (bSelected)
	{
		HideLandscapeAttributeDisplay();
		bShowPropSettingOverlay = false;
		bPropSettingSelected = false;
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::EnterSidewalkOverlay()
{
	if (!IsStandardMode())
	{
		ExitEditorVisModesForInteractiveTool();
	}
	SetSidewalkSelected(true);
}

void FMetaLandRoadEditorModule::DismissSidewalkOverlay()
{
	bShowSidewalkOverlay = false;
	bSidewalkSelected = false;
	SidewalkEdgeSign = 0;
	SidewalkHoverEdgeSign = 0;
	SidewalkHoverS = -1.0;
	SidewalkHoverCornerIndex = INDEX_NONE;
	SidewalkSelectedCornerIndex = INDEX_NONE;
	SidewalkHoverSpline = nullptr;
	SidewalkFocusSpline = nullptr;
	SidewalkHoverJunction = nullptr;
	SidewalkSelectedJunction = nullptr;
	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();
	BroadcastSidewalkEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::ShowSidewalkOverlayForActor(AActor* Actor)
{
	AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Actor);
	AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(Actor);
	if (!Road && !Junction)
	{
		return;
	}

	EnterSidewalkOverlay();
	SetLandscapeAttributeOverlayVisible(false);
	if (Road)
	{
		if (UMetaLandRoadSplineComponent* Spline = Road->GetSplineComponent())
		{
			if (RoadComponentVisualizer.IsValid())
			{
				RoadComponentVisualizer->SetActiveForSpline(Spline);
			}
		}
	}
	bShowSidewalkOverlay = true;
	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

bool FMetaLandRoadEditorModule::ShouldDrawSidewalkOverlay() const
{
	return bShowSidewalkOverlay
		&& bSidewalkSelected
		&& IsStandardMode()
		&& !IsLiveSplineDrawSession();
}

bool FMetaLandRoadEditorModule::ShouldShowSidewalkDetailsPanel() const
{
	if (!bSidewalkSelected || !bShowSidewalkOverlay)
	{
		return false;
	}
	if (!GEditor)
	{
		return false;
	}
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (Cast<AMoKuEditSplineActor>(*It) || Cast<AMoKuEditIntersectionActor>(*It))
		{
			return true;
		}
	}
	return false;
}

void FMetaLandRoadEditorModule::BroadcastSidewalkEditorRefresh()
{
	SidewalkEditorRefresh.Broadcast();
}

void FMetaLandRoadEditorModule::SetSidewalkKeyIndex(int32 Index)
{
	SidewalkKeyIndex = FMath::Max(Index, 0);
	BroadcastSidewalkEditorRefresh();
}

void FMetaLandRoadEditorModule::SetSidewalkEdgeSign(int32 Sign)
{
	SidewalkEdgeSign = (Sign > 0) ? 1 : ((Sign < 0) ? -1 : 0);
	BroadcastSidewalkEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::SetSidewalkHover(int32 Sign, double S, UMetaLandRoadSplineComponent* Spline)
{
	const int32 Next = (Sign > 0) ? 1 : ((Sign < 0) ? -1 : 0);
	const double NextS = Next == 0 ? -1.0 : S;
	if (SidewalkHoverEdgeSign == Next
		&& FMath::IsNearlyEqual(SidewalkHoverS, NextS, 1.0)
		&& SidewalkHoverCornerIndex == INDEX_NONE
		&& SidewalkHoverSpline.Get() == (Next == 0 ? nullptr : Spline))
	{
		return;
	}
	SidewalkHoverEdgeSign = Next;
	SidewalkHoverS = NextS;
	SidewalkHoverCornerIndex = INDEX_NONE;
	SidewalkHoverJunction = nullptr;
	SidewalkHoverSpline = Next == 0 ? nullptr : Spline;
}

void FMetaLandRoadEditorModule::SetSidewalkHoverCorner(AMoKuEditIntersectionActor* Junction, int32 CornerIndex)
{
	if (SidewalkHoverCornerIndex == CornerIndex
		&& SidewalkHoverJunction.Get() == Junction
		&& SidewalkHoverEdgeSign == 0)
	{
		return;
	}
	SidewalkHoverCornerIndex = CornerIndex;
	SidewalkHoverJunction = Junction;
	SidewalkHoverEdgeSign = 0;
	SidewalkHoverS = -1.0;
	SidewalkHoverSpline = nullptr;
}

void FMetaLandRoadEditorModule::SetSidewalkFocusSpline(UMetaLandRoadSplineComponent* Spline)
{
	SidewalkFocusSpline = Spline;
}

void FMetaLandRoadEditorModule::SetSidewalkSelectedCorner(AMoKuEditIntersectionActor* Junction, int32 CornerIndex)
{
	SidewalkSelectedCornerIndex = CornerIndex;
	SidewalkSelectedJunction = CornerIndex == INDEX_NONE ? nullptr : Junction;
}

void FMetaLandRoadEditorModule::BroadcastPropSettingEditorRefresh()
{
	if (RoadComponentVisualizer.IsValid())
	{
		if (UMetaLandRoadSplineComponent* Spline = RoadComponentVisualizer->GetEditedMetaLandRoadSpline())
		{
			UMetaLandRoadPropInstanceComponent::RefreshForRoad(
				Cast<AMoKuEditSplineActor>(Spline->GetOwner()));
		}
	}
	PropSettingEditorRefresh.Broadcast();
}

void FMetaLandRoadEditorModule::EnterLandscapeAttributeOverlay()
{
	if (!IsStandardMode())
	{
		ExitEditorVisModesForInteractiveTool();
	}
	SetLandscapeAttributeSelected(true);
}

void FMetaLandRoadEditorModule::HideLandscapeAttributeDisplay()
{
	LandscapeOverlayJunction.Reset();
	SetLandscapeAttributeSelected(false);
}

void FMetaLandRoadEditorModule::DismissLandscapeOverlayKeepAttribute()
{
	LandscapeOverlayJunction.Reset();
	SetLandscapeAttributeOverlayVisible(false);
	BroadcastLandscapeKeyEditorRefresh();
}

bool FMetaLandRoadEditorModule::ShouldShowLandscapeDetailsPanel() const
{
	if (!bLandscapeAttributeSelected || !bShowLandscapeAttributeOverlay)
	{
		return false;
	}

	if (!GEditor)
	{
		return false;
	}

	if (LandscapeOverlayJunction.IsValid())
	{
		return GEditor->GetSelectedActors()
			&& GEditor->GetSelectedActors()->IsSelected(LandscapeOverlayJunction.Get());
	}

	bool bHasRoad = false;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (Cast<AMoKuEditSplineActor>(*It))
		{
			bHasRoad = true;
		}
		else if (Cast<AActor>(*It))
		{
			return false;
		}
	}
	return bHasRoad;
}

AMoKuEditIntersectionActor* FMetaLandRoadEditorModule::GetLandscapeContextJunction() const
{
	return LandscapeOverlayJunction.Get();
}

void FMetaLandRoadEditorModule::ShowLandscapeOverlayForActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(Actor))
	{
		LandscapeOverlayJunction = Junction;
		if (RoadComponentVisualizer.IsValid())
		{
			RoadComponentVisualizer->ClearLandscapeAttributeKeySelection();
		}
		EnterLandscapeAttributeOverlay();
		SetLandscapeAttributeOverlayVisible(true);
		Junction->RefreshJunctionSampleFromGeometryIfNeeded();
		Junction->NotifyLandscapeBrushChanged(true, true, true);
		if (Junction->JunctionData)
		{
			for (AMoKuEditSplineActor* LinkedRoad : Junction->JunctionData->LinkedRoadActors)
			{
				if (LinkedRoad)
				{
					LinkedRoad->NotifyLandscapeBrushChanged(true, false, true);
				}
			}
		}
		BroadcastLandscapeKeyEditorRefresh();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return;
	}

	LandscapeOverlayJunction.Reset();
	UMetaLandRoadSplineComponent* Spline = nullptr;
	if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Actor))
	{
		Spline = Road->GetSplineComponent();
	}
	if (!Spline)
	{
		return;
	}

	EnterLandscapeAttributeOverlay();
	Spline->EnsureAllCurvePointTypesForSmoothCorners(false);
	MetaLandRoadProfile::PrepareLandscapeKeys(Spline, false);
	Spline->UpdateRoadLayout();

	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->SetActiveForSpline(Spline);
		int32 SelectSection = INDEX_NONE;
		int32 SelectKey = INDEX_NONE;
		if (const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			MetaLandRoadProfile::ForEachVisibleLandscapeKey(
				Spline,
				RoadOwner,
				[&SelectSection, &SelectKey](int32 SectionIndex, int32 KeyIndex, double)
				{
					SelectSection = SectionIndex;
					SelectKey = KeyIndex;
					return false;
				});
		}
		if (SelectSection != INDEX_NONE)
		{
			RoadComponentVisualizer->SelectLandscapeAttributeKey(SelectSection, SelectKey);
		}
		else
		{
			RoadComponentVisualizer->ClearLandscapeAttributeKeySelection();
		}
	}

	if (AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
	{
		RoadOwner->NotifyLandscapeBrushChanged(true, false, true);
	}
	SetLandscapeAttributeOverlayVisible(true);
	BroadcastLandscapeKeyEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::SetLandscapeAttributeOverlayVisible(bool bVisible)
{
	bShowLandscapeAttributeOverlay = bVisible;
	if (!bVisible && RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearLandscapeAttributeKeySelection();
	}
	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();
	BroadcastLandscapeKeyEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMetaLandRoadEditorModule::BroadcastLandscapeKeyEditorRefresh()
{
	LandscapeKeyEditorRefresh.Broadcast();
}

bool FMetaLandRoadEditorModule::ShouldDrawLandscapeAttributeOverlay() const
{
	return bShowLandscapeAttributeOverlay
		&& !ShouldDrawPropSettingOverlay()
		&& !LandscapeOverlayJunction.IsValid()
		&& IsStandardMode()
		&& !IsLiveSplineDrawSession();
}

bool FMetaLandRoadEditorModule::IsInteractiveSplineDrawToolActive() const
{
	if (!GEditor)
	{
		return false;
	}

	if (UMetaLandRoadEditorMode* RoadMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(
		UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId))
	{
		return RoadMode->IsInteractiveSplineToolActive();
	}

	return false;
}

bool FMetaLandRoadEditorModule::IsLiveSplineDrawSession() const
{
	if (!GEditor)
	{
		return false;
	}

	UMetaLandRoadEditorMode* RoadMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(
		UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId);
	if (!RoadMode)
	{
		return false;
	}

	UInteractiveToolManager* ToolManager = RoadMode->GetToolManager();
	if (!ToolManager)
	{
		return false;
	}

	UMetaLandRoadInteractiveTool* DrawTool = Cast<UMetaLandRoadInteractiveTool>(
		ToolManager->GetActiveTool(EToolSide::Left));
	return DrawTool && (DrawTool->GetEditState() || DrawTool->IsLaneAnchorDrawSession());
}

void FMetaLandRoadEditorModule::AbortSplineVisualizerTracking()
{
	// EndEditing clears the visualizer edited-component path before draw tool SelectNone.
	// Do not call FEditorViewportClient::StopTracking() here — it is protected; mouse-up
	// ComponentVisManager::TrackingStopped skips when GetEditedComponent() is already null.
	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->EndEditing();
	}
}

bool FMetaLandRoadEditorModule::CanUseEditorVisModeHotkeys() const
{
	return !IsInteractiveSplineDrawToolActive();
}

void FMetaLandRoadEditorModule::ExitEditorVisModesForInteractiveTool()
{
	if (IsStandardMode())
	{
		return;
	}

	if (IsNativeEditMode())
	{
		ExitNativeSplineEditMode();
		return;
	}

	if (IsLaneEditMode())
	{
		ExitLaneEditMode();
		return;
	}

	// Section / Connection → Standard (allowed while tool is active).
	SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
}

void FMetaLandRoadEditorModule::ClearInteractiveSessionLeftovers()
{
	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearNativeKeySelection();
	}

	if (UMetaLandRoadEditorMode* RoadMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(
		UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId))
	{
		RoadMode->ClearEditTarget();
		RoadMode->ResetInteractiveJunctionSession();

		if (UInteractiveToolManager* ToolManager = RoadMode->GetToolManager())
		{
			if (UMetaLandRoadInteractiveTool* DrawTool =
				Cast<UMetaLandRoadInteractiveTool>(ToolManager->GetActiveTool(EToolSide::Left)))
			{
				DrawTool->ResetDrawSessionState();
			}
		}
	}

	if (GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			RestoreAllRoadActorsViewportSelectionInWorld(World);
		}
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void FMetaLandRoadEditorModule::RestoreAllRoadActorsViewportSelection()
{
	if (GEditor)
	{
		RestoreAllRoadActorsViewportSelectionInWorld(GEditor->GetEditorWorldContext().World());
	}
}

void FMetaLandRoadEditorModule::ToggleRoadComponentVisMode()
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

void FMetaLandRoadEditorModule::SetRoadComponentVisMode(bool bSectionLineMode)
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

void FMetaLandRoadEditorModule::RegisterSectionLineEditHotkey()
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
				&FMetaLandRoadEditorModule::OnModulesChangedForHotkey);
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
			if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor")))
			{
				FMetaLandRoadEditorModule::Get().ToggleRoadComponentVisMode();
			}
		}),
		FCanExecuteAction::CreateLambda([]()
		{
			return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor"))
				&& FMetaLandRoadEditorModule::Get().CanUseEditorVisModeHotkeys();
		}),
		FIsActionChecked::CreateLambda([]()
		{
			return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor"))
				&& FMetaLandRoadEditorModule::Get().IsSectionMode();
		}));

	if (Commands.EnterNativeSplineEdit.IsValid())
	{
		LevelEditorActions->MapAction(
			Commands.EnterNativeSplineEdit,
			FExecuteAction::CreateLambda([]()
			{
				if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor")))
				{
					FMetaLandRoadEditorModule::Get().ToggleNativeSplineEditMode();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor"))
					&& FMetaLandRoadEditorModule::Get().CanUseEditorVisModeHotkeys();
			}),
			FIsActionChecked::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor"))
					&& FMetaLandRoadEditorModule::Get().IsNativeEditMode();
			}));
	}

	if (Commands.ToggleLaneEdit.IsValid())
	{
		LevelEditorActions->MapAction(
			Commands.ToggleLaneEdit,
			FExecuteAction::CreateLambda([]()
			{
				if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor")))
				{
					FMetaLandRoadEditorModule::Get().ToggleLaneEditMode();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor"))
					&& FMetaLandRoadEditorModule::Get().CanUseEditorVisModeHotkeys();
			}),
			FIsActionChecked::CreateLambda([]()
			{
				return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoadEditor"))
					&& FMetaLandRoadEditorModule::Get().IsLaneEditMode();
			}));
	}

	bSectionLineHotkeyMapped = true;
	UE_LOG(LogTemp, Log, TEXT("[MetaLandRoad] Alt+E / Alt+W / Alt+T bound on LevelEditor actions."));
}

void FMetaLandRoadEditorModule::OnModulesChangedForHotkey(FName ModuleName, EModuleChangeReason Reason)
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

void FMetaLandRoadEditorModule::UnregisterSectionLineEditHotkey()
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

UMetaLandRoadSplineComponent* FMetaLandRoadEditorModule::FindSelectedOrOwnedRoadSpline() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	// Viewport selection wins over EditTarget (extend session must not hijack Alt+W on another road).
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

	if (UMetaLandRoadEditorMode* RoadMode = MoKuSplineEditorUtils::GetCurrentEditMode<UMetaLandRoadEditorMode>(
		UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId))
	{
		const FMetaLandRoadEditTarget& Target = RoadMode->GetEditTarget();
		if (Target.Spline.IsValid())
		{
			return Target.Spline.Get();
		}
	}

	if (RoadComponentVisualizer.IsValid())
	{
		return RoadComponentVisualizer->GetEditedMetaLandRoadSpline();
	}

	if (IsNativeEditMode() || IsLaneEditMode())
	{
		return ActiveNativeEditSpline.Get();
	}
	return nullptr;
}

void FMetaLandRoadEditorModule::ExitNativeSplineEditMode()
{
	if (!IsNativeEditMode())
	{
		return;
	}

	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearNativeKeySelection();
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

void FMetaLandRoadEditorModule::ToggleNativeSplineEditMode()
{
	if (IsNativeEditMode())
	{
		ExitNativeSplineEditMode();
		return;
	}
	EnterNativeSplineEditMode();
}

void FMetaLandRoadEditorModule::EnterNativeSplineEditMode()
{
	UMetaLandRoadSplineComponent* RoadSpline = FindSelectedOrOwnedRoadSpline();
	if (!RoadSpline)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+W: no road spline selected"));
		return;
	}

	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsInGameView())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MetaLandRoad] Alt+W: GameView was ON — ComponentVisualizer does not draw in GameView; exiting GameView now (or press G)."));
		GCurrentLevelEditingViewportClient->SetGameView(false);
	}

	SetEditorVisMode(EMetaLandRoadEditorVisMode::NativeEdit);
	SyncNativeEditTargetToSelection(RoadSpline);
	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearNativeKeySelection();
	}

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

void FMetaLandRoadEditorModule::ExitLaneEditMode()
{
	if (!IsLaneEditMode())
	{
		return;
	}

	if (RoadComponentVisualizer.IsValid())
	{
		RoadComponentVisualizer->ClearSectionHighlight();
	}

	ActiveNativeEditSpline.Reset();
	SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Alt+T lane edit OFF"));
}

void FMetaLandRoadEditorModule::ToggleLaneEditMode()
{
	if (IsLaneEditMode())
	{
		ExitLaneEditMode();
		return;
	}
	EnterLaneEditMode();
}

void FMetaLandRoadEditorModule::EnterLaneEditMode()
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

void FMetaLandRoadEditorModule::ShutdownModule()
{
	if (EditorFeaturesInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(EditorFeaturesInitHandle);
		EditorFeaturesInitHandle.Reset();
	}

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
	RoadComponentVisualizer.Reset();

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
	UnbindRuntimeHooks();

}


void FMetaLandRoadEditorModule::OnLevelActorAddedToWorld(AActor* Actor)
{
	// Match WaterEditorModule: skip preview / reinstancer temps — mutating TMaps on those crashes (0xFFFFFFFFFFFFFFFF).
	if (!IsValid(Actor)
		|| Actor->bIsEditorPreviewActor
		|| Actor->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| Actor->IsTemplate()
		|| Actor->IsActorBeingDestroyed())
	{
		return;
	}

	UWorld* ActorWorld = Actor->GetWorld();
	if (!ActorWorld || !ActorWorld->IsEditorWorld() || ActorWorld->bIsTearingDown)
	{
		return;
	}

	IRoadBrushActorInterface* RoadBrushActor = Cast<IRoadBrushActorInterface>(Actor);
	if (!RoadBrushActor || !RoadBrushActor->AffectsLandscape())
	{
		return;
	}

	AWorldLandscapeBlueprintBrush::EnsureWorldEditorManagerForRoadActor(Actor);
}

IMPLEMENT_MODULE(FMetaLandRoadEditorModule, MetaLandRoadEditor)


#undef LOCTEXT_NAMESPACE
