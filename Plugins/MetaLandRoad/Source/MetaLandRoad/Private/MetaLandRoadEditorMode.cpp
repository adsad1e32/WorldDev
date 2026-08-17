// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetaLandRoadEditorMode.h"
#include "Engine/Engine.h"
#include "MetaLandRoadEditorModeToolkit.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "MetaLandRoadEditorModeCommands.h"
#include "EngineUtils.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "UnrealWidget.h"
#include "Tools/MetaLandRoadInteractiveTool.h"
#include "InputCoreTypes.h"
#include "SocketMarkbleGizmo.h"
#include "MoKuEditorGizmoBase.h"
#include "Selection.h"
#include "MoKuEditBaseActor.h"
#include "Tools/ShapesGenerationTool.h"
#include "MoKuEditorUtils.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"
#include "ComponentVisualizer.h"
#include "MetaLandRoadModule.h"
#include "MetaLandRoadLaneEditPick.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "EditSplineRoadInfo.h"
#include "Landscape.h"
#include "WorldEditorSettings.h"
#include "WorldBrushEditorManagerFactory.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadLaneVisualization.h"
#include "EditorModeManager.h"
#include "ScopedTransaction.h"
#include "MoKuEditIntersectionActor.h"
#include "LevelEditorViewport.h"

#define LOCTEXT_NAMESPACE "MetaLandRoadEditorMode"

const FEditorModeID UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId = TEXT("EM_MetaLandRoadEditorMode");
FString UMetaLandRoadEditorMode::InteractiveToolName = SPLINETOOLNAME;
FString UMetaLandRoadEditorMode::ShapesGenerationName = SHAPESTOOLNAME;

UMetaLandRoadEditorMode::UMetaLandRoadEditorMode()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	// appearance and icon in the editing mode ribbon can be customized here
	Info = FEditorModeInfo(UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId,
		LOCTEXT("ModeName", "World Builder"),
		FSlateIcon(),
		true);
}


UMetaLandRoadEditorMode::~UMetaLandRoadEditorMode()
{
}

UMetaLandRoadEditorMode::UMetaLandRoadEditorMode(FVTableHelper& Helper)
	: UBaseLegacyWidgetEdMode(Helper)
{
	
}


void UMetaLandRoadEditorMode::ActorSelectionChangeNotify()
{
	UpdateAssetGizmoImpl();
}

void UMetaLandRoadEditorMode::SetEditTarget(const FMetaLandRoadEditTarget& Target)
{
	EditTarget = Target;
	MetaLandRoadLogDrawAnchor(
		TEXT("EditTarget set"),
		EditTarget.bHasPendingAnchor ? EditTarget.PendingAnchor : FMetaLandRoadDrawAnchor());
}

void UMetaLandRoadEditorMode::ClearEditTarget()
{
	EditTarget = FMetaLandRoadEditTarget();
	UE_LOG(LogTemp, Log, TEXT("[LaneAnchor] EditTarget clear"));
}

bool UMetaLandRoadEditorMode::TryHandleLaneAnchorPickClick(
	FEditorViewportClient* InViewportClient,
	const FViewportClick& Click)
{
	if (!IsInteractiveSplineToolActive())
	{
		return false;
	}

	// Extend draw: Shift/Alt on target road = EndSnap mix (InteractiveTool), not EditTarget.
	if (UMetaLandRoadInteractiveTool* DrawTool =
		Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
	{
		if (DrawTool->IsLaneAnchorDrawSession()
			&& !Click.IsControlDown()
			&& (Click.IsShiftDown() || Click.IsAltDown()))
		{
			return false;
		}
	}

	if (!InViewportClient || !GEditor)
	{
		return false;
	}

	FViewport* Viewport = InViewportClient->Viewport;
	if (!Viewport)
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return false;
	}

	const FIntPoint MousePos = Click.GetClickPos();
	const bool bShiftDown = Click.IsShiftDown();
	if (!bShiftDown)
	{
		return false;
	}
	const bool bSelectAllLanesOnSide = Click.IsControlDown() && bShiftDown;
	const FMetaLandRoadDrawAnchor* ExistingAnchor =
		EditTarget.bHasPendingAnchor ? &EditTarget.PendingAnchor : nullptr;

	FMetaLandRoadDrawAnchor PickedAnchor;
	UMetaLandRoadSplineComponent* PickedSpline = nullptr;
	const bool bPicked = MetaLandRoadLaneEditPick::PickLaneAnchorAtMouse(
		World,
		InViewportClient,
		Viewport,
		MousePos,
		bShiftDown,
		bSelectAllLanesOnSide,
		ExistingAnchor,
		PickedAnchor,
		PickedSpline);

	if (!bPicked)
	{
		if (bShiftDown)
		{
			if (UMetaLandRoadInteractiveTool* DrawTool =
				Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
			{
				if (DrawTool->HasLaneDrawEndSnap())
				{
					DrawTool->ClearLaneDrawEndSnap(true);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(
							991130, 2.0f, FColor::Silver, TEXT("Target snap cleared"));
					}
					if (GEditor)
					{
						GEditor->RedrawLevelEditingViewports(true);
					}
					return true;
				}
			}
		}
		// Miss with an existing bundle: keep selection (accidental empty clicks).
		// Only clear when there was nothing to expand.
		if (bShiftDown && !EditTarget.bHasPendingAnchor)
		{
			ClearEditTarget();
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					991130, 2.0f, FColor::Silver, TEXT("Lane selection cleared"));
			}
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports(true);
			}
			return true;
		}
		return false;
	}

	if (!PickedAnchor.IsValid())
	{
		// Toggle removed last lane — clear.
		ClearEditTarget();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return true;
	}

	AMoKuEditSplineActor* RoadActor = PickedSpline ? Cast<AMoKuEditSplineActor>(PickedSpline->GetOwner()) : nullptr;
	if (EditTarget.bHasPendingAnchor
		&& RoadActor
		&& EditTarget.RoadActor.Get()
		&& RoadActor != EditTarget.RoadActor.Get())
	{
		if (UMetaLandRoadInteractiveTool* DrawTool =
			Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
		{
			DrawTool->ApplyIdleEndSnapTarget(PickedAnchor);
		}
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991131,
				2.5f,
				FColor::Cyan,
				TEXT("[MetaLandRoad] Target mouth locked — source kept | A connects"));
		}
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return true;
	}

	FMetaLandRoadEditTarget NewTarget;
	NewTarget.RoadActor = RoadActor;
	NewTarget.Spline = PickedSpline;
	NewTarget.PendingAnchor = PickedAnchor;
	NewTarget.bHasPendingAnchor = true;
	SetEditTarget(NewTarget);
	GEditor->SelectNone(true, true);

	if (UMetaLandRoadInteractiveTool* DrawTool =
		Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
	{
		DrawTool->SyncAnchorPreviewFromEditTarget(NewTarget);
	}

	if (PickedSpline)
	{
		MetaLandRoadLaneVisualization::RefreshLaneVisualization(PickedSpline);
	}

	FTransform MouthTransform = FTransform::Identity;
	if (PickedSpline && PickedSpline->EvalMouthTransform(PickedAnchor, MouthTransform))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[LaneAnchor] EvalMouth  Loc=%s Rot=%s"),
			*MouthTransform.GetLocation().ToString(),
			*MouthTransform.Rotator().ToString());
	}

	if (GEngine)
	{
		const TCHAR* MouthName = PickedAnchor.MouthSide == EMetaLandRoadMouthSide::Start ? TEXT("Start") : TEXT("End");
		TArray<int32> SelectedLanes;
		SelectedLanes.Add(PickedAnchor.LaneIndex);
		for (int32 BundledLane : PickedAnchor.BundledLaneIndices)
		{
			SelectedLanes.AddUnique(BundledLane);
		}
		SelectedLanes.Sort([](int32 A, int32 B)
		{
			return FMath::Abs(A) < FMath::Abs(B);
		});
		FString LaneList;
		for (int32 i = 0; i < SelectedLanes.Num(); ++i)
		{
			if (i > 0)
			{
				LaneList += TEXT(",");
			}
			LaneList += FString::FromInt(SelectedLanes[i]);
		}
		GEngine->AddOnScreenDebugMessage(
			991130,
			5.0f,
			FColor::Cyan,
			FString::Printf(
				TEXT("Bundle [%s] count=%d @ S=%.0f (%s) | Shift+/- | Ctrl+Shift=all | Ctrl=extend"),
				*LaneList,
				SelectedLanes.Num(),
				PickedAnchor.AnchorS,
				MouthName));
		GEditor->RedrawLevelEditingViewports(true);
	}

	return true;
}

void UMetaLandRoadEditorMode::Enter()
{
	Super::Enter();
	ResetInteractiveJunctionSession();
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(TEXT("SocketGizmo"), NewObject<USocketMarkbleGizmoBuilder>());
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(TEXT("AssetMark"), NewObject<UMoKuEditRePlaceGizmoBuilder>());
	const FMetaLandRoadEditorModeCommands& SampleToolCommands = FMetaLandRoadEditorModeCommands::Get();
	RegisterTool(SampleToolCommands.InteractiveTool, InteractiveToolName, NewObject<UMetaLandRoadInteractiveToolBuilder>(this));
	RegisterTool(SampleToolCommands.ShapesTool, ShapesGenerationName, NewObject<UShapesGenerationToolBuilder>(this));

	if (GEditor)
	{
		GEditor->SelectNone(true, true);
		GEditor->OnLevelActorDeleted().AddUObject(this, &UMetaLandRoadEditorMode::RefreshGizmoVisual);
	}

	MoKuSplineEditorUtils::CreateSceneStructure();

	SceneManagement = nullptr;
	if (UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		TArray<ALandscape*> LandscapesNeedingEditor;
		for (ALandscape* Landscape : TActorRange<ALandscape>(World))
		{
			if (IsValid(Landscape) && Landscape->GetLandscapeGuid().IsValid() && Landscape->CanHaveLayersContent())
			{
				LandscapesNeedingEditor.Add(Landscape);
			}
		}

		for (ALandscape* Landscape : LandscapesNeedingEditor)
		{
			if (AWorldSceneManagement* Manager = AWorldLandscapeBlueprintBrush::EnsureWorldEditorManagerForLandscape(Landscape))
			{
				if (!SceneManagement)
				{
					SceneManagement = Manager;
				}
			}
		}

		// Register default "Road" weightmap on existing roads so WorldEditor paints Road_LayerInfo under meshes.
		for (TActorIterator<AMoKuEditBaseActor> It(World); It; ++It)
		{
			AMoKuEditBaseActor* RoadBody = *It;
			if (!IsValid(RoadBody) || !RoadBody->AffectsLandscape())
			{
				continue;
			}
			RoadBody->EnsureDefaultTerrainInfluenceSettings();
			FOnRoadBodyChangedParams Params;
			Params.PropertyChangedEvent.ChangeType = EPropertyChangeType::ValueSet;
			Params.bWeightmapSettingsChanged = true;
			Params.bShapeOrPositionChanged = true;
			Params.bUserTriggered = false;
			RoadBody->OnRoadBodyChanged(Params);
		}

		if (!SceneManagement)
		{
			for (TActorIterator<AWorldSceneManagement> It(World); It; ++It)
			{
				if (IsValid(*It))
				{
					SceneManagement = *It;
					break;
				}
			}
		}
	}
}

void UMetaLandRoadEditorMode::Exit()
{
	ClearEditTarget();
	ResetInteractiveJunctionSession();
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		FMetaLandRoadModule::Get().ClearInteractiveSessionLeftovers();
		FMetaLandRoadModule::Get().SetEditorVisMode(EMetaLandRoadEditorVisMode::Standard);
	}
	UMetaLandRoadInteractiveTool* CurTool = (UMetaLandRoadInteractiveTool*)GetToolManager()->GetActiveTool(EToolSide::Left);

	if (CurTool)
	{
		for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair : CurTool->GizmoListInfo)
		{

			for (auto Item : KeyValuePair.Value)
			{
				GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
			}
		}
	}
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(TEXT("SocketGizmo"));
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(TEXT("AssetMark"));
	GEditor->OnLevelActorDeleted().RemoveAll(this);
	GEditor->OnLevelActorAdded().RemoveAll(this);
	ActiveGizmos.Empty();
	UEdMode::Exit();
}

void UMetaLandRoadEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FMetaLandRoadEditorModeToolkit);
}
void UMetaLandRoadEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	Super::OnToolStarted(Manager, Tool);

	if (Tool
		&& (Tool->IsA<UMetaLandRoadInteractiveTool>() || Tool->IsA<UShapesGenerationTool>())
		&& FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		FMetaLandRoadModule::Get().ExitEditorVisModesForInteractiveTool();
	}
}

void UMetaLandRoadEditorMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	Super::OnToolEnded(Manager, Tool);

	if (Tool && Tool->IsA<UMetaLandRoadInteractiveTool>())
	{
		ClearEditTarget();
		// Vis modes stay Standard while tool was active (cleared on start). Do not force again.
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
	}
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UMetaLandRoadEditorMode::GetModeCommands() const
{
	return FMetaLandRoadEditorModeCommands::Get().GetCommands();
}



bool UMetaLandRoadEditorMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	return false;
}



bool UMetaLandRoadEditorMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	if((InEvent == IE_Pressed) && (InKey == EKeys::Escape))
	{
		if (UMetaLandRoadInteractiveTool* DrawTool =
			Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
		{
			if (DrawTool->HasLaneDrawEndSnap())
			{
				DrawTool->ClearLaneDrawEndSnap(true);
				if (GEditor)
				{
					GEditor->RedrawLevelEditingViewports(false);
				}
				return true;
			}
		}
		GetToolManager()->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);
		ActorSelectionChangeNotify();
		return true;
	}

	if ((InEvent == IE_Pressed) && (InKey == EKeys::A))
	{
		if (UMetaLandRoadInteractiveTool* DrawTool =
			Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
		{
			DrawTool->OnFinishedSplineEdit();
			return true;
		}
	}

	if ((InEvent == IE_Pressed) && (InKey == EKeys::P))
	{
		bool bIsVisibility = false;
		if (DisplayMode == GizmoMode)
		{
			DisplayMode = MoKuEditDisplayMode::EditMode;
			bIsVisibility = false;
		}
		else
		{
			DisplayMode = MoKuEditDisplayMode::GizmoMode;
			bIsVisibility = true;
		}
		for (auto& It : ActiveGizmos)
		{
			It->SetVisibility(bIsVisibility);
		}
	}

	return false;
}

bool UMetaLandRoadEditorMode::InputDelta(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FVector& InDelta,
	FRotator& InRot,
	FVector& InScale)
{
	// WASD / RMB look must not move spline keys — only LMB drag on the gizmo axis.
	const bool bLmbGizmoDrag = ViewportClient && Viewport
		&& ViewportClient->IsTracking()
		&& Viewport->KeyState(EKeys::LeftMouseButton)
		&& !Viewport->KeyState(EKeys::RightMouseButton)
		&& ViewportClient->GetCurrentWidgetAxis() != EAxisList::None;
	if (bLmbGizmoDrag
		&& FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
			FMetaLandRoadModule::Get().GetRoadComponentVisualizer();
		if (RoadVis.IsValid())
		{
			const FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
			if (Module.ShouldShowNativeEditOverlay() || Module.IsLaneEditMode())
			{
				if (RoadVis->HandleInputDelta(
					ViewportClient, Viewport, InDelta, InRot, InScale))
				{
					return true;
				}
			}
		}
	}
	return Super::InputDelta(ViewportClient, Viewport, InDelta, InRot, InScale);
}

void UMetaLandRoadEditorMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);

	UMetaLandRoadInteractiveTool* CurTool = Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left));
	if (CurTool)
	{
		DisplayMode = MoKuEditDisplayMode::EditMode;
		for (auto& It : ActiveGizmos)
		{
			It->SetVisibility(false);
		}
	}

	if (GEditor && FMetaLandRoadModule::Get().IsStandardMode())
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

bool UMetaLandRoadEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (Click.GetKey() == EKeys::LeftMouseButton && !Click.IsControlDown())
	{
		UMetaLandRoadInteractiveTool* DrawTool =
			Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left));

		if (!Click.IsShiftDown())
		{
			// Plain LMB: never lane-pick; clear stale mouth selection after lane session.
			if (EditTarget.bHasPendingAnchor && (!DrawTool || !DrawTool->IsLaneAnchorDrawSession()))
			{
				ClearEditTarget();
				if (DrawTool)
				{
					DrawTool->ClearDrawGuidanceRails();
				}
				if (GEditor)
				{
					GEditor->RedrawLevelEditingViewports(true);
				}
			}
			return false;
		}

		if (DrawTool)
		{
			if (DrawTool->IsLaneAnchorDrawSession()
				&& (Click.IsShiftDown() || Click.IsAltDown()))
			{
				// Let InteractiveTool OnClickPress own EndSnap target mix.
				return false;
			}
		}

		if (TryHandleLaneAnchorPickClick(InViewportClient, Click))
		{
			return true;
		}
	}

	const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadModule::Get().GetRoadComponentVisualizer();
	if (!RoadVis.IsValid())
	{
		return false;
	}

	if (Click.GetKey() != EKeys::RightMouseButton)
	{
		return false;
	}

	if (RoadVis->IsSectionLineEditMode())
	{
		return RoadVis->TryHandleRoadSplinePickClick(
			InViewportClient,
			HitProxy,
			Click,
			true);
	}

	if (FMetaLandRoadModule::Get().IsLaneEditMode())
	{
		// Plain RMB: do not select lane / open menu. Ctrl+RMB only.
		const bool bCtrlDown =
			FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
		if (!bCtrlDown)
		{
			return false;
		}
		return RoadVis->TryHandleLaneEditPickClick(
			InViewportClient,
			HitProxy,
			Click,
			true);
	}

	return false;
}

void UMetaLandRoadEditorMode::RefreshGizmoVisual(AActor* InActor)
{
	if (InActor->IsA<AMoKuEditSplineActor>())
	{
		AMoKuEditSplineActor* DelActor = Cast<AMoKuEditSplineActor>(InActor);
		UMetaLandRoadInteractiveTool* CurTool = (UMetaLandRoadInteractiveTool*)GetToolManager()->GetActiveTool(EToolSide::Left);
		if (CurTool)
		{
			auto RtnVal = CurTool->GizmoListInfo.Find(DelActor);
			if (RtnVal)
			{
				for (auto Item : *RtnVal)
				{
					GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
				}

			}
			CurTool->GizmoListInfo.Remove(DelActor);		
			for (auto& ActiveItem : ActiveGizmos)
			{
				if (ActiveItem->OwnerGizmoParams.OwnerActor)
				{
					GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(ActiveItem);
				}
			}
			ActiveGizmos.Empty();
		}
	}
}

bool UMetaLandRoadEditorMode::UsesTransformWidget() const
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		const FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
		if (Module.ShouldShowNativeEditOverlay() || Module.IsSectionMode())
		{
			return true;
		}
		if (Module.IsLaneEditMode())
		{
			if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
				Module.GetRoadComponentVisualizer())
			{
				return RoadVis->HasSelectedLaneWidthKey();
			}
			return false;
		}
	}
	return Super::UsesTransformWidget();
}

bool UMetaLandRoadEditorMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		const FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
		if (Module.ShouldShowNativeEditOverlay() || Module.IsSectionMode())
		{
			return CheckMode == UE::Widget::WM_Translate || CheckMode == UE::Widget::WM_None;
		}
		if (Module.IsLaneEditMode())
		{
			if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
				Module.GetRoadComponentVisualizer())
			{
				if (RoadVis->HasSelectedLaneWidthKey())
				{
					return CheckMode == UE::Widget::WM_Translate || CheckMode == UE::Widget::WM_None;
				}
			}
			return false;
		}
	}
	return Super::UsesTransformWidget(CheckMode);
}

FVector UMetaLandRoadEditorMode::GetWidgetLocation() const
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
		&& !FMetaLandRoadModule::Get().IsInteractiveSplineDrawToolActive())
	{
		const FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
		if (Module.ShouldShowNativeEditOverlay()
			|| Module.IsSectionMode()
			|| Module.IsLaneEditMode())
		{
			if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
				Module.GetRoadComponentVisualizer())
			{
				FVector KeyLocation = FVector::ZeroVector;
				if (RoadVis->GetWidgetLocation(GCurrentLevelEditingViewportClient, KeyLocation))
				{
					return KeyLocation;
				}
			}
		}
	}
	return Super::GetWidgetLocation();
}

bool UMetaLandRoadEditorMode::ShouldDrawWidget() const
{
	// Alt+W/E/T: CompVis key/section location, not the actor pivot.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		const FMetaLandRoadModule& Module = FMetaLandRoadModule::Get();
		if (Module.ShouldShowNativeEditOverlay() || Module.IsSectionMode())
		{
			return true;
		}
		if (Module.IsLaneEditMode())
		{
			if (const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
				Module.GetRoadComponentVisualizer())
			{
				return RoadVis->HasSelectedLaneWidthKey();
			}
			return false;
		}
	}

	// Interactive Tool owns the viewport — hide actor xform gizmo.
	if (GetInteractiveToolsContext() != nullptr && GetToolManager()->HasAnyActiveTool())
	{
		return false;
	}

	return UBaseLegacyWidgetEdMode::ShouldDrawWidget();
}
void UMetaLandRoadEditorMode::UpdateAssetGizmoImpl()
{

	if (GetInteractiveToolsContext() != nullptr && !GetToolManager()->HasAnyActiveTool())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World != nullptr)
		{
			USelection* SelectedActors = GEditor->GetSelectedActors();
			if (SelectedActors)
			{
				for (FSelectionIterator It(*SelectedActors); It; ++It)
				{
					UObject* SelectedObj = *It;
					if (SelectedObj->IsA<AMoKuEditSplineActor>())
					{
						AMoKuEditSplineActor* ActiveActor = Cast<AMoKuEditSplineActor>(SelectedObj);
						if (ActiveActor)
						{
							CreateActiveGizmos(ActiveActor);
						}

					}
				}
			}

		}

	}
}
void UMetaLandRoadEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (!PDI || !View || !Viewport || PDI->IsHitTesting())
	{
		return;
	}

	if (!GetToolManager())
	{
		return;
	}

	UMetaLandRoadInteractiveTool* DrawTool =
		Cast<UMetaLandRoadInteractiveTool>(GetToolManager()->GetActiveTool(EToolSide::Left));

	// Lane pick overlay: hide only while Ctrl is actively placing points (not during extend/EndSnap mix).
	const bool bSuppressMouthOverlay = DrawTool && DrawTool->ShouldSuppressLanePickOverlay();
	// EditTarget yellow mouth: suppress for whole extend session (not only Ctrl place).
	const bool bSuppressEditHover = DrawTool && DrawTool->ShouldSuppressEditTargetMouthBar();
	FMetaLandRoadDrawAnchor EndSnapMixOverlay;
	const FMetaLandRoadDrawAnchor* EndSnapMixPtr = nullptr;
	if (DrawTool && DrawTool->TryGetLaneDrawEndSnapOverlay(EndSnapMixOverlay))
	{
		EndSnapMixPtr = &EndSnapMixOverlay;
	}
	FMetaLandRoadDrawAnchor EndSnapHoverOverlay;
	const FMetaLandRoadDrawAnchor* EndSnapHoverPtr = nullptr;
	if (DrawTool && DrawTool->TryGetEndSnapMixHoverOverlay(EndSnapHoverOverlay))
	{
		EndSnapHoverPtr = &EndSnapHoverOverlay;
	}

	FMetaLandRoadDrawAnchor ShiftMouthHoverAnchor;
	if (!EndSnapHoverPtr
		&& GEditor
		&& Viewport)
	{
		const FModifierKeysState& ModKeys = FSlateApplication::Get().GetModifierKeys();
		const bool bExtendMixHover =
			DrawTool
			&& DrawTool->IsLaneAnchorDrawSession()
			&& !ModKeys.IsControlDown();
		const bool bIdleShiftHover =
			ModKeys.IsShiftDown()
			&& !ModKeys.IsControlDown();
		const bool bIdleTargetHover =
			EditTarget.bHasPendingAnchor
			&& !ModKeys.IsControlDown();
		if (bExtendMixHover || bIdleShiftHover || bIdleTargetHover)
		{
			if (FEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient)
			{
				if (UWorld* World = GEditor->GetEditorWorldContext().World())
				{
					FIntPoint MousePos;
					Viewport->GetMousePos(MousePos);
					UMetaLandRoadSplineComponent* HoverSpline = nullptr;
					if (MetaLandRoadLaneEditPick::PickLaneAnchorAtMouse(
						World,
						ViewportClient,
						Viewport,
						MousePos,
						/*bShiftDown*/ true,
						/*bSelectAllLanesOnSide*/ false,
						nullptr,
						ShiftMouthHoverAnchor,
						HoverSpline)
						&& ShiftMouthHoverAnchor.IsValid())
					{
						const bool bHoverIsSourceMouth =
							EditTarget.RoadActor.Get()
							&& HoverSpline
							&& HoverSpline->GetOwner() == EditTarget.RoadActor.Get();
						if (!bHoverIsSourceMouth)
						{
							EndSnapHoverPtr = &ShiftMouthHoverAnchor;
						}
					}
				}
			}
		}
	}

	if (FMetaLandRoadModule::Get().IsStandardMode()
		&& IsInteractiveSplineToolActive()
		&& !bSuppressMouthOverlay)
	{
		if (UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
		{
			// Lane pick: per-lane mouth bars only (no face fills).
			MetaLandRoadLaneVisualization::DrawWorldBuilderMouthCrossSections(
				World,
				View,
				Viewport,
				PDI,
				EditTarget,
				EndSnapMixPtr,
				bSuppressEditHover,
				EndSnapHoverPtr);
		}
	}

	if (DrawTool && DrawTool->ShouldDrawExtendOverlay())
	{
		DrawTool->DrawDrawGuidanceRails(PDI);
	}
}
void UMetaLandRoadEditorMode::BindCommands()
{
	const FMetaLandRoadEditorModeCommands& Commands = FMetaLandRoadEditorModeCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
}

void UMetaLandRoadEditorMode::CreateActiveGizmos(AActor* InActor)
{
	for (auto& It : ActiveGizmos)
	{
		GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(It);
	}
	ActiveGizmos.Empty();
	if (InActor)
	{
		FGizmoStoredInfoParams GizmoInfoParams;
		if (AMoKuEditSplineActor* ActiveActor = Cast<AMoKuEditSplineActor>(InActor))
		{
			TArray<UStaticMeshComponent*> LocalMeshes = ActiveActor->GetSplineComponent()->LocalMeshComponents;
			int32 Index = 0;
			for (const auto& Mesh : LocalMeshes)
			{
				if (Mesh)
				{
					GizmoInfoParams.OwnerActor = ActiveActor;
					GizmoInfoParams.OwnerMesh = Mesh->GetStaticMesh();
					GizmoInfoParams.Index = Index;
					CreateGizmoHandle(Mesh, GizmoInfoParams);
					Index++;
				}
			}
			//�����ExtraMesh
			int ExtraNum = ActiveActor->ExtraLocalMeshComponents.Num();
			for (int i = 0; i < ActiveActor->ExtraLocalMeshComponents.Num(); i++)
			{
				int SubIndex = 0;
				for (const auto& ExtraItem : ActiveActor->ExtraLocalMeshComponents[i])
				{
					if (ExtraItem)
					{
						TMap<int, int> ExtraMeshInfo;
						ExtraMeshInfo.Add(i, SubIndex);

						GizmoInfoParams.OwnerActor = ActiveActor;
						GizmoInfoParams.OwnerMesh = ExtraItem->GetStaticMesh();
						GizmoInfoParams.ExtraAssetIndex = ExtraMeshInfo;
						CreateGizmoHandle(ExtraItem, GizmoInfoParams);
						SubIndex++;
					}
				}
			}
		}
	}


}
void UMetaLandRoadEditorMode::CreateGizmoHandle(UStaticMeshComponent* InMesh,const FGizmoStoredInfoParams& InGizmoInfoParams)
{
	AActor* ParentActor = InMesh->GetOwner();
	FVector Location = InMesh->GetComponentTransform().GetLocation();
	if (ParentActor)
	{
		FVector ActorLocation = ParentActor->GetActorLocation();
		FQuat ActorQuat = FQuat(ParentActor->GetActorRotation());
		if (USplineMeshComponent* SplineMesh = Cast<USplineMeshComponent>(InMesh))
		{
			Location = (SplineMesh->GetStartPosition() + SplineMesh->GetEndPosition()) / 2;
			Location = ActorQuat.RotateVector(Location);
			Location += ActorLocation;

		}
	}
	UMoKuEditRePlaceGizmo* AssetGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UMoKuEditRePlaceGizmo>(TEXT("AssetMark"), FString(), this);
	AssetGizmo->CreateGizmoHandle(Location,InMesh->GetComponentTransform().GetRotation());
	AssetGizmo->SetSelectedObject(InGizmoInfoParams);
	if (DisplayMode == MoKuEditDisplayMode::GizmoMode)
	{
		AssetGizmo->SetVisibility(true);
	}
	ActiveGizmos.Add(AssetGizmo);

}



void UMetaLandRoadEditorMode::OnLevelActorAddedToWorld(AActor* Actor)
{
	const UWorldEditorSettings* WorldEditorSettings = GetDefault<UWorldEditorSettings>();
	check(WorldEditorSettings != nullptr);
	AWorldLandscapeBlueprintBrush* WorldEditBlueprintBrush = Cast<AWorldLandscapeBlueprintBrush>(Actor);
	if (WorldEditBlueprintBrush)
	{
		UWorld* ActorWorld = WorldEditBlueprintBrush->GetWorld();

		if (ActorWorld)
		{
			FBox WaterZoneBounds(ForceInit);
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
						WaterZoneBounds += LandscapeBounds;
						bFoundIntersectingLandscape = true;
					}
				}
			}

			if (!bFoundIntersectingLandscape && FallbackLandscape != nullptr)
			{
				FoundLandscapes.Add(FallbackLandscape);
				const FBox LandscapeBounds = FallbackLandscape->GetCompleteBounds();
				WaterZoneBounds += LandscapeBounds;
			}
			//WorldEditBlueprintBrush->SetTargetLandscape(Landscape);

			TSubclassOf<AWorldLandscapeBlueprintBrush> WorldEditBrushClass = WorldEditorSettings->GetWorldEditorManagerClass();
			if (UClass* WorldEditBrushClassPtr = WorldEditBrushClass.Get())
			{
				for (ALandscape* FoundLandscape : FoundLandscapes)
				{
					check(IsValid(FoundLandscape));

					bool bHasWaterManager = false;
					FoundLandscape->ForEachLayerConst([&bHasWaterManager](const FLandscapeLayer& Layer)
						{
							for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
							{
								bHasWaterManager |= Cast<AWorldLandscapeBlueprintBrush>(Brush.GetBrush()) != nullptr;
								if (bHasWaterManager)
								{
									// Stop iterating the moment we've found the brush manager : 
									return false;
								}
							}

							return true;
						});


					if (!bHasWaterManager)
					{
						//UActorFactory* WorldEditBrushActorFactory = GEditor->FindActorFactoryForActorClass(WorldEditBrushClassPtr);

						// The manager is added from the BP brush tool only and is being spawned on a valid edit layer
						FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { FoundLandscape->GetActorLabel(), WorldEditBrushClassPtr->GetName() });
						FName BrushActorName = MakeUniqueObjectName(FoundLandscape->GetOuter(), WorldEditBrushClassPtr, FName(BrushActorString));
						FActorSpawnParameters SpawnParams;
						SpawnParams.Name = BrushActorName;
						SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
						/*						AWorldLandscapeBlueprintBrush* NewBrush = (WorldEditBrushActorFactory != nullptr)
													? Cast<AWorldLandscapeBlueprintBrush>(WorldEditBrushActorFactory->CreateActor(WorldEditBrushClassPtr, FoundLandscape->GetLevel(), FTransform(WaterZoneBounds.GetCenter()), SpawnParams))
					: */AWorldSceneManagement* NewBrush = ActorWorld->SpawnActor<AWorldSceneManagement>(WorldEditBrushClass, SpawnParams);

					if (NewBrush)
					{
						/*	if (!WaterBrushActorFactory)
							{
								UE_LOG(LogWaterEditor, Warning, TEXT("WaterManager Actor Factory could not be found! The newly spawned %s may have incorrect defaults!"), *NewBrush->GetActorLabel());
							}*/

						bHasWaterManager = true;
						//NewBrush->SetActorLabel(BrushActorString);
						NewBrush->SetTargetLandscape(FoundLandscape);
					}
					}
				}

			}

		}

	}

}


namespace MetaLandRoadEditorModeJunctionPrivate
{
	struct FDetectedArmHit
	{
		TWeakObjectPtr<UMetaLandRoadSplineComponent> Spline;
		FVector IntersectWorld = FVector::ZeroVector;
		double IntersectDistanceS = 0.0;
		double CutDistanceS = 0.0;
	};

	static double ComputeCutDistanceS(
		UMetaLandRoadSplineComponent* Spline,
		const FVector& IntersectWorld,
		double ForwardIndent)
	{
		if (!Spline)
		{
			return 0.0;
		}

		const double SplineLength = Spline->GetSplineLength();
		if (SplineLength <= KINDA_SMALL_NUMBER)
		{
			return 0.0;
		}

		const double IntersectS = Spline->GetDistanceAlongSplineAtLocation(IntersectWorld, ESplineCoordinateSpace::World);
		const double MinSplitDistance = 50.0;
		const double CutS = FMath::Clamp(
			IntersectS - ForwardIndent,
			MinSplitDistance,
			SplineLength - MinSplitDistance);
		return CutS;
	}

	static UMetaLandRoadSplineComponent* GetConnectSourceSpline(
		UMetaLandRoadSplineComponent* RoadSpline,
		bool* bOutConnectAtStart = nullptr);

	/** Fill Arm.ConnectedLaneIndices from per-lane ConnectTo (2-lane extend ≠ full mouth). */
	/** Mouth half-width for indent (subset lanes when set, else full road). */
	static double GetArmIndentHalfW(
		const FMetaLandRoadJunctionArmRef& Arm,
		UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return 1.0;
		}
		const double IntersectS = Arm.IntersectDistanceS;
		const double FullHalf = FMath::Max(Spline->GetRoadHalfWidthAtS(IntersectS), 1.0);
		if (Arm.ConnectedLaneIndices.Num() > 0)
		{
			return FMath::Max(
				Spline->GetRoadHalfWidthForLaneIndicesAtS(IntersectS, Arm.ConnectedLaneIndices),
				1.0);
		}
		return FullHalf;
	}

	static bool IsLaneBranchSpline(const UMetaLandRoadSplineComponent* Spline)
	{
		const AMoKuEditSplineActor* RoadActor = Spline
			? Cast<AMoKuEditSplineActor>(Spline->GetOwner())
			: nullptr;
		return RoadActor && RoadActor->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch")));
	}

	static bool IsMultiHubConnectorRoad(
		const UMetaLandRoadSplineComponent* Spline,
		double IndentHalfW)
	{
		if (!Spline)
		{
			return false;
		}
		const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Spline->GetOwner());
		const double SplineLength = Spline->GetSplineLength();
		if (IsLaneBranchSpline(Spline))
		{
			return true;
		}
		int32 LinkedHubCount = 0;
		if (RoadActor)
		{
			if (RoadActor->StartOfJunction.IsValid())
			{
				++LinkedHubCount;
			}
			if (RoadActor->EndOfJunction.IsValid()
				&& RoadActor->EndOfJunction.Get() != RoadActor->StartOfJunction.Get())
			{
				++LinkedHubCount;
			}
			for (const FMetaLandRoadJunctionTrimZone& Zone : RoadActor->JunctionTrimZones)
			{
				if (AMoKuEditIntersectionActor* Hub = Zone.Junction.Get())
				{
					if (Hub != RoadActor->StartOfJunction.Get()
						&& Hub != RoadActor->EndOfJunction.Get())
					{
						++LinkedHubCount;
					}
				}
			}
		}
		return LinkedHubCount >= 2 || SplineLength < IndentHalfW * 8.0;
	}

	static void ApplyArmCutFromForwardIndent(
		FMetaLandRoadJunctionArmRef& Arm,
		UMetaLandRoadSplineComponent* Spline,
		double ForwardIndent)
	{
		constexpr double MinMargin = 50.0;
		if (!Spline || ForwardIndent <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const double SplineLength = Spline->GetSplineLength();
		const double IntersectS = Arm.IntersectDistanceS;
		const double IndentHalfW = GetArmIndentHalfW(Arm, Spline);
		const bool bMultiHubConnector = IsMultiHubConnectorRoad(Spline, IndentHalfW);

		const double DistToStart = IntersectS;
		const double DistToEnd = SplineLength - IntersectS;
		const double BodyReserve = bMultiHubConnector
			? FMath::Max(IndentHalfW * 2.0, FMath::Min(SplineLength * 0.45, 400.0))
			: FMath::Max(IndentHalfW * 0.5, 150.0);
		const double StubTailReserve = bMultiHubConnector
			? FMath::Max(IndentHalfW * 0.25, 40.0)
			: FMath::Max(IndentHalfW * 0.5, 150.0);

		const double ApproachRoom = FMath::Max(DistToStart - MinMargin - StubTailReserve, 0.0);
		const double DepartRoom = FMath::Max(DistToEnd - MinMargin - StubTailReserve, 0.0);
		double ApproachIndent = FMath::Min(ForwardIndent, ApproachRoom);
		double DepartIndent = FMath::Min(ForwardIndent, DepartRoom);

		if (ApproachIndent > KINDA_SMALL_NUMBER && DepartIndent > KINDA_SMALL_NUMBER)
		{
			const double Used = ApproachIndent + DepartIndent;
			const double MaxUsed = FMath::Max(SplineLength - BodyReserve, 0.0);
			if (Used > MaxUsed && Used > KINDA_SMALL_NUMBER)
			{
				const double Scale = MaxUsed / Used;
				ApproachIndent *= Scale;
				DepartIndent *= Scale;
			}
		}

		Arm.ForwardIndent = FMath::Max(ApproachIndent, DepartIndent);
		Arm.CutDistanceS = FMath::Clamp(IntersectS - ApproachIndent, MinMargin, SplineLength - MinMargin);
		Arm.DepartCutS = FMath::Clamp(IntersectS + DepartIndent, MinMargin, SplineLength - MinMargin);

		const bool bHasApproach = Arm.CutDistanceS > MinMargin + KINDA_SMALL_NUMBER;
		const bool bHasDepart = Arm.DepartCutS < SplineLength - MinMargin - KINDA_SMALL_NUMBER;
		Arm.bThroughArm = bHasApproach && bHasDepart && Arm.CutDistanceS < Arm.DepartCutS;

		// Stub (single mouth): only one CutS toward hub — drop the far-side cut so purge /
		// lane markers / trim don't spawn a second mouth at IntersectS (图二橙线漂移).
		if (!Arm.bThroughArm)
		{
			const bool bStubAtEnd = IntersectS >= SplineLength * 0.5;
			if (bStubAtEnd)
			{
				Arm.DepartCutS = -1.0;
			}
			else
			{
				Arm.CutDistanceS = -1.0;
			}
		}
	}

	/** All ports share the same hub indent — avoids one arm expanding alone (比例变形). */
	static void ApplyUniformHubArmCuts(FMetaLandRoadJunctionNode& Junction)
	{
		double HubIndent = 0.0;
		for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			HubIndent = FMath::Max(HubIndent, Arm.ForwardIndent);
		}
		if (HubIndent <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		Junction.ForwardIndent = HubIndent;
		for (FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
			if (!Spline)
			{
				continue;
			}
			ApplyArmCutFromForwardIndent(Arm, Spline, HubIndent);
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[JunctionCut] uniform hub indent=%.1f across %d arms (proportional 外扩)"),
			HubIndent,
			Junction.Arms.Num());
	}

	static bool ShouldRefineJunctionCutsAfterConnectSide(const FMetaLandRoadJunctionNode& Junction)
	{
		// N-way / subset / lane-branch: never shrink one port only.
		if (Junction.Arms.Num() != 2)
		{
			return false;
		}
		for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			if (Arm.ConnectedLaneIndices.Num() > 0)
			{
				return false;
			}
			if (UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get()))
			{
				if (IsLaneBranchSpline(Spline))
				{
					return false;
				}
			}
		}
		return true;
	}

	static void PopulateArmConnectedLaneIndices(FMetaLandRoadJunctionNode& Junction)
	{
		TSet<UMetaLandRoadSplineComponent*> ArmSplines;
		for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			if (UMetaLandRoadSplineComponent* ArmSpline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get()))
			{
				ArmSplines.Add(ArmSpline);
			}
		}

		const FVector Anchor = Junction.AnchorWorld;
		constexpr double BranchNearAnchorCm = 2500.0;

		auto IsJunctionRelatedOther = [&ArmSplines, &Anchor](UMetaLandRoadSplineComponent* Other) -> bool
		{
			if (!Other)
			{
				return false;
			}
			if (ArmSplines.Contains(Other))
			{
				return true;
			}
			// Connect-end branch arms are often purged from Arms; still count if tip is at this hub.
			const AMoKuEditSplineActor* OtherRoad = Cast<AMoKuEditSplineActor>(Other->GetOwner());
			if (!OtherRoad || !OtherRoad->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch"))))
			{
				return false;
			}
			const double Len = Other->GetSplineLength();
			const FVector TipA = Other->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::World);
			const FVector TipB = Other->GetLocationAtDistanceAlongSpline(Len, ESplineCoordinateSpace::World);
			return FVector::Dist2D(TipA, Anchor) <= BranchNearAnchorCm
				|| FVector::Dist2D(TipB, Anchor) <= BranchNearAnchorCm;
		};

		for (FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			Arm.ConnectedLaneIndices.Reset();
			UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
			if (!Spline || Spline->GetLaneSectionsNum() == 0)
			{
				continue;
			}

			Spline->GetRoadLayoutMutable().UpdateLayout(Spline);

			TArray<int32> Connected;
			TArray<int32> MouthLanes;
			const int32 HostSectionIndex = Spline->GetRoadLayout().FindSectionAtSOffset(Arm.IntersectDistanceS);
			const int32 ProbeSection = HostSectionIndex != INDEX_NONE ? HostSectionIndex : 0;

			auto LaneLinksToJunction = [&IsJunctionRelatedOther, Spline](UMetaLandRoadLaneConnection* LaneConn) -> bool
			{
				if (!IsValid(LaneConn) || !LaneConn->IsConnected())
				{
					return false;
				}
				for (const TWeakObjectPtr<UMetaLandRoadConnection>& WeakConn : LaneConn->OuterRoadConnections)
				{
					UMetaLandRoadConnection* RoadConn = WeakConn.Get();
					if (!IsValid(RoadConn))
					{
						continue;
					}
					UMetaLandRoadSplineComponent* Other = RoadConn->GetOwnedRoadSpline();
					if (Other && Other != Spline && IsJunctionRelatedOther(Other))
					{
						return true;
					}
				}
				return false;
			};

			const int32 LeftSectionIndex = Spline->GetRoadLayout().FindSideSection(
				ProbeSection, EMetaLandRoadLaneSectionSide::Left);
			const int32 RightSectionIndex = Spline->GetRoadLayout().FindSideSection(
				ProbeSection, EMetaLandRoadLaneSectionSide::Right);

			if (LeftSectionIndex != INDEX_NONE)
			{
				const FMetaLandRoadLaneSection& LeftSection = Spline->GetLaneSection(LeftSectionIndex);
				for (int32 LeftIndex = 0; LeftIndex < LeftSection.Left.Num(); ++LeftIndex)
				{
					const int32 LaneIndex = -(LeftIndex + 1);
					MouthLanes.AddUnique(LaneIndex);
					const FMetaLandRoadLane& Lane = LeftSection.Left[LeftIndex];
					if (LaneLinksToJunction(Lane.PredecessorConnection)
						|| LaneLinksToJunction(Lane.SuccessorConnection))
					{
						Connected.AddUnique(LaneIndex);
					}
				}
			}
			if (RightSectionIndex != INDEX_NONE)
			{
				const FMetaLandRoadLaneSection& RightSection = Spline->GetLaneSection(RightSectionIndex);
				for (int32 RightIndex = 0; RightIndex < RightSection.Right.Num(); ++RightIndex)
				{
					const int32 LaneIndex = RightIndex + 1;
					MouthLanes.AddUnique(LaneIndex);
					const FMetaLandRoadLane& Lane = RightSection.Right[RightIndex];
					if (LaneLinksToJunction(Lane.PredecessorConnection)
						|| LaneLinksToJunction(Lane.SuccessorConnection))
					{
						Connected.AddUnique(LaneIndex);
					}
				}
			}

			// If a near lane-branch has more lanes than we detected ConnectTo links,
			// expand the subset so custom 2-lane mouths are not crushed to 1.
			int32 LinkedBranchLaneCount = 0;
			if (UWorld* World = Spline->GetWorld())
			{
				for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
				{
					AMoKuEditSplineActor* OtherRoad = *It;
					if (!OtherRoad || !OtherRoad->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch"))))
					{
						continue;
					}
					UMetaLandRoadSplineComponent* OtherSpline = OtherRoad->GetSplineComponent();
					if (!OtherSpline || !IsJunctionRelatedOther(OtherSpline))
					{
						continue;
					}
					bool bConnectAtStart = true;
					if (GetConnectSourceSpline(OtherSpline, &bConnectAtStart) != Spline)
					{
						continue;
					}
					int32 BranchLanes = 0;
					for (const FMetaLandRoadLaneSection& Sec : OtherSpline->GetLaneSections())
					{
						BranchLanes = FMath::Max(BranchLanes, Sec.Left.Num() + Sec.Right.Num());
					}
					LinkedBranchLaneCount = FMath::Max(LinkedBranchLaneCount, BranchLanes);
				}
			}
			if (LinkedBranchLaneCount > Connected.Num() && Connected.Num() > 0)
			{
				const int32 Seed = Connected[0];
				const bool bSeedRight = Seed > 0;
				TArray<int32> FaceLanes;
				for (const int32 LaneIndex : MouthLanes)
				{
					if ((LaneIndex > 0) == bSeedRight)
					{
						FaceLanes.Add(LaneIndex);
					}
				}
				FaceLanes.Sort([](int32 A, int32 B) { return FMath::Abs(A) < FMath::Abs(B); });
				int32 SeedRank = FaceLanes.IndexOfByKey(Seed);
				if (SeedRank == INDEX_NONE && FaceLanes.Num() > 0)
				{
					SeedRank = 0;
				}
				const int32 Take = FMath::Min(LinkedBranchLaneCount, FaceLanes.Num());
				if (Take > 0 && SeedRank != INDEX_NONE)
				{
					const int32 Rank0 = FMath::Clamp(SeedRank - (Take / 2), 0, FaceLanes.Num() - Take);
					Connected.Reset();
					for (int32 r = Rank0; r < Rank0 + Take; ++r)
					{
						Connected.AddUnique(FaceLanes[r]);
					}
				}
			}

			Connected.Sort();
			MouthLanes.Sort();
			// Proper subset only — all/none connected keeps full-road mouth.
			if (Connected.Num() > 0 && Connected.Num() < MouthLanes.Num())
			{
				Arm.ConnectedLaneIndices = MoveTemp(Connected);
				FString LaneList;
				for (int32 i = 0; i < Arm.ConnectedLaneIndices.Num(); ++i)
				{
					if (i > 0)
					{
						LaneList += TEXT(",");
					}
					LaneList += FString::FromInt(Arm.ConnectedLaneIndices[i]);
				}
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[JunctionCut] arm subset lanes=[%s] (%d/%d) → mouth restricted"),
					*LaneList,
					Arm.ConnectedLaneIndices.Num(),
					MouthLanes.Num());
			}
		}
	}

	/** Cross / dual-through hubs: full CS, unless a lane-branch stub keeps a subset mouth. */
	static void ClearConnectedLaneIndicesForFullCSJunctions(FMetaLandRoadJunctionNode& Junction)
	{
		int32 ThroughCount = 0;
		bool bHasLaneBranchArm = false;
		for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			if (Arm.bThroughArm)
			{
				++ThroughCount;
			}
			if (UMetaLandRoadSplineComponent* ArmSpline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get()))
			{
				if (const AMoKuEditSplineActor* ArmRoad = Cast<AMoKuEditSplineActor>(ArmSpline->GetOwner()))
				{
					bHasLaneBranchArm |= ArmRoad->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch")));
				}
			}
		}
		if (bHasLaneBranchArm)
		{
			return;
		}
		if (Junction.Topology != EMetaLandRoadJunctionTopology::Cross && ThroughCount < 2)
		{
			return;
		}
		for (FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			if (Arm.ConnectedLaneIndices.Num() > 0)
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[JunctionCut] clear subset mouth on Cross/through hub (was %d lanes)"),
					Arm.ConnectedLaneIndices.Num());
			}
			Arm.ConnectedLaneIndices.Reset();
		}
	}

	enum class EJunctionSubsetMouthMode : uint8
	{
		Populate = 0,
		ClearAll = 1,
		KeepExisting = 2,
	};

	static void FinalizeJunctionArmCuts(
		FMetaLandRoadJunctionNode& Junction,
		EJunctionSubsetMouthMode SubsetMode = EJunctionSubsetMouthMode::Populate)
	{
		// Cut from intersect along centerline (equal-width CAD fillet):
		//   d ≈ lerp(Other, HalfA+HalfB, 0.8) / tan(θ/2) × CutExpandScale
		// θ = acute angle between arm tangents (folded into (0, 90°]).
		constexpr double MinMargin = 50.0;
		// Floor half-angle so shallow Y corners don't explode indent.
		constexpr double MinHalfAngleRad = PI / 15.0; // 12°
		constexpr double MaxHalfAngleRad = PI * 0.49;
		Junction.ForwardIndent = 0.0;

		if (SubsetMode == EJunctionSubsetMouthMode::Populate)
		{
			// Keep ConnectedLaneIndices — 2-lane extend must not become full-road mouth.
			PopulateArmConnectedLaneIndices(Junction);
		}
		else if (SubsetMode == EJunctionSubsetMouthMode::ClearAll)
		{
			for (FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
			{
				Arm.ConnectedLaneIndices.Reset();
			}
		}

		for (int32 ArmIndex = 0; ArmIndex < Junction.Arms.Num(); ++ArmIndex)
		{
			FMetaLandRoadJunctionArmRef& Arm = Junction.Arms[ArmIndex];
			UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
			if (!Spline)
			{
				continue;
			}

			const double SplineLength = Spline->GetSplineLength();
			if (SplineLength <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double IntersectS = Arm.IntersectDistanceS;
			const double HalfWidth = GetArmIndentHalfW(Arm, Spline);
			double ForwardIndent = 0.0;
			bool bHasPair = false;

			const FVector ArmTangent = Spline->GetTangentAtDistanceAlongSpline(
				IntersectS,
				ESplineCoordinateSpace::World).GetSafeNormal();

			for (int32 OtherIndex = 0; OtherIndex < Junction.Arms.Num(); ++OtherIndex)
			{
				if (OtherIndex == ArmIndex)
				{
					continue;
				}

				UMetaLandRoadSplineComponent* OtherSpline = Cast<UMetaLandRoadSplineComponent>(
					Junction.Arms[OtherIndex].Spline.Get());
				if (!OtherSpline)
				{
					continue;
				}

				const FVector OtherTangent = OtherSpline->GetTangentAtDistanceAlongSpline(
					Junction.Arms[OtherIndex].IntersectDistanceS,
					ESplineCoordinateSpace::World).GetSafeNormal();
				double AngleRad = FMath::Acos(FMath::Clamp(
					FMath::Abs(FVector::DotProduct(ArmTangent, OtherTangent)),
					0.0,
					1.0));
				// Nearly collinear / opposite through-arm — not a corner pair.
				if (AngleRad < FMath::DegreesToRadians(5.0))
				{
					continue;
				}
				if (AngleRad > PI * 0.5)
				{
					AngleRad = PI - AngleRad;
				}

				const FMetaLandRoadJunctionArmRef& OtherArm = Junction.Arms[OtherIndex];
				const double OtherHalfWidth = GetArmIndentHalfW(OtherArm, OtherSpline);
				const double HalfAngle = FMath::Clamp(AngleRad * 0.5, MinHalfAngleRad, MaxHalfAngleRad);
				// Prefer near Aug2 (A+B) so mouths clear adjacent asphalt — short indent → 穿插.
				const double PairIndent =
					FMath::Lerp(OtherHalfWidth, HalfWidth + OtherHalfWidth, 0.8) / FMath::Tan(HalfAngle);
				ForwardIndent = FMath::Max(ForwardIndent, PairIndent);
				bHasPair = true;
			}

			if (!bHasPair)
			{
				ForwardIndent = HalfWidth;
			}

			const double ExpandScale = FMath::Clamp(
				static_cast<double>(Junction.CutExpandScale > KINDA_SMALL_NUMBER
					? Junction.CutExpandScale
					: 1.15f),
				0.9,
				1.5);
			ForwardIndent *= ExpandScale;

			const double IndentHalfW = HalfWidth;
			const bool bMultiHubConnector = IsMultiHubConnectorRoad(Spline, IndentHalfW);

			const double MaxIndent = bMultiHubConnector
				? FMath::Min(IndentHalfW * 1.35, SplineLength * 0.20)
				: FMath::Min(IndentHalfW * 3.5, SplineLength * 0.35);
			const double MinIndent = bMultiHubConnector
				? IndentHalfW * 0.35
				: IndentHalfW * 0.65;
			ForwardIndent = FMath::Clamp(ForwardIndent, MinIndent, MaxIndent);
			ForwardIndent += bMultiHubConnector
				? FMath::Min(IndentHalfW * 0.08, 40.0)
				: FMath::Min(IndentHalfW * 0.18, 100.0);

			ApplyArmCutFromForwardIndent(Arm, Spline, ForwardIndent);
			Junction.ForwardIndent = FMath::Max(Junction.ForwardIndent, ForwardIndent);

			UE_LOG(
				LogTemp,
				Log,
				TEXT("[JunctionCut] arm=%d HalfW=%.1f indentW=%.1f Indent=%.1f scale=%.2f multiHub=%d CutS=%.1f IntersectS=%.1f DepartS=%.1f through=%d"),
				ArmIndex,
				Spline->GetRoadHalfWidthAtS(IntersectS),
				IndentHalfW,
				Arm.ForwardIndent,
				ExpandScale,
				bMultiHubConnector ? 1 : 0,
				Arm.CutDistanceS,
				IntersectS,
				Arm.DepartCutS,
				Arm.bThroughArm ? 1 : 0);
		}

		// Every port pulls back by the same hub indent (3-way / N-way proportional 外扩).
		ApplyUniformHubArmCuts(Junction);

		// Cross / dual-through: drop subset only when NO lane-branch stub is involved.
		// Multi-junction 2-lane extends must keep ConnectedLaneIndices (was → 全宽错位).
		if (SubsetMode == EJunctionSubsetMouthMode::Populate)
		{
			int32 ThroughCount = 0;
			bool bHadSubset = false;
			bool bHasLaneBranchArm = false;
			for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
			{
				ThroughCount += Arm.bThroughArm ? 1 : 0;
				bHadSubset |= Arm.ConnectedLaneIndices.Num() > 0;
				if (UMetaLandRoadSplineComponent* ArmSpline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get()))
				{
					if (const AMoKuEditSplineActor* ArmRoad = Cast<AMoKuEditSplineActor>(ArmSpline->GetOwner()))
					{
						bHasLaneBranchArm |= ArmRoad->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch")));
					}
				}
			}
			if (bHadSubset && ThroughCount >= 2 && !bHasLaneBranchArm)
			{
				UE_LOG(LogTemp, Log, TEXT("[JunctionCut] Cross/through hub → re-cut full CS (drop subset)"));
				FinalizeJunctionArmCuts(Junction, EJunctionSubsetMouthMode::ClearAll);
				return;
			}
		}

		if (Junction.ForwardIndent <= KINDA_SMALL_NUMBER)
		{
			Junction.ForwardIndent = 200.0;
		}
	}

	static void UpdateJunctionTopology(FMetaLandRoadJunctionNode& Junction);

	/** True T = exactly one through + one stub. Two through arms = Cross (full CS, no half-cut). */
	static void ResolveJunctionConnectSides(FMetaLandRoadJunctionNode& Junction)
	{
		for (FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			Arm.ConnectSide = EMetaLandRoadJunctionConnectSide::Both;
		}

		if (Junction.Arms.Num() != 2)
		{
			return;
		}

		int32 ThroughIndex = INDEX_NONE;
		int32 StubIndex = INDEX_NONE;
		int32 ThroughCount = 0;
		for (int32 ArmIndex = 0; ArmIndex < Junction.Arms.Num(); ++ArmIndex)
		{
			if (Junction.Arms[ArmIndex].bThroughArm)
			{
				++ThroughCount;
				ThroughIndex = ArmIndex;
			}
			else
			{
				StubIndex = ArmIndex;
			}
		}

		// Cross of two roads: both arms through — keep Both on every arm (do NOT half-cut).
		if (ThroughCount != 1 || ThroughIndex == INDEX_NONE || StubIndex == INDEX_NONE)
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[JunctionConnectSide] arms=2 throughCount=%d → Cross/full CS (ConnectSide=Both)"),
				ThroughCount);
			return;
		}

		FMetaLandRoadJunctionArmRef& ThroughArm = Junction.Arms[ThroughIndex];
		const FMetaLandRoadJunctionArmRef& StubArm = Junction.Arms[StubIndex];
		UMetaLandRoadSplineComponent* ThroughSpline = Cast<UMetaLandRoadSplineComponent>(ThroughArm.Spline.Get());
		UMetaLandRoadSplineComponent* StubSpline = Cast<UMetaLandRoadSplineComponent>(StubArm.Spline.Get());
		if (!ThroughSpline || !StubSpline)
		{
			return;
		}

		const FVector ThroughCenter = ThroughSpline->GetLocationAtDistanceAlongSpline(
			ThroughArm.IntersectDistanceS,
			ESplineCoordinateSpace::World);
		const FVector ThroughRight = ThroughSpline->GetRightVectorAtDistanceAlongSpline(
			ThroughArm.IntersectDistanceS,
			ESplineCoordinateSpace::World).GetSafeNormal2D();
		// Sample stub OUTBOARD of the junction — IntersectS≈ThroughCenter makes SideDot noise/flip.
		const double StubLen = StubSpline->GetSplineLength();
		const double StubAwayS = (StubArm.IntersectDistanceS < StubLen * 0.5)
			? FMath::Clamp(
				FMath::Max(StubArm.DepartCutS, StubArm.IntersectDistanceS) + 300.0,
				0.0,
				StubLen)
			: FMath::Clamp(
				FMath::Min(StubArm.CutDistanceS, StubArm.IntersectDistanceS) - 300.0,
				0.0,
				StubLen);
		const FVector StubOutboard = StubSpline->GetLocationAtDistanceAlongSpline(
			StubAwayS,
			ESplineCoordinateSpace::World);
		const double SideDot = FVector::DotProduct(StubOutboard - ThroughCenter, ThroughRight);
		// Native CS Left→Center→Right along +RightVector. Stub on +Right → Right half.
		ThroughArm.ConnectSide = (SideDot >= 0.0)
			? EMetaLandRoadJunctionConnectSide::Right
			: EMetaLandRoadJunctionConnectSide::Left;
		Junction.Arms[StubIndex].ConnectSide = EMetaLandRoadJunctionConnectSide::Both;

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[JunctionConnectSide] T throughArm=%d side=%s (Dot=%.2f awayS=%.1f) stubArm=%d"),
			ThroughIndex,
			ThroughArm.ConnectSide == EMetaLandRoadJunctionConnectSide::Right ? TEXT("Right") : TEXT("Left"),
			SideDot,
			StubAwayS,
			StubIndex);
	}

	/**
	 * T through-arm: open only stub-width lanes (custom mouth), not the whole road.
	 * Fills ConnectedLaneIndices from stub profile when ConnectTo under-reported links.
	 */
	static bool EnsureTThroughCustomMouthFromStub(FMetaLandRoadJunctionNode& Junction)
	{
		if (Junction.Arms.Num() != 2)
		{
			return false;
		}

		int32 ThroughIndex = INDEX_NONE;
		int32 StubIndex = INDEX_NONE;
		int32 ThroughCount = 0;
		for (int32 ArmIndex = 0; ArmIndex < Junction.Arms.Num(); ++ArmIndex)
		{
			if (Junction.Arms[ArmIndex].bThroughArm)
			{
				++ThroughCount;
				ThroughIndex = ArmIndex;
			}
			else
			{
				StubIndex = ArmIndex;
			}
		}
		if (ThroughCount != 1 || ThroughIndex == INDEX_NONE || StubIndex == INDEX_NONE)
		{
			return false;
		}

		FMetaLandRoadJunctionArmRef& ThroughArm = Junction.Arms[ThroughIndex];
		FMetaLandRoadJunctionArmRef& StubArm = Junction.Arms[StubIndex];
		UMetaLandRoadSplineComponent* ThroughSpline =
			Cast<UMetaLandRoadSplineComponent>(ThroughArm.Spline.Get());
		UMetaLandRoadSplineComponent* StubSpline =
			Cast<UMetaLandRoadSplineComponent>(StubArm.Spline.Get());
		if (!ThroughSpline || !StubSpline
			|| ThroughArm.ConnectSide == EMetaLandRoadJunctionConnectSide::Both)
		{
			return false;
		}

		int32 StubLaneCount = 0;
		for (const FMetaLandRoadLaneSection& Sec : StubSpline->GetLaneSections())
		{
			StubLaneCount = FMath::Max(StubLaneCount, Sec.Left.Num() + Sec.Right.Num());
		}
		if (StubLaneCount <= 0)
		{
			return false;
		}

		const int32 HostSection = ThroughSpline->GetRoadLayout().FindSectionAtSOffset(
			ThroughArm.IntersectDistanceS);
		const int32 ProbeSection = HostSection != INDEX_NONE ? HostSection : 0;
		const bool bWantRight = ThroughArm.ConnectSide == EMetaLandRoadJunctionConnectSide::Right;
		const int32 FaceSection = ThroughSpline->GetRoadLayout().FindSideSection(
			ProbeSection,
			bWantRight ? EMetaLandRoadLaneSectionSide::Right : EMetaLandRoadLaneSectionSide::Left);
		if (FaceSection == INDEX_NONE)
		{
			return false;
		}

		TArray<int32> FaceLanes;
		const FMetaLandRoadLaneSection& Face = ThroughSpline->GetLaneSection(FaceSection);
		if (bWantRight)
		{
			for (int32 i = 0; i < Face.Right.Num(); ++i)
			{
				FaceLanes.Add(i + 1);
			}
		}
		else
		{
			for (int32 i = 0; i < Face.Left.Num(); ++i)
			{
				FaceLanes.Add(-(i + 1));
			}
		}
		if (FaceLanes.Num() == 0)
		{
			return false;
		}

		const int32 Take = FMath::Min(StubLaneCount, FaceLanes.Num());
		// Prefer lanes nearest the curb (outer) so the stub sits on the road edge.
		TArray<int32> Chosen;
		if (bWantRight)
		{
			for (int32 i = FaceLanes.Num() - Take; i < FaceLanes.Num(); ++i)
			{
				Chosen.Add(FaceLanes[i]);
			}
		}
		else
		{
			// Left face: most-negative = outer curb first in FaceLanes ascending.
			for (int32 i = 0; i < Take; ++i)
			{
				Chosen.Add(FaceLanes[i]);
			}
		}
		Chosen.Sort();

		const bool bChanged = ThroughArm.ConnectedLaneIndices != Chosen;
		ThroughArm.ConnectedLaneIndices = MoveTemp(Chosen);
		// Stub mouth is already its full (narrow) profile.
		StubArm.ConnectedLaneIndices.Reset();

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[JunctionCut] T custom mouth through=%d stubLanes=%d → subset=%d side=%s"),
			ThroughIndex,
			StubLaneCount,
			ThroughArm.ConnectedLaneIndices.Num(),
			bWantRight ? TEXT("Right") : TEXT("Left"));
		return bChanged;
	}

	/** After ConnectSide: shrink T half-arm indent to curb-face half-width (not full L+R). */
	static void RefineJunctionCutsAfterConnectSide(FMetaLandRoadJunctionNode& Junction)
	{
		constexpr double MinMargin = 50.0;
		for (FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			if (Arm.ConnectSide == EMetaLandRoadJunctionConnectSide::Both)
			{
				continue;
			}
			UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
			if (!Spline)
			{
				continue;
			}
			const double IntersectS = Arm.IntersectDistanceS;
			const double FullHalf = FMath::Max(
				Spline->GetRoadHalfWidthAtS(IntersectS),
				1.0);
			TArray<FVector> MouthCS;
			if (!Spline->GetRoadCrossSectionPointsForLaneIndicesAtS(
				IntersectS, Arm.ConnectedLaneIndices, MouthCS, ESplineCoordinateSpace::World)
				|| MouthCS.Num() < 2)
			{
				continue;
			}
			FVector EdgeL, EdgeR, EdgeC;
			if (!Spline->GetRoadEdgePointsAtS(
				IntersectS, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
			{
				continue;
			}
			// Subset mouths: face half from mouth CS ends (not full-road curb).
			const FVector MouthLeft = MouthCS[0];
			const FVector MouthRight = MouthCS.Last();
			const FVector Curb =
				Arm.ConnectSide == EMetaLandRoadJunctionConnectSide::Left ? MouthLeft : MouthRight;
			const FVector FaceInner =
				Arm.ConnectedLaneIndices.Num() > 0
					? ((MouthLeft + MouthRight) * 0.5)
					: EdgeC;
			const double FaceHalf = FMath::Max(FVector::Dist2D(Curb, FaceInner), 1.0);
			if (FaceHalf >= FullHalf * 0.9 || Arm.ForwardIndent <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const double Scale = FaceHalf / FullHalf;
			const double NewIndent = FMath::Max(Arm.ForwardIndent * Scale, FaceHalf * 0.25);
			const double SplineLength = Spline->GetSplineLength();
			const double DistToStart = IntersectS;
			const double DistToEnd = SplineLength - IntersectS;
			const double ApproachIndent = FMath::Min(NewIndent, FMath::Max(DistToStart - MinMargin, FaceHalf * 0.2));
			const double DepartIndent = FMath::Min(NewIndent, FMath::Max(DistToEnd - MinMargin, FaceHalf * 0.2));
			Arm.ForwardIndent = FMath::Max(ApproachIndent, DepartIndent);
			Arm.CutDistanceS = FMath::Clamp(IntersectS - ApproachIndent, MinMargin, SplineLength - MinMargin);
			Arm.DepartCutS = FMath::Clamp(IntersectS + DepartIndent, MinMargin, SplineLength - MinMargin);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[JunctionCut] refine half-CS arm Indent→%.1f (FaceHalf=%.1f FullHalf=%.1f)"),
				Arm.ForwardIndent,
				FaceHalf,
				FullHalf);
		}
	}

	static void FinalizeTopologyConnectAndCustomMouth(FMetaLandRoadJunctionNode& Junction)
	{
		UpdateJunctionTopology(Junction);
		ResolveJunctionConnectSides(Junction);
		// T: open only stub-width lanes on through arm (2-lane extend ≠ all lanes).
		if (EnsureTThroughCustomMouthFromStub(Junction))
		{
			if (ShouldRefineJunctionCutsAfterConnectSide(Junction))
			{
				RefineJunctionCutsAfterConnectSide(Junction);
			}
			else
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[JunctionCut] skip asymmetric refine — N-way/subset/lane-branch proportional 外扩"));
			}
		}
		// Re-sync cuts after mouth/subset changes; keeps all ports at the same indent.
		ApplyUniformHubArmCuts(Junction);
	}

	static UMetaLandRoadSplineComponent* GetConnectSourceSpline(
		UMetaLandRoadSplineComponent* RoadSpline,
		bool* bOutConnectAtStart)
	{
		if (!RoadSpline)
		{
			return nullptr;
		}

		if (UMetaLandRoadConnection* Pred = RoadSpline->GetPredecessorConnection())
		{
			if (Pred->IsConnected())
			{
				if (UMetaLandRoadLaneConnection* LaneConn = Pred->OuterLaneConnection.Get())
				{
					if (bOutConnectAtStart)
					{
						*bOutConnectAtStart = true;
					}
					return LaneConn->OwnedRoadSpline.Get();
				}
			}
		}

		if (UMetaLandRoadConnection* Succ = RoadSpline->GetSuccessorConnection())
		{
			if (Succ->IsConnected())
			{
				if (UMetaLandRoadLaneConnection* LaneConn = Succ->OuterLaneConnection.Get())
				{
					if (bOutConnectAtStart)
					{
						*bOutConnectAtStart = false;
					}
					return LaneConn->OwnedRoadSpline.Get();
				}
			}
		}

		return nullptr;
	}

	static double GetConnectExcludeMarginS(UMetaLandRoadSplineComponent* RoadSpline, double IntersectS)
	{
		const double HalfW = FMath::Max(RoadSpline->GetRoadHalfWidthAtS(IntersectS), 1.0);
		return FMath::Max(HalfW * 2.5, 400.0);
	}

	/** ConnectTo / FromConnection mouth is NOT a junction hub — exclude from detect + merge. */
	static bool IsConnectEndIntersection(
		UMetaLandRoadSplineComponent* RoadSpline,
		UMetaLandRoadSplineComponent* OtherSpline,
		const FVector& IntersectWorld)
	{
		if (!RoadSpline)
		{
			return false;
		}

		const double IntersectS = RoadSpline->GetDistanceAlongSplineAtLocation(
			IntersectWorld,
			ESplineCoordinateSpace::World);
		const double SplineLength = RoadSpline->GetSplineLength();
		const double ExcludeMargin = GetConnectExcludeMarginS(RoadSpline, IntersectS);

		bool bConnectAtStart = true;
		UMetaLandRoadSplineComponent* ConnectSource = GetConnectSourceSpline(RoadSpline, &bConnectAtStart);
		if (ConnectSource && OtherSpline && ConnectSource == OtherSpline)
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[JunctionDetect] skip ConnectTo pair road=%s source=%s atS=%.1f"),
				*RoadSpline->GetName(),
				*OtherSpline->GetName(),
				IntersectS);
			return true;
		}

		const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(RoadSpline->GetOwner());
		const bool bLaneBranch = RoadActor && RoadActor->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch")));
		if (bLaneBranch)
		{
			if (!ConnectSource)
			{
				bConnectAtStart = true;
			}
			const bool bNearConnectEnd = bConnectAtStart
				? (IntersectS <= ExcludeMargin)
				: (IntersectS >= SplineLength - ExcludeMargin);
			if (bNearConnectEnd)
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[JunctionDetect] skip lane-branch connect end road=%s atS=%.1f margin=%.1f"),
					*RoadSpline->GetName(),
					IntersectS,
					ExcludeMargin);
				return true;
			}
		}

		return false;
	}

	static bool ShouldExcludeConnectIntersection(
		UMetaLandRoadSplineComponent* SplineA,
		UMetaLandRoadSplineComponent* SplineB,
		const FVector& IntersectWorld)
	{
		return IsConnectEndIntersection(SplineA, SplineB, IntersectWorld)
			|| IsConnectEndIntersection(SplineB, SplineA, IntersectWorld);
	}

	static bool IsConnectEndArmRef(const FMetaLandRoadJunctionArmRef& Arm)
	{
		UMetaLandRoadSplineComponent* ArmSpline = Cast<UMetaLandRoadSplineComponent>(Arm.Spline.Get());
		if (!ArmSpline)
		{
			return false;
		}

		const double IntersectS = Arm.IntersectDistanceS;
		const double SplineLength = ArmSpline->GetSplineLength();
		if (SplineLength <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		bool bConnectAtStart = true;
		const UMetaLandRoadSplineComponent* ConnectSource = GetConnectSourceSpline(ArmSpline, &bConnectAtStart);
		const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ArmSpline->GetOwner());
		const bool bLaneBranch = RoadActor && RoadActor->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch")));
		if (!ConnectSource && !bLaneBranch)
		{
			return false;
		}

		if (!ConnectSource && bLaneBranch)
		{
			bConnectAtStart = true;
		}

		// HalfW*2.5 can exceed short spur length → Length-Margin < 0 → every S matches
		// → Recook purged all arms → ClearJunctionCoverMesh (junctions vanished).
		const double RawMargin = GetConnectExcludeMarginS(ArmSpline, IntersectS);
		const double ExcludeMargin = FMath::Clamp(RawMargin, 50.0, SplineLength * 0.35);
		if (ExcludeMargin < 50.0)
		{
			return false;
		}

		return bConnectAtStart
			? (IntersectS <= ExcludeMargin)
			: (IntersectS >= SplineLength - ExcludeMargin);
	}

	/** Remove junction trim accidentally applied at ConnectTo mouth (stale cooks). */
	static void ClearSpuriousConnectEndTrim(AMoKuEditSplineActor* RoadActor)
	{
		if (!RoadActor)
		{
			return;
		}

		UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(RoadActor->GetSplineComponent());
		if (!RoadSpline)
		{
			return;
		}

		bool bConnectAtStart = true;
		const UMetaLandRoadSplineComponent* ConnectSource = GetConnectSourceSpline(RoadSpline, &bConnectAtStart);
		const bool bLaneBranch = RoadActor->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch")));
		if (!ConnectSource && !bLaneBranch)
		{
			return;
		}

		if (!ConnectSource && bLaneBranch)
		{
			bConnectAtStart = true;
		}

		const double SplineLength = RoadSpline->GetSplineLength();
		const double Margin = GetConnectExcludeMarginS(
			RoadSpline,
			bConnectAtStart ? 0.0 : SplineLength);

		auto IsNearConnectS = [&](double CutS) -> bool
		{
			if (CutS < 0.0)
			{
				return false;
			}
			return bConnectAtStart
				? (CutS <= Margin)
				: (CutS >= SplineLength - Margin);
		};

		const int32 Removed = RoadActor->JunctionTrimZones.RemoveAll(
			[&](const FMetaLandRoadJunctionTrimZone& Zone)
			{
				return IsNearConnectS(Zone.ApproachCutS) || IsNearConnectS(Zone.DepartCutS);
			});
		if (Removed > 0)
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[JunctionDetect] cleared %d connect-end trim zone(s) on road=%s"),
				Removed,
				*RoadActor->GetName());
		}

		if (bConnectAtStart)
		{
			if (IsNearConnectS(RoadActor->StartJunctionMeshTrimS))
			{
				RoadActor->StartOfJunction = nullptr;
				RoadActor->StartJunctionMeshTrimS = -1.0;
			}
		}
		else if (IsNearConnectS(RoadActor->EndJunctionMeshTrimS))
		{
			RoadActor->EndOfJunction = nullptr;
			RoadActor->EndJunctionMeshTrimS = -1.0;
		}
	}

	static void DetectCenterlineIntersections(
		UWorld* World,
		UMetaLandRoadSplineComponent* DrawnSpline,
		TArray<FDetectedArmHit>& OutHits,
		float MaxHeightDeltaCm = 0.0f)
	{
		OutHits.Reset();
		if (!World || !DrawnSpline || DrawnSpline->GetNumberOfSplinePoints() < 2)
		{
			return;
		}

		for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
		{
			AMoKuEditSplineActor* OtherActor = *It;
			UMoKuEditSplinesComponent* OtherSpline = OtherActor ? OtherActor->GetSplineComponent() : nullptr;
			if (!OtherSpline || OtherSpline == DrawnSpline || OtherSpline->GetNumberOfSplinePoints() < 2)
			{
				continue;
			}

			FVector IntersectWorld = FVector::ZeroVector;
			if (!MoKuSplineEditorUtils::DoSplineIntersect(
					DrawnSpline, OtherSpline, IntersectWorld, MaxHeightDeltaCm))
			{
				continue;
			}

			if (UMetaLandRoadSplineComponent* OtherRoadSpline = Cast<UMetaLandRoadSplineComponent>(OtherSpline))
			{
				if (ShouldExcludeConnectIntersection(DrawnSpline, OtherRoadSpline, IntersectWorld))
				{
					continue;
				}
			}

			FDetectedArmHit Hit;
			Hit.Spline = OtherSpline;
			Hit.IntersectWorld = IntersectWorld;
			Hit.IntersectDistanceS = OtherSpline->GetDistanceAlongSplineAtLocation(
				IntersectWorld,
				ESplineCoordinateSpace::World);
			OutHits.Add(Hit);
		}
	}

	static FGuid FindOrCreateJunctionId(
		TArray<FMetaLandRoadJunctionNode>& Junctions,
		const FVector& WorldPos,
		double ClusterRadius)
	{
		const double ClusterRadiusSq = ClusterRadius * ClusterRadius;
		for (FMetaLandRoadJunctionNode& Junction : Junctions)
		{
			if (FVector::DistSquared(Junction.AnchorWorld, WorldPos) <= ClusterRadiusSq)
			{
				return Junction.Id;
			}
		}

		FMetaLandRoadJunctionNode NewJunction;
		NewJunction.Id = FGuid::NewGuid();
		NewJunction.AnchorWorld = WorldPos;
		Junctions.Add(NewJunction);
		return NewJunction.Id;
	}

	static FMetaLandRoadJunctionNode* FindJunctionMutable(
		TArray<FMetaLandRoadJunctionNode>& Junctions,
		const FGuid& JunctionId)
	{
		for (FMetaLandRoadJunctionNode& Junction : Junctions)
		{
			if (Junction.Id == JunctionId)
			{
				return &Junction;
			}
		}
		return nullptr;
	}

	static void AttachArmToJunction(
		FMetaLandRoadJunctionNode& Junction,
		const FDetectedArmHit& Hit)
	{
		UMetaLandRoadSplineComponent* Spline = Hit.Spline.Get();
		if (!Spline)
		{
			return;
		}

		for (const FMetaLandRoadJunctionArmRef& Existing : Junction.Arms)
		{
			if (Existing.Spline.Get() == Spline)
			{
				return;
			}
		}

		FMetaLandRoadJunctionArmRef Arm;
		Arm.Spline = Spline;
		Arm.IntersectDistanceS = Hit.IntersectDistanceS;
		Arm.CutDistanceS = Hit.CutDistanceS;
		Junction.Arms.Add(Arm);
	}

	static void RebuildAllJunctionsFromWorld(
		UWorld* World,
		TArray<FMetaLandRoadJunctionNode>& Junctions,
		double ForwardIndent,
		double ClusterRadius,
		float MaxHeightDeltaCm = 0.0f)
	{
		Junctions.Reset();
		if (!World)
		{
			return;
		}

		TArray<UMoKuEditSplinesComponent*> RoadSplines;
		for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
		{
			AMoKuEditSplineActor* RoadActor = *It;
			UMoKuEditSplinesComponent* RoadSpline = RoadActor ? RoadActor->GetSplineComponent() : nullptr;
			if (!RoadSpline || RoadSpline->GetNumberOfSplinePoints() < 2)
			{
				continue;
			}

			MetaLandRoadProfile::EnsureDefaultRoadProfile(RoadSpline);
			RoadSplines.Add(RoadSpline);
		}

		for (int32 FirstIndex = 0; FirstIndex < RoadSplines.Num(); ++FirstIndex)
		{
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < RoadSplines.Num(); ++SecondIndex)
			{
				UMoKuEditSplinesComponent* SplineA = RoadSplines[FirstIndex];
				UMoKuEditSplinesComponent* SplineB = RoadSplines[SecondIndex];
				if (!SplineA || !SplineB)
				{
					continue;
				}

				FVector IntersectWorld = FVector::ZeroVector;
				if (!MoKuSplineEditorUtils::DoSplineIntersect(
						SplineA, SplineB, IntersectWorld, MaxHeightDeltaCm))
				{
					continue;
				}

				UMetaLandRoadSplineComponent* RoadA = Cast<UMetaLandRoadSplineComponent>(SplineA);
				UMetaLandRoadSplineComponent* RoadB = Cast<UMetaLandRoadSplineComponent>(SplineB);
				if (RoadA && RoadB && ShouldExcludeConnectIntersection(RoadA, RoadB, IntersectWorld))
				{
					continue;
				}

				for (UMoKuEditSplinesComponent* RoadSpline : {SplineA, SplineB})
				{
					FDetectedArmHit Hit;
					Hit.Spline = RoadSpline;
					Hit.IntersectWorld = IntersectWorld;
					Hit.IntersectDistanceS = RoadSpline->GetDistanceAlongSplineAtLocation(
						IntersectWorld,
						ESplineCoordinateSpace::World);
					Hit.CutDistanceS = 0.0;

					const FGuid JunctionId = FindOrCreateJunctionId(Junctions, Hit.IntersectWorld, ClusterRadius);
					FMetaLandRoadJunctionNode* Junction = FindJunctionMutable(Junctions, JunctionId);
					if (!Junction)
					{
						continue;
					}

					Junction->ForwardIndent = ForwardIndent;
					Junction->AnchorWorld = Hit.IntersectWorld;
					AttachArmToJunction(*Junction, Hit);
				}
			}
		}

		for (FMetaLandRoadJunctionNode& Junction : Junctions)
		{
			FinalizeJunctionArmCuts(Junction);
			FinalizeTopologyConnectAndCustomMouth(Junction);
		}
	}

	static void UpdateJunctionTopology(FMetaLandRoadJunctionNode& Junction)
	{
		switch (Junction.Arms.Num())
		{
		case 2:
		{
			// Two crossing through-roads = Cross. One through + stub = T.
			int32 ThroughCount = 0;
			for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
			{
				if (Arm.bThroughArm)
				{
					++ThroughCount;
				}
			}
			Junction.Topology = (ThroughCount >= 2)
				? EMetaLandRoadJunctionTopology::Cross
				: EMetaLandRoadJunctionTopology::T;
			break;
		}
		case 4:
			Junction.Topology = EMetaLandRoadJunctionTopology::Cross;
			break;
		case 0:
		case 1:
			Junction.Topology = EMetaLandRoadJunctionTopology::Unknown;
			break;
		default:
			Junction.Topology = EMetaLandRoadJunctionTopology::MultiWay;
			break;
		}
		ClearConnectedLaneIndicesForFullCSJunctions(Junction);
	}

	/**
	 * Clear the whole cut corridor (mouth ↔ intersect + outward pad).
	 * Narrow ±80cm left previous CutS markers when expand grows → 图二画蛇添足橙线.
	 */
	static void PurgeLegacyJunctionCutSections(
		UMetaLandRoadSplineComponent* Spline,
		double CutS,
		double OtherCutS,
		double IntersectS)
	{
		if (!Spline || Spline->GetLaneSectionsNum() < 2)
		{
			return;
		}

		const double SplineLength = Spline->GetSplineLength();
		const double HalfW = FMath::Max(Spline->GetRoadHalfWidthAtS(
			IntersectS > KINDA_SMALL_NUMBER ? IntersectS : CutS), 1.0);
		// Outward pad catches old CutS when indent increases (mouth moves away from hub).
		const double OutwardPad = FMath::Max(HalfW * 2.0, 500.0);
		const double HubPad = FMath::Max(HalfW * 0.35, 80.0);

		TArray<TPair<double, double>> PurgeBands;
		auto AddCorridor = [&PurgeBands, SplineLength, OutwardPad, HubPad](double MouthS, double HubS)
		{
			if (MouthS <= KINDA_SMALL_NUMBER || HubS <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			double Lo = FMath::Min(MouthS, HubS) - HubPad;
			double Hi = FMath::Max(MouthS, HubS) + HubPad;
			// Extend past mouth away from hub so drifted old cuts are removed.
			if (MouthS <= HubS)
			{
				Lo = MouthS - OutwardPad;
			}
			else
			{
				Hi = MouthS + OutwardPad;
			}
			Lo = FMath::Clamp(Lo, 0.0, SplineLength);
			Hi = FMath::Clamp(Hi, 0.0, SplineLength);
			if (Hi > Lo + KINDA_SMALL_NUMBER)
			{
				PurgeBands.Emplace(Lo, Hi);
			}
		};

		const double HubS = IntersectS > KINDA_SMALL_NUMBER ? IntersectS : CutS;
		AddCorridor(CutS, HubS);
		if (OtherCutS > KINDA_SMALL_NUMBER)
		{
			AddCorridor(OtherCutS, HubS);
			// Through vacancy between mouths.
			const double GapLo = FMath::Min(CutS, OtherCutS);
			const double GapHi = FMath::Max(CutS, OtherCutS);
			if (GapHi > GapLo)
			{
				PurgeBands.Emplace(GapLo, GapHi);
			}
		}

		if (PurgeBands.Num() == 0)
		{
			return;
		}

		FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
		bool bRemoved = false;
		for (int32 SectionIndex = Layout.Sections.Num() - 1; SectionIndex >= 1; --SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = Layout.Sections[SectionIndex];
			bool bInBand = false;
			for (const TPair<double, double>& Band : PurgeBands)
			{
				if (Section.SOffset >= Band.Key && Section.SOffset <= Band.Value)
				{
					bInBand = true;
					break;
				}
			}
			if (!bInBand)
			{
				continue;
			}
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[JunctionCook] purge section side=%d at S=%.1f (cuts=%.1f/%.1f hub=%.1f) on %s"),
				static_cast<int32>(Section.Side),
				Section.SOffset,
				CutS,
				OtherCutS,
				HubS,
				*GetNameSafe(Spline->GetOwner()));
			Layout.Sections.RemoveAt(SectionIndex);
			bRemoved = true;
		}
		if (bRemoved)
		{
			Spline->UpdateLaneSectionBounds();
			Spline->UpdateRoadLayout();
		}
	}

	/** Copy lane widths onto the empty face so mouth CS always has Left+Right. */
	static FMetaLandRoadLane CloneLaneConstantWidth(const FMetaLandRoadLane& Source)
	{
		FMetaLandRoadLane NewLane = Source;
		float WidthCm = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);
		if (Source.Width.GetNumKeys() > 0)
		{
			WidthCm = Source.Width.Keys[0].Value;
		}
		else
		{
			WidthCm = static_cast<float>(Source.Width.GetDefaultValue());
		}
		if (WidthCm <= KINDA_SMALL_NUMBER)
		{
			WidthCm = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth);
		}
		NewLane.Width.Reset();
		NewLane.Width.SetDefaultValue(WidthCm);
		return NewLane;
	}

	/**
	 * Standardize Left/Right faces at the approach tip: extend missing-side lanes
	 * (mirror opposite face) so section Side owns both arrays — fixes 左右 section 未分.
	 */
	static void EnsureStandardLeftRightLanes(UMetaLandRoadSplineComponent* Spline, double AtS)
	{
		if (!Spline || Spline->GetLaneSectionsNum() == 0)
		{
			return;
		}

		// Lane-branch / N-lane extend is intentionally one-sided + ROffset-centered.
		// Mirroring the empty face here turns "2 lanes" into full L+R and shifts the tip.
		if (const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			if (RoadActor->Tags.Contains(FName(TEXT("MetaLandRoadLaneBranch"))))
			{
				return;
			}
		}

		FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
		const int32 Host = Layout.FindSectionAtSOffset(AtS);
		if (Host == INDEX_NONE)
		{
			return;
		}

		const int32 LeftSec = Layout.FindSideSection(Host, EMetaLandRoadLaneSectionSide::Left);
		const int32 RightSec = Layout.FindSideSection(Host, EMetaLandRoadLaneSectionSide::Right);
		const bool bHaveLeft = LeftSec != INDEX_NONE && !Layout.Sections[LeftSec].Left.IsEmpty();
		const bool bHaveRight = RightSec != INDEX_NONE && !Layout.Sections[RightSec].Right.IsEmpty();
		if (bHaveLeft && bHaveRight)
		{
			// Prefer a Both host so FindSideSection never falls to opposite face.
			FMetaLandRoadLaneSection& HostSection = Layout.Sections[Host];
			bool bChanged = false;
			if (HostSection.Side == EMetaLandRoadLaneSectionSide::Left && bHaveRight)
			{
				HostSection.Side = EMetaLandRoadLaneSectionSide::Both;
				if (HostSection.Right.IsEmpty() && RightSec != INDEX_NONE)
				{
					HostSection.Right = Layout.Sections[RightSec].Right;
				}
				bChanged = true;
			}
			else if (HostSection.Side == EMetaLandRoadLaneSectionSide::Right && bHaveLeft)
			{
				HostSection.Side = EMetaLandRoadLaneSectionSide::Both;
				if (HostSection.Left.IsEmpty() && LeftSec != INDEX_NONE)
				{
					HostSection.Left = Layout.Sections[LeftSec].Left;
				}
				bChanged = true;
			}
			if (bChanged)
			{
				Spline->UpdateLaneSectionBounds();
				Spline->UpdateRoadLayout();
			}
			return;
		}

		FMetaLandRoadLaneSection& HostSection = Layout.Sections[Host];
		if (!bHaveLeft && bHaveRight)
		{
			const TArray<FMetaLandRoadLane>& Src = Layout.Sections[RightSec].Right;
			HostSection.Left.Reset();
			for (const FMetaLandRoadLane& Lane : Src)
			{
				HostSection.Left.Add(CloneLaneConstantWidth(Lane));
			}
			HostSection.Side = EMetaLandRoadLaneSectionSide::Both;
			if (HostSection.Right.IsEmpty())
			{
				HostSection.Right = Src;
			}
			UE_LOG(LogTemp, Log,
				TEXT("[JunctionCook] extend Left lanes (%d) from Right at S=%.1f on %s"),
				HostSection.Left.Num(), AtS, *GetNameSafe(Spline->GetOwner()));
		}
		else if (!bHaveRight && bHaveLeft)
		{
			const TArray<FMetaLandRoadLane>& Src = Layout.Sections[LeftSec].Left;
			HostSection.Right.Reset();
			for (const FMetaLandRoadLane& Lane : Src)
			{
				HostSection.Right.Add(CloneLaneConstantWidth(Lane));
			}
			HostSection.Side = EMetaLandRoadLaneSectionSide::Both;
			if (HostSection.Left.IsEmpty())
			{
				HostSection.Left = Src;
			}
			UE_LOG(LogTemp, Log,
				TEXT("[JunctionCook] extend Right lanes (%d) from Left at S=%.1f on %s"),
				HostSection.Right.Num(), AtS, *GetNameSafe(Spline->GetOwner()));
		}
		else
		{
			// Both empty — seed one lane per face from default width.
			FMetaLandRoadLane Seed;
			FMetaLandRoadLaneDriving Driving;
			Driving.DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;
			Seed.LaneInstance.InitializeAs<FMetaLandRoadLaneDriving>(Driving);
			Seed.Width.SetDefaultValue(static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth));
			HostSection.Left = { Seed };
			HostSection.Right = { Seed };
			HostSection.Side = EMetaLandRoadLaneSectionSide::Both;
			UE_LOG(LogTemp, Warning,
				TEXT("[JunctionCook] seed default L+R lanes at S=%.1f on %s"),
				AtS, *GetNameSafe(Spline->GetOwner()));
		}

		Spline->UpdateLaneSectionBounds();
		Spline->UpdateRoadLayout();
	}

	/**
	 * Lock co-located Left@CutS + Right@CutS (MetaRoad face tracks), not a single Both.
	 * Ensures both face tips share mouth S after EnsureStandardLeftRightLanes.
	 */
	static bool ApplyLaneCutAtDistance(UMetaLandRoadSplineComponent* Spline, double CutS)
	{
		if (!Spline || Spline->GetLaneSectionsNum() == 0)
		{
			return false;
		}

		constexpr double MinMarginCm = 10.0;
		const double SplineLength = Spline->GetSplineLength();
		if (CutS <= MinMarginCm || CutS >= SplineLength - MinMarginCm)
		{
			return false;
		}

		EnsureStandardLeftRightLanes(Spline, FMath::Max(CutS - 1.0, MinMarginCm));

		FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
		bool bHaveLeftAtCut = false;
		bool bHaveRightAtCut = false;
		bool bDropped = false;
		for (int32 SectionIndex = Layout.Sections.Num() - 1; SectionIndex >= 1; --SectionIndex)
		{
			FMetaLandRoadLaneSection& Existing = Layout.Sections[SectionIndex];
			if (!FMath::IsNearlyEqual(Existing.SOffset, CutS, 10.0))
			{
				continue;
			}
			if (Existing.Side == EMetaLandRoadLaneSectionSide::Left)
			{
				Existing.SOffset = CutS;
				bHaveLeftAtCut = true;
				continue;
			}
			if (Existing.Side == EMetaLandRoadLaneSectionSide::Right)
			{
				Existing.SOffset = CutS;
				bHaveRightAtCut = true;
				continue;
			}
			// Drop Both / junk — replace with explicit L|R pair.
			Layout.Sections.RemoveAt(SectionIndex);
			bDropped = true;
		}
		if (bDropped)
		{
			Spline->UpdateLaneSectionBounds();
		}

		bool bOk = true;
		if (!bHaveLeftAtCut)
		{
			bOk = Spline->SplitSectionAtDistanceAlongSpline(
				CutS, EMetaLandRoadLaneSectionSide::Left) != INDEX_NONE && bOk;
		}
		if (!bHaveRightAtCut)
		{
			bOk = Spline->SplitSectionAtDistanceAlongSpline(
				CutS, EMetaLandRoadLaneSectionSide::Right) != INDEX_NONE && bOk;
		}
		if (bHaveLeftAtCut || bHaveRightAtCut)
		{
			Spline->UpdateLaneSectionBounds();
			Spline->UpdateRoadLayout();
		}
		return bOk;
	}

	static void RefreshArmMesh(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return;
		}

		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			RoadActor->RefreshDynamicRoadMesh();
		}
	}

	static AMoKuEditIntersectionActor* FindJunctionActorNear(
		UWorld* World,
		const FVector& AnchorWorld,
		double ClusterRadius)
	{
		if (!World)
		{
			return nullptr;
		}

		const double ClusterRadiusSq = ClusterRadius * ClusterRadius;
		AMoKuEditIntersectionActor* BestActor = nullptr;
		int32 BestArmCount = -1;
		double BestDistSq = ClusterRadiusSq;
		for (TActorIterator<AMoKuEditIntersectionActor> It(World); It; ++It)
		{
			AMoKuEditIntersectionActor* JunctionActor = *It;
			if (!JunctionActor || !JunctionActor->JunctionData)
			{
				continue;
			}

			const FVector ActorAnchor = JunctionActor->JunctionData->JunctionNode.AnchorWorld.IsNearlyZero()
				? JunctionActor->GetActorLocation()
				: JunctionActor->JunctionData->JunctionNode.AnchorWorld;
			const double DistSq = FVector::DistSquared(ActorAnchor, AnchorWorld);
			if (DistSq > ClusterRadiusSq)
			{
				continue;
			}

			const int32 ArmCount = JunctionActor->JunctionData->JunctionNode.Arms.Num();
			// Prefer richer node, then closer — avoids keeping an empty duplicate over the real N-way.
			if (ArmCount > BestArmCount || (ArmCount == BestArmCount && DistSq < BestDistSq))
			{
				BestArmCount = ArmCount;
				BestDistSq = DistSq;
				BestActor = JunctionActor;
			}
		}
		return BestActor;
	}

	/** Preserve arms already registered on a hub actor (N-way extension without full-world scan). */
	static void MergeArmsFromExistingJunctionActor(
		UWorld* World,
		FMetaLandRoadJunctionNode& Junction,
		double ClusterRadius)
	{
		AMoKuEditIntersectionActor* ExistingActor = FindJunctionActorNear(
			World,
			Junction.AnchorWorld,
			ClusterRadius);
		if (!ExistingActor || !ExistingActor->JunctionData)
		{
			return;
		}

		const FMetaLandRoadJunctionNode& ExistingNode = ExistingActor->JunctionData->JunctionNode;
		for (const FMetaLandRoadJunctionArmRef& ExistingArm : ExistingNode.Arms)
		{
			if (!ExistingArm.Spline.IsValid() || IsConnectEndArmRef(ExistingArm))
			{
				continue;
			}

			FDetectedArmHit Hit;
			Hit.Spline = ExistingArm.Spline;
			Hit.IntersectWorld = Junction.AnchorWorld;
			Hit.IntersectDistanceS = ExistingArm.IntersectDistanceS;
			Hit.CutDistanceS = ExistingArm.CutDistanceS;
			AttachArmToJunction(Junction, Hit);
		}
	}

	/** Local: only intersections involving DrawnSpline (+ merge existing hub arms). */
	static void RebuildJunctionsForDrawnRoad(
		UWorld* World,
		UMetaLandRoadSplineComponent* DrawnSpline,
		TArray<FMetaLandRoadJunctionNode>& Junctions,
		double ForwardIndent,
		double ClusterRadius,
		float MaxHeightDeltaCm = 0.0f)
	{
		Junctions.Reset();
		if (!World || !DrawnSpline || DrawnSpline->GetNumberOfSplinePoints() < 2)
		{
			return;
		}

		MetaLandRoadProfile::EnsureDefaultRoadProfile(DrawnSpline);

		TArray<FDetectedArmHit> IntersectionHits;
		DetectCenterlineIntersections(World, DrawnSpline, IntersectionHits, MaxHeightDeltaCm);

		for (const FDetectedArmHit& OtherHit : IntersectionHits)
		{
			const FVector IntersectWorld = OtherHit.IntersectWorld;
			const FGuid JunctionId = FindOrCreateJunctionId(Junctions, IntersectWorld, ClusterRadius);
			FMetaLandRoadJunctionNode* Junction = FindJunctionMutable(Junctions, JunctionId);
			if (!Junction)
			{
				continue;
			}

			Junction->ForwardIndent = ForwardIndent;
			Junction->AnchorWorld = IntersectWorld;

			FDetectedArmHit DrawnHit;
			DrawnHit.Spline = DrawnSpline;
			DrawnHit.IntersectWorld = IntersectWorld;
			DrawnHit.IntersectDistanceS = DrawnSpline->GetDistanceAlongSplineAtLocation(
				IntersectWorld,
				ESplineCoordinateSpace::World);
			DrawnHit.CutDistanceS = 0.0;

			AttachArmToJunction(*Junction, DrawnHit);
			AttachArmToJunction(*Junction, OtherHit);
		}

		for (FMetaLandRoadJunctionNode& Junction : Junctions)
		{
			MergeArmsFromExistingJunctionActor(World, Junction, ClusterRadius);
			FinalizeJunctionArmCuts(Junction);
			FinalizeTopologyConnectAndCustomMouth(Junction);
		}
	}

	/** One logical junction = one actor. Destroy stacked duplicates (抬高不能替代合并). */
	static void DestroyDuplicateJunctionActorsNear(
		UWorld* World,
		AMoKuEditIntersectionActor* KeepActor,
		const FVector& AnchorWorld,
		double ClusterRadius)
	{
		if (!World || !KeepActor)
		{
			return;
		}

		const double ClusterRadiusSq = ClusterRadius * ClusterRadius;
		TArray<AMoKuEditIntersectionActor*> ToDestroy;
		for (TActorIterator<AMoKuEditIntersectionActor> It(World); It; ++It)
		{
			AMoKuEditIntersectionActor* JunctionActor = *It;
			if (!JunctionActor || JunctionActor == KeepActor)
			{
				continue;
			}

			const FVector ActorAnchor =
				(JunctionActor->JunctionData && !JunctionActor->JunctionData->JunctionNode.AnchorWorld.IsNearlyZero())
					? JunctionActor->JunctionData->JunctionNode.AnchorWorld
					: JunctionActor->GetActorLocation();
			if (FVector::DistSquared(ActorAnchor, AnchorWorld) <= ClusterRadiusSq)
			{
				ToDestroy.Add(JunctionActor);
			}
		}

		for (AMoKuEditIntersectionActor* Duplicate : ToDestroy)
		{
			if (!Duplicate)
			{
				continue;
			}
			if (Duplicate->JunctionData)
			{
				Duplicate->JunctionData->ClearLinks();
			}
			Duplicate->Modify();
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[JunctionDedup] destroy stacked %s near keep=%s"),
				*Duplicate->GetName(),
				*KeepActor->GetName());
			World->DestroyActor(Duplicate);
		}
	}

	static AMoKuEditIntersectionActor* FindOrSpawnJunctionActor(
		UWorld* World,
		const FVector& AnchorWorld)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = World->PersistentLevel;
		SpawnParams.bNoFail = true;
		SpawnParams.ObjectFlags |= RF_Transactional;
		AMoKuEditIntersectionActor* JunctionActor = World->SpawnActor<AMoKuEditIntersectionActor>(
			AnchorWorld,
			FRotator::ZeroRotator,
			SpawnParams);
		if (JunctionActor)
		{
			JunctionActor->SetFolderPath(SCENE_JUNCTION_PATH);
			JunctionActor->SetActorLocation(AnchorWorld);
			JunctionActor->SetActorLabel(TEXT("MoKuEditIntersectionActor"));
		}
		return JunctionActor;
	}

	static AMoKuEditIntersectionActor* PersistJunctionNodeToActor(
		UWorld* World,
		FMetaLandRoadJunctionNode& Junction,
		double ClusterRadius)
	{
		if (!World || Junction.Arms.IsEmpty())
		{
			return nullptr;
		}

		MoKuSplineEditorUtils::CreateSceneStructure();

		AMoKuEditIntersectionActor* JunctionActor = FindJunctionActorNear(
			World,
			Junction.AnchorWorld,
			ClusterRadius);
		if (!JunctionActor)
		{
			JunctionActor = FindOrSpawnJunctionActor(World, Junction.AnchorWorld);
		}
		if (!JunctionActor || !JunctionActor->JunctionData)
		{
			return nullptr;
		}

		// Collapse prior stacked IntersectionActors at this hub before rewriting links/mesh.
		DestroyDuplicateJunctionActorsNear(World, JunctionActor, Junction.AnchorWorld, ClusterRadius);

		JunctionActor->Modify();
		JunctionActor->JunctionData->Modify();
		if (!Junction.AnchorWorld.IsNearlyZero())
		{
			JunctionActor->SetActorLocation(Junction.AnchorWorld);
		}
		JunctionActor->JunctionData->ApplyNode(Junction);
		JunctionActor->JunctionData->ClearLinks();

		for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
		{
			UMetaLandRoadSplineComponent* Spline = Arm.Spline.Get();
			if (!Spline)
			{
				continue;
			}

			AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(Spline->GetOwner());
			if (!RoadActor)
			{
				continue;
			}

			RoadActor->Modify();
			JunctionActor->JunctionData->LinkRoadActor(RoadActor, Arm);
		}

		JunctionActor->ConnectionActors.Reset();
		for (AMoKuEditSplineActor* LinkedRoad : JunctionActor->JunctionData->LinkedRoadActors)
		{
			if (LinkedRoad)
			{
				JunctionActor->ConnectionActors.AddUnique(LinkedRoad);
			}
		}

		return JunctionActor;
	}
}

void UMetaLandRoadEditorMode::RecookJunctionActor(
	AMoKuEditIntersectionActor* JunctionActor,
	bool bRefreshLinkedRoadMeshes)
{
	RecookJunctionActorStatic(JunctionActor, bRefreshLinkedRoadMeshes);
}

namespace
{
	int32 GJunctionRecookDepth = 0;
	TSet<TWeakObjectPtr<AMoKuEditIntersectionActor>> GDeferredJunctionRecook;

	void DrainDeferredJunctionRecooks()
	{
		constexpr int32 MaxPasses = 32;
		for (int32 Pass = 0; Pass < MaxPasses && GDeferredJunctionRecook.Num() > 0; ++Pass)
		{
			TArray<TWeakObjectPtr<AMoKuEditIntersectionActor>> Pending = GDeferredJunctionRecook.Array();
			GDeferredJunctionRecook.Reset();
			for (const TWeakObjectPtr<AMoKuEditIntersectionActor>& WeakJunction : Pending)
			{
				if (AMoKuEditIntersectionActor* Deferred = WeakJunction.Get())
				{
					UMetaLandRoadEditorMode::RecookJunctionActorStatic(Deferred, true);
				}
			}
		}
	}
}

void UMetaLandRoadEditorMode::RecookJunctionActorStatic(
	AMoKuEditIntersectionActor* JunctionActor,
	bool bRefreshLinkedRoadMeshes)
{
	if (!JunctionActor || !JunctionActor->JunctionData)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Recook] abort — null junction/data"));
		return;
	}

	// Package/map load: actor has no World yet — Publish RegisterComponent ensure-crashes.
	if (!JunctionActor->GetWorld() || UE::GetIsEditorLoadingPackage())
	{
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("[Recook] defer — no world / package loading (%s)"),
			*JunctionActor->GetName());
		return;
	}

	// Global depth: RoadA→J1→RoadB→J2 nesting still hit RegisterComponent / splash spin.
	if (GJunctionRecookDepth > 0)
	{
		GDeferredJunctionRecook.Add(JunctionActor);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Recook] deferred junction=%s depth=%d (queued for follow-up)"),
			*JunctionActor->GetName(),
			GJunctionRecookDepth);
		return;
	}

	// ApplyLaneCut/Purge → UpdateRoadLayout → RefreshDynamicRoadMesh → Recook again
	// used to spin forever on map load (splash 92%). Suppress for the WHOLE cook.
	if (JunctionActor->bIsRecookInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Recook] re-entrant skip junction=%s"), *JunctionActor->GetName());
		return;
	}
	TGuardValue<int32> RecookDepthGuard(GJunctionRecookDepth, GJunctionRecookDepth + 1);
	TGuardValue<bool> RecookGuard(JunctionActor->bIsRecookInProgress, true);
	TGuardValue<bool> SuppressRoadRecook(
		AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);

	FMetaLandRoadJunctionNode& Junction = JunctionActor->JunctionData->JunctionNode;

	// ConnectTo mouth is lane topology, not a junction hub — drop stale connect arms.
	// Never leave the hub with arms<2 (short-road margin bug wiped T junctions).
	{
		TArray<FMetaLandRoadJunctionArmRef> ArmsBackup = Junction.Arms;
		const int32 RemovedConnectArms = Junction.Arms.RemoveAll(
			[](const FMetaLandRoadJunctionArmRef& Arm)
			{
				return MetaLandRoadEditorModeJunctionPrivate::IsConnectEndArmRef(Arm);
			});
		if (Junction.Arms.Num() < 2)
		{
			Junction.Arms = MoveTemp(ArmsBackup);
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[Recook] skipped connect-end purge on %s — would leave arms<2 (kept %d)"),
				*JunctionActor->GetName(),
				Junction.Arms.Num());
		}
		else if (RemovedConnectArms > 0)
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[Recook] purged %d connect-mouth arm(s) from junction=%s"),
				RemovedConnectArms,
				*JunctionActor->GetName());
		}
	}

	if (Junction.Arms.Num() < 2)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[Recook] abort — arms=%d on junction=%s"),
			Junction.Arms.Num(),
			*JunctionActor->GetName());
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Recook] BEGIN junction=%s arms=%d refreshRoads=%d"),
		*JunctionActor->GetName(),
		Junction.Arms.Num(),
		bRefreshLinkedRoadMeshes ? 1 : 0);

	// Algorithm debug: purge previous cover before re-cut / re-link (no stale 重面).
	JunctionActor->ClearJunctionCoverMesh();

	// Actor Details CutExpandScale drives auto indent (persist on node before cut).
	if (JunctionActor->CutExpandScale > KINDA_SMALL_NUMBER)
	{
		Junction.CutExpandScale = JunctionActor->CutExpandScale;
	}

	MetaLandRoadEditorModeJunctionPrivate::FinalizeJunctionArmCuts(Junction);
	MetaLandRoadEditorModeJunctionPrivate::FinalizeTopologyConnectAndCustomMouth(Junction);
	++Junction.LayoutVersion;

	JunctionActor->Modify();
	JunctionActor->JunctionData->Modify();
	if (!Junction.AnchorWorld.IsNearlyZero())
	{
		JunctionActor->SetActorLocation(Junction.AnchorWorld);
	}
	JunctionActor->JunctionData->ApplyNode(Junction);
	JunctionActor->CutExpandScale = Junction.CutExpandScale;
	JunctionActor->JunctionData->ClearLinks();

	for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
	{
		UMetaLandRoadSplineComponent* ArmSpline = Arm.Spline.Get();
		if (!ArmSpline)
		{
			continue;
		}

		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ArmSpline->GetOwner()))
		{
			RoadActor->Modify();
		}
		ArmSpline->Modify();
		// Wipe stale mouth markers, then lock co-located Left@CutS + Right@CutS.
		MetaLandRoadEditorModeJunctionPrivate::PurgeLegacyJunctionCutSections(
			ArmSpline, Arm.CutDistanceS, Arm.DepartCutS, Arm.IntersectDistanceS);
		constexpr double MinCutS = 50.0;
		if (Arm.CutDistanceS > MinCutS)
		{
			MetaLandRoadEditorModeJunctionPrivate::ApplyLaneCutAtDistance(
				ArmSpline, Arm.CutDistanceS);
		}
		if (Arm.bThroughArm && Arm.DepartCutS > MinCutS)
		{
			MetaLandRoadEditorModeJunctionPrivate::ApplyLaneCutAtDistance(
				ArmSpline, Arm.DepartCutS);
		}
		else if (!Arm.bThroughArm && Arm.DepartCutS > MinCutS
			&& Arm.IntersectDistanceS < ArmSpline->GetSplineLength() * 0.5)
		{
			MetaLandRoadEditorModeJunctionPrivate::ApplyLaneCutAtDistance(
				ArmSpline, Arm.DepartCutS);
		}

		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ArmSpline->GetOwner()))
		{
			JunctionActor->JunctionData->LinkRoadActor(RoadActor, Arm);
		}
	}

	JunctionActor->ConnectionActors.Reset();
	for (AMoKuEditSplineActor* LinkedRoad : JunctionActor->JunctionData->LinkedRoadActors)
	{
		if (LinkedRoad)
		{
			JunctionActor->ConnectionActors.AddUnique(LinkedRoad);
		}
	}

	// Do NOT RefreshArmMesh here — RefreshJunctionCoverMesh rebuilds linked roads once.
	// Extra RefreshArmMesh + cover refresh was double/triple mesh rebuild (巨卡 on T cook).
	(void)bRefreshLinkedRoadMeshes;

	for (const FMetaLandRoadJunctionArmRef& Arm : Junction.Arms)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Recook/CutAlign] arm CutS=%.1f DepartS=%.1f HubS=%.1f through=%d"),
			Arm.CutDistanceS,
			Arm.DepartCutS,
			Arm.IntersectDistanceS,
			Arm.bThroughArm ? 1 : 0);
	}

	// Mouth CS / ConnectEdge for debug viz — must follow lane-width changes on any arm.
	JunctionActor->SyncJunctionFrameworkFromArms();

	JunctionActor->RefreshJunctionCoverMesh();
	const int32 TriCount = JunctionActor->DynamicMeshComponent && JunctionActor->DynamicMeshComponent->GetDynamicMesh()
		? JunctionActor->DynamicMeshComponent->GetDynamicMesh()->GetTriangleCount()
		: 0;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Recook] END junction=%s topology=%d tris=%d arms=%d"),
		*JunctionActor->GetName(),
		static_cast<int32>(Junction.Topology),
		TriCount,
		Junction.Arms.Num());
	DrainDeferredJunctionRecooks();
}

bool UMetaLandRoadEditorMode::IsInteractiveSplineToolActive() const
{
	const UInteractiveTool* Tool = GetToolManager()->GetActiveTool(EToolSide::Left);
	return Tool && Tool->IsA<UMetaLandRoadInteractiveTool>();
}

void UMetaLandRoadEditorMode::ResetInteractiveJunctionSession()
{
	InteractiveJunctions.Reset();
}

void UMetaLandRoadEditorMode::ProcessInteractiveFinishedRoadDraw(
	AMoKuEditSplineActor* DrawnRoad,
	bool bFullWorldRebuild,
	float MaxHeightDeltaCm)
{
	if (!DrawnRoad || !GEditor)
	{
		return;
	}

	UMoKuEditSplinesComponent* DrawnSpline = DrawnRoad->GetSplineComponent();
	if (!DrawnSpline || DrawnSpline->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	UWorld* World = DrawnRoad->GetWorld();
	if (!World)
	{
		return;
	}

	UMetaLandRoadSplineComponent* DrawnRoadSpline = Cast<UMetaLandRoadSplineComponent>(DrawnSpline);
	if (!DrawnRoadSpline)
	{
		return;
	}

	if (bFullWorldRebuild)
	{
		MetaLandRoadEditorModeJunctionPrivate::RebuildAllJunctionsFromWorld(
			World,
			InteractiveJunctions,
			InteractiveJunctionForwardIndent,
			InteractiveJunctionClusterRadius,
			MaxHeightDeltaCm);
	}
	else
	{
		MetaLandRoadEditorModeJunctionPrivate::RebuildJunctionsForDrawnRoad(
			World,
			DrawnRoadSpline,
			InteractiveJunctions,
			InteractiveJunctionForwardIndent,
			InteractiveJunctionClusterRadius,
			MaxHeightDeltaCm);
	}

	if (InteractiveJunctions.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("InteractiveJunctionCook", "Interactive Junction Lane Cut"));
	DrawnRoad->Modify();
	DrawnSpline->Modify();

	int32 CookedJunctionCount = 0;
	AMoKuEditIntersectionActor* LastJunctionActor = nullptr;
	for (FMetaLandRoadJunctionNode& Junction : InteractiveJunctions)
	{
		if (Junction.Arms.Num() < 2)
		{
			continue;
		}

		MetaLandRoadEditorModeJunctionPrivate::UpdateJunctionTopology(Junction);
		MetaLandRoadEditorModeJunctionPrivate::ResolveJunctionConnectSides(Junction);
		++Junction.LayoutVersion;

		if (AMoKuEditIntersectionActor* JunctionActor =
			MetaLandRoadEditorModeJunctionPrivate::PersistJunctionNodeToActor(
				World,
				Junction,
				InteractiveJunctionClusterRadius))
		{
			LastJunctionActor = JunctionActor;
			++CookedJunctionCount;
			RecookJunctionActor(JunctionActor, true);
		}
	}

	MetaLandRoadEditorModeJunctionPrivate::ClearSpuriousConnectEndTrim(DrawnRoad);
	if (UMetaLandRoadSplineComponent* ConnectSource =
		MetaLandRoadEditorModeJunctionPrivate::GetConnectSourceSpline(DrawnRoadSpline))
	{
		if (AMoKuEditSplineActor* SourceRoad = Cast<AMoKuEditSplineActor>(ConnectSource->GetOwner()))
		{
			MetaLandRoadEditorModeJunctionPrivate::ClearSpuriousConnectEndTrim(SourceRoad);
		}
	}

	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		DrawnRoad->RefreshDynamicRoadMesh();
		if (UMetaLandRoadSplineComponent* ConnectSource =
			MetaLandRoadEditorModeJunctionPrivate::GetConnectSourceSpline(DrawnRoadSpline))
		{
			if (AMoKuEditSplineActor* SourceRoad = Cast<AMoKuEditSplineActor>(ConnectSource->GetOwner()))
			{
				SourceRoad->RefreshDynamicRoadMesh();
			}
		}
	}

	if (LastJunctionActor)
	{
		GEditor->SelectActor(LastJunctionActor, true, true, true);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			CookedJunctionCount > 0 ? FColor::Green : FColor::Yellow,
			bFullWorldRebuild
				? FString::Printf(
					TEXT("Junction cook (full world): %d node(s), %d hub(s) scanned"),
					CookedJunctionCount,
					InteractiveJunctions.Num())
				: FString::Printf(
					TEXT("Junction cook (local): %d node(s), %d hub(s) on drawn road"),
					CookedJunctionCount,
					InteractiveJunctions.Num()));
	}

	GEditor->RedrawLevelEditingViewports(true);
}


#undef LOCTEXT_NAMESPACE
