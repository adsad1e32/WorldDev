#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

#include "MetaLandRoadSectionHover.h"
#include "MetaLandRoadEditorColors.h"
#include "MetaLandRoadNativeEditVisualization.h"
#include "MetaLandRoadSectionEditVisualization.h"
#include "MetaLandRoadLaneEditPick.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadEditorMode.h"
#include "MoKuEditorUtils.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadStyle.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "Misc/ScopeExit.h"
#include "Utils/MetaLandRoadCompVisUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "EngineUtils.h"
#include "Layout/WidgetPath.h"
#include "Modules/ModuleManager.h"
#include "SEditorViewport.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Styling/StyleColors.h"
#include "UnrealWidget.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "SceneManagement.h"
#include "SceneView.h"

#define LOCTEXT_NAMESPACE "FMoKuEditSplinesComponentVisualizer"

namespace
{
bool CanSafelyUseEngineSplineVisualizer(const USplineComponent* SplineComp)
{
	return IsValid(SplineComp) && IsValid(SplineComp->GetOwner());
}
}

static bool IsViewportNavigationKey(const FKey& Key)
{
	return Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D
		|| Key == EKeys::Q || Key == EKeys::E
		|| Key == EKeys::Up || Key == EKeys::Down
		|| Key == EKeys::Left || Key == EKeys::Right;
}

static bool IsMouseGizmoDrag(const FEditorViewportClient* ViewportClient, const FViewport* Viewport)
{
	if (!ViewportClient || !Viewport || !ViewportClient->IsTracking())
	{
		return false;
	}
	if (Viewport->KeyState(EKeys::RightMouseButton) || Viewport->KeyState(EKeys::MiddleMouseButton))
	{
		return false;
	}
	if (!Viewport->KeyState(EKeys::LeftMouseButton))
	{
		return false;
	}
	return ViewportClient->GetCurrentWidgetAxis() != EAxisList::None;
}

static FVector CaptureSplineAddClickWorld(USplineComponent* SplineComp, int32 Seg, const FVector& Fallback)
{
	if (!SplineComp || !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		return Fallback;
	}

	FIntPoint MousePos;
	GCurrentLevelEditingViewportClient->Viewport->GetMousePos(MousePos);
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		GCurrentLevelEditingViewportClient->Viewport,
		GCurrentLevelEditingViewportClient->GetScene(),
		GCurrentLevelEditingViewportClient->EngineShowFlags));
	FSceneView* View = GCurrentLevelEditingViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return Fallback;
	}

	const FViewportClick Click(
		View,
		GCurrentLevelEditingViewportClient,
		EKeys::RightMouseButton,
		IE_Pressed,
		MousePos.X,
		MousePos.Y);
	constexpr int32 NumSubdivisions = 24;
	FVector SubStart = SplineComp->GetLocationAtSplineInputKey(
		static_cast<float>(Seg), ESplineCoordinateSpace::World);
	double BestDist = TNumericLimits<double>::Max();
	FVector BestLoc = Fallback;
	for (int32 Step = 1; Step <= NumSubdivisions; ++Step)
	{
		const float KeyParam = static_cast<float>(Seg)
			+ static_cast<float>(Step) / static_cast<float>(NumSubdivisions);
		const FVector SubEnd = SplineComp->GetLocationAtSplineInputKey(
			KeyParam, ESplineCoordinateSpace::World);
		FVector SplineClosest;
		FVector RayClosest;
		FMath::SegmentDistToSegmentSafe(
			SubStart,
			SubEnd,
			Click.GetOrigin(),
			Click.GetOrigin() + Click.GetDirection() * 50000.0f,
			SplineClosest,
			RayClosest);
		const double Dist = FVector::DistSquared(SplineClosest, RayClosest);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestLoc = SplineClosest;
		}
		SubStart = SubEnd;
	}
	return BestLoc;
}

static void UpdateLaneFaceHoverFromViewportMouse(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FSceneView* View,
	FMetaLandRoadLaneEditHighlight& Highlight)
{
	if (!SplineComp || !View || !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		return;
	}

	FIntPoint MousePos;
	GCurrentLevelEditingViewportClient->Viewport->GetMousePos(MousePos);
	FMetaLandRoadLaneFacePickResult FacePick;
	if (MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *View, MousePos, FacePick))
	{
		Highlight.HoveredSectionIndex = FacePick.SectionIndex;
		Highlight.HoveredLaneIndex = FacePick.LaneIndex;
		Highlight.HoveredWidthIndex = INDEX_NONE;
		Highlight.bHoveredWidthIsKey = false;
		Highlight.bHoveredLaneBoundaryLine = false;
	}
}

IMPLEMENT_HIT_PROXY(HMetaLandRoadPropKeyVisProxy, HMetaLandRoadPropRailVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadPropRailVisProxy, HMetaLandRoadSectionVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadAttributeKeyVisProxy, HMetaLandRoadSectionVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSectionKeyVisProxy, HMetaLandRoadLaneVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadLaneWidthKeyVisProxy, HMetaLandRoadLaneWidthSegmentVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadLaneWidthSegmentVisProxy, HMetaLandRoadLaneVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadLaneVisProxy, HMetaLandRoadSectionVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSectionVisProxy, HMetaLandRoadSplineVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSplineVisProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadNativeSplineVisProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSplineKeyVisProxy, HMetaLandRoadNativeSplineVisProxy);
IMPLEMENT_HIT_PROXY(HMetaLandRoadSplineSegmentVisProxy, HMetaLandRoadNativeSplineVisProxy);

class FMetaLandRoadSectionComponentVisualizerCommands : public TCommands<FMetaLandRoadSectionComponentVisualizerCommands>
{
public:
	FMetaLandRoadSectionComponentVisualizerCommands()
		: TCommands<FMetaLandRoadSectionComponentVisualizerCommands>(
			TEXT("MetaLandRoadSectionComponentVisualizer"),
			LOCTEXT("MetaLandRoadSectionComponentVisualizer", "MetaLandRoad Section Component Visualizer"),
			NAME_None,
			FMetaLandRoadStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(AddLaneToLeft, "Add Lane to Left", "Add a new road lane to the left side of the currently selected lane.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddLaneToRight, "Add Lane to Right", "Add a new road lane to the right side of the currently selected lane.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteLane, "Delete Lane", "Delete selected lane.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddLeftSection, "Add Left Section", "Split a Left face section at the pick.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddRightSection, "Add Right Section", "Split a Right face section at the pick.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteLeftSection, "Delete Left", "Delete the Left face of the selected section.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteRightSection, "Delete Right", "Delete the Right face of the selected section.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddWidthKey, "Add Width Key", "Add a width key on the selected lane boundary (Alt+T).", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteWidthKey, "Delete Width Key", "Delete the selected width key.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddLandscapeKey, "Add Landscape Key", "Add a Landscape attribute key on the center line.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(DeleteLandscapeKey, "Delete Landscape Key", "Delete the selected Landscape attribute key.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SetKeyToCurve, "Curve", "Set spline point to Curve type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToCurveCustomTangent, "Curve Custom Tangent", "Set spline point to Curve Custom Tangent type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToCurveClamped, "Curve Clamped", "Set spline point to Curve Clamped type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToLinear, "Linear", "Set spline point to Linear type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToConstant, "Constant", "Set spline point to Constant type.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToArc, "Arc", "The segment from this point to the next one will try to maintain a circular arc.", EUserInterfaceActionType::RadioButton, FInputChord());
	}

public:
	TSharedPtr<FUICommandInfo> AddLaneToLeft;
	TSharedPtr<FUICommandInfo> AddLaneToRight;
	TSharedPtr<FUICommandInfo> DeleteLane;
	TSharedPtr<FUICommandInfo> AddLeftSection;
	TSharedPtr<FUICommandInfo> AddRightSection;
	TSharedPtr<FUICommandInfo> DeleteLeftSection;
	TSharedPtr<FUICommandInfo> DeleteRightSection;
	TSharedPtr<FUICommandInfo> AddWidthKey;
	TSharedPtr<FUICommandInfo> DeleteWidthKey;
	TSharedPtr<FUICommandInfo> AddLandscapeKey;
	TSharedPtr<FUICommandInfo> DeleteLandscapeKey;
	TSharedPtr<FUICommandInfo> SetKeyToCurve;
	TSharedPtr<FUICommandInfo> SetKeyToCurveCustomTangent;
	TSharedPtr<FUICommandInfo> SetKeyToCurveClamped;
	TSharedPtr<FUICommandInfo> SetKeyToLinear;
	TSharedPtr<FUICommandInfo> SetKeyToConstant;
	TSharedPtr<FUICommandInfo> SetKeyToArc;
};

FMoKuEditSplinesComponentVisualizer::FMoKuEditSplinesComponentVisualizer()
{
	FMetaLandRoadSectionComponentVisualizerCommands::Register();
	SectionComponentVisualizerActions = MakeShareable(new FUICommandList);
	RoadSectionSelectionState = NewObject<UMetaLandRoadSectionComponentVisualizerSelectionState>(
		GetTransientPackage(),
		TEXT("MetaLandRoadSectionComponentVisualizerSelectionState"),
		RF_Transactional);
}

FMoKuEditSplinesComponentVisualizer::~FMoKuEditSplinesComponentVisualizer() = default;

FString FMoKuEditSplinesComponentVisualizer::GetReferencerName() const
{
	return GetReferencerNameStatic();
}

UMetaLandRoadSectionComponentVisualizerSelectionState* FMoKuEditSplinesComponentVisualizer::GetSelectionState() const
{
	return RoadSectionSelectionState;
}

void FMoKuEditSplinesComponentVisualizer::RefreshRoadMeshForSpline(UMetaLandRoadSplineComponent* SplineComp) const
{
	if (!SplineComp)
	{
		return;
	}

	SplineComp->EnsureAllCurvePointTypesForSmoothCorners();

	if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		RoadActor->RefreshDynamicRoadMesh();
	}
	SplineComp->RefreshLaneVisualization();
}

void FMoKuEditSplinesComponentVisualizer::RefreshRoadMeshAndRedraw(UMetaLandRoadSplineComponent* SplineComp, bool bForceRedraw) const
{
	RefreshRoadMeshForSpline(SplineComp);
	if (GEditor && bForceRedraw)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMoKuEditSplinesComponentVisualizer::EnsureDefaultProfileIfNeeded(UMetaLandRoadSplineComponent* SplineComp) const
{
	if (SplineComp && SplineComp->GetLaneSectionsNum() == 0)
	{
		MetaLandRoadProfile::EnsureDefaultRoadProfile(SplineComp);
	}
}

void FMoKuEditSplinesComponentVisualizer::ClearSectionHighlight()
{
	if (SelectionState)
	{
		SelectionState->ClearSelectedKeys();
	}
	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ResetSelection(true);
	}
	HoveredSectionIndex = INDEX_NONE;
	HoveredBoundarySectionIndex = INDEX_NONE;
	HoveredSplineKey = -1.0f;
	CachedLaneHoveredSectionIndex = INDEX_NONE;
	CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	CachedLaneHoveredWidthIndex = INDEX_NONE;
	CachedLaneHoveredWidthIsKey = false;
	CachedLaneHoveredSectionBoundaryIndex = INDEX_NONE;
	CachedLaneHoveredBoundaryLine = false;
	CachedLaneHoveredSplineKey = -1.0f;
	CachedLandscapeHoveredSectionIndex = INDEX_NONE;
	CachedLandscapeHoveredKeyIndex = INDEX_NONE;
	CachedLandscapeHoveredWorldS = -1.0;
	bCachedLandscapeHoveredLine = false;
}

void FMoKuEditSplinesComponentVisualizer::SyncSectionLineEditModeFlag(bool bEnabled)
{
	// Avoid clearing selection when the flag already matches Module mode (desync fix).
	if (bSectionLineEditMode == bEnabled)
	{
		return;
	}

	bSectionLineEditMode = bEnabled;
	if (bEnabled)
	{
		CachedNativeHoveredKey = INDEX_NONE;
		CachedNativeHoveredSegment = INDEX_NONE;
	}
	else
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		if (RoadSectionSelectionState)
		{
			RoadSectionSelectionState->ResetSelection(true);
		}
	}
}

bool FMoKuEditSplinesComponentVisualizer::SelectSectionBoundaryForDrag(int32 SectionIndex)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();

	if (!SplineComp || SectionIndex <= 0 || SectionIndex >= SplineComp->GetLaneSectionsNum())
	{
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MetaLandRoadSectionVis", "SelectSectionBoundary", "Select Section Boundary"));

	FComponentPropertyPath SplinePropertyPath(SplineComp);
	if (SelectionState && SplinePropertyPath.IsValid())
	{
		SelectionState->ClearSelectedKeys();
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
	}

	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		if (SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(SectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
	}

	ActivateRoadVisualization();

	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
		GCurrentLevelEditingViewportClient->SetRequiredCursorOverride(false);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::SelectMainLineForPathDrag(int32 SectionIndex, float SplineKey)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				SplineComp = Road->GetSplineComponent();
				break;
			}
		}
	}
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
	if (NumPoints < 1)
	{
		return false;
	}

	float UseKey = SplineKey;
	if (UseKey < 0.0f)
	{
		const double MidS = 0.5 * (SplineComp->GetLaneSection(SectionIndex).SOffset
			+ (SectionIndex + 1 < SplineComp->GetLaneSectionsNum()
				? SplineComp->GetLaneSection(SectionIndex + 1).SOffset
				: SplineComp->GetSplineLength()));
		UseKey = SplineComp->GetInputKeyValueAtDistanceAlongSpline(MidS);
	}

	int32 BestKey = FMath::Clamp(FMath::RoundToInt(UseKey), 0, NumPoints - 1);
	float BestDistSq = TNumericLimits<float>::Max();
	const FVector PickWorld = SplineComp->GetLocationAtSplineInputKey(UseKey, ESplineCoordinateSpace::World);
	for (int32 KeyIdx = 0; KeyIdx < NumPoints; ++KeyIdx)
	{
		const FVector KeyWorld = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
		const float DistSq = FVector::DistSquared(PickWorld, KeyWorld);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestKey = KeyIdx;
		}
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("MetaLandRoadSectionVis", "SelectMainLinePathDrag", "Select Road Path Point"));

	FComponentPropertyPath SplinePropertyPath(SplineComp);
	if (SelectionState && SplinePropertyPath.IsValid())
	{
		SelectionState->Modify();
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		SelectionState->ClearSelectedKeys();
		SelectionState->ClearSelectedSegmentIndex();
		SelectionState->ClearSelectedTangentHandle();
		SelectionState->ModifySelectedKeys().Add(BestKey);
		SelectionState->SetLastKeyIndexSelected(BestKey);
		SelectionState->SetCachedRotation(
			SplineComp->GetQuaternionAtSplinePoint(BestKey, ESplineCoordinateSpace::World));
	}

	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		if (SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(false);
		RoadSectionSelectionState->RecordPickSplineKey(UseKey);
		FVector WidgetPos = SplineComp->GetLocationAtSplinePoint(BestKey, ESplineCoordinateSpace::World);
		WidgetPos.Z += SplineComp->GetMainCenterLineHeightOffset();
		RoadSectionSelectionState->SetCachedData(
			WidgetPos,
			SplineComp->GetQuaternionAtSplinePoint(BestKey, ESplineCoordinateSpace::World),
			static_cast<float>(BestKey));
	}

	ActivateRoadVisualization();
	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::ApplySectionBoundaryWorldDrag(int32 SectionIndex, const FVector& WorldPoint)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && RoadSectionSelectionState)
	{
		SplineComp = RoadSectionSelectionState->GetSelectedSpline();
	}
	if (!SplineComp && GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				SplineComp = Road->GetSplineComponent();
				break;
			}
		}
	}
	if (!SplineComp || SectionIndex <= 0 || SectionIndex >= SplineComp->GetLaneSectionsNum())
	{
		return false;
	}

	static constexpr double MinSectionLength = 10.0;
	if (SplineComp->GetSplineLength() <= MinSectionLength * 2.0)
	{
		return false;
	}

	const float ClosestKey = SplineComp->FindInputKeyClosestToWorldLocation(WorldPoint);
	double ClosestS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);
	ClosestS = FMath::Clamp(ClosestS, 0.0, SplineComp->GetSplineLength());

	static constexpr double SameBoundaryTolerance = 0.1;
	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	const double OldS = Section.SOffset;
	ClosestS = MetaLandRoad::ClampSectionBoundaryS(
		SplineComp, SectionIndex, ClosestS, MinSectionLength, SameBoundaryTolerance);

	if (FMath::IsNearlyEqual(OldS, ClosestS, SameBoundaryTolerance))
	{
		if (RoadSectionSelectionState)
		{
			RoadSectionSelectionState->SetCachedDataAtSectionStart(SectionIndex);
		}
		return true;
	}

	for (FMetaLandRoadLaneSection& LaneSection : SplineComp->GetLaneSectionsMutable())
	{
		if (FMath::IsNearlyEqual(LaneSection.SOffset, OldS, SameBoundaryTolerance))
		{
			LaneSection.SOffset = ClosestS;
		}
	}

	SplineComp->GetLaneSectionsMutable().StableSort(
		[](const FMetaLandRoadLaneSection& A, const FMetaLandRoadLaneSection& B)
		{
			if (!FMath::IsNearlyEqual(A.SOffset, B.SOffset, 0.1))
			{
				return A.SOffset < B.SOffset;
			}
			return static_cast<uint8>(A.Side) < static_cast<uint8>(B.Side);
		});

	// Re-map selection to the moved boundary after sort.
	if (RoadSectionSelectionState)
	{
		const int32 NewSectionIndex = CompVisUtils::FindBestFit(
			SplineComp->GetLaneSections(),
			[ClosestS](const FMetaLandRoadLaneSection& It)
			{
				return FMath::Abs(It.SOffset - ClosestS);
			});
		if (NewSectionIndex != INDEX_NONE)
		{
			SectionIndex = NewSectionIndex;
		}
	}

	SplineComp->bAutoMaintainMidpointSectionSplit = false;
	SplineComp->UpdateLaneSectionBounds();

	if (RoadSectionSelectionState)
	{
		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (RoadSectionSelectionState->GetSelectedSpline() != SplineComp && SplinePropertyPath.IsValid())
		{
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(SectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
	}

	// Overlay only while dragging  - mesh / layout / hit-proxy rebuild is FinishSectionBoundaryDrag.
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
	return true;
}

void FMoKuEditSplinesComponentVisualizer::FinishSectionBoundaryDrag()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && RoadSectionSelectionState)
	{
		SplineComp = RoadSectionSelectionState->GetSelectedSpline();
	}
	if (!SplineComp)
	{
		return;
	}

	for (FMetaLandRoadLaneSection& LaneSection : SplineComp->GetLaneSectionsMutable())
	{
		LaneSection.Trim(true);
	}
	SplineComp->UpdateRoadLayout();
	RefreshRoadMeshAndRedraw(SplineComp);
	SplineComp->MarkRenderStateDirty();
}

FString FMoKuEditSplinesComponentVisualizer::GetReferencerNameStatic()
{
	return TEXT("FMoKuEditSplinesComponentVisualizer");
}

bool FMoKuEditSplinesComponentVisualizer::IsSectionLineEditMode() const
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
		&& FMetaLandRoadEditorModule::Get().IsSectionMode();
}

void FMoKuEditSplinesComponentVisualizer::SetSectionLineEditMode(bool bEnabled)
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		FMetaLandRoadEditorModule::Get().SetRoadComponentVisMode(bEnabled);
	}
	else
	{
		SyncSectionLineEditModeFlag(bEnabled);
	}
}

void FMoKuEditSplinesComponentVisualizer::ToggleSectionLineEditMode()
{
	SetSectionLineEditMode(!IsSectionLineEditMode());
}

void FMoKuEditSplinesComponentVisualizer::OnRegister()
{
	FSplineComponentVisualizer::OnRegister();

	if (SplineComponentVisualizerActions.IsValid())
	{
		if (const TSharedPtr<FUICommandInfo> AddKeyCmd = FInputBindingManager::Get().FindCommandInContext(
			FName(TEXT("SplineComponentVisualizer")),
			FName(TEXT("AddKey"))))
		{
			SplineComponentVisualizerActions->UnmapAction(AddKeyCmd.ToSharedRef());
			SplineComponentVisualizerActions->MapAction(
				AddKeyCmd.ToSharedRef(),
				FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnSafeAddKeyToSegment),
				FCanExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::CanAddSplinePointOnCurve));
		}
	}

	const FMetaLandRoadSectionComponentVisualizerCommands& Commands = FMetaLandRoadSectionComponentVisualizerCommands::Get();

	// Alt+E toggle: LevelEditor global action only (mapping here caused double-toggle with ProcessCommandBindings).

	SectionComponentVisualizerActions->MapAction(
		Commands.AddLaneToLeft,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddLane, true),
		FCanExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::CanAddLaneInCurrentMode));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddLaneToRight,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddLane, false),
		FCanExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::CanAddLaneInCurrentMode));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteLane,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteLane),
		FCanExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::CanDeleteLaneInCurrentMode));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddLeftSection,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnSplitSectionSide, EMetaLandRoadLaneSectionSide::Left),
		FCanExecuteAction::CreateLambda([this]()
		{
			return bSectionLineEditMode && HasMainLineSectionMenuContext();
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddRightSection,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnSplitSectionSide, EMetaLandRoadLaneSectionSide::Right),
		FCanExecuteAction::CreateLambda([this]()
		{
			return bSectionLineEditMode && HasMainLineSectionMenuContext();
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteLeftSection,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteSectionSide, EMetaLandRoadLaneSectionSide::Left),
		FCanExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::CanDeleteSectionSide, EMetaLandRoadLaneSectionSide::Left));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteRightSection,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteSectionSide, EMetaLandRoadLaneSectionSide::Right),
		FCanExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::CanDeleteSectionSide, EMetaLandRoadLaneSectionSide::Right));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddWidthKey,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddWidthKey),
		FCanExecuteAction::CreateLambda([this]()
		{
			if (!FMetaLandRoadEditorModule::Get().IsLaneEditMode())
			{
				return false;
			}
			if (CachedLaneHoveredLaneIndex != LANE_INDEX_NONE)
			{
				return true;
			}
			return RoadSectionSelectionState
				&& (RoadSectionSelectionState->HasLaneWidthMenuContext()
					|| RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Lane);
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteWidthKey,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteWidthKey),
		FCanExecuteAction::CreateLambda([this]()
		{
			return FMetaLandRoadEditorModule::Get().IsLaneEditMode()
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key;
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.AddLandscapeKey,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnAddLandscapeKey),
		FCanExecuteAction::CreateLambda([this]()
		{
			return FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay();
		}));

	SectionComponentVisualizerActions->MapAction(
		Commands.DeleteLandscapeKey,
		FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnDeleteLandscapeKey),
		FCanExecuteAction::CreateLambda([this]()
		{
			return FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay()
				&& HasSelectedLandscapeAttributeKey();
		}));

	auto MapRoadSplinePointTypeOnList = [this](
		const TSharedPtr<FUICommandInfo>& Command,
		EMetaLandRoadSplinePointType Mode,
		TSharedPtr<FUICommandList> CommandList)
	{
		if (!CommandList.IsValid())
		{
			return;
		}

		CommandList->MapAction(
			Command,
			FExecuteAction::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::OnSetRoadSplinePointType, Mode),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FMoKuEditSplinesComponentVisualizer::IsRoadSplinePointTypeSet, Mode));
	};

	MapRoadSplinePointTypeOnList(Commands.SetKeyToCurve, EMetaLandRoadSplinePointType::Curve, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToCurveCustomTangent, EMetaLandRoadSplinePointType::CurveCustomTangent, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToCurveClamped, EMetaLandRoadSplinePointType::CurveClamped, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToLinear, EMetaLandRoadSplinePointType::Linear, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToConstant, EMetaLandRoadSplinePointType::Constant, SectionComponentVisualizerActions);
	MapRoadSplinePointTypeOnList(Commands.SetKeyToArc, EMetaLandRoadSplinePointType::Arc, SectionComponentVisualizerActions);

	const FInputBindingManager& InputBindingManager = FInputBindingManager::Get();
	const auto RemapEngineSplinePointType = [this, &InputBindingManager, &MapRoadSplinePointTypeOnList](
		const FName CommandName,
		EMetaLandRoadSplinePointType Mode)
	{
		const TSharedPtr<FUICommandInfo> EngineCommand = InputBindingManager.FindCommandInContext(
			TEXT("SplineComponentVisualizer"),
			CommandName);
		if (EngineCommand.IsValid())
		{
			MapRoadSplinePointTypeOnList(EngineCommand, Mode, SectionComponentVisualizerActions);
		}
	};

	RemapEngineSplinePointType(TEXT("SetKeyToCurve"), EMetaLandRoadSplinePointType::Curve);
	RemapEngineSplinePointType(TEXT("SetKeyToLinear"), EMetaLandRoadSplinePointType::Linear);
	RemapEngineSplinePointType(TEXT("SetKeyToConstant"), EMetaLandRoadSplinePointType::Constant);
}

void FMoKuEditSplinesComponentVisualizer::AddReferencedObjects(FReferenceCollector& Collector)
{
	FSplineComponentVisualizer::AddReferencedObjects(Collector);
	if (RoadSectionSelectionState)
	{
		Collector.AddReferencedObject(RoadSectionSelectionState);
	}
}

bool FMoKuEditSplinesComponentVisualizer::ShouldShowForSelectedSubcomponents(const UActorComponent* Component)
{
	if (const UMoKuEditSplinesComponent* MoKuSpline = Cast<const UMoKuEditSplinesComponent>(Component))
	{
		return MetaLandRoadSectionEditVisualization::IsMainRoadSpline(MoKuSpline);
	}

	return FSplineComponentVisualizer::ShouldShowForSelectedSubcomponents(Component);
}

void FMoKuEditSplinesComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (!Component || !PDI)
	{
		return;
	}

	if (const UMoKuEditSplinesComponent* MoKuSpline = Cast<const UMoKuEditSplinesComponent>(Component))
	{
		if (!MetaLandRoadSectionEditVisualization::IsMainRoadSpline(MoKuSpline))
		{
			return;
		}
	}

	const UMetaLandRoadSplineComponent* SplineComp = Cast<const UMetaLandRoadSplineComponent>(Component);
	if (!SplineComp)
	{
		FSplineComponentVisualizer::DrawVisualization(Component, View, PDI);
		return;
	}

	// Standard: road mesh only, unless Landscape attribute overlay is shown.
	if (FMetaLandRoadEditorModule::Get().IsStandardMode())
	{
		if (FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay()
			&& !FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay())
		{
			DrawLandscapeAttributeOverlay(SplineComp, PDI, View);
		}
		return;
	}

	// Interactive Tool owns the viewport  - W/E/T overlays stay off.
	if (FMetaLandRoadEditorModule::Get().IsInteractiveSplineDrawToolActive())
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	const bool bIsOwnerSelected = MetaLandRoadSectionEditVisualization::IsSplineOwnerSelected(SplineComp);
	const bool bIsComponentSelected = CompVisUtils::IsSelectedInViewport(SplineComp);
	const UMetaLandRoadSplineComponent* EditedSpline = GetEditedMetaLandRoadSpline();
	const bool bIsEditedSpline = EditedSpline == SplineComp;

	const bool bActiveNativeMatch =
		FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline() == SplineComp;
	const bool bShouldDrawRoad =
		bIsOwnerSelected || bIsComponentSelected || bIsEditedSpline || bActiveNativeMatch;

	if (bShouldDrawRoad && SelectionState)
	{
		FComponentPropertyPath SplinePropertyPath(const_cast<UMetaLandRoadSplineComponent*>(SplineComp));
		if (SplinePropertyPath.IsValid()
			&& SelectionState->GetSplinePropertyPath().GetComponent() != SplineComp)
		{
			SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		}

		if (RoadSectionSelectionState && SplinePropertyPath.IsValid())
		{
			// Never call SetSelectedSpline here  - it ResetSelection and clears lane multi-select.
			if (RoadSectionSelectionState->GetSelectedSpline() == SplineComp)
			{
				RoadSectionSelectionState->AssignSelectedSplinePath(SplinePropertyPath);
			}
		}
	}

	// Standard only: engine spline draw. Alt+E must NOT call parent (draws key rings).
	// Alt+W uses DrawNativeEdit* overlays below.
	if (bShouldDrawRoad
		&& !IsSectionLineEditMode()
		&& !FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& !FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
	{
		FSplineComponentVisualizer::DrawVisualization(Component, View, PDI);
	}

	if (!bShouldDrawRoad)
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	if (!IsSectionLineEditMode()
		&& !FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
	{
		const UMetaLandRoadSplineComponent* ActiveNative =
			FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
		if (!ActiveNative || ActiveNative != SplineComp)
		{
			return;
		}

		if (SelectionState)
		{
			const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->SyncEngineSelectionStateForSpline(SplineComp);
		}

		const int32 SelectedSegmentIndex = SelectionState ? SelectionState->GetSelectedSegmentIndex() : INDEX_NONE;
		const TSet<int32>& SelectedKeys = SelectionState
			? SelectionState->GetSelectedKeys()
			: GetSelectedKeys();
		// Point and segment selection are mutually exclusive in Alt+W.
		const int32 EffectiveSelectedSegment =
			SelectedKeys.Num() > 0 ? INDEX_NONE : SelectedSegmentIndex;
		const int32 HoveredSegment = CachedNativeHoveredKey == INDEX_NONE ? CachedNativeHoveredSegment : INDEX_NONE;

		// Custom spline draw + engine HSpline*Proxy hit tests (no FSplineComponentVisualizer::DrawVisualization).
		MetaLandRoadNativeEditVisualization::DrawNativeEditSplineOverlay(
			SplineComp,
			PDI,
			CachedNativeHoveredKey,
			HoveredSegment,
			EffectiveSelectedSegment,
			SelectedKeys);

		MetaLandRoadNativeEditVisualization::DrawNativeEditRingOverlay(
			View,
			CachedNativeHoveredKey,
			SelectedKeys,
			EffectiveSelectedSegment,
			SplineComp,
			PDI);

		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	// Alt+T  - lane boundaries only (no main center line).
	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode())
	{
		if (!bShouldDrawRoad)
		{
			CachedLaneHoveredSectionIndex = INDEX_NONE;
			CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
			CachedLaneHoveredWidthIndex = INDEX_NONE;
			CachedLaneHoveredWidthIsKey = false;
			CachedLaneHoveredSectionBoundaryIndex = INDEX_NONE;
			CachedLaneHoveredBoundaryLine = false;
			CachedLaneHoveredSplineKey = -1.0f;
			return;
		}

		FMetaLandRoadLaneEditHighlight Highlight;
		Highlight.HoveredSectionIndex = CachedLaneHoveredSectionIndex;
		Highlight.HoveredLaneIndex = CachedLaneHoveredLaneIndex;
		Highlight.HoveredWidthIndex = CachedLaneHoveredWidthIndex;
		Highlight.bHoveredWidthIsKey = CachedLaneHoveredWidthIsKey;
		Highlight.bHoveredLaneBoundaryLine = CachedLaneHoveredBoundaryLine;
		// Alt+T: section split lines are display-only  - no hover/selection thick highlight.
		const UMetaLandRoadSplineComponent* HighlightSpline =
			RoadSectionSelectionState ? RoadSectionSelectionState->GetSelectedSpline() : nullptr;
		const bool bHighlightThisSpline =
			RoadSectionSelectionState
			&& (HighlightSpline == SplineComp
				|| FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline() == SplineComp);
		if (bHighlightThisSpline)
		{
			Highlight.SelectedSectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
			Highlight.SelectedLaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
			Highlight.bLaneFacePrimSelected = RoadSectionSelectionState->IsLaneFacePrimSelected();

			const bool bWidthKeySelected =
				RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key;
			const bool bLaneFacePrimSelected = RoadSectionSelectionState->IsLaneFacePrimSelected();

			auto AlreadyInHighlight = [&Highlight](int32 SpanSection, int32 Lane) -> bool
			{
				for (const FMetaLandRoadSelectedLaneRef& Existing : Highlight.SelectedLanes)
				{
					if (Existing.SectionIndex == SpanSection && Existing.LaneIndex == Lane)
					{
						return true;
					}
				}
				return false;
			};

			auto AddHighlightLaneRef = [&](int32 StoredSectionIndex, int32 LaneIndex)
			{
				if (StoredSectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
				{
					return;
				}

				const int32 LaneDataSection = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
					SplineComp, StoredSectionIndex, LaneIndex);
				if (LaneDataSection == INDEX_NONE)
				{
					if (!AlreadyInHighlight(StoredSectionIndex, LaneIndex))
					{
						Highlight.SelectedLanes.Add({ StoredSectionIndex, LaneIndex });
					}
					return;
				}

				// Map lane-data section ids  - span section ids (draw/highlight use span keys).
				for (int32 SpanSectionIndex = 0; SpanSectionIndex < SplineComp->GetLaneSectionsNum(); ++SpanSectionIndex)
				{
					if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(
						SplineComp, SpanSectionIndex, LaneIndex))
					{
						continue;
					}

					const int32 SpanLaneDataSection = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
						SplineComp, SpanSectionIndex, LaneIndex);
					if (SpanLaneDataSection != LaneDataSection)
					{
						continue;
					}

					if (!AlreadyInHighlight(SpanSectionIndex, LaneIndex))
					{
						Highlight.SelectedLanes.Add({ SpanSectionIndex, LaneIndex });
					}
				}
			};

			for (const FMetaLandRoadSelectedLaneRefU& Ref : RoadSectionSelectionState->GetMultiSelectedLanes())
			{
				if (!bWidthKeySelected && bLaneFacePrimSelected)
				{
					AddHighlightLaneRef(Ref.SectionIndex, Ref.LaneIndex);
				}
			}
			if (Highlight.SelectedSectionIndex != INDEX_NONE
				&& Highlight.SelectedLaneIndex != LANE_INDEX_NONE
				&& !bWidthKeySelected
				&& bLaneFacePrimSelected)
			{
				AddHighlightLaneRef(Highlight.SelectedSectionIndex, Highlight.SelectedLaneIndex);
			}

			if (!bWidthKeySelected && Highlight.SelectedLaneIndex != LANE_INDEX_NONE)
			{
				for (const FMetaLandRoadSelectedLaneRef& Ref : Highlight.SelectedLanes)
				{
					if (Ref.LaneIndex == Highlight.SelectedLaneIndex)
					{
						Highlight.SelectedSectionIndex = Ref.SectionIndex;
						break;
					}
				}
			}

			if (bWidthKeySelected)
			{
				Highlight.SelectedWidthIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
			}
		}

		// World-builder lane anchor mouth highlight is drawn only by Spline tool (EditorMode::Render).

		MetaLandRoadLaneVisualization::DrawLaneModeSectionGuides(SplineComp, PDI, Highlight);
		MetaLandRoadLaneVisualization::DrawLaneEditVisualization(SplineComp, PDI, Highlight);

		MetaLandRoadNativeEditVisualization::DrawLaneWidthKeyRingOverlay(
			View,
			SplineComp,
			PDI,
			Highlight,
			CachedLaneHoveredSectionIndex,
			CachedLaneHoveredLaneIndex,
			CachedLaneHoveredWidthIndex);

		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	if (!IsSectionLineEditMode())
	{
		HoveredSectionIndex = INDEX_NONE;
		HoveredBoundarySectionIndex = INDEX_NONE;
		HoveredSplineKey = -1.0f;
		return;
	}

	// Alt+E  - section center lines / boundary handles.
	// Hover comes ONLY from TickEditorViewport / FMetaLandRoadSectionHover::Tick.
	// Never call Viewport->GetHitProxy here: hit-proxy render re-enters DrawVisualization  - stack overflow.
	HoveredSectionIndex = INDEX_NONE;
	HoveredBoundarySectionIndex = INDEX_NONE;
	HoveredSplineKey = -1.0f;
	if (FMetaLandRoadSectionHover::GetLastHoveredBoundarySection() != INDEX_NONE)
	{
		HoveredBoundarySectionIndex = FMetaLandRoadSectionHover::GetLastHoveredBoundarySection();
	}
	else if (FMetaLandRoadSectionHover::GetLastHoveredSection() != INDEX_NONE)
	{
		HoveredSectionIndex = FMetaLandRoadSectionHover::GetLastHoveredSection();
		HoveredSplineKey = FMetaLandRoadSectionHover::GetLastHoveredSplineKey();
	}

	MetaLandRoadSectionEditVisualization::DrawSectionEditLines(
		const_cast<UMetaLandRoadSplineComponent*>(SplineComp),
		PDI,
		RoadSectionSelectionState,
		HoveredSectionIndex);

	// Alt+E: lane boundary guides (display only  - no hover / hit proxies).
	MetaLandRoadLaneVisualization::DrawSectionModeLaneGuides(SplineComp, PDI);

	if (SplineComp->GetLaneSectionsNum() >= 2)
	{
		MetaLandRoadSectionEditVisualization::DrawSectionBoundaryHandles(
			SplineComp,
			PDI,
			RoadSectionSelectionState,
			HoveredBoundarySectionIndex);
	}
}

void FMoKuEditSplinesComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	// Alt+W only  - Alt+E uses 3D PDI lines, no HUD chrome.
	if (!Canvas || !View || !Component
		|| (Cast<const UMoKuEditSplinesComponent>(Component)
			&& !MetaLandRoadSectionEditVisualization::IsMainRoadSpline(Component))
		|| !FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
	{
		return;
	}

	FSplineComponentVisualizer::DrawVisualizationHUD(Component, Viewport, View, Canvas);
}

const UMetaLandRoadSplineComponent* FMoKuEditSplinesComponentVisualizer::UpdateSelectedComponentAndSectionAndLane(HComponentVisProxy* VisProxy)
{
	if (!RoadSectionSelectionState || !VisProxy || !VisProxy->Component.IsValid())
	{
		return nullptr;
	}

	const UMetaLandRoadSplineComponent* NewSplineComp = Cast<const UMetaLandRoadSplineComponent>(VisProxy->Component.Get());
	if (!NewSplineComp)
	{
		return nullptr;
	}

	// Cache proxy indices before DeselectAllExcept  - selection notify can invalidate viewport hit proxies.
	int32 ProxySectionIndex = INDEX_NONE;
	int32 ProxyLaneIndex = LANE_INDEX_NONE;
	bool bIsSectionProxy = false;
	bool bIsWidthSegmentProxy = false;
	if (VisProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType()))
	{
		bIsSectionProxy = true;
		const HMetaLandRoadSectionVisProxy* SectionProxy = static_cast<const HMetaLandRoadSectionVisProxy*>(VisProxy);
		ProxySectionIndex = SectionProxy->SectionIndex;
		bIsWidthSegmentProxy = VisProxy->IsA(HMetaLandRoadLaneWidthSegmentVisProxy::StaticGetType());
		if (VisProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType()))
		{
			ProxyLaneIndex = static_cast<const HMetaLandRoadLaneVisProxy*>(VisProxy)->LaneIndex;
		}
	}

	AActor* OldSplineOwningActor = RoadSectionSelectionState->GetSplinePropertyPath().GetParentOwningActor();
	FComponentPropertyPath NewSplinePropertyPath(NewSplineComp);
	RoadSectionSelectionState->SetSelectedSpline(NewSplinePropertyPath);

	if (OldSplineOwningActor != NewSplinePropertyPath.GetParentOwningActor())
	{
		RoadSectionSelectionState->ResetSelection(true);
	}

	if (NewSplinePropertyPath.IsValid())
	{
		if (RoadSectionSelectionState->GetSelectedSpline() != NewSplineComp)
		{
			RoadSectionSelectionState->ResetCachedData();
		}
	}
	else
	{
		RoadSectionSelectionState->ResetSelection(false);
		return nullptr;
	}

	CompVisUtils::DeselectAllExcept(NewSplineComp);

	if (SelectionState)
	{
		SelectionState->SetSplinePropertyPath(NewSplinePropertyPath);
	}

	// Do not read VisProxy after DeselectAllExcept  - use cached flags/indices only.
	if (bIsSectionProxy)
	{
		if (ProxySectionIndex < 0 || ProxySectionIndex >= NewSplineComp->GetLaneSectionsNum())
		{
			return NewSplineComp;
		}
		RoadSectionSelectionState->SetSelectedSection(ProxySectionIndex);

		if (!IsSectionLineEditMode()
			&& ProxyLaneIndex != LANE_INDEX_NONE
			&& !bIsWidthSegmentProxy)
		{
			const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
				NewSplineComp, ProxySectionIndex, ProxyLaneIndex);
			if (LaneSectionIndex != INDEX_NONE)
			{
				const FMetaLandRoadLaneSection& Section = NewSplineComp->GetLaneSection(LaneSectionIndex);
				if (Section.CheckLaneIndex(ProxyLaneIndex))
				{
					RoadSectionSelectionState->SetSelectedLane(ProxyLaneIndex);
				}
			}
		}
	}

	return NewSplineComp;
}

bool FMoKuEditSplinesComponentVisualizer::TryCompleteNativeEditPointSelection(
	HComponentVisProxy* VisProxy,
	FEditorViewportClient* InViewportClient,
	const USplineComponent* SplineComp)
{
	if (!SelectionState || !VisProxy || !SplineComp)
	{
		return false;
	}

	if (SelectionState->GetLastKeyIndexSelected() == INDEX_NONE)
	{
		SelectionState->SetSplinePropertyPath(FComponentPropertyPath());
		return false;
	}

	SelectionState->ClearSelectedSegmentIndex();
	SelectionState->SetCachedRotation(
		SplineComp->GetQuaternionAtSplinePoint(
			SelectionState->GetLastKeyIndexSelected(), ESplineCoordinateSpace::World));

	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ResetSelection(true);
	}

	if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
			const_cast<UActorComponent*>(VisProxy->Component.Get())))
	{
		SetActiveForSpline(MetaSpline);
		EnsureNativeEditTranslateWidget(InViewportClient);
	}

	return true;
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleNativeEditMousePress(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport)
{
	if (!FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay()
		|| IsSectionLineEditMode()
		|| FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		|| !ViewportClient
		|| !Viewport
		|| !SelectionState)
	{
		return false;
	}

	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);
	if (MetaLandRoadLaneEditPick::ShouldDeferInputToTranslateWidget(
		ViewportClient, Viewport, MousePos.X, MousePos.Y))
	{
		return false;
	}

	UMetaLandRoadSplineComponent* MetaSpline = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	if (!MetaSpline)
	{
		MetaSpline = GetEditedMetaLandRoadSpline();
	}
	if (!MetaSpline)
	{
		return false;
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	int32 KeyAtMouse = CachedNativeHoveredKey;
	if (KeyAtMouse == INDEX_NONE)
	{
		KeyAtMouse = MetaLandRoadNativeEditVisualization::PickHoveredControlPointKey(
			MetaSpline,
			View,
			MousePos,
			MetaLandRoadNativeEditVisualization::NativeEditKeyPickRadiusPx);
	}
	if (KeyAtMouse == INDEX_NONE)
	{
		return false;
	}

	const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
	if (SelectedKeys.Num() == 1 && SelectedKeys.Contains(KeyAtMouse))
	{
		SetActiveForSpline(MetaSpline);
		ActivateRoadVisualization();
		EnsureNativeEditTranslateWidget(ViewportClient);
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SelectSplinePointNative", "Select Spline Point"));
	SelectionState->Modify();

	FComponentPropertyPath SplinePropertyPath(MetaSpline);
	if (SplinePropertyPath.IsValid())
	{
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
	}

	ChangeSelectionState(KeyAtMouse, ViewportClient->IsCtrlPressed());
	SelectionState->ClearSelectedSegmentIndex();
	SelectionState->ClearSelectedTangentHandle();

	HMetaLandRoadSplineKeyVisProxy KeyProxy(MetaSpline, KeyAtMouse, HPP_Foreground);
	return TryCompleteNativeEditPointSelection(&KeyProxy, ViewportClient, MetaSpline);
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleNativeEditClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay()
		|| !VisProxy
		|| !VisProxy->Component.IsValid()
		|| !SelectionState
		|| (Click.GetKey() != EKeys::LeftMouseButton && Click.GetKey() != EKeys::RightMouseButton))
	{
		return false;
	}

	int32 KeyAtMouse = CachedNativeHoveredKey;
	if (KeyAtMouse == INDEX_NONE
		&& InViewportClient
		&& InViewportClient->Viewport)
	{
		if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
				const_cast<UActorComponent*>(VisProxy->Component.Get())))
		{
			FIntPoint MousePos;
			InViewportClient->Viewport->GetMousePos(MousePos);
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				InViewportClient->Viewport,
				InViewportClient->GetScene(),
				InViewportClient->EngineShowFlags));
			if (FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily))
			{
				KeyAtMouse = MetaLandRoadNativeEditVisualization::PickHoveredControlPointKey(
					MetaSpline,
					View,
					MousePos,
					MetaLandRoadNativeEditVisualization::NativeEditKeyPickRadiusPx);
			}
		}
	}

	if (KeyAtMouse != INDEX_NONE)
	{
		if (Click.GetKey() == EKeys::LeftMouseButton)
		{
			const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
			if (SelectedKeys.Num() == 1 && SelectedKeys.Contains(KeyAtMouse))
			{
				if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
						const_cast<UActorComponent*>(VisProxy->Component.Get())))
				{
					SetActiveForSpline(MetaSpline);
					ActivateRoadVisualization();
					EnsureNativeEditTranslateWidget(InViewportClient);
				}
				return false;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("SelectSplinePointNative", "Select Spline Point"));
		SelectionState->Modify();
		if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
		{
			const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
			if (Click.GetKey() != EKeys::RightMouseButton || !SelectedKeys.Contains(KeyAtMouse))
			{
				ChangeSelectionState(KeyAtMouse, InViewportClient && InViewportClient->IsCtrlPressed());
			}
			SelectionState->ClearSelectedSegmentIndex();
			SelectionState->ClearSelectedTangentHandle();
			return TryCompleteNativeEditPointSelection(VisProxy, InViewportClient, SplineComp);
		}
		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadSplineKeyVisProxy::StaticGetType()))
	{
		const HMetaLandRoadSplineKeyVisProxy* KeyProxy =
			static_cast<const HMetaLandRoadSplineKeyVisProxy*>(VisProxy);
		if (KeyProxy->KeyIndex == INDEX_NONE)
		{
			return false;
		}

		if (Click.GetKey() == EKeys::LeftMouseButton)
		{
			const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
			if (SelectedKeys.Num() == 1 && SelectedKeys.Contains(KeyProxy->KeyIndex))
			{
				return false;
			}

			const FScopedTransaction Transaction(LOCTEXT("SelectSplinePointNative", "Select Spline Point"));
			SelectionState->Modify();
			if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
			{
				ChangeSelectionState(KeyProxy->KeyIndex, InViewportClient && InViewportClient->IsCtrlPressed());
				SelectionState->ClearSelectedSegmentIndex();
				SelectionState->ClearSelectedTangentHandle();
				return TryCompleteNativeEditPointSelection(VisProxy, InViewportClient, SplineComp);
			}
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SelectSplinePointNative", "Select Spline Point"));
		SelectionState->Modify();

		if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
		{
			const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
			if (Click.GetKey() != EKeys::RightMouseButton || !SelectedKeys.Contains(KeyProxy->KeyIndex))
			{
				ChangeSelectionState(KeyProxy->KeyIndex, InViewportClient->IsCtrlPressed());
			}
			SelectionState->ClearSelectedSegmentIndex();
			SelectionState->ClearSelectedTangentHandle();
			return TryCompleteNativeEditPointSelection(VisProxy, InViewportClient, SplineComp);
		}

		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadSplineSegmentVisProxy::StaticGetType()))
	{
		const HMetaLandRoadSplineSegmentVisProxy* SegProxy =
			static_cast<const HMetaLandRoadSplineSegmentVisProxy*>(VisProxy);
		if (SegProxy->SegmentIndex == INDEX_NONE)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SelectSplineSegmentNative", "Select Spline Segment"));
		SelectionState->Modify();

		if (const USplineComponent* SplineComp = UpdateSelectedSplineComponent(VisProxy))
		{
			SelectionState->ClearSelectedKeys();
			SelectionState->ClearSelectedTangentHandle();
			SelectionState->SetLastKeyIndexSelected(INDEX_NONE);
			SelectionState->SetSelectedSegmentIndex(SegProxy->SegmentIndex);

			constexpr int32 NumSubdivisions = 16;
			const int32 SelectedSegmentIndex = SegProxy->SegmentIndex;
			float SubsegmentStartKey = static_cast<float>(SelectedSegmentIndex);
			FVector SubsegmentStart = SplineComp->GetLocationAtSplineInputKey(
				SubsegmentStartKey, ESplineCoordinateSpace::World);
			double ClosestDistance = TNumericLimits<double>::Max();
			FVector BestLocation = SubsegmentStart;
			for (int32 Step = 1; Step < NumSubdivisions; ++Step)
			{
				const float SubsegmentEndKey = static_cast<float>(SelectedSegmentIndex)
					+ static_cast<float>(Step) / static_cast<float>(NumSubdivisions);
				const FVector SubsegmentEnd = SplineComp->GetLocationAtSplineInputKey(
					SubsegmentEndKey, ESplineCoordinateSpace::World);
				FVector SplineClosest;
				FVector RayClosest;
				FMath::SegmentDistToSegmentSafe(
					SubsegmentStart,
					SubsegmentEnd,
					Click.GetOrigin(),
					Click.GetOrigin() + Click.GetDirection() * 50000.0f,
					SplineClosest,
					RayClosest);
				const double Distance = FVector::DistSquared(SplineClosest, RayClosest);
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					BestLocation = SplineClosest;
				}
				SubsegmentStartKey = SubsegmentEndKey;
				SubsegmentStart = SubsegmentEnd;
			}

			SelectionState->SetSelectedSplinePosition(BestLocation);

			if (UMetaLandRoadSplineComponent* MetaSpline = Cast<UMetaLandRoadSplineComponent>(
					const_cast<UActorComponent*>(VisProxy->Component.Get())))
			{
				SetActiveForSpline(MetaSpline);
				EnsureNativeEditTranslateWidget(InViewportClient);
			}
			return true;
		}

		return false;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleEngineSplineEditClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !MetaLandRoadSectionEditVisualization::IsEngineSplineEditProxy(VisProxy))
	{
		return false;
	}

	const bool bHandled = FSplineComponentVisualizer::VisProxyHandleClick(InViewportClient, VisProxy, Click);
	if (bHandled)
	{
		if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
		{
			if (UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(
					const_cast<UActorComponent*>(VisProxy->Component.Get())))
			{
				SetActiveForSpline(SplineComp);
			}
		}

		if (RoadSectionSelectionState)
		{
			const FScopedTransaction Transaction(LOCTEXT("SelectSplinePoint", "Select Spline Point"));
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->ResetSelection(true);
		}
	}

	return bHandled;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleMetaLandRoadProxyClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->Component.IsValid()
		|| !MetaLandRoadSectionEditVisualization::IsMetaLandRoadVisProxy(VisProxy))
	{
		return false;
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode() && Click.GetKey() != EKeys::LeftMouseButton)
	{
		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadLaneWidthSegmentVisProxy::StaticGetType()))
	{
		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType()))
	{
		const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionLane", "Select Road Lane"));
		RoadSectionSelectionState->Modify();
		HMetaLandRoadLaneVisProxy* Proxy = static_cast<HMetaLandRoadLaneVisProxy*>(VisProxy);
		if (const UMetaLandRoadSplineComponent* SplineComp = UpdateSelectedComponentAndSectionAndLane(VisProxy))
		{
			if (SelectionState)
			{
				SelectionState->ClearSelectedKeys();
			}

			const bool bBoundaryKey = VisProxy->IsA(HMetaLandRoadSectionKeyVisProxy::StaticGetType());
			if (bBoundaryKey)
			{
				RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
				RoadSectionSelectionState->SetCachedDataAtSectionStart(Proxy->SectionIndex);
				RoadSectionSelectionState->SetSectionBoundarySelected(true);
			}
			else if (IsSectionLineEditMode() || Proxy->LaneIndex == LANE_INDEX_NONE)
			{
				const float Key = FMetaLandRoadSectionHover::GetLastHoveredSplineKey() >= 0.0f
					? FMetaLandRoadSectionHover::GetLastHoveredSplineKey()
					: SplineComp->FindInputKeyClosestToWorldLocation(
						Click.GetOrigin() + Click.GetDirection() * 5000.0f);
				if (IsSectionLineEditMode())
				{
					SelectMainLineForPathDrag(Proxy->SectionIndex, Key);
				}
				else
				{
					RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
					RoadSectionSelectionState->RecordPickSplineKey(Key);
				}
			}
			else
			{
				const bool bCtrlDown =
					FSlateApplication::IsInitialized()
					&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
				// Alt+T: lane select / menu only with Ctrl (plain LMB/RMB must not pick a lane).
				if (FMetaLandRoadEditorModule::Get().IsLaneEditMode() && !bCtrlDown)
				{
					return false;
				}

				if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
				{
					RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
				}

				if (FMetaLandRoadEditorModule::Get().IsLaneEditMode() && bCtrlDown
					&& Click.GetKey() != EKeys::RightMouseButton)
				{
					RoadSectionSelectionState->ToggleLaneInMultiSelection(Proxy->SectionIndex, Proxy->LaneIndex);
				}
				else
				{
					RoadSectionSelectionState->SetSelectedSection(Proxy->SectionIndex);
					RoadSectionSelectionState->SetPrimaryLaneSelection(Proxy->SectionIndex, Proxy->LaneIndex, false);
				}
				RoadSectionSelectionState->SetSectionBoundarySelected(false);

				const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
					SplineComp->GetLaneRange(Proxy->SectionIndex, Proxy->LaneIndex);
				if (Range.EndS > Range.StartS + KINDA_SMALL_NUMBER)
				{
					const float Key = SplineComp->ClosestKeyToSegment2(
						static_cast<float>(Range.StartS),
						static_cast<float>(Range.EndS),
						Click.GetOrigin(),
						Click.GetOrigin() + Click.GetDirection() * 50000.0f);
					RoadSectionSelectionState->RecordPickSplineKey(Key);
				}
				else
				{
					RoadSectionSelectionState->RecordPickSplineKey(
						SplineComp->GetInputKeyValueAtDistanceAlongSpline(Range.StartS));
				}
			}

			ActivateRoadVisualization();
			GEditor->RedrawLevelEditingViewports(true);
			return true;
		}

		return false;
	}

	if (VisProxy->IsA(HMetaLandRoadSectionVisProxy::StaticGetType())
		|| VisProxy->IsA(HMetaLandRoadSplineVisProxy::StaticGetType()))
	{
		const FScopedTransaction Transaction(LOCTEXT("UnselectRoad", "Unselect Road"));
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ResetSelection(false);
		GEditor->RedrawLevelEditingViewports(true);
		return true;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::ClearLaneEditSelection()
{
	if (!RoadSectionSelectionState)
	{
		return false;
	}

	const EMetaLandRoadSectionSelectionState Verified = RoadSectionSelectionState->GetStateVerified();
	const bool bHadKey = Verified >= EMetaLandRoadSectionSelectionState::Key;
	const bool bHadLane =
		Verified >= EMetaLandRoadSectionSelectionState::Lane
		|| RoadSectionSelectionState->GetMultiSelectedLanes().Num() > 0
		|| RoadSectionSelectionState->GetSelectedLaneIndex() != LANE_INDEX_NONE;
	if (!bHadKey && !bHadLane)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("ClearLaneEditSelection", "Clear Lane Selection"));
	RoadSectionSelectionState->Modify();
	if (bHadKey)
	{
		RoadSectionSelectionState->ClearWidthKeySelection();
	}
	RoadSectionSelectionState->SetLaneFacePrimSelected(false);
	RoadSectionSelectionState->SetLaneWidthMenuContext(false);
	RoadSectionSelectionState->ClearLaneMultiSelection();
	if (RoadSectionSelectionState->GetSelectedLaneIndex() != LANE_INDEX_NONE)
	{
		RoadSectionSelectionState->SetSelectedLane(LANE_INDEX_NONE);
	}
	RoadSectionSelectionState->SetSectionBoundarySelected(false);
	CachedLaneHoveredSectionIndex = INDEX_NONE;
	CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	CachedLaneHoveredWidthIndex = INDEX_NONE;
	CachedLaneHoveredWidthIsKey = false;
	CachedLaneHoveredSectionBoundaryIndex = INDEX_NONE;
	CachedLaneHoveredBoundaryLine = false;
	CachedLaneHoveredSplineKey = -1.0f;
	ActivateRoadVisualization();
	GEditor->RedrawLevelEditingViewports(true);
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::SelectLaneFaceFromScreenPick(
	UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	int32 LaneIndex,
	const FViewportClick& Click,
	bool bLaneFaceFill)
{
	if (!RoadSectionSelectionState || !SplineComp || SectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
	{
		return false;
	}

	const bool bCtrlDown =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
	const bool bRightClick = Click.GetKey() == EKeys::RightMouseButton;

	// Alt+T: lane face select / menu requires Ctrl (plain LMB/RMB must not pick).
	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode() && !bCtrlDown)
	{
		return false;
	}

	if (!MetaLandRoadLaneEditPick::IsLaneValidOnSpanSection(SplineComp, SectionIndex, LaneIndex))
	{
		return false;
	}

	// Selection/highlight keys use span section id (DrawLaneEditVisualization loop).
	// LaneIndex stays signed: Left < 0 (green), Right > 0 (red). Data section resolves on demand.
	const int32 SelectSectionIndex = SectionIndex;

	const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionLane", "Select Road Lane"));
	RoadSectionSelectionState->Modify();

	FComponentPropertyPath NewSplinePropertyPath(SplineComp);
	// Do not call SetSelectedSpline every pick  - it ResetSelection and wipes multi-select.
	if (RoadSectionSelectionState->GetSelectedSpline() != SplineComp || !NewSplinePropertyPath.IsValid()
		|| RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Component)
	{
		RoadSectionSelectionState->SetSelectedSpline(NewSplinePropertyPath);
		CompVisUtils::DeselectAllExcept(SplineComp);
	}

	if (SelectionState && NewSplinePropertyPath.IsValid())
	{
		SelectionState->SetSplinePropertyPath(NewSplinePropertyPath);
		SelectionState->ClearSelectedKeys();
	}

	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		RoadSectionSelectionState->SetSelectedSection(SelectSectionIndex);
	}

	RoadSectionSelectionState->ClearWidthKeySelection();
	RoadSectionSelectionState->SetLaneWidthMenuContext(false);

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode() && bCtrlDown && !bRightClick)
	{
		RoadSectionSelectionState->ToggleLaneInMultiSelection(SelectSectionIndex, LaneIndex);
	}
	else
	{
		RoadSectionSelectionState->SetSelectedSection(SelectSectionIndex);
		RoadSectionSelectionState->SetPrimaryLaneSelection(SelectSectionIndex, LaneIndex, false);
	}
	RoadSectionSelectionState->SetLaneFacePrimSelected(bLaneFaceFill);
	RoadSectionSelectionState->SetSectionBoundarySelected(false);

	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		SplineComp->GetLaneRange(SelectSectionIndex, LaneIndex);
	if (Range.EndS > Range.StartS + KINDA_SMALL_NUMBER)
	{
		const float Key = SplineComp->ClosestKeyToSegment2(
			static_cast<float>(Range.StartS),
			static_cast<float>(Range.EndS),
			Click.GetOrigin(),
			Click.GetOrigin() + Click.GetDirection() * 50000.0f);
		RoadSectionSelectionState->RecordPickSplineKey(Key);
	}
	else
	{
		RoadSectionSelectionState->RecordPickSplineKey(
			SplineComp->GetInputKeyValueAtDistanceAlongSpline(
				SplineComp->GetLaneSection(SelectSectionIndex).SOffset));
	}

	ActivateRoadVisualization();
	GEditor->RedrawLevelEditingViewports(true);
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleLaneEditMousePress(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport)
{
	if (!FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		|| !ViewportClient
		|| !Viewport
		|| !RoadSectionSelectionState)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || SplineComp->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);

	if (MetaLandRoadLaneEditPick::ShouldDeferInputToTranslateWidget(
		ViewportClient, Viewport, MousePos.X, MousePos.Y))
	{
		return false;
	}

	const bool bCtrlDown =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsControlDown();

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	int32 PrimarySectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	int32 PrimaryLaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
	int32 PrimaryWidthIndex = INDEX_NONE;
	TArray<FMetaLandRoadSelectedLaneRefU> MultiSelectedLanes = RoadSectionSelectionState->GetMultiSelectedLanes();
	if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
	{
		PrimaryWidthIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
	}

	const bool bKeySelected =
		RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key;

	auto DeselectWidthKeyOnly = [&]() -> bool
	{
		if (!bKeySelected)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("DeselectWidthKey", "Deselect Width Key"));
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->ClearWidthKeySelection();
		ActivateRoadVisualization();
		GEditor->RedrawLevelEditingViewports(true);
		return true;
	};

	auto IsMouseNearSelectedWidthKeyGizmo = [&](float RadiusPx) -> bool
	{
		if (!bKeySelected)
		{
			return false;
		}

		FVector2D WidgetScreen;
		if (!View->WorldToPixel(RoadSectionSelectionState->GetCachedPosition(), WidgetScreen))
		{
			return false;
		}

		return FVector2D::DistSquared(FVector2D(MousePos.X, MousePos.Y), WidgetScreen)
			<= FMath::Square(RadiusPx);
	};

	auto IsSameSelectedWidthKey = [&](int32 SectionIndex, int32 LaneIndex, int32 WidthIndex) -> bool
	{
		return bKeySelected
			&& SectionIndex == PrimarySectionIndex
			&& LaneIndex == PrimaryLaneIndex
			&& WidthIndex == PrimaryWidthIndex;
	};

	// Ctrl+LMB: lane face first, then deselect already-selected width key only.
	if (bCtrlDown)
	{
		FMetaLandRoadLaneFacePickResult FacePick;
		if (MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *View, MousePos, FacePick))
		{
			const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
			return SelectLaneFaceFromScreenPick(SplineComp, FacePick.SectionIndex, FacePick.LaneIndex, Click);
		}

		if (HMetaLandRoadLaneVisProxy* LaneProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneVisProxy>(
			Viewport, MousePos.X, MousePos.Y, MetaLandRoadLaneEditPick::LaneFaceClickRadiusPx))
		{
			if (LaneProxy->Component.Get() == SplineComp && LaneProxy->LaneIndex != LANE_INDEX_NONE)
			{
				const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
				return SelectLaneFaceFromScreenPick(
					SplineComp, LaneProxy->SectionIndex, LaneProxy->LaneIndex, Click);
			}
		}

		FMetaLandRoadLaneWidthSegmentPickResult WidthPick;
		if (MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
			SplineComp,
			*View,
			MousePos,
			PrimarySectionIndex,
			PrimaryLaneIndex,
			PrimaryWidthIndex,
			MultiSelectedLanes,
			CachedLaneHoveredSectionIndex,
			CachedLaneHoveredLaneIndex,
			static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx),
			WidthPick)
			&& IsSameSelectedWidthKey(WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex))
		{
			return DeselectWidthKeyOnly();
		}

		// Missed lane face  - do not consume Ctrl+LMB (keep actor / editor selection working).
		return false;
	}

	// Width-key gizmo: defer only when re-clicking the same key near the widget (ring pick wins otherwise).
	FMetaLandRoadLaneWidthSegmentPickResult WidthPick;
	const bool bPickedWidthKey = MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
		SplineComp,
		*View,
		MousePos,
		PrimarySectionIndex,
		PrimaryLaneIndex,
		PrimaryWidthIndex,
		MultiSelectedLanes,
		CachedLaneHoveredSectionIndex,
		CachedLaneHoveredLaneIndex,
		static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx),
		WidthPick);

	if (bPickedWidthKey)
	{
		if (IsSameSelectedWidthKey(WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex))
		{
			constexpr float WidthKeyGizmoPriorityRadiusPx = 20.0f;
			if (IsMouseNearSelectedWidthKeyGizmo(WidthKeyGizmoPriorityRadiusPx))
			{
				return false;
			}
			return false;
		}

		HMetaLandRoadLaneWidthKeyVisProxy WidthKeyProxy(
			SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex, HPP_Foreground);
		const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
		return VisProxyHandleLaneWidthClick(ViewportClient, &WidthKeyProxy, Click);
	}

	if (bKeySelected && IsMouseNearSelectedWidthKeyGizmo(20.0f))
	{
		return false;
	}

	if (!bKeySelected
		&& MetaLandRoadLaneEditPick::PickLaneWidthSegmentAtMouse(
		SplineComp,
		*View,
		MousePos,
		static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthSegmentClickRadiusPx),
		WidthPick))
	{
		HMetaLandRoadLaneWidthSegmentVisProxy WidthSegProxy(
			SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex, HPP_Foreground);
		const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
		return VisProxyHandleLaneWidthClick(ViewportClient, &WidthSegProxy, Click);
	}

	if (!bKeySelected
		&& !bCtrlDown
		&& MetaLandRoadLaneEditPick::PickLaneBoundaryOnSectionAtMouse(
			SplineComp,
			*View,
			MousePos,
			static_cast<float>(MetaLandRoadLaneEditPick::SectionBoundaryHoverRadiusPx),
			WidthPick))
	{
		if (MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
			SplineComp,
			*View,
			MousePos,
			WidthPick.SectionIndex,
			WidthPick.LaneIndex,
			INDEX_NONE,
			{},
			WidthPick.SectionIndex,
			WidthPick.LaneIndex,
			static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx),
			WidthPick))
		{
			HMetaLandRoadLaneWidthKeyVisProxy WidthKeyProxy(
				SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex, HPP_Foreground);
			const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
			return VisProxyHandleLaneWidthClick(ViewportClient, &WidthKeyProxy, Click);
		}

		if (MetaLandRoadLaneEditPick::PickLaneWidthSegmentAtMouse(
			SplineComp,
			*View,
			MousePos,
			static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthSegmentClickRadiusPx),
			WidthPick))
		{
			HMetaLandRoadLaneWidthSegmentVisProxy WidthSegProxy(
				SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex, HPP_Foreground);
			const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
			return VisProxyHandleLaneWidthClick(ViewportClient, &WidthSegProxy, Click);
		}

		HMetaLandRoadLaneWidthSegmentVisProxy WidthSegProxy(
			SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, 0, HPP_Foreground);
		const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
		return VisProxyHandleLaneWidthClick(ViewportClient, &WidthSegProxy, Click);
	}

	if (HMetaLandRoadLaneWidthKeyVisProxy* WidthKeyProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneWidthKeyVisProxy>(
		Viewport, MousePos.X, MousePos.Y, MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx))
	{
		if (WidthKeyProxy->Component.Get() == SplineComp)
		{
			if (IsSameSelectedWidthKey(WidthKeyProxy->SectionIndex, WidthKeyProxy->LaneIndex, WidthKeyProxy->WidthIndex))
			{
				return false;
			}

			const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
			return VisProxyHandleLaneWidthClick(ViewportClient, WidthKeyProxy, Click);
		}
	}

	if (!bKeySelected)
	{
		if (HMetaLandRoadLaneWidthSegmentVisProxy* WidthSegProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneWidthSegmentVisProxy>(
			Viewport, MousePos.X, MousePos.Y, MetaLandRoadLaneEditPick::LaneWidthSegmentClickRadiusPx))
		{
			if (WidthSegProxy->Component.Get() == SplineComp)
			{
				const FViewportClick Click(View, ViewportClient, EKeys::LeftMouseButton, IE_Pressed, MousePos.X, MousePos.Y);
				return VisProxyHandleLaneWidthClick(ViewportClient, WidthSegProxy, Click);
			}
		}
	}

	if (bKeySelected)
	{
		// Gizmo / axis clicks already returned above; blank LMB clears width-key selection.
		return ClearLaneEditSelection();
	}

	// Blank LMB (no modifiers): clear lane face selection.
	return ClearLaneEditSelection();
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleLaneWidthClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		|| !RoadSectionSelectionState
		|| !VisProxy
		|| !VisProxy->Component.IsValid()
		|| Click.GetKey() != EKeys::LeftMouseButton)
	{
		return false;
	}

	const bool bCtrlDown =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsControlDown();

	const UMetaLandRoadSplineComponent* SplineComp = Cast<const UMetaLandRoadSplineComponent>(VisProxy->Component.Get());
	if (!SplineComp)
	{
		return false;
	}

	auto ResolveLaneSection = [&](int32 SpanSectionIndex, int32 LaneIndex) -> int32
	{
		return MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(SplineComp, SpanSectionIndex, LaneIndex);
	};

	if (VisProxy->IsA(HMetaLandRoadLaneWidthKeyVisProxy::StaticGetType())
		|| VisProxy->IsA(HMetaLandRoadLaneWidthSegmentVisProxy::StaticGetType()))
	{
		HMetaLandRoadLaneWidthSegmentVisProxy* Proxy = static_cast<HMetaLandRoadLaneWidthSegmentVisProxy*>(VisProxy);
		if (!SplineComp->GetLaneSections().IsValidIndex(Proxy->SectionIndex)
			|| Proxy->LaneIndex == LANE_INDEX_NONE)
		{
			return false;
		}

		const int32 LaneSectionIndex = ResolveLaneSection(Proxy->SectionIndex, Proxy->LaneIndex);
		if (LaneSectionIndex == INDEX_NONE)
		{
			return false;
		}

		const FMetaLandRoadLaneSection& LaneSection = SplineComp->GetLaneSection(LaneSectionIndex);
		if (!LaneSection.CheckLaneIndex(Proxy->LaneIndex))
		{
			return false;
		}

		const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(Proxy->LaneIndex);
		const int32 NumKeys = Lane.Width.GetNumKeys();
		const int32 NumSegments = FMath::Max(NumKeys, 1);
		const bool bKeyPick = VisProxy->IsA(HMetaLandRoadLaneWidthKeyVisProxy::StaticGetType());

		// Ctrl+LMB: only allow deselecting the already-selected width key; no segment/key select or add.
		if (bCtrlDown)
		{
			if (!bKeyPick)
			{
				return false;
			}

			if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key
				&& RoadSectionSelectionState->GetSelectedSectionIndex() == Proxy->SectionIndex
				&& RoadSectionSelectionState->GetSelectedLaneIndex() == Proxy->LaneIndex
				&& RoadSectionSelectionState->GetSelectedKeyIndex() == Proxy->WidthIndex)
			{
				const FScopedTransaction Transaction(LOCTEXT("DeselectWidthKeyProxy", "Deselect Width Key"));
				RoadSectionSelectionState->Modify();
				RoadSectionSelectionState->ClearWidthKeySelection();
				ActivateRoadVisualization();
				GEditor->RedrawLevelEditingViewports(true);
				return true;
			}

			return false;
		}

		if (bKeyPick)
		{
			if (!Lane.Width.Keys.IsValidIndex(Proxy->WidthIndex))
			{
				return false;
			}

			// Already selected: keep for translate gizmo drag (deselect is Ctrl+LMB in TryHandleLaneEditMousePress).
			if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key
				&& RoadSectionSelectionState->GetSelectedSectionIndex() == Proxy->SectionIndex
				&& RoadSectionSelectionState->GetSelectedLaneIndex() == Proxy->LaneIndex
				&& RoadSectionSelectionState->GetSelectedKeyIndex() == Proxy->WidthIndex)
			{
				return false;
			}
		}
		else if (Proxy->WidthIndex < 0 || Proxy->WidthIndex >= NumSegments)
		{
			return false;
		}
		else if (NumKeys > 0 && !Lane.Width.Keys.IsValidIndex(Proxy->WidthIndex))
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionLaneWidthKey", "Select Road Lane Width Key"));
		RoadSectionSelectionState->Modify();

		if (!UpdateSelectedComponentAndSectionAndLane(VisProxy))
		{
			return false;
		}

		if (SelectionState)
		{
			SelectionState->ClearSelectedKeys();
		}

		RoadSectionSelectionState->SetWidthKeySelection(
			Proxy->SectionIndex,
			Proxy->LaneIndex,
			bKeyPick ? Proxy->WidthIndex : (NumKeys > 0 ? Proxy->WidthIndex : INDEX_NONE));
		RoadSectionSelectionState->SetSectionBoundarySelected(false);

		CachedLaneHoveredSectionIndex = INDEX_NONE;
		CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
		CachedLaneHoveredWidthIndex = INDEX_NONE;
		CachedLaneHoveredWidthIsKey = false;
		CachedLaneHoveredSectionBoundaryIndex = INDEX_NONE;
		CachedLaneHoveredBoundaryLine = false;
		CachedLaneHoveredSplineKey = -1.0f;

		const double LaneStartS = Lane.GetStartOffset();
		const double LaneEndS = Lane.GetEndOffset();
		double SegS0 = LaneStartS;
		double SegS1 = LaneEndS;
		if (NumKeys > 0)
		{
			SegS0 = LaneStartS + Lane.Width.Keys[Proxy->WidthIndex].Time;
			SegS1 = (Proxy->WidthIndex + 1 < NumKeys)
				? LaneStartS + Lane.Width.Keys[Proxy->WidthIndex + 1].Time
				: LaneEndS;
		}

		if (bKeyPick && NumKeys > 0)
		{
			const double KeyS = LaneStartS + Lane.Width.Keys[Proxy->WidthIndex].Time;
			RoadSectionSelectionState->SetCachedDataAtLane(Proxy->SectionIndex, Proxy->LaneIndex, KeyS, 1.0);
			RoadSectionSelectionState->SetLastPickSplineKey(
				SplineComp->GetInputKeyValueAtDistanceAlongSpline(KeyS));
		}
		else
		{
			double PickS = SegS0;
			if (SegS1 > SegS0 + KINDA_SMALL_NUMBER)
			{
				const float InputKey = SplineComp->ClosestKeyToSegment2(
					static_cast<float>(SegS0),
					static_cast<float>(SegS1),
					Click.GetOrigin(),
					Click.GetOrigin() + Click.GetDirection() * 50000.0f);
				PickS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(InputKey);
			}

			RoadSectionSelectionState->SetCachedDataAtLane(
				Proxy->SectionIndex, Proxy->LaneIndex, PickS, 1.0);
			RoadSectionSelectionState->SetLastPickSplineKey(
				SplineComp->GetInputKeyValueAtDistanceAlongSpline(PickS));
		}

		ActivateRoadVisualization();
		if (GCurrentLevelEditingViewportClient)
		{
			GCurrentLevelEditingViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
		}
		GEditor->RedrawLevelEditingViewports(true);
		return true;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (TryHandleLandscapeOverlayVisProxyClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& Click.GetKey() == EKeys::LeftMouseButton
		&& InViewportClient
		&& InViewportClient->Viewport)
	{
		const FIntPoint ClickPos = Click.GetClickPos();
		if (MetaLandRoadLaneEditPick::ShouldDeferInputToTranslateWidget(
			InViewportClient, InViewportClient->Viewport, ClickPos.X, ClickPos.Y))
		{
			return false;
		}
	}

	if (VisProxyHandleNativeEditClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	// Alt+E: never route engine key/segment proxies (Add/Delete/Duplicate Point = Alt+W only).
	if ((IsSectionLineEditMode() || FMetaLandRoadEditorModule::Get().IsLaneEditMode())
		&& VisProxy
		&& MetaLandRoadSectionEditVisualization::IsEngineSplineEditProxy(VisProxy))
	{
		return false;
	}

	if (VisProxyHandleEngineSplineEditClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	// While a width key (+ gizmo) is selected, ignore proxy clicks that would steal selection.
	// Deselect / switch keys is handled in TryHandleLaneEditMousePress (far empty / other key).
	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& Click.GetKey() == EKeys::LeftMouseButton
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
	{
		if (InViewportClient && InViewportClient->Viewport)
		{
			const FIntPoint ClickPos = Click.GetClickPos();
			if (MetaLandRoadLaneEditPick::ShouldDeferInputToTranslateWidget(
				InViewportClient, InViewportClient->Viewport, ClickPos.X, ClickPos.Y))
			{
				return false;
			}
		}

		if (VisProxy && VisProxy->IsA(HMetaLandRoadLaneWidthKeyVisProxy::StaticGetType()))
		{
			if (VisProxyHandleLaneWidthClick(InViewportClient, VisProxy, Click))
			{
				return true;
			}
		}
		return false;
	}

	if (VisProxyHandleLaneWidthClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	if (VisProxyHandleMetaLandRoadProxyClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	// Alt+T: RMB opens lane menu. Ctrl+RMB also picks the hovered face first.
	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode() && Click.GetKey() == EKeys::RightMouseButton)
	{
		const bool bCtrlDown =
			FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
		if (bCtrlDown && TryHandleLaneEditPickClick(InViewportClient, VisProxy, Click, true))
		{
			return true;
		}
		EnsureLaneEditMenuSelection();
		return FinishPickContextMenu(InViewportClient, true);
	}

	return FSplineComponentVisualizer::VisProxyHandleClick(InViewportClient, VisProxy, Click);
}

int32 FMoKuEditSplinesComponentVisualizer::GetSelectedSectionIndex() const
{
	return RoadSectionSelectionState ? RoadSectionSelectionState->GetSelectedSectionIndex() : INDEX_NONE;
}

UMetaLandRoadSplineComponent* FMoKuEditSplinesComponentVisualizer::GetEditedMetaLandRoadSpline() const
{
	// Prefer explicit road-section / lane selection FIRST.
	// ActiveNativeEditSpline is kept across Alt+W→E/T for draw continuity, but using it
	// before selection made Add Lane / Delete Lane mutate a *previous* actor (Fig1 red
	// selected, green still ActiveNative).
	auto PickValidRoadSpline = [](UMetaLandRoadSplineComponent* Candidate) -> UMetaLandRoadSplineComponent*
	{
		if (!Candidate || !MetaLandRoadSectionEditVisualization::IsMainRoadSpline(Candidate))
		{
			return nullptr;
		}
		return Candidate;
	};

	if (UMetaLandRoadSplineComponent* FromRoadSel = RoadSectionSelectionState
		? PickValidRoadSpline(RoadSectionSelectionState->GetSelectedSpline())
		: nullptr)
	{
		return FromRoadSel;
	}

	if (UMetaLandRoadSplineComponent* FromEditor = PickValidRoadSpline(
		MetaLandRoadSectionEditVisualization::FindSelectedRoadSplineFromEditor()))
	{
		return FromEditor;
	}

	if (UMetaLandRoadSplineComponent* FromParent = PickValidRoadSpline(
		Cast<UMetaLandRoadSplineComponent>(FSplineComponentVisualizer::GetEditedSplineComponent())))
	{
		if (CompVisUtils::IsSelectedInViewport(FromParent)
			|| MetaLandRoadSectionEditVisualization::IsSplineOwnerSelected(FromParent))
		{
			return FromParent;
		}
	}

	// Fallback: ActiveNative only when nothing else is selected (W→E handoff / first pick).
	if (UMetaLandRoadSplineComponent* ActiveNative = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline())
	{
		if (FMetaLandRoadEditorModule::Get().IsNativeEditMode()
			|| FMetaLandRoadEditorModule::Get().IsSectionMode()
			|| FMetaLandRoadEditorModule::Get().IsLaneEditMode())
		{
			return PickValidRoadSpline(ActiveNative);
		}
	}

	return nullptr;
}

UActorComponent* FMoKuEditSplinesComponentVisualizer::GetEditedComponent() const
{
	// Prefer the engine SelectionState path (native Add Point / key edit). Fall back to road selection.
	if (UActorComponent* Edited = FSplineComponentVisualizer::GetEditedComponent())
	{
		return Edited;
	}
	return GetEditedMetaLandRoadSpline();
}

bool FMoKuEditSplinesComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
	{
		if (!SelectionState
			|| SelectionState->GetLastKeyIndexSelected() == INDEX_NONE
			|| SelectionState->GetSelectedKeys().Num() == 0)
		{
			return false;
		}

		const int32 KeyIndex = SelectionState->GetLastKeyIndexSelected();
		const USplineComponent* SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
		if (!SplineComp)
		{
			SplineComp = GetEditedMetaLandRoadSpline();
		}
		if (!SplineComp)
		{
			SplineComp = GetEditedSplineComponent();
		}
		if (!SplineComp || KeyIndex < 0 || KeyIndex >= SplineComp->GetNumberOfSplinePoints())
		{
			return false;
		}

		OutLocation = SplineComp->GetLocationAtSplinePoint(KeyIndex, ESplineCoordinateSpace::World);
		return true;
	}

	// Boundary cross  - SOffset gizmo (Alt+E only).
	if (IsSectionLineEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->IsSectionBoundarySelected()
		&& RoadSectionSelectionState->GetSelectedSpline()
		&& RoadSectionSelectionState->GetSelectedSectionIndex() > 0
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE)
	{
		OutLocation = RoadSectionSelectionState->GetCachedPosition();
		return true;
	}

	// Alt+E main-line path drag / native key drag.
	if (GetSelectedKeys().Num() > 0 && SelectionState && SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		if (!FSplineComponentVisualizer::GetWidgetLocation(ViewportClient, OutLocation))
		{
			return false;
		}
		if (IsSectionLineEditMode())
		{
			if (const UMetaLandRoadSplineComponent* RoadSpline = GetEditedMetaLandRoadSpline())
			{
				OutLocation.Z += RoadSpline->GetMainCenterLineHeightOffset();
			}
		}
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
	{
		OutLocation = RoadSectionSelectionState->GetCachedPosition();
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay()
		&& HasSelectedLandscapeAttributeKey())
	{
		OutLocation = RoadSectionSelectionState->GetCachedPosition();
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay()
		&& HasSelectedPropRail())
	{
		OutLocation = PropCachedWidgetWorld;
		return true;
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const
{
	if (FMetaLandRoadEditorModule::Get().IsNativeEditMode() && SelectionState)
	{
		if (SelectionState->GetLastKeyIndexSelected() != INDEX_NONE
			|| SelectionState->GetSelectedSegmentIndex() != INDEX_NONE)
		{
			return FSplineComponentVisualizer::GetCustomInputCoordinateSystem(ViewportClient, OutMatrix);
		}
	}

	if (GetSelectedKeys().Num() > 0 && SelectionState && SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		return FSplineComponentVisualizer::GetCustomInputCoordinateSystem(ViewportClient, OutMatrix);
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
	{
		OutMatrix = FRotationMatrix::Make(RoadSectionSelectionState->GetCachedRotation());
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay()
		&& HasSelectedLandscapeAttributeKey()
		&& RoadSectionSelectionState)
	{
		OutMatrix = FRotationMatrix::Make(RoadSectionSelectionState->GetCachedRotation());
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay()
		&& HasSelectedPropRail())
	{
		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			const double WorldS = PropFocusWorldS >= 0.0 ? PropFocusWorldS : CachedPropHoveredWorldS;
			if (WorldS >= 0.0)
			{
				const FVector Tangent = SplineComp->GetTangentAtDistanceAlongSpline(
					WorldS, ESplineCoordinateSpace::World);
				OutMatrix = FRotationMatrix::MakeFromX(Tangent.GetSafeNormal());
				return true;
			}
		}
	}

	if (ViewportClient->GetWidgetCoordSystemSpace() == COORD_Local || ViewportClient->GetWidgetMode() == UE::Widget::WM_Rotate)
	{
		if (RoadSectionSelectionState
			&& RoadSectionSelectionState->IsSectionBoundarySelected()
			&& MetaLandRoadSectionEditVisualization::IsRoadSectionEditingActive(RoadSectionSelectionState)
			&& GetEditedMetaLandRoadSpline())
		{
			OutMatrix = FRotationMatrix::Make(RoadSectionSelectionState->GetCachedRotation());
			return true;
		}
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::IsVisualizingArchetype() const
{
	return FSplineComponentVisualizer::IsVisualizingArchetype();
}

static bool LaneWidthCurveContainsKeyAtS(const FRichCurve& Width, double S, float Tolerance = 30.0f)
{
	for (const FRichCurveKey& Key : Width.Keys)
	{
		if (FMath::Abs(static_cast<float>(S) - Key.Time) <= Tolerance)
		{
			return true;
		}
	}
	return false;
}

static int32 FindBestFitWidthKeyIndex(const FRichCurve& Width, double SOffset)
{
	int32 BestIndex = 0;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Width.Keys.Num(); ++Index)
	{
		const float Dist = FMath::Abs(static_cast<float>(SOffset) - Width.Keys[Index].Time);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestIndex = Index;
		}
	}
	return BestIndex;
}

bool FMoKuEditSplinesComponentVisualizer::HandleInputDelta(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FVector& DeltaTranslate,
	FRotator& DeltaRotate,
	FVector& DeltaScale)
{
	check(RoadSectionSelectionState);

	if (!IsMouseGizmoDrag(ViewportClient, Viewport))
	{
		return false;
	}

	if (HandlePropSettingInputDelta(ViewportClient, Viewport, DeltaTranslate))
	{
		return true;
	}

	if (HandleLandscapeAttributeInputDelta(ViewportClient, Viewport, DeltaTranslate))
	{
		return true;
	}

	// Alt+W: engine spline key/segment move + road mesh refresh after drag.
	if (FMetaLandRoadEditorModule::Get().IsNativeEditMode()
		&& !IsSectionLineEditMode()
		&& SelectionState)
	{
		if (UMetaLandRoadSplineComponent* RoadSpline = GetEditedMetaLandRoadSpline())
		{
			SyncEngineSelectionStateForSpline(RoadSpline);
			const int32 NumPts = RoadSpline->GetNumberOfSplinePoints();
			const TSet<int32>& SelectedKeys = SelectionState->GetSelectedKeys();
			bool bKeysValid = SelectedKeys.Num() > 0;
			for (int32 KeyIndex : SelectedKeys)
			{
				if (KeyIndex < 0 || KeyIndex >= NumPts)
				{
					bKeysValid = false;
					break;
				}
			}
			const int32 LastKey = SelectionState->GetLastKeyIndexSelected();
			if (!bKeysValid
				|| LastKey == INDEX_NONE
				|| LastKey < 0
				|| LastKey >= NumPts)
			{
				ClearNativeKeySelection();
			}
		}

		if (SelectionState->GetSelectedKeys().Num() == 0
			|| SelectionState->GetLastKeyIndexSelected() == INDEX_NONE)
		{
			return false;
		}

		return FSplineComponentVisualizer::HandleInputDelta(
			ViewportClient, Viewport, DeltaTranslate, DeltaRotate, DeltaScale);
	}

	const EMetaLandRoadSectionSelectionState State = RoadSectionSelectionState->GetStateVerified();

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& State >= EMetaLandRoadSectionSelectionState::Key)
	{
		if (!ViewportClient || !ViewportClient->IsTracking())
		{
			return false;
		}

		UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
		if (!SplineComp)
		{
			return false;
		}

		SplineComp->Modify();
		RoadSectionSelectionState->Modify();

		const FVector WidgetLocationWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaTranslate;
		const float ClosestKey = SplineComp->FindInputKeyClosestToWorldLocation(WidgetLocationWorld);
		const float ClosestS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);

		const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
		const int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
		const int32 KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();

		const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp,
			SectionIndex,
			LaneIndex);
		if (LaneSectionIndex == INDEX_NONE)
		{
			return false;
		}

		FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(LaneSectionIndex);
		FMetaLandRoadLane& Lane = Section.GetLaneByIndex(LaneIndex);
		if (!Lane.Width.Keys.IsValidIndex(KeyIndex))
		{
			return false;
		}

		FRichCurveKey& Key = Lane.Width.Keys[KeyIndex];
		const double LaneStartS = Lane.GetStartOffset();
		const double FillSOffset = LaneStartS + Key.Time;

		const FTransform KeyTransform = SplineComp->GetTransformAtSplineInputKey(
			SplineComp->GetInputKeyValueAtDistanceAlongSpline(FillSOffset),
			ESplineCoordinateSpace::World);
		const FVector WidgetLocationLocal = KeyTransform.InverseTransformPositionNoScale(WidgetLocationWorld);

		const double TargetROffset = WidgetLocationLocal.Y;
		const double CurrentROffset =
			Section.EvalLaneROffset(LaneIndex, FillSOffset, 1.0) + SplineComp->EvalROffset(FillSOffset);

		const double SOffset = ClosestS - LaneStartS;
		Key.Value += static_cast<float>((TargetROffset - CurrentROffset) * (LaneIndex >= LANE_INDEX_NONE ? 1.0 : -1.0));
		Key.Time = static_cast<float>(SOffset);

		Lane.Width.Keys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });

		const int32 NewKeyIndex = FindBestFitWidthKeyIndex(Lane.Width, SOffset);
		MetaLandRoad::EnsureLaneWidthCurveCubic(Lane.Width);
		RoadSectionSelectionState->SetSelectedKeyIndex(NewKeyIndex);
		RoadSectionSelectionState->SetCachedDataAtLane(
			SectionIndex,
			LaneIndex,
			LaneStartS + Lane.Width.Keys[NewKeyIndex].Time,
			1.0);

		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return true;
	}

	const bool bDragSectionBoundary =
		RoadSectionSelectionState->IsSectionBoundarySelected()
		&& RoadSectionSelectionState->GetSelectedSectionIndex() > 0
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE
		&& (State == EMetaLandRoadSectionSelectionState::Section
			|| State == EMetaLandRoadSectionSelectionState::Lane);

	if (bDragSectionBoundary)
	{
		const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
		FVector WidgetLocationWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaTranslate;

		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			if (SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
			{
				const float Key = SplineComp->GetInputKeyValueAtDistanceAlongSpline(
					SplineComp->GetLaneSection(SectionIndex).SOffset);
				const FVector Tangent = SplineComp->GetDirectionAtSplineInputKey(
					Key, ESplineCoordinateSpace::World).GetSafeNormal();
				const FVector DeltaAlong = Tangent * FVector::DotProduct(DeltaTranslate, Tangent);
				WidgetLocationWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaAlong;
			}
		}

		if (ApplySectionBoundaryWorldDrag(SectionIndex, WidgetLocationWorld))
		{
			return true;
		}
		return false;
	}

	// Alt+E main-line path drag / native key drag.
	if (GetSelectedKeys().Num() > 0 && SelectionState && SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		return FSplineComponentVisualizer::HandleInputDelta(
			ViewportClient, Viewport, DeltaTranslate, DeltaRotate, DeltaScale);
	}

	return false;
}

void FMoKuEditSplinesComponentVisualizer::TrackingStarted(FEditorViewportClient* InViewportClient)
{
	if (UMoKuEditSplinesComponent* MoKuSpline = Cast<UMoKuEditSplinesComponent>(GetEditedMetaLandRoadSpline()))
	{
		MoKuSpline->bDeferRoadMeshRefresh = true;
	}

	if (FMetaLandRoadEditorModule::Get().IsNativeEditMode()
		&& SelectionState
		&& SelectionState->GetLastKeyIndexSelected() != INDEX_NONE)
	{
		EnsureNativeEditTranslateWidget(InViewportClient);
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
	{
		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			SplineComp->Modify();
			RoadSectionSelectionState->Modify();
		}
	}

	if (RoadSectionSelectionState
		&& RoadSectionSelectionState->IsSectionBoundarySelected()
		&& RoadSectionSelectionState->GetSelectedSectionIndex() > 0
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE)
	{
		if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
		{
			SplineComp->Modify();
			RoadSectionSelectionState->Modify();
		}
	}

	if (CanSafelyUseEngineSplineVisualizer(GetEditedSplineComponent())
		&& !FMetaLandRoadEditorModule::Get().IsInteractiveSplineDrawToolActive())
	{
		FSplineComponentVisualizer::TrackingStarted(InViewportClient);
	}
}

bool FMoKuEditSplinesComponentVisualizer::HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	// Engine FSplineComponentVisualizer RMB builds Snap/Align / Closed Loop. Use our menu only.
	if (Key == EKeys::RightMouseButton)
	{
		return false;
	}

	// WASD / QE / arrows fly the viewport  - do not select or nudge spline keys.
	if (IsViewportNavigationKey(Key))
	{
		return false;
	}

	// Alt+W / Alt+E mode toggles are owned by LevelEditor global actions only (avoid double-toggle).

	if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay() && !IsSectionLineEditMode())
	{
		if (Event == IE_Pressed)
		{
			if (UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline())
			{
				SetActiveForSpline(SplineComp);
				if (SelectionState)
				{
					SyncEngineSelectionStateForSpline(SplineComp);
				}
			}
		}

		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed && Viewport && ViewportClient)
		{
			if (TryHandleNativeEditMousePress(ViewportClient, Viewport))
			{
				return true;
			}
		}

		if ((Key == EKeys::Delete || Key == EKeys::BackSpace) && Event == IE_Pressed)
		{
			if (CanDeleteSelectedSplinePoints())
			{
				OnDeleteSelectedSplinePoints();
			}
			return true;
		}

		const bool bHasKeySelection = SelectionState
			&& SelectionState->GetLastKeyIndexSelected() != INDEX_NONE
			&& SelectionState->GetSelectedKeys().Num() > 0;
		if (!bHasKeySelection)
		{
			return false;
		}

		return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (SplineComp != nullptr)
	{
		if (RoadSectionSelectionState->GetSelectedSectionIndex() != INDEX_NONE
			&& RoadSectionSelectionState->GetSelectedSectionIndex() >= SplineComp->GetLaneSectionsNum())
		{
			EndEditing();
			return false;
		}
	}

	if (Event == IE_Pressed)
	{
		// Section split/lane commands only  - ToggleSectionLineEdit is LevelEditor-only (no double-toggle).
		// Skip command bindings on Ctrl+LMB in Alt+T (lane face / width-key deselect only).
		const bool bSkipLaneEditCommandBindings =
			FMetaLandRoadEditorModule::Get().IsLaneEditMode()
			&& Key == EKeys::LeftMouseButton
			&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
		if (!bSkipLaneEditCommandBindings
			&& SectionComponentVisualizerActions->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false))
		{
			return true;
		}

		// Alt+T: width-key ring pick before translate gizmo; gizmo only when not tracking and no ring hit.
		if (FMetaLandRoadEditorModule::Get().IsLaneEditMode()
			&& Key == EKeys::LeftMouseButton
			&& Viewport
			&& !(ViewportClient && ViewportClient->IsTracking()
				&& RoadSectionSelectionState
				&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key))
		{
			if (TryHandleLaneEditMousePress(ViewportClient, Viewport))
			{
				return true;
			}

			FIntPoint MousePos;
			Viewport->GetMousePos(MousePos);
			if (MetaLandRoadLaneEditPick::ShouldDeferInputToTranslateWidget(
				ViewportClient, Viewport, MousePos.X, MousePos.Y))
			{
				return false;
			}
		}

		// LMB: boundary select / click-empty clears highlight.
		if (IsSectionLineEditMode() && Key == EKeys::LeftMouseButton && Viewport && ViewportClient
			&& !FSlateApplication::Get().GetModifierKeys().IsAltDown()
			&& !FSlateApplication::Get().GetModifierKeys().IsControlDown())
		{
			if (SplineComp)
			{
				FIntPoint MousePos;
				Viewport->GetMousePos(MousePos);

				FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
					Viewport,
					ViewportClient->GetScene(),
					ViewportClient->EngineShowFlags));
				FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
				if (View)
				{
					int32 BoundarySection = INDEX_NONE;
					if (SplineComp->GetLaneSectionsNum() >= 2
						&& MetaLandRoadSectionEditVisualization::PickSectionBoundaryByScreenDistance(
							SplineComp, *View, MousePos, 48.0f, BoundarySection))
					{
						if (SelectSectionBoundaryForDrag(BoundarySection))
						{
							return true;
						}
					}

					const bool bHitOurProxy =
						CompVisUtils::GetHitProxy<HMetaLandRoadSectionKeyVisProxy>(Viewport, MousePos.X, MousePos.Y, 48)
						|| CompVisUtils::GetHitProxy<HMetaLandRoadLaneVisProxy>(
							Viewport,
							MousePos.X,
							MousePos.Y,
							MetaLandRoadLaneEditPick::LaneFaceClickRadiusPx);
					int32 NearMainSection = INDEX_NONE;
					float NearMainKey = -1.0f;
					const bool bNearMain = FMetaLandRoadSectionHover::PickSectionAtMouse(
						SplineComp, *View, MousePos, NearMainSection, NearMainKey, 56.0f);
					if (bNearMain && BoundarySection == INDEX_NONE)
					{
						if (SelectMainLineForPathDrag(NearMainSection, NearMainKey))
						{
							return true;
						}
					}
					if (!bHitOurProxy && BoundarySection == INDEX_NONE && !bNearMain)
					{
						ClearSectionHighlight();
						FMetaLandRoadSectionHover::ClearHoverOverlay(SplineComp);
						FMetaLandRoadSectionHover::GetLastHoveredBoundarySection() = INDEX_NONE;
						FMetaLandRoadSectionHover::GetLastHoveredSection() = INDEX_NONE;
						FMetaLandRoadSectionHover::GetLastHoveredSplineKey() = -1.0f;
						GEditor->RedrawLevelEditingViewports(true);
						return true;
					}
				}
			}
		}
	}

	// Alt+E: custom input only.
	if (IsSectionLineEditMode())
	{
		return false;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay()
		&& Key == EKeys::LeftMouseButton
		&& Event == IE_Pressed
		&& Viewport
		&& ViewportClient)
	{
		if (TryHandleNativeEditMousePress(ViewportClient, Viewport))
		{
			return true;
		}
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode())
	{
		return false;
	}

	return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleLaneEditPickClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click,
	bool bShowContextMenu)
{
	if (!InViewportClient || !FMetaLandRoadEditorModule::Get().IsLaneEditMode() || !RoadSectionSelectionState)
	{
		return false;
	}

	if (Click.GetKey() != EKeys::RightMouseButton)
	{
		return false;
	}

	const bool bCtrlForMenu =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
	if (!bCtrlForMenu)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && HitProxy)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			const HActor* ActorProxy = static_cast<const HActor*>(HitProxy);
			if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActorProxy->Actor))
			{
				SplineComp = RoadActor->GetSplineComponent();
			}
			else if (AActor* HitActor = ActorProxy->Actor)
			{
				SplineComp = HitActor->FindComponentByClass<UMetaLandRoadSplineComponent>();
			}
		}
		else if (const HComponentVisProxy* ComponentProxy = HitProxyCast<HComponentVisProxy>(HitProxy))
		{
			if (const UActorComponent* HitComponent = ComponentProxy->Component.Get())
			{
				if (UMetaLandRoadSplineComponent* AsRoadSpline = Cast<UMetaLandRoadSplineComponent>(
						const_cast<UActorComponent*>(HitComponent)))
				{
					SplineComp = AsRoadSpline;
				}
				else if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(HitComponent->GetOwner()))
				{
					SplineComp = RoadActor->GetSplineComponent();
				}
				else if (AActor* Owner = HitComponent->GetOwner())
				{
					SplineComp = Owner->FindComponentByClass<UMetaLandRoadSplineComponent>();
				}
			}
		}
	}

	if (!SplineComp || SplineComp->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	EnsureDefaultProfileIfNeeded(SplineComp);

	// Ensure selection is at least Section so Add Lane menu entries can execute.
	{
		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (RoadSectionSelectionState->GetSelectedSpline() != SplineComp
			|| RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Component)
		{
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
		{
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->SetSelectedSection(0);
		}
	}

	FViewport* Viewport = InViewportClient->Viewport;
	if (!Viewport)
	{
		return false;
	}

	const FIntPoint ClickPos = Click.GetClickPos();
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		InViewportClient->GetScene(),
		InViewportClient->EngineShowFlags));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	const bool bCtrlDown =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsControlDown();

	int32 PrimarySectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	int32 PrimaryLaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
	int32 PrimaryWidthIndex = INDEX_NONE;
	TArray<FMetaLandRoadSelectedLaneRefU> MultiSelectedLanes = RoadSectionSelectionState->GetMultiSelectedLanes();
	if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
	{
		PrimaryWidthIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
	}

	auto FinishAfterPick = [&]() -> bool
	{
		ActivateRoadVisualization();
		GEditor->RedrawLevelEditingViewports(true);
		return FinishPickContextMenu(InViewportClient, bShowContextMenu);
	};

	FMetaLandRoadLaneWidthSegmentPickResult WidthPick;
	if (MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
		SplineComp,
		*View,
		ClickPos,
		PrimarySectionIndex,
		PrimaryLaneIndex,
		PrimaryWidthIndex,
		MultiSelectedLanes,
		CachedLaneHoveredSectionIndex,
		CachedLaneHoveredLaneIndex,
		static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx),
		WidthPick))
	{
		HMetaLandRoadLaneWidthKeyVisProxy WidthKeyProxy(
			SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex, HPP_Foreground);
		const FViewportClick SelectClick(
			View, InViewportClient, EKeys::LeftMouseButton, IE_Pressed, ClickPos.X, ClickPos.Y);
		if (VisProxyHandleLaneWidthClick(InViewportClient, &WidthKeyProxy, SelectClick))
		{
			return FinishAfterPick();
		}
	}

	if (!bCtrlDown)
	{
		if (MetaLandRoadLaneEditPick::PickLaneWidthSegmentAtMouse(
			SplineComp,
			*View,
			ClickPos,
			static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthSegmentClickRadiusPx),
			WidthPick))
		{
			HMetaLandRoadLaneWidthSegmentVisProxy WidthSegProxy(
				SplineComp, WidthPick.SectionIndex, WidthPick.LaneIndex, WidthPick.WidthIndex, HPP_Foreground);
			const FViewportClick SelectClick(
				View, InViewportClient, EKeys::LeftMouseButton, IE_Pressed, ClickPos.X, ClickPos.Y);
			if (VisProxyHandleLaneWidthClick(InViewportClient, &WidthSegProxy, SelectClick))
			{
				return FinishAfterPick();
			}
		}

		if (MetaLandRoadLaneEditPick::PickLaneBoundaryOnSectionAtMouse(
			SplineComp,
			*View,
			ClickPos,
			static_cast<float>(MetaLandRoadLaneEditPick::SectionBoundaryHoverRadiusPx),
			WidthPick))
		{
			FMetaLandRoadLaneWidthSegmentPickResult SubPick;
			if (MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
				SplineComp,
				*View,
				ClickPos,
				WidthPick.SectionIndex,
				WidthPick.LaneIndex,
				INDEX_NONE,
				{},
				WidthPick.SectionIndex,
				WidthPick.LaneIndex,
				static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx),
				SubPick))
			{
				HMetaLandRoadLaneWidthKeyVisProxy WidthKeyProxy(
					SplineComp, SubPick.SectionIndex, SubPick.LaneIndex, SubPick.WidthIndex, HPP_Foreground);
				const FViewportClick SelectClick(
					View, InViewportClient, EKeys::LeftMouseButton, IE_Pressed, ClickPos.X, ClickPos.Y);
				if (VisProxyHandleLaneWidthClick(InViewportClient, &WidthKeyProxy, SelectClick))
				{
					return FinishAfterPick();
				}
			}

			if (MetaLandRoadLaneEditPick::PickLaneWidthSegmentAtMouse(
				SplineComp,
				*View,
				ClickPos,
				static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthSegmentClickRadiusPx),
				SubPick))
			{
				HMetaLandRoadLaneWidthSegmentVisProxy WidthSegProxy(
					SplineComp, SubPick.SectionIndex, SubPick.LaneIndex, SubPick.WidthIndex, HPP_Foreground);
				const FViewportClick SelectClick(
					View, InViewportClient, EKeys::LeftMouseButton, IE_Pressed, ClickPos.X, ClickPos.Y);
				if (VisProxyHandleLaneWidthClick(InViewportClient, &WidthSegProxy, SelectClick))
				{
					return FinishAfterPick();
				}
			}

			const FScopedTransaction Transaction(LOCTEXT("SelectLaneBoundaryMenu", "Select Lane Boundary"));
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->SetLaneFacePrimSelected(false);
			RoadSectionSelectionState->ClearWidthKeySelection();
			RoadSectionSelectionState->SetSelectedSection(WidthPick.SectionIndex);
			RoadSectionSelectionState->SetPrimaryLaneSelection(
				WidthPick.SectionIndex, WidthPick.LaneIndex, false);
			RoadSectionSelectionState->SetLaneWidthMenuContext(true);

			const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
				SplineComp->GetLaneRange(WidthPick.SectionIndex, WidthPick.LaneIndex);
			if (Range.EndS > Range.StartS + KINDA_SMALL_NUMBER)
			{
				const float InputKey = SplineComp->ClosestKeyToSegment2(
					static_cast<float>(Range.StartS),
					static_cast<float>(Range.EndS),
					Click.GetOrigin(),
					Click.GetOrigin() + Click.GetDirection() * 50000.0f);
				const double PickS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(InputKey);
				RoadSectionSelectionState->SetCachedDataAtLane(
					WidthPick.SectionIndex, WidthPick.LaneIndex, PickS, 1.0);
				RoadSectionSelectionState->SetLastPickSplineKey(InputKey);
			}
			return FinishAfterPick();
		}
	}

	// RMB lane face: yellow fill + Lane menu only (no Width section).
	{
		FMetaLandRoadLaneFacePickResult FacePick;
		if (MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *View, ClickPos, FacePick))
		{
			if (SelectLaneFaceFromScreenPick(SplineComp, FacePick.SectionIndex, FacePick.LaneIndex, Click, true))
			{
				return FinishAfterPick();
			}
		}

		if (HMetaLandRoadLaneVisProxy* LaneProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneVisProxy>(
			Viewport, ClickPos.X, ClickPos.Y, MetaLandRoadLaneEditPick::LaneFaceClickRadiusPx))
		{
			if (LaneProxy->Component.Get() == SplineComp && LaneProxy->LaneIndex != LANE_INDEX_NONE)
			{
				if (SelectLaneFaceFromScreenPick(
					SplineComp, LaneProxy->SectionIndex, LaneProxy->LaneIndex, Click, true))
				{
					return FinishAfterPick();
				}
			}
		}
	}

	if (!bCtrlDown
		&& CachedLaneHoveredSectionIndex != INDEX_NONE
		&& CachedLaneHoveredLaneIndex != LANE_INDEX_NONE)
	{
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->SetLaneFacePrimSelected(false);
		RoadSectionSelectionState->ClearWidthKeySelection();
		RoadSectionSelectionState->SetSelectedSection(CachedLaneHoveredSectionIndex);
		RoadSectionSelectionState->SetPrimaryLaneSelection(
			CachedLaneHoveredSectionIndex, CachedLaneHoveredLaneIndex, false);
		RoadSectionSelectionState->SetLaneWidthMenuContext(true);
		if (CachedLaneHoveredSplineKey >= 0.0f)
		{
			RoadSectionSelectionState->SetLastPickSplineKey(CachedLaneHoveredSplineKey);
		}
		return FinishAfterPick();
	}

	// Alt+T: always show lane/width context menu when editing this spline.
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (RoadSectionSelectionState->GetSelectedSpline() != SplineComp)
		{
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
		}
		if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Component
			&& SplineComp->GetLaneSectionsNum() > 0)
		{
			RoadSectionSelectionState->Modify();
			RoadSectionSelectionState->SetSelectedSection(0);
		}
	}

	if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section)
	{
		return FinishPickContextMenu(InViewportClient, bShowContextMenu);
	}

	return false;
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleRoadSplinePickClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click,
	bool bShowContextMenu)
{
	if (!InViewportClient)
	{
		return false;
	}

	// Alt+W native RMB goes through ComponentVisManager hit proxies  - not this screen-pick path.
	if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay() && !IsSectionLineEditMode())
	{
		return false;
	}

	if (!IsSectionLineEditMode())
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();

	if (!SplineComp && HitProxy)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			const HActor* ActorProxy = static_cast<const HActor*>(HitProxy);
			if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(ActorProxy->Actor))
			{
				SplineComp = RoadActor->GetSplineComponent();
			}
			else if (AActor* HitActor = ActorProxy->Actor)
			{
				SplineComp = HitActor->FindComponentByClass<UMetaLandRoadSplineComponent>();
			}
		}
		else if (const HComponentVisProxy* ComponentProxy = HitProxyCast<HComponentVisProxy>(HitProxy))
		{
			if (const UActorComponent* HitComponent = ComponentProxy->Component.Get())
			{
				if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(HitComponent->GetOwner()))
				{
					SplineComp = RoadActor->GetSplineComponent();
				}
			}
		}
	}

	if (!SplineComp || !InViewportClient)
	{
		return false;
	}

	EnsureDefaultProfileIfNeeded(SplineComp);

	if (SplineComp->GetLaneSectionsNum() == 0)
	{
		return false;
	}

	// In section-line mode, engine key proxies keep native edit; section line uses our proxies.
	if (HitProxy && MetaLandRoadSectionEditVisualization::IsEngineSplineEditProxy(HitProxy)
		&& Click.GetKey() != EKeys::RightMouseButton)
	{
		return false;
	}

	if (HitProxy && HitProxy->IsA(HMetaLandRoadLaneVisProxy::StaticGetType()))
	{
		HMetaLandRoadLaneVisProxy* LaneProxy = static_cast<HMetaLandRoadLaneVisProxy*>(HitProxy);
		if (LaneProxy->Component.Get() == SplineComp)
		{
			if (VisProxyHandleClick(InViewportClient, LaneProxy, Click))
			{
				return FinishPickContextMenu(InViewportClient, bShowContextMenu);
			}
		}
	}

	if (FViewport* Viewport = InViewportClient->Viewport)
	{
		const FIntPoint ClickPos = Click.GetClickPos();
		if (HMetaLandRoadSectionKeyVisProxy* SectionKeyProxy = CompVisUtils::GetHitProxy<HMetaLandRoadSectionKeyVisProxy>(Viewport, ClickPos.X, ClickPos.Y, 48))
		{
			if (SectionKeyProxy->Component.Get() == SplineComp)
			{
				if (VisProxyHandleClick(InViewportClient, SectionKeyProxy, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}

		if (HMetaLandRoadLaneWidthKeyVisProxy* WidthKeyProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneWidthKeyVisProxy>(
			Viewport, ClickPos.X, ClickPos.Y, MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx))
		{
			if (WidthKeyProxy->Component.Get() == SplineComp)
			{
				if (VisProxyHandleClick(InViewportClient, WidthKeyProxy, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}

		if (HMetaLandRoadLaneWidthSegmentVisProxy* WidthSegProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneWidthSegmentVisProxy>(
			Viewport, ClickPos.X, ClickPos.Y, MetaLandRoadLaneEditPick::LaneWidthSegmentClickRadiusPx))
		{
			if (WidthSegProxy->Component.Get() == SplineComp)
			{
				if (VisProxyHandleClick(InViewportClient, WidthSegProxy, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}

		FSceneViewFamilyContext LaneFacePickViewFamily(FSceneViewFamily::ConstructionValues(
			Viewport,
			InViewportClient->GetScene(),
			InViewportClient->EngineShowFlags));
		if (FSceneView* LaneFacePickView = InViewportClient->CalcSceneView(&LaneFacePickViewFamily))
		{
			FMetaLandRoadLaneFacePickResult FacePick;
			if (MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *LaneFacePickView, ClickPos, FacePick))
			{
				if (SelectLaneFaceFromScreenPick(SplineComp, FacePick.SectionIndex, FacePick.LaneIndex, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}

		if (HMetaLandRoadLaneVisProxy* LaneProxy = CompVisUtils::GetHitProxy<HMetaLandRoadLaneVisProxy>(
			Viewport, ClickPos.X, ClickPos.Y, MetaLandRoadLaneEditPick::LaneFaceClickRadiusPx))
		{
			if (LaneProxy->Component.Get() == SplineComp)
			{
				if (VisProxyHandleClick(InViewportClient, LaneProxy, Click))
				{
					return FinishPickContextMenu(InViewportClient, bShowContextMenu);
				}
			}
		}
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewportClient->Viewport,
		InViewportClient->GetScene(),
		InViewportClient->EngineShowFlags));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	int32 BoundarySectionIndex = INDEX_NONE;
	if (MetaLandRoadSectionEditVisualization::PickSectionBoundaryByScreenDistance(
		SplineComp,
		*View,
		Click.GetClickPos(),
		40.0f,
		BoundarySectionIndex))
	{
		const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionBoundary", "Select Road Section Boundary"));
		RoadSectionSelectionState->Modify();

		HMetaLandRoadSectionKeyVisProxy SectionKeyProxy(SplineComp, BoundarySectionIndex, HPP_UI);
		if (!UpdateSelectedComponentAndSectionAndLane(&SectionKeyProxy))
		{
			return false;
		}

		RoadSectionSelectionState->SetSelectedSection(BoundarySectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(BoundarySectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
		ActivateRoadVisualization();
		GEditor->RedrawLevelEditingViewports(true);
		// Boundary pick: select/drag only  - no Add Section menu.
		return true;
	}

	int32 SectionIndex = INDEX_NONE;
	float SplineKey = 0.0f;
	if (!FMetaLandRoadSectionHover::PickSectionAtMouse(
		SplineComp,
		*View,
		Click.GetClickPos(),
		SectionIndex,
		SplineKey,
		56.0f))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SelectRoadSectionLane", "Select Road Lane"));
	RoadSectionSelectionState->Modify();

	HMetaLandRoadLaneVisProxy LaneProxy(SplineComp, SectionIndex, LANE_INDEX_NONE, HPP_UI);
	if (!UpdateSelectedComponentAndSectionAndLane(&LaneProxy))
	{
		return false;
	}

	RoadSectionSelectionState->SetSelectedSection(SectionIndex);
	RoadSectionSelectionState->RecordPickSplineKey(SplineKey);
	RoadSectionSelectionState->SetSectionBoundarySelected(false);
	HoveredSectionIndex = SectionIndex;
	HoveredBoundarySectionIndex = INDEX_NONE;
	HoveredSplineKey = SplineKey;

	if (SelectionState)
	{
		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (SplinePropertyPath.IsValid())
		{
			SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		}
	}

	ActivateRoadVisualization();
	GEditor->RedrawLevelEditingViewports(true);
	return FinishPickContextMenu(InViewportClient, bShowContextMenu);
}

void FMoKuEditSplinesComponentVisualizer::ActivateRoadVisualization()
{
	if (!GUnrealEd || !GCurrentLevelEditingViewportClient)
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();

	if (SplineComp)
	{
		if (bSectionLineEditMode)
		{
			EnsureDefaultProfileIfNeeded(SplineComp);
		}

		FComponentPropertyPath SplinePropertyPath(SplineComp);
		if (SplinePropertyPath.IsValid() && SelectionState)
		{
			SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		}
		if (RoadSectionSelectionState && SplinePropertyPath.IsValid())
		{
			const UMetaLandRoadSplineComponent* SelectedSpline = RoadSectionSelectionState->GetSelectedSpline();
			if (SelectedSpline == SplineComp)
			{
				RoadSectionSelectionState->AssignSelectedSplinePath(SplinePropertyPath);
			}
			else if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Lane)
			{
				RoadSectionSelectionState->SetSelectedSpline(SplinePropertyPath);
			}
		}
	}

	FEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	TSharedPtr<FComponentVisualizer> ComponentVis = StaticCastSharedPtr<FComponentVisualizer>(SharedThis(this).ToSharedPtr());
	GUnrealEd->ComponentVisManager.SetActiveComponentVis(ViewportClient, ComponentVis);
}

bool FMoKuEditSplinesComponentVisualizer::HasFocusOnSelectionBoundingBox(FBox& OutBoundingBox)
{
	return false;
}

bool FMoKuEditSplinesComponentVisualizer::HandleSnapTo(
	const bool bInAlign,
	const bool bInUseLineTrace,
	const bool bInUseBounds,
	const bool bInUsePivot,
	AActor* InDestination)
{
	return false;
}

void FMoKuEditSplinesComponentVisualizer::TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	UMoKuEditSplinesComponent* MoKuSpline = Cast<UMoKuEditSplinesComponent>(SplineComp);
	if (!MoKuSpline)
	{
		MoKuSpline = Cast<UMoKuEditSplinesComponent>(GetEditedSplineComponent());
	}

	// Parent may NotifyPropertyModified(ValueSet)  - keep defer on so mesh waits for this function.
	if (CanSafelyUseEngineSplineVisualizer(GetEditedSplineComponent())
		&& !FMetaLandRoadEditorModule::Get().IsInteractiveSplineDrawToolActive())
	{
		FSplineComponentVisualizer::TrackingStopped(InViewportClient, bInDidMove);
	}

	if (MoKuSpline)
	{
		MoKuSpline->bDeferRoadMeshRefresh = false;
	}

	if (!bInDidMove || !SplineComp)
	{
		return;
	}

	if (RoadSectionSelectionState && RoadSectionSelectionState->IsSectionBoundarySelected())
	{
		FinishSectionBoundaryDrag();
		return;
	}

	if (FMetaLandRoadEditorModule::Get().IsNativeEditMode() && !IsSectionLineEditMode())
	{
		SplineComp->EnsureAllCurvePointTypesForSmoothCorners();
		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
		{
			RoadActor->UpdateInterpolateType(0);
		}
	}

	if (RoadSectionSelectionState
		&& RoadSectionSelectionState->GetState() == EMetaLandRoadSectionSelectionState::Section)
	{
		const EMetaLandRoadSectionSelectionState StateVerified = RoadSectionSelectionState->GetStateVerified();
		double SOffset = 0.0;
		bool bValid = false;
		if (StateVerified >= EMetaLandRoadSectionSelectionState::Section)
		{
			SOffset = SplineComp->GetLaneSection(RoadSectionSelectionState->GetSelectedSectionIndex()).SOffset;
			bValid = true;
		}

		SplineComp->TrimLaneSections();
		RoadSectionSelectionState->FixState();
		if (bValid)
		{
			const int32 BestSectionIndex = CompVisUtils::FindBestFit(
				SplineComp->GetLaneSections(),
				[SOffset](const FMetaLandRoadLaneSection& It)
				{
					return FMath::Abs(SOffset - It.SOffset);
				});
			RoadSectionSelectionState->SetSelectedSection(BestSectionIndex);
			RoadSectionSelectionState->SetCachedDataAtSectionStart(BestSectionIndex);
		}
	}

	SplineComp->GetRoadLayoutMutable().UpdateLayoutVersion();
	SplineComp->UpdateRoadLayout();
	RefreshRoadMeshAndRedraw(SplineComp);
	SplineComp->MarkRenderStateDirty();

	PrepareLandscapeKeys(SplineComp);
	NotifyLandscapeAttributeChanged(SplineComp);
}

void FMoKuEditSplinesComponentVisualizer::EndEditing()
{
	if (!GIsTransacting && IsValid(RoadSectionSelectionState))
	{
		RoadSectionSelectionState->ResetSelection(false);
	}
	if (CanSafelyUseEngineSplineVisualizer(GetEditedSplineComponent()))
	{
		FSplineComponentVisualizer::EndEditing();
	}
}

void FMoKuEditSplinesComponentVisualizer::OnSplitSectionSide(EMetaLandRoadLaneSectionSide Side)
{
	TrySplitSectionSide(Side, -1.0f);
}

EMetaLandRoadLaneSectionSide FMoKuEditSplinesComponentVisualizer::ResolveAddSectionSideFromLaneSelection() const
{
	if (!RoadSectionSelectionState)
	{
		return EMetaLandRoadLaneSectionSide::Both;
	}

	bool bHasLeft = false;
	bool bHasRight = false;
	auto ConsiderLane = [&](int32 LaneIndex)
	{
		if (LaneIndex > 0)
		{
			bHasRight = true;
		}
		else if (LaneIndex < 0)
		{
			bHasLeft = true;
		}
	};

	ConsiderLane(RoadSectionSelectionState->GetSelectedLaneIndex());
	for (const FMetaLandRoadSelectedLaneRefU& Ref : RoadSectionSelectionState->GetMultiSelectedLanes())
	{
		ConsiderLane(Ref.LaneIndex);
	}

	if (bHasLeft && bHasRight)
	{
		return EMetaLandRoadLaneSectionSide::Both;
	}
	if (bHasLeft)
	{
		return EMetaLandRoadLaneSectionSide::Left;
	}
	if (bHasRight)
	{
		return EMetaLandRoadLaneSectionSide::Right;
	}
	return EMetaLandRoadLaneSectionSide::Both;
}

int32 FMoKuEditSplinesComponentVisualizer::TryAddSectionFromLaneSelection(float OverrideSplitKey)
{
	return TrySplitSectionSide(ResolveAddSectionSideFromLaneSelection(), OverrideSplitKey);
}

// =============================================================================
// [KEY-ALGO] TrySplitSectionSide
// 菜单 Add Left Section / Add Right Section / Add Section ->SplitSection -
//  -EnsureBothFacesOnRoad，再按粉 -hover  -SplineKey 切面 -
// =============================================================================
int32 FMoKuEditSplinesComponentVisualizer::TrySplitSectionSide(EMetaLandRoadLaneSectionSide Side, float OverrideSplitKey)
{
	if (!bSectionLineEditMode)
	{
		SetSectionLineEditMode(true);
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991201,
				3.0f,
				FColor::Orange,
				TEXT("[MetaLandRoad] Add Section failed: no edited road spline"));
		}
		return INDEX_NONE;
	}

	EnsureDefaultProfileIfNeeded(SplineComp);
	MetaLandRoadProfile::EnsureBothFacesOnRoad(SplineComp);

	// Ensure a section is selected so the new span can highlight after split.
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		FComponentPropertyPath Path(SplineComp);
		if (Path.IsValid() && RoadSectionSelectionState->GetSelectedSpline() != SplineComp)
		{
			RoadSectionSelectionState->SetSelectedSpline(Path);
		}
		RoadSectionSelectionState->SetSelectedSection(0);
	}

	float SplitKey = OverrideSplitKey;
	if (SplitKey < 0.0f)
	{
		const float HoverKey = HoveredSplineKey >= 0.0f
			? HoveredSplineKey
			: FMetaLandRoadSectionHover::GetLastHoveredSplineKey();
		SplitKey = MetaLandRoadSectionEditVisualization::ResolveSplitSplineKey(
			SplineComp,
			RoadSectionSelectionState,
			HoverKey);
	}

	if (SplitKey < 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Add Section failed: invalid SplitKey (road too short or pick missed)"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991202,
				4.0f,
				FColor::Orange,
				TEXT("[MetaLandRoad] Add Section failed: pick a point on the pink center line (not ends / orange boundary)"));
		}
		return INDEX_NONE;
	}

	const FScopedTransaction Transaction(LOCTEXT("SplitSection", "Split Section"));
	SplineComp->Modify();

	int32 NewSectionIndex = INDEX_NONE;
	switch (Side)
	{
	case EMetaLandRoadLaneSectionSide::Left:
		NewSectionIndex = SplineComp->EditorAddLeftSection(SplitKey);
		break;
	case EMetaLandRoadLaneSectionSide::Right:
		NewSectionIndex = SplineComp->EditorAddRightSection(SplitKey);
		break;
	default:
		NewSectionIndex = SplineComp->SplitSection(SplitKey, EMetaLandRoadLaneSectionSide::Both);
		break;
	}

	if (NewSectionIndex != INDEX_NONE)
	{
		if (SelectionState)
		{
			SelectionState->ClearSelectedKeys();
		}
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->SetSelectedSection(NewSectionIndex);
		RoadSectionSelectionState->SetCachedDataAtSectionStart(NewSectionIndex);
		RoadSectionSelectionState->SetSectionBoundarySelected(true);
		ActivateRoadVisualization();
		RefreshRoadMeshAndRedraw(SplineComp);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991203,
				3.0f,
				FColor::Cyan,
				FString::Printf(
					TEXT("[MetaLandRoad] Add Section OK side=%d key=%.2f -> section %d"),
					static_cast<int32>(Side),
					SplitKey,
					NewSectionIndex));
		}
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991204,
			4.0f,
			FColor::Orange,
			TEXT("[MetaLandRoad] Add Section failed: see Output Log (end of road / overlapping face / invalid S)"));
	}

	return NewSectionIndex;
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteSection()
{
	if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section)
	{
		const FScopedTransaction Transaction(LOCTEXT("DeleteSection", "Delete Section"));

		UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
		SplineComp->Modify();
		SplineComp->GetLaneSectionsMutable().RemoveAt(RoadSectionSelectionState->GetSelectedSectionIndex());
		SplineComp->UpdateRoadLayout();
		SplineComp->UpdateLaneSectionBounds();
		SplineComp->TrimLaneSections();
		RefreshRoadMeshAndRedraw(SplineComp);
		SplineComp->MarkRenderStateDirty();

		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->SetSelectedLane(LANE_INDEX_NONE);
	}
}

bool FMoKuEditSplinesComponentVisualizer::CanDeleteSectionSide(EMetaLandRoadLaneSectionSide Side) const
{
	if (!bSectionLineEditMode || !HasMainLineSectionMenuContext() || !RoadSectionSelectionState)
	{
		return false;
	}

	const UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return false;
	}

	int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	if (SectionIndex == INDEX_NONE)
	{
		SectionIndex = HoveredSectionIndex;
	}
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	if (Section.Side == Side)
	{
		return true;
	}
	if (Section.Side != EMetaLandRoadLaneSectionSide::Both)
	{
		return false;
	}
	return Side == EMetaLandRoadLaneSectionSide::Left
		? !Section.Left.IsEmpty()
		: !Section.Right.IsEmpty();
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteSectionSide(EMetaLandRoadLaneSectionSide Side)
{
	if (Side != EMetaLandRoadLaneSectionSide::Left && Side != EMetaLandRoadLaneSectionSide::Right)
	{
		return;
	}
	if (!CanDeleteSectionSide(Side))
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !RoadSectionSelectionState)
	{
		return;
	}

	int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	if (SectionIndex == INDEX_NONE)
	{
		SectionIndex = HoveredSectionIndex;
	}
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteSectionSide", "Delete Section Side"));
	SplineComp->Modify();

	TArray<FMetaLandRoadLaneSection>& Sections = SplineComp->GetLaneSectionsMutable();
	const double SelectedS = Sections[SectionIndex].SOffset;
	for (int32 Index = Sections.Num() - 1; Index >= 0; --Index)
	{
		if (!FMath::IsNearlyEqual(Sections[Index].SOffset, SelectedS, 0.1))
		{
			continue;
		}

		FMetaLandRoadLaneSection& Section = Sections[Index];
		if (Section.Side == Side)
		{
			Sections.RemoveAt(Index);
			continue;
		}
		if (Section.Side != EMetaLandRoadLaneSectionSide::Both)
		{
			continue;
		}

		if (Side == EMetaLandRoadLaneSectionSide::Left)
		{
			Section.Left.Reset();
			Section.Side = EMetaLandRoadLaneSectionSide::Right;
			if (Section.Right.IsEmpty())
			{
				Sections.RemoveAt(Index);
			}
		}
		else
		{
			Section.Right.Reset();
			Section.Side = EMetaLandRoadLaneSectionSide::Left;
			if (Section.Left.IsEmpty())
			{
				Sections.RemoveAt(Index);
			}
		}
	}

	if (Sections.Num() == 0)
	{
		MetaLandRoadProfile::EnsureDefaultRoadProfile(SplineComp);
	}

	SplineComp->UpdateRoadLayout();
	SplineComp->UpdateLaneSectionBounds();
	SplineComp->TrimLaneSections();
	RefreshRoadMeshAndRedraw(SplineComp);
	SplineComp->MarkRenderStateDirty();

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->SetSelectedLane(LANE_INDEX_NONE);
	if (Sections.Num() > 0)
	{
		const int32 KeepIndex = FMath::Clamp(SectionIndex, 0, Sections.Num() - 1);
		RoadSectionSelectionState->SetSelectedSection(KeepIndex);
	}
}

void FMoKuEditSplinesComponentVisualizer::OnAddLane(bool bOnLeft)
{
	if (!EnsureLaneEditMenuSelection()
		|| RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991210,
				3.0f,
				FColor::Orange,
				TEXT("[MetaLandRoad] Add Lane failed: select the road first"));
		}
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddLane", "Add Lane"));

	// Must be the spline owned by the current selection  - not a stale ActiveNative.
	UMetaLandRoadSplineComponent* SplineComp = RoadSectionSelectionState->GetSelectedSpline();
	if (!SplineComp || !MetaLandRoadSectionEditVisualization::IsMainRoadSpline(SplineComp))
	{
		SplineComp = GetEditedMetaLandRoadSpline();
	}
	if (!SplineComp)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991210,
				3.0f,
				FColor::Orange,
				TEXT("[MetaLandRoad] Add Lane failed: select the road actor / lane first"));
		}
		return;
	}
	SplineComp->Modify();

	const int32 SpanSectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	const int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
	if (!SplineComp->GetLaneSections().IsValidIndex(SpanSectionIndex))
	{
		return;
	}

	const MetaLandRoadProfile::FMetaLandRoadAddLaneResult AddResult =
		MetaLandRoadProfile::AddLaneToRoadLayout(SplineComp, SpanSectionIndex, LaneIndex, bOnLeft);
	if (!AddResult.bSuccess)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991212,
				3.0f,
				FColor::Orange,
				TEXT("[MetaLandRoad] Add Lane failed on selected road (see Output Log)"));
		}
		return;
	}
	if (GEngine)
	{
		AActor* Owner = SplineComp->GetOwner();
		GEngine->AddOnScreenDebugMessage(
			991213,
			3.0f,
			FColor::Cyan,
			FString::Printf(
				TEXT("[MetaLandRoad] Add Lane OK ->%s section=%d lane=%d"),
				Owner ? *Owner->GetActorLabel() : TEXT("?"),
				SpanSectionIndex,
				AddResult.NewSelectedLaneIndex));
	}

	SplineComp->UpdateRoadLayout();
	// Local hub recook from new CS width; suppress duplicate recook during mesh refresh.
	MetaLandRoadProfile::RecookLocalJunctionsForSpline(SplineComp);
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		MetaLandRoadProfile::RefreshConnectedRoadMeshes(SplineComp);
	}

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->SetPrimaryLaneSelection(
		SpanSectionIndex,
		AddResult.NewSelectedLaneIndex,
		false);
	GEditor->RedrawLevelEditingViewports(true);
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteLane()
{
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Lane
		&& RoadSectionSelectionState->GetMultiSelectedLanes().Num() == 0)
	{
		if (CachedLaneHoveredLaneIndex != LANE_INDEX_NONE
			&& CachedLaneHoveredSectionIndex != INDEX_NONE)
		{
			EnsureLaneEditMenuSelection();
			RoadSectionSelectionState->SetPrimaryLaneSelection(
				CachedLaneHoveredSectionIndex,
				CachedLaneHoveredLaneIndex,
				false);
		}
	}
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Lane
		&& RoadSectionSelectionState->GetMultiSelectedLanes().Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteLane", "Delete Lane"));

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}
	SplineComp->Modify();

	TArray<FMetaLandRoadSelectedLaneRefU> LanesToDelete = RoadSectionSelectionState->GetMultiSelectedLanes();
	if (LanesToDelete.Num() == 0)
	{
		const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
		const int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
		if (SectionIndex != INDEX_NONE && LaneIndex != LANE_INDEX_NONE)
		{
			LanesToDelete.Add({ SectionIndex, LaneIndex });
		}
	}

	LanesToDelete.Sort([](const FMetaLandRoadSelectedLaneRefU& A, const FMetaLandRoadSelectedLaneRefU& B)
	{
		if (A.SectionIndex != B.SectionIndex)
		{
			return A.SectionIndex > B.SectionIndex;
		}
		return FMath::Abs(A.LaneIndex) > FMath::Abs(B.LaneIndex);
	});

	TSet<int32> TouchedSections;
	for (const FMetaLandRoadSelectedLaneRefU& Ref : LanesToDelete)
	{
		if (Ref.SectionIndex == INDEX_NONE || Ref.LaneIndex == LANE_INDEX_NONE)
		{
			continue;
		}

		const int32 LaneDataSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
			SplineComp, Ref.SectionIndex, Ref.LaneIndex);
		if (LaneDataSectionIndex == INDEX_NONE
			|| !SplineComp->GetLaneSections().IsValidIndex(LaneDataSectionIndex))
		{
			continue;
		}

		FMetaLandRoadLaneSection& LaneSection = SplineComp->GetLaneSection(LaneDataSectionIndex);
		if (!LaneSection.CheckLaneIndex(Ref.LaneIndex))
		{
			continue;
		}

		if (Ref.LaneIndex > 0 && Ref.LaneIndex <= LaneSection.Right.Num())
		{
			LaneSection.Right.RemoveAt(Ref.LaneIndex - 1);
			TouchedSections.Add(LaneDataSectionIndex);
		}
		else if (Ref.LaneIndex < 0 && -Ref.LaneIndex <= LaneSection.Left.Num())
		{
			LaneSection.Left.RemoveAt(-Ref.LaneIndex - 1);
			TouchedSections.Add(LaneDataSectionIndex);
		}
	}

	if (TouchedSections.Num() == 0)
	{
		return;
	}

	SplineComp->UpdateRoadLayout();
	RefreshRoadMeshAndRedraw(SplineComp);
	SplineComp->MarkRenderStateDirty();

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->ClearLaneMultiSelection();
	RoadSectionSelectionState->SetSelectedLane(LANE_INDEX_NONE);
}

void FMoKuEditSplinesComponentVisualizer::OnAddWidthKey()
{
	EnsureLaneEditMenuSelection();
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Section)
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}

	int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
	if (CachedLaneHoveredLaneIndex != LANE_INDEX_NONE && CachedLaneHoveredSectionIndex != INDEX_NONE)
	{
		RoadSectionSelectionState->Modify();
		RoadSectionSelectionState->SetSelectedSection(CachedLaneHoveredSectionIndex);
		RoadSectionSelectionState->SetSelectedLane(CachedLaneHoveredLaneIndex);
		SectionIndex = CachedLaneHoveredSectionIndex;
		LaneIndex = CachedLaneHoveredLaneIndex;
		if (CachedLaneHoveredSplineKey >= 0.0f)
		{
			RoadSectionSelectionState->SetLastPickSplineKey(CachedLaneHoveredSplineKey);
		}
	}
	if (LaneIndex == LANE_INDEX_NONE || SectionIndex == INDEX_NONE
		|| !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991220, 3.0f, FColor::Orange,
				TEXT("[MetaLandRoad] Add Width Key failed: select a lane in the section first"));
		}
		return;
	}

	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		SplineComp, SectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE
		|| !SplineComp->GetLaneSections().IsValidIndex(LaneSectionIndex)
		|| !SplineComp->GetLaneSection(LaneSectionIndex).CheckLaneIndex(LaneIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddWidthKey", "Add Width Key"));
	SplineComp->Modify();

	FMetaLandRoadLaneSection& SelectedSection = SplineComp->GetLaneSection(LaneSectionIndex);
	FMetaLandRoadLane& SelectedLane = SelectedSection.GetLaneByIndex(LaneIndex);

	if (SelectedLane.Width.GetNumKeys() == 0)
	{
		const FKeyHandle StartHandle = SelectedLane.Width.AddKey(0.0f, SelectedLane.Width.GetDefaultValue());
		SelectedLane.Width.GetKey(StartHandle).InterpMode = ERichCurveInterpMode::RCIM_Cubic;
		SelectedLane.Width.GetKey(StartHandle).TangentMode = ERichCurveTangentMode::RCTM_Auto;
	}

	const FMetaLandRoadLaneSection& WriteSection = SplineComp->GetLaneSection(LaneSectionIndex);
	const double SpanS0 = WriteSection.SOffset;
	const double SpanS1 = MetaLandRoad::ResolveSectionEndS(SplineComp, LaneSectionIndex);
	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		SplineComp->GetLaneRange(LaneSectionIndex, LaneIndex);
	const double RangeS0 = Range.EndS > Range.StartS + KINDA_SMALL_NUMBER ? Range.StartS : SpanS0;
	const double RangeS1 = Range.EndS > Range.StartS + KINDA_SMALL_NUMBER ? Range.EndS : SpanS1;
	const double ClipS0 = FMath::Max(SpanS0, RangeS0);
	const double ClipS1 = FMath::Min(SpanS1, RangeS1);
	if (ClipS1 <= ClipS0 + KINDA_SMALL_NUMBER)
	{
		return;
	}

	double S0 = -1.0;
	float SplineInputKey = CachedLaneHoveredSplineKey;
	if (SplineInputKey < 0.0f)
	{
		SplineInputKey = RoadSectionSelectionState->GetLastPickSplineKey();
	}
	if (SplineInputKey < 0.0f)
	{
		SplineInputKey = RoadSectionSelectionState->GetCachedSplineKey();
	}
	if (SplineInputKey >= 0.0f)
	{
		S0 = SplineComp->GetDistanceAlongSplineAtSplineInputKey(SplineInputKey);
	}
	if (S0 < ClipS0 || S0 > ClipS1)
	{
		S0 = 0.5 * (ClipS0 + ClipS1);
	}

	double S = S0 - RangeS0;
	if (LaneWidthCurveContainsKeyAtS(SelectedLane.Width, S))
	{
		bool bFoundSlot = false;
		for (int32 Step = 1; Step <= 8; ++Step)
		{
			const double CandidateS = FMath::Lerp(ClipS0, ClipS1, static_cast<double>(Step) / 9.0) - RangeS0;
			if (!LaneWidthCurveContainsKeyAtS(SelectedLane.Width, CandidateS))
			{
				S = CandidateS;
				bFoundSlot = true;
				break;
			}
		}
		if (!bFoundSlot)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					991221, 3.0f, FColor::Orange,
					TEXT("[MetaLandRoad] Add Width Key failed: no free slot on this section"));
			}
			return;
		}
	}

	int32 KeyIndex = 0;
	for (int32 Index = 1; Index < SelectedLane.Width.GetNumKeys(); ++Index)
	{
		if (S > SelectedLane.Width.Keys[Index].Time)
		{
			KeyIndex = Index;
		}
		else
		{
			break;
		}
	}

	FRichCurveKey NewKey = SelectedLane.Width.Keys[KeyIndex];
	NewKey.Time = static_cast<float>(S);
	NewKey.Value = SelectedLane.Width.Eval(static_cast<float>(S));
	NewKey.InterpMode = ERichCurveInterpMode::RCIM_Cubic;
	NewKey.TangentMode = ERichCurveTangentMode::RCTM_Auto;
	SelectedLane.Width.Keys.Insert(NewKey, KeyIndex + 1);
	MetaLandRoad::EnsureLaneWidthCurveCubic(SelectedLane.Width);

	const int32 NewKeyIndex = KeyIndex + 1;
	const double NewKeyS = SelectedLane.GetStartOffset() + SelectedLane.Width.Keys[NewKeyIndex].Time;
	SplineComp->UpdateRoadLayout();

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->SetWidthKeySelection(SectionIndex, LaneIndex, NewKeyIndex);
	RoadSectionSelectionState->SetCachedDataAtLane(SectionIndex, LaneIndex, NewKeyS, 1.0);
	EnsureNativeEditTranslateWidget();
	RefreshRoadMeshAndRedraw(SplineComp);
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteWidthKey()
{
	if (RoadSectionSelectionState->GetStateVerified() < EMetaLandRoadSectionSelectionState::Key)
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteWidthKey", "Delete Width Key"));
	SplineComp->Modify();

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	const int32 LaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
	const int32 KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
	const int32 LaneSectionIndex = MetaLandRoadLaneVisualization::ResolveLaneSectionIndexForSpan(
		SplineComp,
		SectionIndex,
		LaneIndex);
	if (LaneSectionIndex == INDEX_NONE)
	{
		return;
	}
	FMetaLandRoadLane& SelectedLane = SplineComp->GetLaneSection(LaneSectionIndex).GetLaneByIndex(LaneIndex);

	if (!SelectedLane.Width.Keys.IsValidIndex(KeyIndex))
	{
		return;
	}

	if (SelectedLane.Width.Keys.Num() > 1)
	{
		SelectedLane.Width.Keys.RemoveAt(KeyIndex);
		MetaLandRoad::EnsureLaneWidthCurveCubic(SelectedLane.Width);
	}

	RoadSectionSelectionState->Modify();
	RoadSectionSelectionState->ClearWidthKeySelection();

	SplineComp->UpdateRoadLayout();
	RefreshRoadMeshAndRedraw(SplineComp);
}

void FMoKuEditSplinesComponentVisualizer::GenerateLaneWidthContextMenuSections(FMenuBuilder& InMenuBuilder) const
{
	if (!FMetaLandRoadEditorModule::Get().IsLaneEditMode() || !RoadSectionSelectionState)
	{
		return;
	}

	const FMetaLandRoadSectionComponentVisualizerCommands& Commands = FMetaLandRoadSectionComponentVisualizerCommands::Get();
	const bool bWidthKeySelected =
		RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key;
	const bool bShowWidthMenu = RoadSectionSelectionState->HasLaneWidthMenuContext()
		|| CachedLaneHoveredLaneIndex != LANE_INDEX_NONE
		|| RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;

	InMenuBuilder.BeginSection("MetaLandRoadLaneEdit", LOCTEXT("ContextMenuRoadLane", "Lane"));
	InMenuBuilder.AddMenuEntry(Commands.AddLaneToLeft);
	InMenuBuilder.AddMenuEntry(Commands.AddLaneToRight);
	InMenuBuilder.AddMenuEntry(Commands.DeleteLane);
	InMenuBuilder.EndSection();

	if (bShowWidthMenu)
	{
		InMenuBuilder.BeginSection("MetaLandRoadLaneWidth", LOCTEXT("ContextMenuRoadWidth", "Width"));
		InMenuBuilder.AddMenuEntry(Commands.AddWidthKey);
		if (bWidthKeySelected)
		{
			InMenuBuilder.AddMenuEntry(Commands.DeleteWidthKey);
		}
		InMenuBuilder.EndSection();
	}
}

bool FMoKuEditSplinesComponentVisualizer::HasMainLineSectionMenuContext() const
{
	if (!bSectionLineEditMode)
	{
		return false;
	}

	// Hovering the center main line (not a boundary cross).
	if (HoveredSectionIndex != INDEX_NONE && HoveredBoundarySectionIndex == INDEX_NONE)
	{
		return true;
	}

	// Selected lane strips ->Add Section uses their faces.
	if (RoadSectionSelectionState
		&& (RoadSectionSelectionState->GetSelectedLaneIndex() != LANE_INDEX_NONE
			|| RoadSectionSelectionState->GetMultiSelectedLanes().Num() > 0))
	{
		return true;
	}

	// Selected section span (not boundary-only SOffset pick).
	if (RoadSectionSelectionState
		&& RoadSectionSelectionState->GetSelectedSectionIndex() != INDEX_NONE
		&& !RoadSectionSelectionState->IsSectionBoundarySelected())
	{
		return true;
	}

	return false;
}

void FMoKuEditSplinesComponentVisualizer::AppendMetaLandRoadCurveMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.BeginSection("MetaLandRoadCurve", LOCTEXT("MetaLandRoadCurve", "MetaLandRoad Curve"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddSplinePointHere", "Add Spline Point Here"),
		LOCTEXT("AddSplinePointHereTooltip", "Insert a point on the Bezier at the cursor."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(
				const_cast<FMoKuEditSplinesComponentVisualizer*>(this),
				&FMoKuEditSplinesComponentVisualizer::OnSafeAddKeyToSegment),
			FCanExecuteAction::CreateSP(
				const_cast<FMoKuEditSplinesComponentVisualizer*>(this),
				&FMoKuEditSplinesComponentVisualizer::CanAddSplinePointOnCurve)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteSelectedSplinePoint", "Delete Selected Point"),
		LOCTEXT("DeleteSelectedSplinePointTooltip", "Delete the selected spline control point. Delete / Backspace also work."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(
				const_cast<FMoKuEditSplinesComponentVisualizer*>(this),
				&FMoKuEditSplinesComponentVisualizer::OnDeleteSelectedSplinePoints),
			FCanExecuteAction::CreateSP(
				const_cast<FMoKuEditSplinesComponentVisualizer*>(this),
				&FMoKuEditSplinesComponentVisualizer::CanDeleteSelectedSplinePoints)));
	MenuBuilder.EndSection();
}

TSharedPtr<SWidget> FMoKuEditSplinesComponentVisualizer::GenerateContextMenu() const
{
	if (FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay())
	{
		FMenuBuilder PropMenu(true, nullptr);
		GeneratePropSettingContextMenuSections(PropMenu);
		return PropMenu.MakeWidget();
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay())
	{
		FMenuBuilder LandscapeMenu(true, SectionComponentVisualizerActions);
		GenerateLandscapeAttributeContextMenuSections(LandscapeMenu);
		return LandscapeMenu.MakeWidget();
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode())
	{
		FMenuBuilder LaneMenu(true, SectionComponentVisualizerActions);
		GenerateLaneWidthContextMenuSections(LaneMenu);
		return LaneMenu.MakeWidget();
	}

	if (IsSectionLineEditMode())
	{
		if (!HasMainLineSectionMenuContext())
		{
			return nullptr;
		}

		FMenuBuilder SectionMenu(true, SectionComponentVisualizerActions);
		GenerateContextMenuSections(SectionMenu);
		return SectionMenu.MakeWidget();
	}

	FMenuBuilder MenuBuilder(true, nullptr);
	AppendMetaLandRoadCurveMenu(MenuBuilder);
	return MenuBuilder.MakeWidget();
}

void FMoKuEditSplinesComponentVisualizer::GenerateContextMenuSections(FMenuBuilder& InMenuBuilder) const
{
	if (!bSectionLineEditMode)
	{
		// Never forward to FSplineComponentVisualizer  - that is the default Snap/Align menu.
		AppendMetaLandRoadCurveMenu(InMenuBuilder);
		return;
	}

	if (!HasMainLineSectionMenuContext())
	{
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}

	check(RoadSectionSelectionState);
	if (SplineComp->GetLaneSectionsNum() == 0)
	{
		return;
	}
	auto MakeSectionAction = [this](TFunction<void()> ExecuteFn, TFunction<bool()> CanExecuteFn)
	{
		return FUIAction(
			FExecuteAction::CreateLambda([ExecuteFn]() { ExecuteFn(); }),
			FCanExecuteAction::CreateLambda([CanExecuteFn]() { return CanExecuteFn(); }));
	};

	const auto CanEditSection = [this]()
	{
		return this->bSectionLineEditMode && this->HasMainLineSectionMenuContext();
	};

	const FMetaLandRoadSectionComponentVisualizerCommands& Commands =
		FMetaLandRoadSectionComponentVisualizerCommands::Get();

	InMenuBuilder.BeginSection("MetaLandRoadSection", LOCTEXT("ContextMenuRoadSection", "Road Section"));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("AddSectionFromSelection", "Add Section"),
		LOCTEXT(
			"AddSectionFromSelectionTooltip",
			"Split at the pick using selected lanes' faces. No lane selection ->both faces."),
		FSlateIcon(),
		MakeSectionAction(
			[this]()
			{
				const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->TryAddSectionFromLaneSelection();
			},
			CanEditSection));
	InMenuBuilder.AddMenuEntry(Commands.AddLeftSection);
	InMenuBuilder.AddMenuEntry(Commands.AddRightSection);
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteSection", "Delete Section"),
		LOCTEXT("DeleteSectionTooltip", "Delete current road section."),
		FSlateIcon(),
		MakeSectionAction(
			[this]() { const_cast<FMoKuEditSplinesComponentVisualizer*>(this)->OnDeleteSection(); },
			CanEditSection));
	InMenuBuilder.AddMenuEntry(Commands.DeleteLeftSection);
	InMenuBuilder.AddMenuEntry(Commands.DeleteRightSection);
	InMenuBuilder.EndSection();
}

void FMoKuEditSplinesComponentVisualizer::OnSetRoadSplinePointType(EMetaLandRoadSplinePointType Mode)
{
	const FScopedTransaction Transaction(LOCTEXT("SetRoadSplinePointType", "Set Spline Point Type"));

	UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(GetEditedSplineComponent());
	if (!SplineComp)
	{
		return;
	}

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	int32 LastEditedPointIndex = INDEX_NONE;
	const TSet<int32>& SelectedKeys = GetSelectedKeys();
	for (int32 SelectedKeyIndex : SelectedKeys)
	{
		if (SelectedKeyIndex >= 0 && SelectedKeyIndex < SplineComp->GetNumberOfSplinePoints())
		{
			SplineComp->SetRoadSplinePointType(SelectedKeyIndex, Mode, false);
			LastEditedPointIndex = SelectedKeyIndex;
		}
	}

	SplineComp->UpdateSpline(LastEditedPointIndex);
	SplineComp->TrimLaneSections();
	SplineComp->bSplineHasBeenEdited = true;
	GEditor->RedrawLevelEditingViewports(true);
}

bool FMoKuEditSplinesComponentVisualizer::IsRoadSplinePointTypeSet(EMetaLandRoadSplinePointType Mode) const
{
	if (!IsKeySelectionValid())
	{
		return false;
	}

	const UMetaLandRoadSplineComponent* SplineComp = Cast<const UMetaLandRoadSplineComponent>(GetEditedSplineComponent());
	if (!SplineComp)
	{
		return false;
	}

	for (int32 SelectedKeyIndex : GetSelectedKeys())
	{
		if (SelectedKeyIndex >= 0
			&& SelectedKeyIndex < SplineComp->GetNumberOfSplinePoints()
			&& SplineComp->GetRoadSplinePointType(SelectedKeyIndex) == Mode)
		{
			return true;
		}
	}

	return false;
}

void FMoKuEditSplinesComponentVisualizer::GenerateRoadSplinePointTypeSubMenu(FMenuBuilder& MenuBuilder) const
{
	const FMetaLandRoadSectionComponentVisualizerCommands& Commands = FMetaLandRoadSectionComponentVisualizerCommands::Get();

	MenuBuilder.AddMenuEntry(Commands.SetKeyToCurve);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToCurveCustomTangent);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToCurveClamped);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToLinear);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToConstant);
	MenuBuilder.AddMenuEntry(Commands.SetKeyToArc);
}

FTSTicker::FDelegateHandle FMoKuEditSplinesComponentVisualizer::EditorViewportTickerHandle;

void FMoKuEditSplinesComponentVisualizer::EnsureNativeEditTranslateWidget(FEditorViewportClient* ViewportClient)
{
	FEditorViewportClient* Client = ViewportClient ? ViewportClient : GCurrentLevelEditingViewportClient;
	if (Client)
	{
		Client->SetWidgetMode(UE::Widget::WM_Translate);
		Client->ShowWidget(true);
	}
	GLevelEditorModeTools().SetWidgetMode(UE::Widget::WM_Translate);
	GLevelEditorModeTools().SetShowWidget(true);
}

bool FMoKuEditSplinesComponentVisualizer::HasSelectedLaneWidthKey() const
{
	return FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key
		&& RoadSectionSelectionState->GetSelectedKeyIndex() != INDEX_NONE;
}

void FMoKuEditSplinesComponentVisualizer::GenerateLandscapeAttributeContextMenuSections(FMenuBuilder& InMenuBuilder) const
{
	const FMetaLandRoadSectionComponentVisualizerCommands& Commands =
		FMetaLandRoadSectionComponentVisualizerCommands::Get();
	InMenuBuilder.BeginSection("LandscapeAttribute", LOCTEXT("LandscapeAttributeMenu", "Landscape Attribute"));
	InMenuBuilder.AddMenuEntry(Commands.AddLandscapeKey);
	InMenuBuilder.AddMenuEntry(Commands.DeleteLandscapeKey);
	InMenuBuilder.EndSection();
}

bool FMoKuEditSplinesComponentVisualizer::FinishPickContextMenu(
	FEditorViewportClient* ViewportClient,
	bool bShowContextMenu) const
{
	if (!bShowContextMenu)
	{
		return true;
	}
	if (!ViewportClient)
	{
		return false;
	}

	// Add Section menu only when the main center line is highlighted / selected.
	if (IsSectionLineEditMode() && !HasMainLineSectionMenuContext())
	{
		return true;
	}

	const TSharedPtr<SWidget> MenuWidget = GenerateContextMenu();
	const TSharedPtr<SEditorViewport> ViewportWidget = ViewportClient->GetEditorViewportWidget();
	if (!MenuWidget.IsValid() || !ViewportWidget.IsValid())
	{
		return false;
	}

	FSlateApplication::Get().PushMenu(
		ViewportWidget.ToSharedRef(),
		FWidgetPath(),
		MenuWidget.ToSharedRef(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	return true;
}

void FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		StopEditorViewportTick();
		return;
	}

	if (FMetaLandRoadEditorModule::Get().IsSectionMode()
		|| FMetaLandRoadEditorModule::Get().IsLaneEditMode()
		|| FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay()
		|| FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay()
		|| FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay())
	{
		StartEditorViewportTick();
		return;
	}

	StopEditorViewportTick();
}

void FMoKuEditSplinesComponentVisualizer::StartEditorViewportTick()
{
	if (EditorViewportTickerHandle.IsValid())
	{
		return;
	}

	const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
		FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer();
	if (!RoadVis.IsValid())
	{
		return;
	}

	EditorViewportTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(
			RoadVis.ToSharedRef(),
			&FMoKuEditSplinesComponentVisualizer::TickEditorViewport),
		0.033f);
}

void FMoKuEditSplinesComponentVisualizer::StopEditorViewportTick()
{
	if (EditorViewportTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorViewportTickerHandle);
		EditorViewportTickerHandle.Reset();
	}
}

bool FMoKuEditSplinesComponentVisualizer::TickEditorViewport(float /*DeltaTime*/)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad")))
	{
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay())
	{
		TickPropSettingHover();
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay())
	{
		TickLandscapeAttributeHover();
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay())
	{
		TickNativeEditHover();
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode())
	{
		TickLaneEditHover();
		return true;
	}

	if (FMetaLandRoadEditorModule::Get().IsSectionMode())
	{
		return FMetaLandRoadSectionHover::Tick(0.0f);
	}

	return true;
}

void FMoKuEditSplinesComponentVisualizer::TickLaneEditHover()
{
	const int32 PrevSection = CachedLaneHoveredSectionIndex;
	const int32 PrevLane = CachedLaneHoveredLaneIndex;
	const int32 PrevWidth = CachedLaneHoveredWidthIndex;
	const int32 PrevBoundary = CachedLaneHoveredSectionBoundaryIndex;
	const bool PrevWidthIsKey = CachedLaneHoveredWidthIsKey;
	const bool PrevBoundaryLine = CachedLaneHoveredBoundaryLine;
	const float PrevSplineKey = CachedLaneHoveredSplineKey;

	// Freeze a valid hover only while a context menu is actually open.
	if (PrevSection != INDEX_NONE
		&& FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().AnyMenusVisible())
	{
		return;
	}

	CachedLaneHoveredSectionIndex = INDEX_NONE;
	CachedLaneHoveredLaneIndex = LANE_INDEX_NONE;
	CachedLaneHoveredWidthIndex = INDEX_NONE;
	CachedLaneHoveredWidthIsKey = false;
	CachedLaneHoveredSectionBoundaryIndex = INDEX_NONE;
	CachedLaneHoveredBoundaryLine = false;
	CachedLaneHoveredSplineKey = -1.0f;

	if (!GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		if (GEditor && (PrevSection != INDEX_NONE || PrevLane != LANE_INDEX_NONE || PrevWidth != INDEX_NONE
			|| PrevBoundary != INDEX_NONE || PrevWidthIsKey || PrevBoundaryLine))
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	if (!SplineComp)
	{
		if (GEditor && (PrevSection != INDEX_NONE || PrevLane != LANE_INDEX_NONE || PrevWidth != INDEX_NONE
			|| PrevBoundary != INDEX_NONE || PrevWidthIsKey || PrevBoundaryLine))
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	FViewport* Viewport = GCurrentLevelEditingViewportClient->Viewport;
	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GCurrentLevelEditingViewportClient->GetScene(),
		GCurrentLevelEditingViewportClient->EngineShowFlags));
	FSceneView* View = GCurrentLevelEditingViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return;
	}

	// Screen-space picks only  - GetHitProxy re-enters DrawVisualization.
	int32 PrimarySectionIndex = INDEX_NONE;
	int32 PrimaryLaneIndex = LANE_INDEX_NONE;
	int32 PrimaryWidthIndex = INDEX_NONE;
	TArray<FMetaLandRoadSelectedLaneRefU> MultiSelectedLanes;
	if (RoadSectionSelectionState)
	{
		PrimarySectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
		PrimaryLaneIndex = RoadSectionSelectionState->GetSelectedLaneIndex();
		MultiSelectedLanes = RoadSectionSelectionState->GetMultiSelectedLanes();
		if (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Key)
		{
			PrimaryWidthIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
		}
	}

	FMetaLandRoadLaneWidthSegmentPickResult WidthPick;
	const bool bCtrlDown =
		FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsControlDown();
	const bool bDraggingGizmo = IsMouseGizmoDrag(GCurrentLevelEditingViewportClient, Viewport);

	auto RecordHoveredSplineKey = [&](int32 SectionIndex)
	{
		CachedLaneHoveredSplineKey = -1.0f;
		if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex) || !View
			|| !GCurrentLevelEditingViewportClient)
		{
			return;
		}

		const FViewportClick Click(
			View,
			GCurrentLevelEditingViewportClient,
			EKeys::LeftMouseButton,
			IE_Pressed,
			MousePos.X,
			MousePos.Y);
		const double S0 = SplineComp->GetLaneSection(SectionIndex).SOffset;
		const double S1 = MetaLandRoad::ResolveSectionEndS(SplineComp, SectionIndex);
		CachedLaneHoveredSplineKey = SplineComp->ClosestKeyToSegment2(
			static_cast<float>(S0),
			static_cast<float>(S1),
			Click.GetOrigin(),
			Click.GetOrigin() + Click.GetDirection() * 50000.0f);
	};

	auto ApplyWidthKeyHover = [&]()
	{
		CachedLaneHoveredSectionIndex = WidthPick.SectionIndex;
		CachedLaneHoveredLaneIndex = WidthPick.LaneIndex;
		CachedLaneHoveredWidthIndex = WidthPick.WidthIndex;
		CachedLaneHoveredWidthIsKey = true;
		CachedLaneHoveredBoundaryLine = false;
		RecordHoveredSplineKey(WidthPick.SectionIndex);
	};

	auto ApplySectionBoundaryHover = [&]()
	{
		CachedLaneHoveredSectionIndex = WidthPick.SectionIndex;
		CachedLaneHoveredLaneIndex = WidthPick.LaneIndex;
		CachedLaneHoveredWidthIndex = INDEX_NONE;
		CachedLaneHoveredWidthIsKey = false;
		CachedLaneHoveredBoundaryLine = true;
		RecordHoveredSplineKey(WidthPick.SectionIndex);
	};

	auto ApplyFaceHover = [&](const FMetaLandRoadLaneFacePickResult& FacePick)
	{
		CachedLaneHoveredSectionIndex = FacePick.SectionIndex;
		CachedLaneHoveredLaneIndex = FacePick.LaneIndex;
		CachedLaneHoveredWidthIndex = INDEX_NONE;
		CachedLaneHoveredWidthIsKey = false;
		CachedLaneHoveredBoundaryLine = true;
		RecordHoveredSplineKey(FacePick.SectionIndex);
	};

	// Only freeze hover while the width-key widget is being dragged.
	if (bDraggingGizmo)
	{
		CachedLaneHoveredSectionIndex = PrevSection;
		CachedLaneHoveredLaneIndex = PrevLane;
		CachedLaneHoveredWidthIndex = PrevWidth;
		CachedLaneHoveredWidthIsKey = PrevWidthIsKey;
		CachedLaneHoveredSectionBoundaryIndex = PrevBoundary;
		CachedLaneHoveredBoundaryLine = PrevBoundaryLine;
		CachedLaneHoveredSplineKey = PrevSplineKey;
	}
	else if (bCtrlDown)
	{
		FMetaLandRoadLaneFacePickResult FacePick;
		if (MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *View, MousePos, FacePick))
		{
			ApplyFaceHover(FacePick);
		}
	}
	else if (MetaLandRoadLaneEditPick::PickLaneWidthKeyAtMouse(
		SplineComp,
		*View,
		MousePos,
		PrimarySectionIndex,
		PrimaryLaneIndex,
		PrimaryWidthIndex,
		MultiSelectedLanes,
		INDEX_NONE,
		LANE_INDEX_NONE,
		static_cast<float>(MetaLandRoadLaneEditPick::LaneWidthKeyHoverRadiusPx),
		WidthPick))
	{
		ApplyWidthKeyHover();
	}
	else if (MetaLandRoadLaneEditPick::PickLaneBoundaryOnSectionAtMouse(
		SplineComp,
		*View,
		MousePos,
		static_cast<float>(MetaLandRoadLaneEditPick::LaneBoundarySegmentHoverRadiusPx),
		WidthPick))
	{
		ApplySectionBoundaryHover();
	}
	else
	{
		FMetaLandRoadLaneFacePickResult FacePick;
		if (MetaLandRoadLaneVisualization::PickLaneFaceAtMouse(SplineComp, *View, MousePos, FacePick))
		{
			ApplyFaceHover(FacePick);
		}
	}

	if (GEditor && (PrevSection != CachedLaneHoveredSectionIndex
		|| PrevLane != CachedLaneHoveredLaneIndex
		|| PrevWidth != CachedLaneHoveredWidthIndex
		|| PrevBoundary != CachedLaneHoveredSectionBoundaryIndex
		|| PrevWidthIsKey != CachedLaneHoveredWidthIsKey
		|| PrevBoundaryLine != CachedLaneHoveredBoundaryLine))
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

void FMoKuEditSplinesComponentVisualizer::UpdateNativeHoverCacheFromViewport()
{
	if (!FMetaLandRoadEditorModule::Get().ShouldShowNativeEditOverlay() || IsSectionLineEditMode())
	{
		CachedNativeHoveredKey = INDEX_NONE;
		CachedNativeHoveredSegment = INDEX_NONE;
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	if (!SplineComp || !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		CachedNativeHoveredKey = INDEX_NONE;
		CachedNativeHoveredSegment = INDEX_NONE;
		return;
	}

	FViewport* Viewport = GCurrentLevelEditingViewportClient->Viewport;
	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GCurrentLevelEditingViewportClient->GetScene(),
		GCurrentLevelEditingViewportClient->EngineShowFlags));
	FSceneView* View = GCurrentLevelEditingViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return;
	}

	CachedNativeHoveredKey = MetaLandRoadNativeEditVisualization::PickHoveredControlPointKey(SplineComp, View, MousePos);
	CachedNativeHoveredSegment = INDEX_NONE;
	if (CachedNativeHoveredKey == INDEX_NONE)
	{
		CachedNativeHoveredSegment = MetaLandRoadNativeEditVisualization::PickHoveredSplineSegment(SplineComp, View, MousePos, 48.0f);
	}
}

void FMoKuEditSplinesComponentVisualizer::TickNativeEditHover()
{
	const int32 PrevKey = CachedNativeHoveredKey;
	const int32 PrevSegment = CachedNativeHoveredSegment;
	UpdateNativeHoverCacheFromViewport();

	if (GEditor && (PrevKey != CachedNativeHoveredKey || PrevSegment != CachedNativeHoveredSegment))
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

bool FMoKuEditSplinesComponentVisualizer::EnsureLaneEditMenuSelection()
{
	UMetaLandRoadSplineComponent* SplineComp = nullptr;
	if (RoadSectionSelectionState)
	{
		SplineComp = RoadSectionSelectionState->GetSelectedSpline();
	}
	if (!SplineComp)
	{
		SplineComp = GetEditedMetaLandRoadSpline();
	}
	if (!SplineComp)
	{
		SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	}
	if (!SplineComp)
	{
		return false;
	}

	SetActiveForSpline(SplineComp);
	FComponentPropertyPath Path(SplineComp);
	if (Path.IsValid() && RoadSectionSelectionState)
	{
		if (RoadSectionSelectionState->GetSelectedSpline() != SplineComp)
		{
			RoadSectionSelectionState->SetSelectedSpline(Path);
		}
		if (RoadSectionSelectionState->GetSelectedSectionIndex() == INDEX_NONE
			&& SplineComp->GetLaneSectionsNum() > 0)
		{
			RoadSectionSelectionState->SetSelectedSection(0);
		}
	}

	if (CachedLaneHoveredLaneIndex != LANE_INDEX_NONE
		&& CachedLaneHoveredSectionIndex != INDEX_NONE
		&& RoadSectionSelectionState
		&& RoadSectionSelectionState->GetSelectedLaneIndex() == LANE_INDEX_NONE)
	{
		RoadSectionSelectionState->SetPrimaryLaneSelection(
			CachedLaneHoveredSectionIndex,
			CachedLaneHoveredLaneIndex,
			false);
	}

	return RoadSectionSelectionState
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
}

bool FMoKuEditSplinesComponentVisualizer::CanAddLaneInCurrentMode() const
{
	if (!RoadSectionSelectionState)
	{
		return false;
	}
	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode())
	{
		return GetEditedMetaLandRoadSpline() != nullptr
			|| RoadSectionSelectionState->GetSelectedSpline() != nullptr
			|| FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline() != nullptr;
	}
	return bSectionLineEditMode
		&& RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Section;
}

bool FMoKuEditSplinesComponentVisualizer::CanDeleteLaneInCurrentMode() const
{
	if (!RoadSectionSelectionState)
	{
		return false;
	}
	if (FMetaLandRoadEditorModule::Get().IsLaneEditMode())
	{
		return RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Lane
			|| RoadSectionSelectionState->GetMultiSelectedLanes().Num() > 0
			|| CachedLaneHoveredLaneIndex != LANE_INDEX_NONE;
	}
	return bSectionLineEditMode
		&& (RoadSectionSelectionState->GetStateVerified() >= EMetaLandRoadSectionSelectionState::Lane
			|| RoadSectionSelectionState->GetMultiSelectedLanes().Num() > 0);
}

void FMoKuEditSplinesComponentVisualizer::SetActiveForSpline(UMetaLandRoadSplineComponent* SplineComp)
{
	if (!SplineComp)
	{
		return;
	}

	FComponentPropertyPath Path(SplineComp);
	if (Path.IsValid() && SelectionState)
	{
		SelectionState->SetSplinePropertyPath(Path);
	}
	if (Path.IsValid() && RoadSectionSelectionState)
	{
		RoadSectionSelectionState->SetSelectedSpline(Path);
	}

	if (GUnrealEd && GCurrentLevelEditingViewportClient)
	{
		ActivateRoadVisualization();
	}
}

bool FMoKuEditSplinesComponentVisualizer::EnsureKeySelectionForSegmentSplit()
{
	USplineComponent* SplineComp = ResolveSplineForAddPoint();
	if (!SplineComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[MetaLandRoad] Add point aborted: no edited spline"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991140, 3.0f, FColor::Red,
				TEXT("[MetaLandRoad] Add point failed: no spline selection"));
		}
		return false;
	}

	if (SelectionState)
	{
		FComponentPropertyPath Path(SplineComp);
		if (Path.IsValid())
		{
			SelectionState->SetSplinePropertyPath(Path);
		}
	}

	const int32 NumPts = SplineComp->GetNumberOfSplinePoints();
	if (NumPts < 2)
	{
		return false;
	}

	SplineComp->UpdateSpline();
	int32 NumSegs = SplineComp->GetNumberOfSplineSegments();
	if (NumSegs < 1)
	{
		NumSegs = SplineComp->IsClosedLoop() ? NumPts : (NumPts - 1);
	}

	int32 Seg = SelectionState ? SelectionState->GetSelectedSegmentIndex() : INDEX_NONE;
	if (Seg == INDEX_NONE || Seg < 0 || Seg >= NumSegs)
	{
		Seg = CachedNativeHoveredSegment;
	}
	if (Seg == INDEX_NONE || Seg < 0 || Seg >= NumSegs)
	{
		if (CachedNativeHoveredKey != INDEX_NONE && CachedNativeHoveredKey >= 0)
		{
			Seg = FMath::Clamp(CachedNativeHoveredKey, 0, NumSegs - 1);
		}
		else
		{
			Seg = 0;
		}
	}

	const int32 EndPt = SplineComp->IsClosedLoop()
		? ((Seg + 1) % NumPts)
		: FMath::Min(Seg + 1, NumPts - 1);
	const FVector SegA = SplineComp->GetLocationAtSplinePoint(Seg, ESplineCoordinateSpace::World);
	const FVector SegB = SplineComp->GetLocationAtSplinePoint(EndPt, ESplineCoordinateSpace::World);
	FVector ClickWorld = SelectionState ? SelectionState->GetSelectedSplinePosition() : FVector::ZeroVector;
	const bool bClickOnSegment = !ClickWorld.IsNearlyZero()
		&& FVector::DistSquared(ClickWorld, SegA) + FVector::DistSquared(ClickWorld, SegB)
			< FMath::Square(FVector::Dist(SegA, SegB) + 4000.0);
	if (!bClickOnSegment)
	{
		ClickWorld = CaptureSplineAddClickWorld(SplineComp, Seg, (SegA + SegB) * 0.5);
	}
	if (ClickWorld.IsNearlyZero())
	{
		ClickWorld = (SegA + SegB) * 0.5;
	}

	if (SelectionState)
	{
		SelectionState->Modify();
		SelectionState->SetSelectedSegmentIndex(Seg);
		SelectionState->SetSelectedSplinePosition(ClickWorld);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MetaLandRoad] Add point ready: seg=%d pts=%d pos=%s (no key auto-select)"),
		Seg,
		NumPts,
		*ClickWorld.ToCompactString());
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::TryInsertSplinePointPreservingCurve()
{
	USplineComponent* SplineComp = ResolveSplineForAddPoint();
	if (!SplineComp)
	{
		return false;
	}
	if (SelectionState)
	{
		FComponentPropertyPath Path(SplineComp);
		if (Path.IsValid())
		{
			SelectionState->SetSplinePropertyPath(Path);
		}
	}

	const int32 NumPts = SplineComp->GetNumberOfSplinePoints();
	if (NumPts < 2)
	{
		UE_LOG(LogTemp, Error, TEXT("[MetaLandRoad] Add point: need at least 2 spline points"));
		return false;
	}

	SplineComp->UpdateSpline();
	int32 NumSegs = SplineComp->GetNumberOfSplineSegments();
	if (NumSegs < 1)
	{
		NumSegs = SplineComp->IsClosedLoop() ? NumPts : (NumPts - 1);
	}
	int32 Seg = SelectionState ? SelectionState->GetSelectedSegmentIndex() : INDEX_NONE;
	if (Seg == INDEX_NONE || Seg < 0 || Seg >= NumSegs)
	{
		Seg = CachedNativeHoveredSegment;
	}
	if (Seg == INDEX_NONE || Seg < 0 || Seg >= NumSegs)
	{
		Seg = 0;
	}
	if (Seg + 1 >= NumPts && !SplineComp->IsClosedLoop())
	{
		UE_LOG(LogTemp, Error, TEXT("[MetaLandRoad] Add point: invalid segment %d (pts=%d segs=%d)"), Seg, NumPts, NumSegs);
		return false;
	}

	const int32 I0 = Seg;
	const int32 I1 = (Seg + 1 < NumPts) ? (Seg + 1) : 0;
	const int32 InsertIndex = Seg + 1;
	const FVector P0 = SplineComp->GetLocationAtSplinePoint(I0, ESplineCoordinateSpace::World);
	const FVector P1 = SplineComp->GetLocationAtSplinePoint(I1, ESplineCoordinateSpace::World);
	FVector ClickWorld = SelectionState ? SelectionState->GetSelectedSplinePosition() : FVector::ZeroVector;
	if (ClickWorld.IsNearlyZero())
	{
		ClickWorld = CaptureSplineAddClickWorld(SplineComp, Seg, (P0 + P1) * 0.5);
	}
	const FVector Leave0 = SplineComp->GetLeaveTangentAtSplinePoint(I0, ESplineCoordinateSpace::World);
	const FVector Arrive1 = SplineComp->GetArriveTangentAtSplinePoint(I1, ESplineCoordinateSpace::World);
	const FVector C1 = P0 + Leave0 / 3.0;
	const FVector C2 = P1 - Arrive1 / 3.0;

	auto EvalBez = [](const FVector& A, const FVector& B, const FVector& C, const FVector& D, double T)
	{
		const double U = 1.0 - T;
		return (U * U * U) * A + (3.0 * U * U * T) * B + (3.0 * U * T * T) * C + (T * T * T) * D;
	};

	double BestT = 0.5;
	double BestDistSq = TNumericLimits<double>::Max();
	FVector InsertWorld = EvalBez(P0, C1, C2, P1, 0.5);
	for (int32 Step = 1; Step < 32; ++Step)
	{
		const double T = static_cast<double>(Step) / 32.0;
		const FVector P = EvalBez(P0, C1, C2, P1, T);
		const double DistSq = FVector::DistSquared(P, ClickWorld);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestT = T;
			InsertWorld = P;
		}
	}
	BestT = FMath::Clamp(BestT, 0.08, 0.92);

	const FVector A = FMath::Lerp(P0, C1, BestT);
	const FVector B = FMath::Lerp(C1, C2, BestT);
	const FVector C = FMath::Lerp(C2, P1, BestT);
	const FVector D = FMath::Lerp(A, B, BestT);
	const FVector E = FMath::Lerp(B, C, BestT);
	InsertWorld = FMath::Lerp(D, E, BestT);

	const FScopedTransaction Transaction(NSLOCTEXT("MetaLandRoad", "AddSplinePointOnCurve", "Add Spline Point On Curve"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	const int32 NewIndex = InsertIndex;
	const int32 EndIndexAfter = (I1 == 0) ? 0 : (I1 + 1);
	SplineComp->AddSplinePointAtIndex(InsertWorld, NewIndex, ESplineCoordinateSpace::World, false);
	if (SplineComp->GetNumberOfSplinePoints() <= NumPts)
	{
		UE_LOG(LogTemp, Error, TEXT("[MetaLandRoad] AddSplinePointAtIndex failed at %d (pts=%d)"), NewIndex, NumPts);
		return false;
	}
	SplineComp->SetSplinePointType(I0, ESplinePointType::CurveCustomTangent, false);
	SplineComp->SetSplinePointType(NewIndex, ESplinePointType::CurveCustomTangent, false);
	SplineComp->SetSplinePointType(EndIndexAfter, ESplinePointType::CurveCustomTangent, false);

	const FVector NewLeave0 = 3.0 * (A - P0);
	const FVector NewArriveMid = 3.0 * (InsertWorld - D);
	const FVector NewLeaveMid = 3.0 * (E - InsertWorld);
	const FVector NewArrive1 = 3.0 * (P1 - C);
	const FVector KeepArrive0 = SplineComp->GetArriveTangentAtSplinePoint(I0, ESplineCoordinateSpace::World);
	const FVector KeepLeave1 = SplineComp->GetLeaveTangentAtSplinePoint(EndIndexAfter, ESplineCoordinateSpace::World);
	SplineComp->SetTangentsAtSplinePoint(I0, KeepArrive0, NewLeave0, ESplineCoordinateSpace::World, false);
	SplineComp->SetTangentsAtSplinePoint(NewIndex, NewArriveMid, NewLeaveMid, ESplineCoordinateSpace::World, false);
	SplineComp->SetTangentsAtSplinePoint(EndIndexAfter, NewArrive1, KeepLeave1, ESplineCoordinateSpace::World, false);
	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	SelectNativeEditKey(NewIndex);

	if (FProperty* CurvesProp = SplineCurvesProperty)
	{
		NotifyPropertyModified(SplineComp, CurvesProp);
	}
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SplineComp))
	{
		RoadSpline->GetRoadLayoutMutable().UpdateBounds(RoadSpline->GetSplineLength());
		RoadSpline->UpdateLaneSectionBounds();
		RoadSpline->GetRoadLayoutMutable().UpdateLayout(RoadSpline);
		RefreshRoadMeshAndRedraw(RoadSpline, true);
	}
	else if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->PostEditMove(true);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[MetaLandRoad] Inserted curve point %d on seg %d t=%.2f at %s"),
		NewIndex,
		Seg,
		BestT,
		*InsertWorld.ToCompactString());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			991142,
			3.0f,
			FColor::Green,
			FString::Printf(TEXT("[MetaLandRoad] Added spline point %d on curve"), NewIndex));
	}
	return true;
}

USplineComponent* FMoKuEditSplinesComponentVisualizer::ResolveSplineForAddPoint() const
{
	if (FMetaLandRoadEditorModule::Get().IsNativeEditMode())
	{
		if (UMetaLandRoadSplineComponent* Active = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline())
		{
			return Active;
		}
	}
	if (USplineComponent* Edited = GetEditedSplineComponent())
	{
		return Edited;
	}
	if (UMetaLandRoadSplineComponent* Meta = GetEditedMetaLandRoadSpline())
	{
		return Meta;
	}
	if (!GEditor)
	{
		return nullptr;
	}
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
		{
			if (UMoKuEditSplinesComponent* Spline = Road->GetSplineComponent())
			{
				return Spline;
			}
		}
	}
	return nullptr;
}

bool FMoKuEditSplinesComponentVisualizer::CanAddSplinePointOnCurve() const
{
	const USplineComponent* Spline = ResolveSplineForAddPoint();
	return Spline && Spline->GetNumberOfSplinePoints() >= 2;
}

bool FMoKuEditSplinesComponentVisualizer::CanDeleteSelectedSplinePoints() const
{
	const USplineComponent* Spline = ResolveSplineForAddPoint();
	if (!Spline)
	{
		return false;
	}

	TSet<int32> KeysToDelete;
	if (SelectionState)
	{
		KeysToDelete = SelectionState->GetSelectedKeys();
	}
	if (KeysToDelete.Num() == 0 && CachedNativeHoveredKey != INDEX_NONE)
	{
		KeysToDelete.Add(CachedNativeHoveredKey);
	}

	const int32 NumPts = Spline->GetNumberOfSplinePoints();
	int32 ValidCount = 0;
	for (const int32 KeyIndex : KeysToDelete)
	{
		if (KeyIndex >= 0 && KeyIndex < NumPts)
		{
			++ValidCount;
		}
	}

	const int32 MinKeep = Spline->IsClosedLoop() ? 3 : 2;
	return ValidCount > 0 && (NumPts - ValidCount) >= MinKeep;
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteSelectedSplinePoints()
{
	USplineComponent* SplineComp = ResolveSplineForAddPoint();
	if (!SplineComp || !CanDeleteSelectedSplinePoints())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991141,
				3.0f,
				FColor::Yellow,
				TEXT("[MetaLandRoad] Delete point: select a control point (keep at least 2)"));
		}
		return;
	}

	TSet<int32> KeysToDelete;
	if (SelectionState)
	{
		KeysToDelete = SelectionState->GetSelectedKeys();
	}
	if (KeysToDelete.Num() == 0 && CachedNativeHoveredKey != INDEX_NONE)
	{
		KeysToDelete.Add(CachedNativeHoveredKey);
	}

	TArray<int32> SortedKeys = KeysToDelete.Array();
	SortedKeys.Sort([](const int32 A, const int32 B) { return A > B; });

	const FScopedTransaction Transaction(NSLOCTEXT("MetaLandRoad", "DeleteSplinePoint", "Delete Spline Point"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	int32 Deleted = 0;
	for (const int32 KeyIndex : SortedKeys)
	{
		if (KeyIndex >= 0 && KeyIndex < SplineComp->GetNumberOfSplinePoints())
		{
			SplineComp->RemoveSplinePoint(KeyIndex, false);
			++Deleted;
		}
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;
	ClearNativeKeySelection();
	CachedNativeHoveredKey = INDEX_NONE;

	const int32 Remaining = SplineComp->GetNumberOfSplinePoints();
	if (Remaining > 0)
	{
		SelectNativeEditKey(FMath::Clamp(SortedKeys.Last(), 0, Remaining - 1));
	}

	if (FProperty* CurvesProp = SplineCurvesProperty)
	{
		NotifyPropertyModified(SplineComp, CurvesProp);
	}
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(SplineComp))
	{
		RoadSpline->GetRoadLayoutMutable().UpdateBounds(RoadSpline->GetSplineLength());
		RoadSpline->UpdateLaneSectionBounds();
		RoadSpline->GetRoadLayoutMutable().UpdateLayout(RoadSpline);
		RefreshRoadMeshAndRedraw(RoadSpline, true);
		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(RoadSpline->GetOwner()))
		{
			RoadActor->NotifyLandscapeBrushChanged(true, false, true);
		}
	}
	else if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->PostEditMove(true);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Deleted %d spline point(s), remaining=%d"), Deleted, Remaining);
}

void FMoKuEditSplinesComponentVisualizer::OnSafeAddKeyToSegment()
{
	USplineComponent* Spline = ResolveSplineForAddPoint();
	if (!Spline)
	{
		UE_LOG(LogTemp, Error, TEXT("[MetaLandRoad] Add point: no selected road spline"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991140, 3.0f, FColor::Red, TEXT("[MetaLandRoad] Add point failed: select the road first"));
		}
		return;
	}

	if (SelectionState)
	{
		FComponentPropertyPath Path(Spline);
		if (Path.IsValid())
		{
			SelectionState->SetSplinePropertyPath(Path);
		}
	}
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(Spline))
	{
		SetActiveForSpline(RoadSpline);
	}

	EnsureKeySelectionForSegmentSplit();

	if (!TryInsertSplinePointPreservingCurve())
	{
		UE_LOG(LogTemp, Error, TEXT("[MetaLandRoad] Add point on curve failed"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				991140,
				4.0f,
				FColor::Red,
				TEXT("[MetaLandRoad] Add point failed: could not insert on curve"));
		}
	}
}

void FMoKuEditSplinesComponentVisualizer::SyncEngineSelectionStateForSpline(
	const UMetaLandRoadSplineComponent* SplineComp)
{
	if (!SelectionState || !SplineComp)
	{
		return;
	}

	FComponentPropertyPath SplinePropertyPath(const_cast<UMetaLandRoadSplineComponent*>(SplineComp));
	if (SplinePropertyPath.IsValid()
		&& SelectionState->GetSplinePropertyPath().GetComponent() != SplineComp)
	{
		const int32 KeptKey = SelectionState->GetLastKeyIndexSelected();
		const int32 KeptSeg = SelectionState->GetSelectedSegmentIndex();
		SelectionState->SetSplinePropertyPath(SplinePropertyPath);
		const int32 NumPts = SplineComp->GetNumberOfSplinePoints();
		if (KeptKey != INDEX_NONE && KeptKey >= 0 && KeptKey < NumPts)
		{
			SelectionState->ClearSelectedKeys();
			SelectionState->ModifySelectedKeys().Add(KeptKey);
			SelectionState->SetLastKeyIndexSelected(KeptKey);
			if (KeptSeg != INDEX_NONE && KeptSeg >= 0 && KeptSeg < SplineComp->GetNumberOfSplineSegments())
			{
				SelectionState->SetSelectedSegmentIndex(KeptSeg);
			}
		}
		else
		{
			ClearNativeKeySelection();
		}
	}
}

void FMoKuEditSplinesComponentVisualizer::ClearNativeKeySelection()
{
	if (!SelectionState)
	{
		return;
	}
	SelectionState->ClearSelectedKeys();
	SelectionState->ClearSelectedSegmentIndex();
	SelectionState->ClearSelectedTangentHandle();
	SelectionState->SetLastKeyIndexSelected(INDEX_NONE);
}

void FMoKuEditSplinesComponentVisualizer::SelectNativeEditKey(
	int32 KeyIndex,
	FEditorViewportClient* ViewportClient)
{
	UMetaLandRoadSplineComponent* SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	if (!SplineComp)
	{
		SplineComp = GetEditedMetaLandRoadSpline();
	}
	if (!SplineComp || !SelectionState
		|| KeyIndex < 0
		|| KeyIndex >= SplineComp->GetNumberOfSplinePoints())
	{
		return;
	}

	FComponentPropertyPath Path(SplineComp);
	if (Path.IsValid())
	{
		SelectionState->SetSplinePropertyPath(Path);
	}
	SelectionState->ClearSelectedKeys();
	SelectionState->ModifySelectedKeys().Add(KeyIndex);
	SelectionState->SetLastKeyIndexSelected(KeyIndex);
	SelectionState->ClearSelectedSegmentIndex();
	SelectionState->ClearSelectedTangentHandle();
	SelectionState->SetSelectedSplinePosition(
		SplineComp->GetLocationAtSplinePoint(KeyIndex, ESplineCoordinateSpace::World));
	SetActiveForSpline(SplineComp);
	ActivateRoadVisualization();
	EnsureNativeEditTranslateWidget(ViewportClient);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

#undef LOCTEXT_NAMESPACE
