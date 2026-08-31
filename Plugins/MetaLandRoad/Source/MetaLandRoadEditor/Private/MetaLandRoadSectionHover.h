#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MoKuEditSplineActor.h"
#include "Selection.h"
#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"
#include "MetaLandRoadVisProxy.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "HitProxies.h"

/**
 * Alt+E section hover state (pick + highlight). Visuals draw in ComponentVisualizer PDI only.
 */
struct FMetaLandRoadSectionHover
{
	static FTSTicker::FDelegateHandle& GetTickerHandle()
	{
		static FTSTicker::FDelegateHandle Handle;
		return Handle;
	}

	static int32& GetLastHoveredSection()
	{
		static int32 Index = INDEX_NONE;
		return Index;
	}

	/** Orange 副线 (section StartS cross) under cursor. Independent of main-line hover. */
	static int32& GetLastHoveredBoundarySection()
	{
		static int32 Index = INDEX_NONE;
		return Index;
	}

	static float& GetLastHoveredSplineKey()
	{
		static float Key = -1.0f;
		return Key;
	}

	static bool& GetWasLeftMouseDown()
	{
		static bool bDown = false;
		return bDown;
	}

	/** Re-apply boundary selection next tick so viewport LMB deselection cannot win the race. */
	static int32& GetPendingDragSection()
	{
		static int32 Section = INDEX_NONE;
		return Section;
	}

	static TWeakObjectPtr<UMetaLandRoadSplineComponent>& GetLastSpline()
	{
		static TWeakObjectPtr<UMetaLandRoadSplineComponent> Spline;
		return Spline;
	}

	static int32& GetActiveDragSection()
	{
		static int32 Section = INDEX_NONE;
		return Section;
	}

	static bool& GetDragTransactionOpen()
	{
		static bool bOpen = false;
		return bOpen;
	}

	/**
	 * Boundary drag only when the cursor is near the section CROSS (StartS bar).
	 * Hovering the longitudinal section span must NOT count as boundary select.
	 */
	static int32 ResolveDragSectionIndex(
		const UMetaLandRoadSplineComponent* Spline,
		const FSceneView& View,
		const FIntPoint& MousePos,
		float MaxScreenDistance = 28.0f,
		float* OutBestDistSq = nullptr)
	{
		if (OutBestDistSq)
		{
			*OutBestDistSq = TNumericLimits<float>::Max();
		}
		if (!Spline || Spline->GetLaneSectionsNum() < 2)
		{
			return INDEX_NONE;
		}

		const FVector2D Mouse(MousePos.X, MousePos.Y);
		const float MaxDistSq = MaxScreenDistance * MaxScreenDistance;
		float BestDistSq = MaxDistSq;
		int32 BestSection = INDEX_NONE;

		for (int32 SectionIndex = 1; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
		{
			FVector Left;
			FVector Right;
			FVector Center;
			GetSectionHoverCross(Spline, SectionIndex, Left, Right, Center);

			FVector2D Left2D;
			FVector2D Right2D;
			FVector2D Center2D;
			if (!View.WorldToPixel(Left, Left2D) || !View.WorldToPixel(Right, Right2D) || !View.WorldToPixel(Center, Center2D))
			{
				continue;
			}

			const float DistSq = FMath::Min(
				FMath::Square(MetaLandRoad::DistancePointToSegment2D(Mouse, Left2D, Right2D)),
				FVector2D::DistSquared(Mouse, Center2D));
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestSection = SectionIndex;
			}
		}

		if (OutBestDistSq && BestSection != INDEX_NONE)
		{
			*OutBestDistSq = BestDistSq;
		}
		return BestSection;
	}

	/** Screen distance² from mouse to the pink main/accent span of a section. */
	static float GetMainLineScreenDistSq(
		const UMetaLandRoadSplineComponent* Spline,
		const FSceneView& View,
		const FIntPoint& MousePos,
		int32 SectionIndex)
	{
		if (!Spline || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
		{
			return TNumericLimits<float>::Max();
		}

		TArray<FVector> Points;
		BuildSectionHoverPolyline(Spline, SectionIndex, Points);
		if (Points.Num() < 2)
		{
			return TNumericLimits<float>::Max();
		}

		const FVector2D Mouse(MousePos.X, MousePos.Y);
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 StepIdx = 1; StepIdx < Points.Num(); ++StepIdx)
		{
			FVector2D A2D;
			FVector2D B2D;
			if (!View.WorldToPixel(Points[StepIdx - 1], A2D) || !View.WorldToPixel(Points[StepIdx], B2D))
			{
				continue;
			}
			BestDistSq = FMath::Min(BestDistSq, FMath::Square(MetaLandRoad::DistancePointToSegment2D(Mouse, A2D, B2D)));
		}
		return BestDistSq;
	}

	static FVector MouseToWorldOnRoadPlane(FEditorViewportClient* ViewportClient, const UMetaLandRoadSplineComponent* Spline, int32 SectionIndex)
	{
		const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
		const FVector Origin = Cursor.GetOrigin();
		const FVector Dir = Cursor.GetDirection();

		if (!Spline || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
		{
			if (FMath::Abs(Dir.Z) > KINDA_SMALL_NUMBER)
			{
				const double T = (0.0 - Origin.Z) / Dir.Z;
				if (T > 0.0)
				{
					return Origin + Dir * T;
				}
			}
			return Origin + Dir * 5000.0f;
		}

		double PlaneZ = Spline->EvalLanePosition(
			SectionIndex,
			LANE_INDEX_NONE,
			Spline->GetLaneSection(SectionIndex).SOffset,
			0.0,
			ESplineCoordinateSpace::World).Z + Spline->LaneVisualizationHeightOffset;

		FVector HitOnPlane = Origin + Dir * 5000.0f;
		if (FMath::Abs(Dir.Z) > KINDA_SMALL_NUMBER)
		{
			const double T = (PlaneZ - Origin.Z) / Dir.Z;
			if (T > 0.0)
			{
				HitOnPlane = Origin + Dir * T;
			}
		}

		const float Key = Spline->FindInputKeyClosestToWorldLocation(HitOnPlane);
		double S = Spline->GetDistanceAlongSplineAtSplineInputKey(Key);
		S = FMath::Clamp(S, 0.0, Spline->GetSplineLength());
		if (SectionIndex > 0)
		{
			S = MetaLandRoad::ClampSectionBoundaryS(Spline, SectionIndex, S);
		}

		const float Z = Spline->LaneVisualizationHeightOffset + 8.0f;
		return Spline->GetLocationAtDistanceAlongSpline(S, ESplineCoordinateSpace::World) + FVector(0.0f, 0.0f, Z);
	}

	static uint32 GetHoverBatchId(const UMetaLandRoadSplineComponent* Spline)
	{
		return static_cast<uint32>(Spline->GetUniqueID()) ^ 0x00A0BE01u;
	}

	static UMetaLandRoadSplineComponent* FindTargetSpline()
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

		return GetLastSpline().Get();
	}

	static double ResolveEndS(const UMetaLandRoadSplineComponent* Spline, int32 SectionIndex)
	{
		return MetaLandRoad::ResolveSectionEndS(Spline, SectionIndex);
	}

	static void BuildSectionHoverPolyline(
		const UMetaLandRoadSplineComponent* Spline,
		int32 SectionIndex,
		TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		if (!Spline || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
		{
			return;
		}

		const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
		const double StartS = Section.SOffset;
		const double EndS = ResolveEndS(Spline, SectionIndex);
		if (EndS <= StartS + KINDA_SMALL_NUMBER)
		{
			return;
		}

		// Match Alt+E LineBatch main (+8) so yellow hover sits on the visible pink.
		const float Z = Spline->LaneVisualizationHeightOffset + 8.0f;
		constexpr int32 NumSteps = 24;

		// Always center (pink main)  - never outer edge 副线.
		for (int32 StepIdx = 0; StepIdx <= NumSteps; ++StepIdx)
		{
			const double S = FMath::Lerp(StartS, EndS, StepIdx / static_cast<double>(NumSteps));
			const FVector Pos = Spline->EvalLanePosition(
				SectionIndex,
				LANE_INDEX_NONE,
				S,
				0.0,
				ESplineCoordinateSpace::World) + FVector(0, 0, Z);
			OutPoints.Add(Pos);
		}
	}

	static void GetSectionHoverCross(
		const UMetaLandRoadSplineComponent* Spline,
		int32 SectionIndex,
		FVector& OutLeft,
		FVector& OutRight,
		FVector& OutCenter)
	{
		OutLeft = OutRight = OutCenter = FVector::ZeroVector;
		if (!Spline || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
		{
			return;
		}

		const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
		const double S = Section.SOffset;
		const float Z = Spline->LaneVisualizationHeightOffset;

		FVector EdgeL;
		FVector EdgeR;
		FVector EdgeC;
		if (Spline->GetRoadEdgePointsAtS(S, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
		{
			OutLeft = EdgeL + FVector(0, 0, Z);
			OutRight = EdgeR + FVector(0, 0, Z);
			OutCenter = EdgeC + FVector(0, 0, Z);
			return;
		}

		OutCenter = Spline->EvalLanePosition(SectionIndex, LANE_INDEX_NONE, S, 0.0, ESplineCoordinateSpace::World)
			+ FVector(0, 0, Z);
		OutLeft = OutCenter;
		OutRight = OutCenter;

		const FMetaLandRoadLayout& Layout = Spline->GetRoadLayout();
		const FVector RightVec = Spline->GetRightVectorAtDistanceAlongSpline(S, ESplineCoordinateSpace::World);
		const float HalfW = static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth * 2.0);

		if (Section.Side == EMetaLandRoadLaneSectionSide::Both || Section.Side == EMetaLandRoadLaneSectionSide::Left)
		{
			const int32 LeftSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
			if (LeftSectionIndex != INDEX_NONE && !Spline->GetLaneSection(LeftSectionIndex).Left.IsEmpty())
			{
				const FMetaLandRoadLaneSection& LeftSection = Spline->GetLaneSection(LeftSectionIndex);
				OutLeft = Spline->EvalLanePosition(LeftSectionIndex, -LeftSection.Left.Num(), S, 1.0, ESplineCoordinateSpace::World)
					+ FVector(0, 0, Z);
			}
			else
			{
				OutLeft = OutCenter - RightVec * HalfW;
			}
		}

		if (Section.Side == EMetaLandRoadLaneSectionSide::Both || Section.Side == EMetaLandRoadLaneSectionSide::Right)
		{
			const int32 RightSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);
			if (RightSectionIndex != INDEX_NONE && !Spline->GetLaneSection(RightSectionIndex).Right.IsEmpty())
			{
				const FMetaLandRoadLaneSection& RightSection = Spline->GetLaneSection(RightSectionIndex);
				OutRight = Spline->EvalLanePosition(RightSectionIndex, RightSection.Right.Num(), S, 1.0, ESplineCoordinateSpace::World)
					+ FVector(0, 0, Z);
			}
			else
			{
				OutRight = OutCenter + RightVec * HalfW;
			}
		}
	}

	/** Map distance-along-spline to the covering section (uses ResolveEndS, not stale cache). */
	static int32 FindSectionContainingS(const UMetaLandRoadSplineComponent* Spline, double S)
	{
		if (!Spline || Spline->GetLaneSectionsNum() == 0)
		{
			return INDEX_NONE;
		}

		int32 Fallback = INDEX_NONE;
		for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
		{
			const double StartS = Spline->GetLaneSection(SectionIndex).SOffset;
			const double EndS = ResolveEndS(Spline, SectionIndex);
			if (S + KINDA_SMALL_NUMBER < StartS || S > EndS + KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Fallback = SectionIndex;
			// Prefer Both when Left/Right share the same S band (should be rare).
			if (Spline->GetLaneSection(SectionIndex).Side == EMetaLandRoadLaneSectionSide::Both)
			{
				return SectionIndex;
			}
		}
		if (Fallback != INDEX_NONE)
		{
			return Fallback;
		}
		return Spline->GetLaneSectionsNum() - 1;
	}

	/**
	 * Main-line pick at lane-viz height  - center only (never outer edge 副线).
	 * Samples EVERY section span (Both / Left / Right). Skipping Left/Right left hover gaps.
	 */
	static bool PickSectionAtMouse(
		const UMetaLandRoadSplineComponent* Spline,
		const FSceneView& View,
		const FIntPoint& MousePos,
		int32& OutSectionIndex,
		float& OutSplineKey,
		float MaxScreenDistance = 120.0f)
	{
		OutSectionIndex = INDEX_NONE;
		OutSplineKey = -1.0f;
		if (!Spline || Spline->GetLaneSectionsNum() == 0)
		{
			return false;
		}

		const FVector2D Mouse(MousePos.X, MousePos.Y);
		const float MaxDistSq = MaxScreenDistance * MaxScreenDistance;
		float BestDistSq = MaxDistSq;
		double BestS = 0.0;
		constexpr int32 NumSteps = 32;
		// Match Alt+E LineBatch main height (+8) so screen pick aligns with visible pink.
		const float Z = Spline->LaneVisualizationHeightOffset + 8.0f;

		for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
		{
			const double StartS = Spline->GetLaneSection(SectionIndex).SOffset;
			const double EndS = ResolveEndS(Spline, SectionIndex);
			if (EndS <= StartS + KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FVector PrevPos = FVector::ZeroVector;
			double PrevS = StartS;
			for (int32 StepIdx = 0; StepIdx <= NumSteps; ++StepIdx)
			{
				const double S = FMath::Lerp(StartS, EndS, StepIdx / static_cast<double>(NumSteps));
				const FVector Pos = Spline->EvalLanePosition(
					SectionIndex,
					LANE_INDEX_NONE,
					S,
					0.0,
					ESplineCoordinateSpace::World) + FVector(0, 0, Z);

				if (StepIdx > 0)
				{
					FVector2D A2D;
					FVector2D B2D;
					if (View.WorldToPixel(PrevPos, A2D) && View.WorldToPixel(Pos, B2D))
					{
						const FVector2D AB = B2D - A2D;
						const float Denom = AB.SizeSquared();
						float T = 0.0f;
						if (Denom > KINDA_SMALL_NUMBER)
						{
							T = FMath::Clamp(FVector2D::DotProduct(Mouse - A2D, AB) / Denom, 0.0f, 1.0f);
						}
						const float DistSq = FVector2D::DistSquared(Mouse, A2D + AB * T);
						if (DistSq < BestDistSq)
						{
							BestDistSq = DistSq;
							BestS = FMath::Lerp(PrevS, S, T);
						}
					}
				}
				PrevPos = Pos;
				PrevS = S;
			}
		}

		if (BestDistSq >= MaxDistSq)
		{
			return false;
		}

		OutSectionIndex = FindSectionContainingS(Spline, BestS);
		OutSplineKey = Spline->GetInputKeyValueAtDistanceAlongSpline(BestS);
		return OutSectionIndex != INDEX_NONE;
	}

	/**
	 * Orange 副线 hover only when clearly on the cross.
	 * If pink main is closer, return INDEX_NONE so main-line hover wins.
	 */
	static int32 PickHoveredBoundarySection(
		UMetaLandRoadSplineComponent* Spline,
		FViewport* Viewport,
		const FSceneView& View,
		const FIntPoint& MousePos)
	{
		if (!Spline || !Viewport || Spline->GetLaneSectionsNum() < 2)
		{
			return INDEX_NONE;
		}

		// Screen-distance only  - never Viewport->GetHitProxy from hover tick
		// (hit-proxy render re-enters CompVis DrawVisualization).
		float BoundaryDistSq = TNumericLimits<float>::Max();
		const int32 Boundary = ResolveDragSectionIndex(Spline, View, MousePos, 22.0f, &BoundaryDistSq);
		if (Boundary == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		// Prefer pink main when it is closer (or equally close) than the orange cross.
		int32 MainSection = INDEX_NONE;
		float MainKey = -1.0f;
		if (PickSectionAtMouse(Spline, View, MousePos, MainSection, MainKey, 72.0f))
		{
			const float MainDistSq = GetMainLineScreenDistSq(Spline, View, MousePos, MainSection);
			if (MainDistSq <= BoundaryDistSq)
			{
				return INDEX_NONE;
			}
		}

		return Boundary;
	}

	/** Pink main-line hover via screen-distance (no GetHitProxy  - avoids CompVis re-entry). */
	static int32 PickHoveredMainSection(
		UMetaLandRoadSplineComponent* Spline,
		FViewport* Viewport,
		const FSceneView& View,
		const FIntPoint& MousePos)
	{
		(void)Viewport;

		int32 SectionIndex = INDEX_NONE;
		float SplineKey = -1.0f;
		// Wide screen pick  - mesh / LineBatch often steal hit proxies along curves.
		if (PickSectionAtMouse(Spline, View, MousePos, SectionIndex, SplineKey, 72.0f))
		{
			GetLastHoveredSplineKey() = SplineKey;
			return SectionIndex;
		}

		GetLastHoveredSplineKey() = -1.0f;
		return INDEX_NONE;
	}

	/** Main-line hover from hit proxy only (not screen-distance). Ignores orange 副线. */
	static int32 PickHoveredSectionFromHitProxy(UMetaLandRoadSplineComponent* Spline, FViewport* Viewport, const FIntPoint& MousePos)
	{
		if (!Spline || !Viewport)
		{
			GetLastHoveredSplineKey() = -1.0f;
			return INDEX_NONE;
		}

		HHitProxy* HitProxy = Viewport->GetHitProxy(MousePos.X, MousePos.Y);
		if (!HitProxy)
		{
			GetLastHoveredSplineKey() = -1.0f;
			return INDEX_NONE;
		}

		// Boundary cross is not main-line hover (prevents 双向高亮).
		if (HitProxy->IsA(HMetaLandRoadSectionKeyVisProxy::StaticGetType()))
		{
			GetLastHoveredSplineKey() = -1.0f;
			return INDEX_NONE;
		}

		if (HMetaLandRoadLaneVisProxy* LaneProxy = HitProxyCast<HMetaLandRoadLaneVisProxy>(HitProxy))
		{
			if (LaneProxy->Component.Get() == Spline
				&& LaneProxy->LaneIndex == LANE_INDEX_NONE
				&& Spline->GetLaneSections().IsValidIndex(LaneProxy->SectionIndex))
			{
				GetLastHoveredSplineKey() = PickSplineKeyFromMouseRay(Spline, GCurrentLevelEditingViewportClient);
				return LaneProxy->SectionIndex;
			}
		}

		if (HMetaLandRoadSectionVisProxy* SectionProxy = HitProxyCast<HMetaLandRoadSectionVisProxy>(HitProxy))
		{
			if (SectionProxy->Component.Get() == Spline
				&& Spline->GetLaneSections().IsValidIndex(SectionProxy->SectionIndex))
			{
				GetLastHoveredSplineKey() = PickSplineKeyFromMouseRay(Spline, GCurrentLevelEditingViewportClient);
				return SectionProxy->SectionIndex;
			}
		}

		GetLastHoveredSplineKey() = -1.0f;
		return INDEX_NONE;
	}

	/** Live mouse ->closest approach to spline (avoids mid-height plane miss on long/curved roads). */
	static float PickSplineKeyFromMouseRay(const UMetaLandRoadSplineComponent* Spline, FEditorViewportClient* ViewportClient)
	{
		if (!Spline || !ViewportClient || !ViewportClient->Viewport)
		{
			return -1.0f;
		}

		const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
		const FVector Origin = Cursor.GetOrigin();
		FVector Dir = Cursor.GetDirection();
		if (Dir.IsNearlyZero())
		{
			return -1.0f;
		}
		Dir.Normalize();

		const double SplineLength = Spline->GetSplineLength();
		if (SplineLength <= KINDA_SMALL_NUMBER)
		{
			return -1.0f;
		}

		const float LiftZ = Spline->LaneVisualizationHeightOffset;
		const int32 NumSteps = FMath::Clamp(FMath::CeilToInt(static_cast<float>(SplineLength / 150.0)), 24, 320);
		double BestDistSq = TNumericLimits<double>::Max();
		double BestS = 0.0;
		for (int32 StepIdx = 0; StepIdx <= NumSteps; ++StepIdx)
		{
			const double S = SplineLength * (static_cast<double>(StepIdx) / NumSteps);
			FVector Point = Spline->GetLocationAtDistanceAlongSpline(S, ESplineCoordinateSpace::World);
			Point.Z += LiftZ;
			const double T = FMath::Max(0.0, FVector::DotProduct(Point - Origin, Dir));
			const FVector OnRay = Origin + Dir * static_cast<float>(T);
			const double DistSq = FVector::DistSquared(OnRay, Point);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestS = S;
			}
		}

		FVector RefineHit = Spline->GetLocationAtDistanceAlongSpline(BestS, ESplineCoordinateSpace::World);
		RefineHit.Z += LiftZ;
		const double RefineT = FMath::Max(0.0, FVector::DotProduct(RefineHit - Origin, Dir));
		RefineHit = Origin + Dir * static_cast<float>(RefineT);
		return Spline->FindInputKeyClosestToWorldLocation(RefineHit);
	}

	static void ClearHoverOverlay(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return;
		}
		if (UWorld* World = Spline->GetWorld())
		{
			if (ULineBatchComponent* LineBatch = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
			{
				LineBatch->ClearBatch(GetHoverBatchId(Spline));
			}
		}
	}

	/** Clears legacy LineBatch hover batches from older builds. Hover draws in PDI. */
	static void DrawBoundaryHoverOverlay(UMetaLandRoadSplineComponent* Spline, int32 SectionIndex)
	{
		(void)SectionIndex;
		ClearHoverOverlay(Spline);
	}

	static void DrawHoverOverlay(UMetaLandRoadSplineComponent* Spline, int32 SectionIndex)
	{
		(void)SectionIndex;
		ClearHoverOverlay(Spline);
	}

	static bool Tick(float /*DeltaTime*/)
	{
		if (!GEditor || !GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
		{
			return true;
		}

		if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaLandRoad"))
			|| !FMetaLandRoadEditorModule::Get().IsRoadSectionLineVisMode())
		{
			if (UMetaLandRoadSplineComponent* Prev = GetLastSpline().Get())
			{
				ClearHoverOverlay(Prev);
			}
			GetLastHoveredSection() = INDEX_NONE;
			GetLastHoveredBoundarySection() = INDEX_NONE;
			GetLastSpline() = nullptr;
			return true;
		}

		const TSharedPtr<FMoKuEditSplinesComponentVisualizer> RoadVis =
			FMetaLandRoadEditorModule::Get().GetRoadComponentVisualizer();

		UMetaLandRoadSplineComponent* Spline = FindTargetSpline();
		if (!Spline || Spline->GetLaneSectionsNum() == 0)
		{
			return true;
		}
		GetLastSpline() = Spline;

		// Win the race against viewport LMB clearing ComponentVis selection.
		if (GetPendingDragSection() > 0 && RoadVis.IsValid())
		{
			const int32 Pending = GetPendingDragSection();
			GetPendingDragSection() = INDEX_NONE;
			RoadVis->SelectSectionBoundaryForDrag(Pending);
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
			return true;
		}

		// Pink main hover by default; orange 副线 only when clearly closer to the cross.
		const int32 BoundaryHovered = PickHoveredBoundarySection(
			Spline,
			GCurrentLevelEditingViewportClient->Viewport,
			*View,
			MousePos);
		const int32 MainHovered = (BoundaryHovered == INDEX_NONE)
			? PickHoveredMainSection(
				Spline,
				GCurrentLevelEditingViewportClient->Viewport,
				*View,
				MousePos)
			: INDEX_NONE;

		const bool bHoverChanged =
			BoundaryHovered != GetLastHoveredBoundarySection()
			|| MainHovered != GetLastHoveredSection();

		GetLastHoveredBoundarySection() = BoundaryHovered;
		GetLastHoveredSection() = MainHovered;
		if (MainHovered == INDEX_NONE)
		{
			GetLastHoveredSplineKey() = -1.0f;
		}

		if (bHoverChanged)
		{
			if (BoundaryHovered != INDEX_NONE)
			{
				DrawBoundaryHoverOverlay(Spline, BoundaryHovered);
			}
			else if (MainHovered != INDEX_NONE)
			{
				DrawHoverOverlay(Spline, MainHovered);
			}
			else
			{
				ClearHoverOverlay(Spline);
			}

			GEditor->RedrawLevelEditingViewports(false);
		}

		// LMB: boundary select/drag, or click-empty to clear highlight.
		const bool bLMB =
			GCurrentLevelEditingViewportClient->Viewport->KeyState(EKeys::LeftMouseButton)
			|| FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
		const bool bLMBPressed = bLMB && !GetWasLeftMouseDown();
		const bool bLMBReleased = !bLMB && GetWasLeftMouseDown();
		GetWasLeftMouseDown() = bLMB;

		const bool bMods =
			FSlateApplication::Get().GetModifierKeys().IsAltDown()
			|| FSlateApplication::Get().GetModifierKeys().IsControlDown()
			|| FSlateApplication::Get().GetModifierKeys().IsShiftDown();

		if (bLMBPressed && !bMods && RoadVis.IsValid())
		{
			const int32 DragSection = (BoundaryHovered != INDEX_NONE)
				? BoundaryHovered
				: ResolveDragSectionIndex(Spline, *View, MousePos);
			if (DragSection > 0)
			{
				GetPendingDragSection() = DragSection;
				GetActiveDragSection() = DragSection;
				RoadVis->SelectSectionBoundaryForDrag(DragSection);
				Spline->Modify();
				if (!GetDragTransactionOpen())
				{
					GEditor->BeginTransaction(NSLOCTEXT("MetaLandRoadSectionHover", "DragSectionBoundary", "Drag Section Boundary"));
					GetDragTransactionOpen() = true;
				}
				Spline->Modify();
			}
			else
			{
				// Only clear when click misses orange 副线 AND main span (hit + screen).
				int32 ScreenSection = INDEX_NONE;
				float ScreenKey = -1.0f;
				const bool bNearMain = PickSectionAtMouse(
					Spline, *View, MousePos, ScreenSection, ScreenKey, 40.0f);
				if (!bNearMain)
				{
					RoadVis->ClearSectionHighlight();
					ClearHoverOverlay(Spline);
					GetLastHoveredBoundarySection() = INDEX_NONE;
					GetLastHoveredSection() = INDEX_NONE;
					GetLastHoveredSplineKey() = -1.0f;
					GEditor->RedrawLevelEditingViewports(true);
				}
			}
		}

		if (bLMB && GetActiveDragSection() > 0 && RoadVis.IsValid())
		{
			const int32 DragSection = GetActiveDragSection();
			const FVector WorldPoint = MouseToWorldOnRoadPlane(GCurrentLevelEditingViewportClient, Spline, DragSection);
			if (RoadVis->ApplySectionBoundaryWorldDrag(DragSection, WorldPoint))
			{
				if (UMetaLandRoadSectionComponentVisualizerSelectionState* Sel =
					RoadVis->GetSelectionState())
				{
					if (Sel->IsSectionBoundarySelected())
					{
						GetActiveDragSection() = Sel->GetSelectedSectionIndex();
					}
				}
				DrawBoundaryHoverOverlay(Spline, GetActiveDragSection());
			}
		}

		if (bLMBReleased)
		{
			const bool bDidBoundaryDrag = GetActiveDragSection() > 0 && RoadVis.IsValid();
			if (GetDragTransactionOpen())
			{
				GEditor->EndTransaction();
				GetDragTransactionOpen() = false;
			}
			if (bDidBoundaryDrag)
			{
				RoadVis->FinishSectionBoundaryDrag();
			}
			GetActiveDragSection() = INDEX_NONE;
		}

		return true;
	}

	static void Start()
	{
		if (GetTickerHandle().IsValid())
		{
			return;
		}
		GetTickerHandle() = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&FMetaLandRoadSectionHover::Tick),
			0.0f);
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Section hover ticker STARTED"));
	}

	static void Stop()
	{
		if (GetTickerHandle().IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GetTickerHandle());
			GetTickerHandle().Reset();
		}
		if (UMetaLandRoadSplineComponent* Prev = GetLastSpline().Get())
		{
			ClearHoverOverlay(Prev);
		}
		GetLastHoveredSection() = INDEX_NONE;
		GetLastHoveredBoundarySection() = INDEX_NONE;
		GetLastHoveredSplineKey() = -1.0f;
		GetPendingDragSection() = INDEX_NONE;
		GetActiveDragSection() = INDEX_NONE;
		if (GetDragTransactionOpen() && GEditor)
		{
			GEditor->EndTransaction();
			GetDragTransactionOpen() = false;
		}
		GetWasLeftMouseDown() = false;
		GetLastSpline() = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] Section hover ticker STOPPED"));
	}
};
