#pragma once

#include "CoreMinimal.h"
#include "HitProxies.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Selection.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace CompVisUtils
{
	inline bool IsSelectedInViewport(const UActorComponent* TargetComponent)
	{
		if (!TargetComponent || !GEditor)
		{
			return false;
		}

		USelection* SelectedComponents = GEditor->GetSelectedComponents();
		for (FSelectionIterator Iter(*SelectedComponents); Iter; ++Iter)
		{
			if (Cast<UActorComponent>(*Iter) == TargetComponent)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Paste_CanExecute ensureMsgf requires: if any component is selected, SelectedActorCount == 1.
	 * Never leave components selected with 0 actors.
	 */
	inline void EnsurePasteSafeActorSelection(AActor* Owner)
	{
		if (!GEditor || !Owner)
		{
			return;
		}

		// Drop every other actor without SelectNone (SelectNone can clear components too).
		TArray<AActor*> ActorsToDeselect;
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Selected = Cast<AActor>(*It))
			{
				if (Selected != Owner)
				{
					ActorsToDeselect.Add(Selected);
				}
			}
		}
		for (AActor* Other : ActorsToDeselect)
		{
			GEditor->GetSelectedActors()->Deselect(Other);
		}

		if (!GEditor->GetSelectedActors()->IsSelected(Owner))
		{
			// bNotify=false — avoid re-entrancy into OnEditorSelectionChanged mid-fix.
			GEditor->SelectActor(Owner, true, false);
		}

		// Last resort if still wrong (should be rare).
		if (GEditor->GetSelectedComponentCount() > 0 && GEditor->GetSelectedActorCount() != 1)
		{
			GEditor->GetSelectedActors()->DeselectAll();
			GEditor->SelectActor(Owner, true, false);
		}
	}

	/** True when the component handle is in the level editor actor typed-element selection set. */
	bool IsComponentInActorElementSelection(const UActorComponent* TargetComponent);

	/** Rebuild GUnrealEd::VisualizersForSelection from the current actor selection (no deselect/reselect). */
	void RefreshComponentVisualizersForSelection();

	/** Exclusive select: actor + spline in typed-element actor selection (enables ComponentVisualizer draw). */
	void SelectRoadSplineExclusive(AActor* Owner, UActorComponent* TargetComponent, bool bNotify = true);

	/** @deprecated Alias for SelectRoadSplineExclusive. */
	inline void SelectRoadSplineForNativeEdit(UActorComponent* TargetComponent, bool bNotify = true)
	{
		if (!TargetComponent)
		{
			return;
		}
		SelectRoadSplineExclusive(TargetComponent->GetOwner(), TargetComponent, bNotify);
	}

	inline void DeselectAllExcept(const UActorComponent* TargetComponent)
	{
		if (!TargetComponent || !GEditor)
		{
			return;
		}

		SelectRoadSplineExclusive(
			TargetComponent->GetOwner(),
			const_cast<UActorComponent*>(TargetComponent),
			true);
	}

	template<typename T>
	T* GetHitProxy(FViewport* Viewport, int32 X, int32 Y, int32 HitProxySize)
	{
		int32 MinX = X - HitProxySize;
		int32 MinY = Y - HitProxySize;
		int32 MaxX = X + HitProxySize;
		int32 MaxY = Y + HitProxySize;

		const FIntPoint VPSize = Viewport->GetSizeXY();
		MinX = FMath::Clamp(MinX, 0, VPSize.X - 1);
		MinY = FMath::Clamp(MinY, 0, VPSize.Y - 1);
		MaxX = FMath::Clamp(MaxX, 0, VPSize.X - 1);
		MaxY = FMath::Clamp(MaxY, 0, VPSize.Y - 1);

		const int32 TestSizeX = MaxX - MinX + 1;
		const int32 TestSizeY = MaxY - MinY + 1;
		T* HitProxy = nullptr;

		if (TestSizeX > 0 && TestSizeY > 0)
		{
			TArray<HHitProxy*> ProxyMap;
			Viewport->GetHitProxyMap(FIntRect(MinX, MinY, MaxX + 1, MaxY + 1), ProxyMap);

			double MinDist = TNumericLimits<double>::Max();
			for (int32 TestY = 0; TestY < TestSizeY; ++TestY)
			{
				for (int32 TestX = 0; TestX < TestSizeX; ++TestX)
				{
					if (T* TestProxy = HitProxyCast<T>(ProxyMap[TestY * TestSizeX + TestX]))
					{
						const double Dist = FVector2D(static_cast<float>(TestX + MinX - X), static_cast<float>(TestY + MinY - Y)).Size();
						if (!HitProxy || Dist < MinDist)
						{
							MinDist = Dist;
							HitProxy = TestProxy;
						}
					}
				}
			}
		}

		return HitProxy;
	}

	template <typename T, typename COMPARATOR_CLASS>
	int FindBestFit(const TArray<T>& Array, const COMPARATOR_CLASS& Comparator)
	{
		int Ind = 0;
		double Diff = Comparator(Array[0]);
		for (int i = 1; i < Array.Num(); ++i)
		{
			double CheckDiff = Comparator(Array[i]);
			if (CheckDiff < Diff)
			{
				Ind = i;
				Diff = CheckDiff;
			}
		}
		return Ind;
	}
}
