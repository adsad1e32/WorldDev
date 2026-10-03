#include "MetaLandRoadSectionComponentVisualizer.h"
#include "MetaLandRoadCurbEditor.h"
#include "MetaLandRoadEditorModule.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"

void FMoKuEditSplinesComponentVisualizer::TickSidewalkHover()
{
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().AnyMenusVisible())
	{
		return;
	}
	if (!GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
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

	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
	const int32 PrevSign = Module.GetSidewalkHoverEdgeSign();
	const double PrevS = Module.GetSidewalkHoverS();
	const int32 PrevCorner = Module.GetSidewalkHoverCornerIndex();
	UMetaLandRoadSplineComponent* PrevSpline = Module.GetSidewalkHoverSpline();

	FSidewalkEdgePick Pick;
	const bool bHit = MetaLandRoadCurbEditor::PickSidewalkOverlay(
		View, MousePos.X, MousePos.Y, Pick);
	if (!bHit)
	{
		if (PrevSign != 0 || PrevCorner != INDEX_NONE)
		{
			Module.SetSidewalkHover(0, -1.0, nullptr);
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports(false);
			}
		}
		return;
	}

	if (Pick.HitCorner())
	{
		if (PrevCorner != Pick.CornerIndex || PrevSign != 0)
		{
			Module.SetSidewalkHoverCorner(Pick.Junction, Pick.CornerIndex);
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports(false);
			}
		}
		return;
	}

	if (PrevSign != Pick.Sign
		|| !FMath::IsNearlyEqual(PrevS, Pick.S, 8.0)
		|| PrevSpline != Pick.Spline
		|| PrevCorner != INDEX_NONE)
	{
		Module.SetSidewalkHover(Pick.Sign, Pick.S, Pick.Spline);
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
	}
}

void FMoKuEditSplinesComponentVisualizer::DrawSidewalkOverlay(
	FPrimitiveDrawInterface* PDI,
	const FSceneView* View) const
{
	(void)View;
	MetaLandRoadCurbEditor::DrawSelectedRoadPreview(PDI);
}

bool FMoKuEditSplinesComponentVisualizer::HandleSidewalkClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click)
{
	(void)HitProxy;
	const bool bLeft = Click.GetKey() == EKeys::LeftMouseButton;
	const bool bRight = Click.GetKey() == EKeys::RightMouseButton;
	if (!bLeft && !bRight)
	{
		return false;
	}
	if (!InViewportClient || !InViewportClient->Viewport)
	{
		return false;
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

	const FIntPoint ClickPos = Click.GetClickPos();
	FSidewalkEdgePick Pick;
	if (!MetaLandRoadCurbEditor::PickSidewalkOverlay(View, ClickPos.X, ClickPos.Y, Pick))
	{
		return false;
	}
	if (!MetaLandRoadCurbEditor::ApplySidewalkPick(Pick))
	{
		return false;
	}
	if (bRight)
	{
		return FinishPickContextMenu(InViewportClient, true);
	}
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}
