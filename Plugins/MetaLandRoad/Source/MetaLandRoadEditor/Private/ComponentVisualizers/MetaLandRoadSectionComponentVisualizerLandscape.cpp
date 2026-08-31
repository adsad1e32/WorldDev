// Landscape overlay story for FMoKuEditSplinesComponentVisualizer (same class, second .cpp).
// Draw / pick / key / hover / notify. Main visualizer file only dispatches.

#include "ComponentVisualizers/MetaLandRoadSectionComponentVisualizer.h"

#include "MetaLandRoadEditorColors.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadNativeEditVisualization.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadVisProxy.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplineActor.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FMoKuEditSplinesComponentVisualizer"

bool FMoKuEditSplinesComponentVisualizer::HasSelectedLandscapeAttributeKey() const
{
	return RoadSectionSelectionState
		&& RoadSectionSelectionState->HasAttributeKeySelection();
}

void FMoKuEditSplinesComponentVisualizer::ClearLandscapeAttributeKeySelection()
{
	if (RoadSectionSelectionState)
	{
		RoadSectionSelectionState->ClearAttributeKeySelection();
	}
	FMetaLandRoadEditorModule::Get().BroadcastLandscapeKeyEditorRefresh();
}

static float GetLandscapeOverlayHeight(const UMetaLandRoadSplineComponent* SplineComp)
{
	return SplineComp ? SplineComp->GetMainCenterLineHeightOffset() : 2.0f;
}

static int32 FindSectionIndexAtDistance(const UMetaLandRoadSplineComponent* SplineComp, double WorldS)
{
	if (!SplineComp || SplineComp->GetLaneSectionsNum() == 0)
	{
		return INDEX_NONE;
	}

	int32 BestIndex = 0;
	const TArray<FMetaLandRoadLaneSection>& Sections = SplineComp->GetLaneSections();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		if (WorldS + KINDA_SMALL_NUMBER >= Sections[SectionIndex].SOffset)
		{
			BestIndex = SectionIndex;
		}
	}
	return BestIndex;
}

static FVector GetLandscapeOverlayWorldAtS(const UMetaLandRoadSplineComponent* SplineComp, double WorldS)
{
	if (!SplineComp)
	{
		return FVector::ZeroVector;
	}

	// Geometric spline mid (ROffset !=0). EvalLanePosition/EvalROffset drifts onto an inner edge.
	FVector World = SplineComp->GetLocationAtDistanceAlongSpline(
		WorldS, ESplineCoordinateSpace::World);
	World = SplineComp->ApplyLinkedJunctionHeightToPosition(
		World, WorldS, ESplineCoordinateSpace::World);
	World.Z += GetLandscapeOverlayHeight(SplineComp);
	return World;
}

static UMetaLandRoadSplineComponent* ResolveLandscapeOverlaySplineFromHit(HHitProxy* HitProxy)
{
	if (!HitProxy)
	{
		return nullptr;
	}

	if (HitProxy->IsA(HComponentVisProxy::StaticGetType()))
	{
		const HComponentVisProxy* CompProxy = static_cast<const HComponentVisProxy*>(HitProxy);
		if (UMetaLandRoadSplineComponent* Spline = Cast<UMetaLandRoadSplineComponent>(
			const_cast<UActorComponent*>(CompProxy->Component.Get())))
		{
			return Spline;
		}
		if (const UActorComponent* HitComponent = CompProxy->Component.Get())
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(HitComponent->GetOwner()))
			{
				return Road->GetSplineComponent();
			}
		}
	}

	if (HitProxy->IsA(HActor::StaticGetType()))
	{
		const HActor* ActorProxy = static_cast<const HActor*>(HitProxy);
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(ActorProxy->Actor))
		{
			return Road->GetSplineComponent();
		}
		if (AActor* HitActor = ActorProxy->Actor)
		{
			return HitActor->FindComponentByClass<UMetaLandRoadSplineComponent>();
		}
	}

	return nullptr;
}

static bool PickNearestVisibleLandscapeKey(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FSceneView* View,
	const FVector2D& Screen,
	float MinPixelRadius,
	int32& OutSectionIndex,
	int32& OutKeyIndex,
	double& OutWorldS)
{
	OutSectionIndex = INDEX_NONE;
	OutKeyIndex = INDEX_NONE;
	OutWorldS = -1.0;
	if (!SplineComp || !View)
	{
		return false;
	}

	const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	FVector RingAxisX = FVector::ForwardVector;
	FVector RingAxisY = FVector::RightVector;
	float BestDistSq = TNumericLimits<float>::Max();
	const float SafeMinPx = FMath::Max(MinPixelRadius, 1.0f);

	MetaLandRoadProfile::ForEachVisibleLandscapeKey(
		SplineComp,
		RoadOwner,
		[SplineComp, View, Screen, SafeMinPx, &RingAxisX, &RingAxisY, &BestDistSq,
			&OutSectionIndex, &OutKeyIndex, &OutWorldS](int32 SectionIndex, int32 KeyIndex, double WorldS)
		{
			const FVector KeyWorld = GetLandscapeOverlayWorldAtS(SplineComp, WorldS);
			FVector2D Pixel;
			if (!View->WorldToPixel(KeyWorld, Pixel))
			{
				return true;
			}

			const float DesiredPx = 24.0f * MetaLandRoadNativeEditVisualization::NativeEditRingDisplayScale;
			const float WorldRadius = MetaLandRoadNativeEditVisualization::GetNativeEditRingWorldRadius(
				View, KeyWorld, RingAxisX, RingAxisY, DesiredPx);
			FVector2D EdgePx;
			const float RingPixelRadius = View->WorldToPixel(KeyWorld + RingAxisX * WorldRadius, EdgePx)
				? FVector2D::Distance(Pixel, EdgePx)
				: DesiredPx;
			const float HoverPixelRadius = FMath::Max(SafeMinPx, RingPixelRadius + 8.0f);
			const float DistSq = FVector2D::DistSquared(Screen, Pixel);
			if (DistSq <= HoverPixelRadius * HoverPixelRadius && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				OutSectionIndex = SectionIndex;
				OutKeyIndex = KeyIndex;
				OutWorldS = WorldS;
			}
			return true;
		});

	return OutKeyIndex != INDEX_NONE && OutWorldS >= 0.0;
}

static bool PickSplineWorldSAtClick(
	UMetaLandRoadSplineComponent* SplineComp,
	const FViewportClick& Click,
	double& OutWorldS,
	FVector& OutWorldPos)
{
	if (!SplineComp)
	{
		return false;
	}

	const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
	if (NumPoints < 2)
	{
		return false;
	}

	FVector BestLoc = SplineComp->GetLocationAtSplineInputKey(0.0f, ESplineCoordinateSpace::World);
	double BestDist = TNumericLimits<double>::Max();
	float BestKey = 0.0f;
	const int32 NumSegments = SplineComp->IsClosedLoop() ? NumPoints : NumPoints - 1;
	constexpr int32 NumSubdivisions = 24;
	for (int32 Seg = 0; Seg < NumSegments; ++Seg)
	{
		FVector SubStart = SplineComp->GetLocationAtSplineInputKey(
			static_cast<float>(Seg), ESplineCoordinateSpace::World);
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
				BestKey = KeyParam;
			}
			SubStart = SubEnd;
		}
	}

	OutWorldPos = BestLoc;
	OutWorldS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(BestKey);
	if (const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		if (!RoadOwner->ContainsJunctionMeshStation(OutWorldS, 1.0))
		{
			OutWorldS = RoadOwner->ClampToNearestJunctionMeshSpan(OutWorldS);
			OutWorldPos = SplineComp->GetLocationAtDistanceAlongSpline(
				OutWorldS, ESplineCoordinateSpace::World);
		}
	}
	return true;
}

void FMoKuEditSplinesComponentVisualizer::DrawLandscapeAttributeOverlay(
	const UMetaLandRoadSplineComponent* SplineComp,
	FPrimitiveDrawInterface* PDI,
	const FSceneView* View) const
{
	if (!SplineComp || !PDI || SplineComp->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
	const int32 NumSegments = SplineComp->IsClosedLoop() ? NumPoints : NumPoints - 1;
	constexpr int32 SamplesPerSegment = 24;
	const FLinearColor LineColor(0.55f, 0.62f, 1.0f, 1.0f);
	const FLinearColor HoverLineColor(FMetaLandRoadEditorColors::HoverColor);
	const bool bHoverLine = bCachedLandscapeHoveredLine && CachedLandscapeHoveredKeyIndex == INDEX_NONE;
	const FLinearColor DrawLineColor = bHoverLine ? HoverLineColor : LineColor;
	const float LineThickness = bHoverLine ? 4.0f : 2.5f;

	const int32 SelectedSection = RoadSectionSelectionState
		? RoadSectionSelectionState->GetSelectedSectionIndex()
		: INDEX_NONE;
	const int32 SelectedKey = HasSelectedLandscapeAttributeKey()
		? RoadSectionSelectionState->GetSelectedKeyIndex()
		: INDEX_NONE;

	PDI->SetHitProxy(new HMetaLandRoadSectionVisProxy(SplineComp, 0));
	const AMoKuEditSplineActor* OverlayRoad = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	TArray<TPair<double, double>> OverlaySpans;
	if (OverlayRoad)
	{
		OverlayRoad->GetJunctionMeshGenerationSpans(OverlaySpans);
	}
	if (OverlaySpans.Num() == 0)
	{
		for (int32 Seg = 0; Seg < NumSegments; ++Seg)
		{
			const double StartS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(static_cast<float>(Seg));
			FVector Prev = GetLandscapeOverlayWorldAtS(SplineComp, StartS);
			for (int32 Step = 1; Step <= SamplesPerSegment; ++Step)
			{
				const float KeyParam = static_cast<float>(Seg)
					+ static_cast<float>(Step) / static_cast<float>(SamplesPerSegment);
				const double NextS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(KeyParam);
				const FVector Next = GetLandscapeOverlayWorldAtS(SplineComp, NextS);
				PDI->DrawLine(Prev, Next, DrawLineColor, SDPG_Foreground, LineThickness, 0.0f, true);
				Prev = Next;
			}
		}
	}
	else
	{
		for (const TPair<double, double>& Span : OverlaySpans)
		{
			const double SpanLen = Span.Value - Span.Key;
			if (SpanLen <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const int32 NumSamples = FMath::Clamp(
				FMath::CeilToInt(SpanLen / 50.0) + 1,
				2,
				256);
			FVector Prev = GetLandscapeOverlayWorldAtS(SplineComp, Span.Key);
			for (int32 Step = 1; Step < NumSamples; ++Step)
			{
				const double NextS = FMath::Lerp(
					Span.Key,
					Span.Value,
					static_cast<double>(Step) / static_cast<double>(NumSamples - 1));
				const FVector Next = GetLandscapeOverlayWorldAtS(SplineComp, NextS);
				PDI->DrawLine(Prev, Next, DrawLineColor, SDPG_Foreground, LineThickness, 0.0f, true);
				Prev = Next;
			}
		}
	}
	PDI->SetHitProxy(nullptr);

	const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	double SelectedDisplayS = -1.0;
	if (SelectedSection != INDEX_NONE
		&& SelectedKey != INDEX_NONE
		&& SplineComp->GetLaneSections().IsValidIndex(SelectedSection))
	{
		const FMetaLandRoadLaneAttribute* SelectedTrack =
			SplineComp->GetLaneSection(SelectedSection).FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
		if (SelectedTrack && SelectedTrack->Keys.IsValidIndex(SelectedKey))
		{
			const double StoredWorldS =
				SplineComp->GetLaneSection(SelectedSection).SOffset + SelectedTrack->Keys[SelectedKey].SOffset;
			SelectedDisplayS = MetaLandRoadProfile::GetLandscapeAttributeDisplayS(
				RoadOwner, SelectedTrack, SelectedKey, StoredWorldS);
		}
	}

	MetaLandRoadProfile::ForEachVisibleLandscapeKey(
		SplineComp,
		RoadOwner,
		[this, SplineComp, PDI, View, SelectedDisplayS](int32 SectionIndex, int32 KeyIndex, double WorldS)
		{
			const FVector KeyWorld = GetLandscapeOverlayWorldAtS(SplineComp, WorldS);
			const bool bSelected = SelectedDisplayS >= 0.0
				&& FMath::Abs(WorldS - SelectedDisplayS) <= 80.0;
			const bool bHovered = CachedLandscapeHoveredWorldS >= 0.0
				&& FMath::Abs(WorldS - CachedLandscapeHoveredWorldS) <= 1.0;

			PDI->SetHitProxy(new HMetaLandRoadAttributeKeyVisProxy(SplineComp, SectionIndex, KeyIndex, WorldS));
			MetaLandRoadNativeEditVisualization::DrawNativeEditSolidBoxPDI(
				PDI, View, KeyWorld, bSelected, bHovered && !bSelected);
			PDI->SetHitProxy(nullptr);
			return true;
		});
}

bool FMoKuEditSplinesComponentVisualizer::HandleLandscapeAttributeClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click)
{
	HComponentVisProxy* VisProxy = nullptr;
	if (HitProxy && HitProxy->IsA(HComponentVisProxy::StaticGetType()))
	{
		VisProxy = static_cast<HComponentVisProxy*>(HitProxy);
	}

	UMetaLandRoadSplineComponent* SplineComp = ResolveLandscapeOverlaySplineFromHit(HitProxy);
	if (!SplineComp)
	{
		SplineComp = GetEditedMetaLandRoadSpline();
	}
	if (!SplineComp)
	{
		SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	}
	if (SplineComp
		&& InViewportClient
		&& TrySelectLandscapeAttributeKeyAtScreen(InViewportClient, SplineComp, Click.GetClickPos()))
	{
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			return FinishPickContextMenu(InViewportClient, true);
		}
		return true;
	}

	if (VisProxyHandleAttributeKeyClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}

	return TryHandleLandscapeAttributePickClick(InViewportClient, VisProxy, Click);
}

bool FMoKuEditSplinesComponentVisualizer::VisProxyHandleAttributeKeyClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->IsA(HMetaLandRoadAttributeKeyVisProxy::StaticGetType()))
	{
		return false;
	}

	HMetaLandRoadAttributeKeyVisProxy* KeyProxy = static_cast<HMetaLandRoadAttributeKeyVisProxy*>(VisProxy);
	UMetaLandRoadSplineComponent* SplineComp = Cast<UMetaLandRoadSplineComponent>(
		const_cast<UActorComponent*>(VisProxy->Component.Get()));
	if (!SplineComp || !RoadSectionSelectionState)
	{
		return false;
	}

	PrepareLandscapeKeys(SplineComp);
	SetActiveForSpline(SplineComp);
	if (KeyProxy->DisplayWorldS >= 0.0)
	{
		if (!SelectLandscapeKeyAtDisplayS(SplineComp, KeyProxy->DisplayWorldS))
		{
			return false;
		}
	}
	else
	{
		ApplyLandscapeAttributeKeySelection(SplineComp, KeyProxy->SectionIndex, KeyProxy->KeyIndex);
	}

	EnsureNativeEditTranslateWidget(InViewportClient);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}

	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		return FinishPickContextMenu(InViewportClient, true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleLandscapeAttributePickClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (Click.GetKey() != EKeys::RightMouseButton)
	{
		if (Click.GetKey() == EKeys::LeftMouseButton && HasSelectedLandscapeAttributeKey())
		{
			ClearLandscapeAttributeKeySelection();
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports(true);
			}
			return true;
		}
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && VisProxy && VisProxy->Component.IsValid())
	{
		SplineComp = Cast<UMetaLandRoadSplineComponent>(
			const_cast<UActorComponent*>(VisProxy->Component.Get()));
	}
	if (!SplineComp)
	{
		return false;
	}

	double WorldS = 0.0;
	FVector WorldPos = FVector::ZeroVector;
	if (!PickSplineWorldSAtClick(SplineComp, Click, WorldS, WorldPos))
	{
		return false;
	}

	PrepareLandscapeKeys(SplineComp);
	SetActiveForSpline(SplineComp);
	FComponentPropertyPath SplinePath(SplineComp);
	RoadSectionSelectionState->AssignSelectedSplinePath(SplinePath);
	RoadSectionSelectionState->RecordPickSplineKey(
		SplineComp->GetInputKeyValueAtDistanceAlongSpline(WorldS));
	RoadSectionSelectionState->SetLastPickSplineKey(
		SplineComp->GetInputKeyValueAtDistanceAlongSpline(WorldS));
	RoadSectionSelectionState->SetCachedData(
		WorldPos + FVector(0.0f, 0.0f, SplineComp->GetMainCenterLineHeightOffset()),
		FQuat::Identity,
		SplineComp->GetInputKeyValueAtDistanceAlongSpline(WorldS));

	const int32 SectionIndex = FindSectionIndexAtDistance(SplineComp, WorldS);
	if (SectionIndex != INDEX_NONE)
	{
		RoadSectionSelectionState->SetSelectedSection(SectionIndex);
	}

	return FinishPickContextMenu(InViewportClient, true);
}

void FMoKuEditSplinesComponentVisualizer::PrepareLandscapeKeys(UMetaLandRoadSplineComponent* SplineComp) const
{
	MetaLandRoadProfile::PrepareLandscapeKeys(SplineComp, false);
}

void FMoKuEditSplinesComponentVisualizer::OnAddLandscapeKey()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return;
	}

	PrepareLandscapeKeys(SplineComp);

	float PickKey = RoadSectionSelectionState ? RoadSectionSelectionState->GetLastPickSplineKey() : -1.0f;
	if (PickKey < 0.0f && RoadSectionSelectionState)
	{
		PickKey = RoadSectionSelectionState->GetCachedSplineKey();
	}
	if (PickKey < 0.0f)
	{
		PickKey = 0.0f;
	}

	double WorldS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(PickKey);
	if (AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		WorldS = RoadOwner->ClampToNearestJunctionMeshSpan(WorldS);
	}
	const int32 SectionIndex = FindSectionIndexAtDistance(SplineComp, WorldS);
	if (SectionIndex == INDEX_NONE)
	{
		return;
	}

	FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	FMetaLandRoadLaneAttribute* Track = Section.PinAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddLandscapeKey", "Add Landscape Key"));
	SplineComp->Modify();
	const int32 NewKeyIndex = Track->AddKey(WorldS - Section.SOffset);
	if (NewKeyIndex != INDEX_NONE)
	{
		ApplyLandscapeAttributeKeySelection(SplineComp, SectionIndex, NewKeyIndex);
		EnsureNativeEditTranslateWidget();
	}

	NotifyLandscapeAttributeChanged(SplineComp);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMoKuEditSplinesComponentVisualizer::OnDeleteLandscapeKey()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !HasSelectedLandscapeAttributeKey())
	{
		return;
	}

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	const int32 KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
	FMetaLandRoadLaneAttribute* Track = SplineComp->GetLaneSection(SectionIndex).FindAttribute(
		FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track || !Track->CanRemoveKey(KeyIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteLandscapeKey", "Delete Landscape Key"));
	SplineComp->Modify();
	Track->RemoveKey(KeyIndex);
	ClearLandscapeAttributeKeySelection();
	NotifyLandscapeAttributeChanged(SplineComp);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMoKuEditSplinesComponentVisualizer::NotifyLandscapeAttributeChanged(UMetaLandRoadSplineComponent* SplineComp) const
{
	if (!SplineComp)
	{
		return;
	}

	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		int32 SectionIndex = 0;
		int32 KeyIndex = 0;
		if (HasSelectedLandscapeAttributeKey() && RoadSectionSelectionState)
		{
			SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
			KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
		}
		RoadActor->SyncHeightmapFalloffFromLandscapeKey(SectionIndex, KeyIndex);
		RoadActor->NotifyLandscapeBrushChanged(true, false, true);

		auto PushKeyToJunction = [&](AMoKuEditIntersectionActor* Junction)
		{
			if (!Junction || Junction->bSyncingJunctionLandscape)
			{
				return;
			}
			if (!RoadActor->IsNearestEditableLandscapeKeyToJunction(Junction, SectionIndex, KeyIndex))
			{
				Junction->NotifyLandscapeBrushChanged(true, false, true);
				return;
			}
			FMetaLandRoadLandscapeAttributeValue KeyValue;
			double UnusedS = 0.0;
			if (!GetSelectedLandscapeKeyValue(KeyValue, UnusedS))
			{
				Junction->NotifyLandscapeBrushChanged(true, false, true);
				return;
			}
			Junction->bSyncingJunctionLandscape = true;
			Junction->ApplyJunctionLandscapeValue(KeyValue, /*bPushToLinkedRoads=*/false);
			Junction->bSyncingJunctionLandscape = false;
		};
		PushKeyToJunction(RoadActor->StartOfJunction.Get());
		PushKeyToJunction(RoadActor->EndOfJunction.Get());
		for (const FMetaLandRoadJunctionTrimZone& Zone : RoadActor->JunctionTrimZones)
		{
			PushKeyToJunction(Zone.Junction.Get());
		}
	}
	FMetaLandRoadEditorModule::Get().BroadcastLandscapeKeyEditorRefresh();
}

void FMoKuEditSplinesComponentVisualizer::RefreshLandscapeKeyCachedWidget(
	UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	int32 KeyIndex) const
{
	if (!SplineComp || !RoadSectionSelectionState
		|| !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return;
	}

	const FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(SectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
	{
		return;
	}

	const double StoredWorldS = SplineComp->GetLaneSection(SectionIndex).SOffset + Track->Keys[KeyIndex].SOffset;
	const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	const double WorldS = MetaLandRoadProfile::GetLandscapeAttributeDisplayS(
		RoadOwner, Track, KeyIndex, StoredWorldS);
	const FVector KeyWorld = GetLandscapeOverlayWorldAtS(SplineComp, WorldS);
	const FVector Tangent = SplineComp->GetTangentAtDistanceAlongSpline(
		WorldS, ESplineCoordinateSpace::World);
	RoadSectionSelectionState->SetCachedData(
		KeyWorld,
		FRotationMatrix::MakeFromX(Tangent.GetSafeNormal()).ToQuat(),
		SplineComp->GetInputKeyValueAtDistanceAlongSpline(WorldS));
	RoadSectionSelectionState->RecordPickSplineKey(
		SplineComp->GetInputKeyValueAtDistanceAlongSpline(WorldS));
}

void FMoKuEditSplinesComponentVisualizer::ApplyLandscapeAttributeKeySelection(
	UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	int32 KeyIndex)
{
	if (!SplineComp || !RoadSectionSelectionState)
	{
		return;
	}

	FComponentPropertyPath SplinePath(SplineComp);
	RoadSectionSelectionState->AssignSelectedSplinePath(SplinePath);
	RoadSectionSelectionState->SetAttributeKeySelection(SectionIndex, KeyIndex);
	RefreshLandscapeKeyCachedWidget(SplineComp, SectionIndex, KeyIndex);
	if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		RoadActor->SyncHeightmapFalloffFromLandscapeKey(SectionIndex, KeyIndex);
	}
	FMetaLandRoadEditorModule::Get().BroadcastLandscapeKeyEditorRefresh();
}

bool FMoKuEditSplinesComponentVisualizer::SelectLandscapeKeyAtDisplayS(
	UMetaLandRoadSplineComponent* SplineComp,
	double WorldS)
{
	if (!SplineComp || WorldS < 0.0)
	{
		return false;
	}

	int32 SectionIndex = INDEX_NONE;
	int32 KeyIndex = INDEX_NONE;
	if (!MetaLandRoadProfile::EnsureLandscapeKeyAtWorldS(SplineComp, WorldS, SectionIndex, KeyIndex))
	{
		return false;
	}

	ApplyLandscapeAttributeKeySelection(SplineComp, SectionIndex, KeyIndex);
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::TrySelectHoveredLandscapeAttributeKey(FEditorViewportClient* InViewportClient)
{
	TickLandscapeAttributeHover();
	if (CachedLandscapeHoveredSectionIndex == INDEX_NONE
		|| CachedLandscapeHoveredKeyIndex == INDEX_NONE)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	}
	if (!SplineComp)
	{
		return false;
	}

	PrepareLandscapeKeys(SplineComp);
	SetActiveForSpline(SplineComp);
	if (CachedLandscapeHoveredWorldS >= 0.0)
	{
		if (!SelectLandscapeKeyAtDisplayS(SplineComp, CachedLandscapeHoveredWorldS))
		{
			return false;
		}
	}
	else
	{
		ApplyLandscapeAttributeKeySelection(
			SplineComp,
			CachedLandscapeHoveredSectionIndex,
			CachedLandscapeHoveredKeyIndex);
	}
	EnsureNativeEditTranslateWidget(InViewportClient);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return HasSelectedLandscapeAttributeKey();
}

bool FMoKuEditSplinesComponentVisualizer::TrySelectLandscapeAttributeKeyAtScreen(
	FEditorViewportClient* InViewportClient,
	UMetaLandRoadSplineComponent* SplineComp,
	const FIntPoint& ScreenPos)
{
	if (!InViewportClient || !InViewportClient->Viewport || !SplineComp)
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

	int32 SectionIndex = INDEX_NONE;
	int32 KeyIndex = INDEX_NONE;
	double WorldS = -1.0;
	if (!PickNearestVisibleLandscapeKey(
		SplineComp,
		View,
		FVector2D(static_cast<float>(ScreenPos.X), static_cast<float>(ScreenPos.Y)),
		MetaLandRoadNativeEditVisualization::NativeEditKeyPickRadiusPx,
		SectionIndex,
		KeyIndex,
		WorldS))
	{
		return false;
	}

	PrepareLandscapeKeys(SplineComp);
	SetActiveForSpline(SplineComp);
	if (WorldS >= 0.0)
	{
		if (!SelectLandscapeKeyAtDisplayS(SplineComp, WorldS))
		{
			return false;
		}
	}
	else
	{
		ApplyLandscapeAttributeKeySelection(SplineComp, SectionIndex, KeyIndex);
	}

	EnsureNativeEditTranslateWidget(InViewportClient);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return HasSelectedLandscapeAttributeKey();
}

bool FMoKuEditSplinesComponentVisualizer::SelectLandscapeAttributeKey(int32 SectionIndex, int32 KeyIndex)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		return false;
	}

	PrepareLandscapeKeys(SplineComp);
	SetActiveForSpline(SplineComp);
	ApplyLandscapeAttributeKeySelection(SplineComp, SectionIndex, KeyIndex);
	EnsureNativeEditTranslateWidget();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return HasSelectedLandscapeAttributeKey();
}

double FMoKuEditSplinesComponentVisualizer::GetLandscapeSectionLocalLength(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex) const
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return 0.0;
	}

	const TArray<FMetaLandRoadLaneSection>& Sections = SplineComp->GetLaneSections();
	const double StartS = Sections[SectionIndex].SOffset;
	const double EndWorldS = (SectionIndex + 1 < Sections.Num())
		? Sections[SectionIndex + 1].SOffset
		: SplineComp->GetSplineLength();
	return FMath::Max(EndWorldS - StartS, 0.0);
}

bool FMoKuEditSplinesComponentVisualizer::GetSelectedLandscapeKeyValue(
	FMetaLandRoadLandscapeAttributeValue& OutValue,
	double& OutSOffset) const
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !RoadSectionSelectionState)
	{
		return false;
	}

	int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	int32 KeyIndex = HasSelectedLandscapeAttributeKey()
		? RoadSectionSelectionState->GetSelectedKeyIndex()
		: 0;
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		SectionIndex = 0;
		KeyIndex = 0;
	}
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	const FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(SectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
	{
		return false;
	}

	if (const FMetaLandRoadLandscapeAttributeValue* LandscapeValue =
		Track->Keys[KeyIndex].Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>())
	{
		OutValue = *LandscapeValue;
		OutSOffset = Track->Keys[KeyIndex].SOffset;
		return true;
	}
	return false;
}

bool FMoKuEditSplinesComponentVisualizer::SetSelectedLandscapeKeyValue(const FMetaLandRoadLandscapeAttributeValue& InValue)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !RoadSectionSelectionState)
	{
		return false;
	}

	int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	int32 KeyIndex = HasSelectedLandscapeAttributeKey()
		? RoadSectionSelectionState->GetSelectedKeyIndex()
		: 0;
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		SectionIndex = 0;
		KeyIndex = 0;
	}
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(SectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("EditLandscapeKey", "Edit Landscape Key"));
	SplineComp->Modify();
	FMetaLandRoadLandscapeAttributeValue StoredValue = InValue;
	StoredValue.SyncFalloffFromMetaRoadFields();
	Track->Keys[KeyIndex].Value.InitializeAs<FMetaLandRoadLandscapeAttributeValue>(StoredValue);
	NotifyLandscapeAttributeChanged(SplineComp);
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::SetSelectedLandscapeKeySOffset(double InSOffset)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !HasSelectedLandscapeAttributeKey())
	{
		return false;
	}

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(SectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("MoveLandscapeKeyS", "Move Landscape Key"));
	SplineComp->Modify();
	const double SectionLength = GetLandscapeSectionLocalLength(SplineComp, SectionIndex);
	Track->SetKeySOffset(RoadSectionSelectionState->GetSelectedKeyIndex(), InSOffset, SectionLength);
	const int32 NewKeyIndex = Track->FindKeyIndex(InSOffset);
	if (NewKeyIndex != INDEX_NONE)
	{
		RoadSectionSelectionState->SetAttributeKeySelection(SectionIndex, NewKeyIndex);
		RefreshLandscapeKeyCachedWidget(SplineComp, SectionIndex, NewKeyIndex);
	}
	NotifyLandscapeAttributeChanged(SplineComp);
	return true;
}

int32 FMoKuEditSplinesComponentVisualizer::GetSelectedLandscapeKeyDisplayIndex() const
{
	if (!HasSelectedLandscapeAttributeKey())
	{
		return 0;
	}
	return RoadSectionSelectionState->GetSelectedKeyIndex();
}

FText FMoKuEditSplinesComponentVisualizer::GetSelectedLandscapeKeyTitle() const
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !RoadSectionSelectionState)
	{
		return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
	}

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	const int32 KeyIndex = HasSelectedLandscapeAttributeKey()
		? RoadSectionSelectionState->GetSelectedKeyIndex()
		: 0;
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
	}

	const FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(SectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track)
	{
		return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
	}

	int32 Ordinal = 0;
	int32 Count = 0;
	const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	if (MetaLandRoadProfile::GetLandscapeOverlayKeyOrdinal(
		RoadOwner,
		SplineComp,
		SectionIndex,
		KeyIndex,
		Ordinal,
		Count))
	{
		if (Ordinal <= 0)
		{
			return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
		}
		if (Count > 0 && Ordinal >= Count - 1)
		{
			return LOCTEXT("LandscapeKeyEndTitle", "Landscape Key End");
		}
		return FText::Format(
			LOCTEXT("LandscapeKeyIndexTitle", "Landscape Key {0}"),
			FText::AsNumber(Ordinal));
	}

	if (Track->IsBeginKey(KeyIndex))
	{
		return LOCTEXT("LandscapeKeyBeginTitle", "Landscape Key Begin");
	}
	if (Track->IsEndKey(KeyIndex))
	{
		return LOCTEXT("LandscapeKeyEndTitle", "Landscape Key End");
	}
	return FText::Format(
		LOCTEXT("LandscapeKeyIndexTitle", "Landscape Key {0}"),
		FText::AsNumber(KeyIndex));
}

bool FMoKuEditSplinesComponentVisualizer::TryHandleLandscapeOverlayVisProxyClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay())
	{
		return false;
	}

	UMetaLandRoadSplineComponent* OverlaySpline = ResolveLandscapeOverlaySplineFromHit(VisProxy);
	if (!OverlaySpline)
	{
		OverlaySpline = GetEditedMetaLandRoadSpline();
	}
	if (OverlaySpline
		&& InViewportClient
		&& TrySelectLandscapeAttributeKeyAtScreen(InViewportClient, OverlaySpline, Click.GetClickPos()))
	{
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			return FinishPickContextMenu(InViewportClient, true);
		}
		return true;
	}
	if (VisProxyHandleAttributeKeyClick(InViewportClient, VisProxy, Click))
	{
		return true;
	}
	if (Click.GetKey() == EKeys::LeftMouseButton
		&& TrySelectHoveredLandscapeAttributeKey(InViewportClient))
	{
		return true;
	}
	return TryHandleLandscapeAttributePickClick(InViewportClient, VisProxy, Click);
}

bool FMoKuEditSplinesComponentVisualizer::HandleLandscapeAttributeInputDelta(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	const FVector& DeltaTranslate)
{
	(void)ViewportClient;
	(void)Viewport;
	if (!FMetaLandRoadEditorModule::Get().ShouldDrawLandscapeAttributeOverlay()
		|| !HasSelectedLandscapeAttributeKey())
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !RoadSectionSelectionState || DeltaTranslate.IsNearlyZero())
	{
		return true;
	}

	const int32 SectionIndex = RoadSectionSelectionState->GetSelectedSectionIndex();
	const int32 KeyIndex = RoadSectionSelectionState->GetSelectedKeyIndex();
	if (!SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return true;
	}

	FMetaLandRoadLaneAttribute* Track = SplineComp->GetLaneSection(SectionIndex).FindAttribute(
		FMetaLandRoadAttributeRegistry::TypeLandscape);
	if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
	{
		return true;
	}

	const FVector NewWorld = RoadSectionSelectionState->GetCachedPosition() + DeltaTranslate;
	const float InputKey = SplineComp->FindInputKeyClosestToWorldLocation(NewWorld);
	const double WorldS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(InputKey);
	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	const double LocalS = FMath::Max(WorldS - Section.SOffset, 0.0);
	const double SectionLength = GetLandscapeSectionLocalLength(SplineComp, SectionIndex);

	const FScopedTransaction Transaction(LOCTEXT("MoveLandscapeKey", "Move Landscape Key"));
	SplineComp->Modify();
	if (Track->IsBeginKey(KeyIndex) || Track->IsEndKey(KeyIndex))
	{
		RefreshLandscapeKeyCachedWidget(SplineComp, SectionIndex, KeyIndex);
		return true;
	}

	Track->SetKeySOffset(KeyIndex, LocalS, SectionLength);
	const double AppliedS = (SectionLength >= 0.0)
		? FMath::Clamp(LocalS, 0.0, SectionLength)
		: FMath::Max(LocalS, 0.0);
	const int32 NewKeyIndex = Track->FindKeyIndex(AppliedS);
	const int32 AppliedKey = NewKeyIndex != INDEX_NONE ? NewKeyIndex : KeyIndex;
	RoadSectionSelectionState->SetAttributeKeySelection(SectionIndex, AppliedKey);
	RefreshLandscapeKeyCachedWidget(SplineComp, SectionIndex, AppliedKey);
	FMetaLandRoadEditorModule::Get().BroadcastLandscapeKeyEditorRefresh();
	return true;
}

void FMoKuEditSplinesComponentVisualizer::TickLandscapeAttributeHover()
{
	const int32 PrevSection = CachedLandscapeHoveredSectionIndex;
	const int32 PrevKey = CachedLandscapeHoveredKeyIndex;
	const double PrevWorldS = CachedLandscapeHoveredWorldS;
	const bool PrevLine = bCachedLandscapeHoveredLine;

	CachedLandscapeHoveredSectionIndex = INDEX_NONE;
	CachedLandscapeHoveredKeyIndex = INDEX_NONE;
	CachedLandscapeHoveredWorldS = -1.0;
	bCachedLandscapeHoveredLine = false;

	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().AnyMenusVisible())
	{
		CachedLandscapeHoveredSectionIndex = PrevSection;
		CachedLandscapeHoveredKeyIndex = PrevKey;
		CachedLandscapeHoveredWorldS = PrevWorldS;
		bCachedLandscapeHoveredLine = PrevLine;
		return;
	}

	if (!GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		if (GEditor && (PrevSection != INDEX_NONE || PrevKey != INDEX_NONE || PrevLine))
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		SplineComp = FMetaLandRoadEditorModule::Get().GetActiveNativeEditSpline();
	}
	if (!SplineComp)
	{
		if (GEditor && (PrevSection != INDEX_NONE || PrevKey != INDEX_NONE || PrevLine))
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

	const FVector2D Mouse(static_cast<float>(MousePos.X), static_cast<float>(MousePos.Y));
	const AMoKuEditSplineActor* RoadOwner = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	PickNearestVisibleLandscapeKey(
		SplineComp,
		View,
		Mouse,
		MetaLandRoadNativeEditVisualization::NativeEditKeyPickRadiusPx,
		CachedLandscapeHoveredSectionIndex,
		CachedLandscapeHoveredKeyIndex,
		CachedLandscapeHoveredWorldS);

	if (CachedLandscapeHoveredKeyIndex == INDEX_NONE)
	{
		const int32 HoveredSeg = MetaLandRoadNativeEditVisualization::PickHoveredSplineSegment(
			SplineComp, View, MousePos, 48.0f);
		if (HoveredSeg != INDEX_NONE)
		{
			const double SegS0 = SplineComp->GetDistanceAlongSplineAtSplineInputKey(
				static_cast<float>(HoveredSeg));
			const double SegS1 = SplineComp->GetDistanceAlongSplineAtSplineInputKey(
				static_cast<float>(HoveredSeg + 1));
			bCachedLandscapeHoveredLine = !RoadOwner
				|| MetaLandRoadProfile::IsLandscapeOverlayStationVisible(RoadOwner, SegS0, 1.0)
				|| MetaLandRoadProfile::IsLandscapeOverlayStationVisible(RoadOwner, SegS1, 1.0);
		}
	}

	if (GEditor && (PrevSection != CachedLandscapeHoveredSectionIndex
		|| PrevKey != CachedLandscapeHoveredKeyIndex
		|| PrevLine != bCachedLandscapeHoveredLine))
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

#undef LOCTEXT_NAMESPACE
