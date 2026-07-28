#pragma once

#include "CoreMinimal.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Components/LineBatchComponent.h"
#include "HitProxies.h"

/**
 * MetaRoad-style hollow ring control points (ref: white selected / lavender idle).
 * Shared by ComponentVisualizer PDI paths (Alt+W native edit).
 */
struct FMetaLandRoadKeyMarkers
{
	static inline FLinearColor IdleRingColor()
	{
		return FLinearColor(0.72f, 0.52f, 1.0f, 1.0f);
	}

	/** Bright orange — must not look like idle lavender under LineBatch/lighting. */
	static inline FLinearColor SelectedRingColor()
	{
		return FLinearColor(1.0f, 0.45f, 0.05f, 1.0f);
	}

	static inline FLinearColor SelectedGlowColor()
	{
		return FLinearColor(1.0f, 0.65f, 0.1f, 0.55f);
	}

	static inline FLinearColor HoveredRingColor()
	{
		return FLinearColor(1.0f, 0.95f, 0.1f, 1.0f);
	}

	static inline FLinearColor HoveredGlowColor()
	{
		return FLinearColor(1.0f, 0.9f, 0.0f, 0.55f);
	}

	static inline void DrawHollowRingPDI(
		FPrimitiveDrawInterface* PDI,
		const FVector& Center,
		float Radius,
		const FLinearColor& Color,
		float Thickness,
		int32 NumSides = 28)
	{
		if (!PDI || Radius <= KINDA_SMALL_NUMBER || NumSides < 3)
		{
			return;
		}

		// Flat on XY (top-down road edit); screen-space thickness keeps size readable.
		const FVector XAxis = FVector::ForwardVector;
		const FVector YAxis = FVector::RightVector;
		DrawCircle(
			PDI,
			Center,
			XAxis,
			YAxis,
			Color,
			Radius,
			NumSides,
			SDPG_Foreground,
			Thickness,
			0.0f,
			true);
	}

	/**
	 * Idle = purple/lavender. Highlight (hover or selected) = yellow.
	 * Optional HitProxy: wraps ring + center point so hollow rings are clickable/hoverable.
	 */
	static inline void DrawControlPointRingPDI(
		FPrimitiveDrawInterface* PDI,
		const FVector& Center,
		bool bSelected,
		float BaseRadius = 380.0f,
		bool bHovered = false,
		HHitProxy* HitProxy = nullptr)
	{
		if (!PDI)
		{
			return;
		}

		const bool bHighlight = bSelected || bHovered;

		if (HitProxy)
		{
			PDI->SetHitProxy(HitProxy);
			if (PDI->IsHitTesting())
			{
				DrawHollowRingPDI(PDI, Center, BaseRadius * 0.75f, IdleRingColor(), 36.0f, 32);
				DrawHollowRingPDI(PDI, Center, BaseRadius * 1.0f, IdleRingColor(), 40.0f, 32);
				DrawHollowRingPDI(PDI, Center, BaseRadius * 1.35f, IdleRingColor(), 36.0f, 32);
				PDI->DrawPoint(Center, FLinearColor::White, 48.0f, SDPG_Foreground);
			}
		}

		if (bHighlight)
		{
			DrawHollowRingPDI(PDI, Center, BaseRadius * 1.75f, HoveredGlowColor(), 30.0f, 36);
			DrawHollowRingPDI(PDI, Center, BaseRadius * 1.25f, HoveredRingColor(), 24.0f, 36);
			PDI->DrawPoint(Center, HoveredRingColor(), 18.0f, SDPG_Foreground);
		}
		else
		{
			DrawHollowRingPDI(PDI, Center, BaseRadius * 1.05f, IdleRingColor(), 20.0f, 28);
			DrawHollowRingPDI(PDI, Center, BaseRadius * 0.88f, IdleRingColor(), 14.0f, 28);
		}

		if (HitProxy)
		{
			PDI->SetHitProxy(nullptr);
		}
	}

	/** Screen-space pick radius covering the hollow ring (center distance), not a tiny 56px disc. */
	static inline float GetRingHoverPixelRadius(
		const FSceneView* View,
		const FVector& RingCenterWorld,
		float WorldRadius)
	{
		if (!View || WorldRadius <= KINDA_SMALL_NUMBER)
		{
			return 80.0f;
		}

		FVector2D CenterPx;
		FVector2D EdgePxX;
		FVector2D EdgePxY;
		if (!View->WorldToPixel(RingCenterWorld, CenterPx))
		{
			return 120.0f;
		}
		const float RadiusPxX = View->WorldToPixel(RingCenterWorld + FVector(WorldRadius, 0.0f, 0.0f), EdgePxX)
			? FVector2D::Distance(CenterPx, EdgePxX)
			: 0.0f;
		const float RadiusPxY = View->WorldToPixel(RingCenterWorld + FVector(0.0f, WorldRadius, 0.0f), EdgePxY)
			? FVector2D::Distance(CenterPx, EdgePxY)
			: 0.0f;
		const float RadiusPx = FMath::Max3(RadiusPxX, RadiusPxY, 64.0f);
		return RadiusPx + 32.0f;
	}

	static inline void DrawHollowRingLineBatch(
		ULineBatchComponent* LineBatch,
		uint32 BatchId,
		const FVector& Center,
		float Radius,
		const FLinearColor& Color,
		float Thickness,
		int32 NumSides = 28)
	{
		if (!LineBatch || Radius <= KINDA_SMALL_NUMBER || NumSides < 3)
		{
			return;
		}

		FVector Prev = Center + FVector(Radius, 0.0f, 0.0f);
		for (int32 Side = 1; Side <= NumSides; ++Side)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Side) / static_cast<float>(NumSides);
			const FVector Next = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			LineBatch->DrawLine(Prev, Next, Color, SDPG_Foreground, Thickness, 0.0f, BatchId);
			Prev = Next;
		}
	}

	/** LineBatch thickness is world cm. Hover and selected both use yellow highlight. */
	static inline void DrawControlPointRingLineBatch(
		ULineBatchComponent* LineBatch,
		uint32 BatchId,
		const FVector& Center,
		bool bSelected,
		float BaseRadius = 380.0f,
		bool bHovered = false)
	{
		if (!LineBatch)
		{
			return;
		}

		const bool bHighlight = bSelected || bHovered;
		const float WorldThickness = FMath::Max(BaseRadius * 0.18f, 56.0f);
		if (bHighlight)
		{
			DrawHollowRingLineBatch(LineBatch, BatchId, Center, BaseRadius * 1.75f, HoveredGlowColor(), WorldThickness * 1.4f, 32);
			DrawHollowRingLineBatch(LineBatch, BatchId, Center, BaseRadius * 1.25f, HoveredRingColor(), WorldThickness * 1.15f, 32);
		}
		else
		{
			DrawHollowRingLineBatch(LineBatch, BatchId, Center, BaseRadius * 1.05f, IdleRingColor(), WorldThickness, 28);
			DrawHollowRingLineBatch(LineBatch, BatchId, Center, BaseRadius * 0.88f, IdleRingColor(), WorldThickness * 0.85f, 28);
		}
	}
};
