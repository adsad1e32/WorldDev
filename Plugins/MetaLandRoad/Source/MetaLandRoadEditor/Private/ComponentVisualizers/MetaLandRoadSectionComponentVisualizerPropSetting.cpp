#include "MetaLandRoadSectionComponentVisualizer.h"
#include "MetaLandRoadEditorColors.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadPropInstances.h"
#include "MetaLandRoadVisProxy.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorViewport.h"
#include "Misc/Guid.h"
#include "Framework/Commands/UIAction.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MetaLandRoadPropSettingOverlay"

namespace MetaLandRoadPropOverlay
{
	struct FRoadBedPick
	{
		double WorldS = -1.0;
		float Alpha = 0.5f;
		int32 SectionIndex = INDEX_NONE;
		double SpanS0 = 0.0;
		double SpanS1 = 0.0;
		FVector WorldPos = FVector::ZeroVector;
	};

	static constexpr float EdgeSnap = 0.12f;
	static constexpr float DrawLiftZ = 4.0f;
	static constexpr float GuidePickPx = 8.0f;
	static constexpr float RailPickPx = 8.0f;
	static constexpr float KeyPickPx = 28.0f;

	static bool IsPlaceGuideHeld()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
		return Mods.IsShiftDown() && !Mods.IsControlDown();
	}

	static void Lift(FVector& Point)
	{
		Point.Z += DrawLiftZ;
	}

	static bool RayHitsTriangle(
		const FVector& Origin,
		const FVector& Dir,
		const FVector& V0,
		const FVector& V1,
		const FVector& V2,
		float& OutT)
	{
		const FVector Edge1 = V1 - V0;
		const FVector Edge2 = V2 - V0;
		const FVector P = FVector::CrossProduct(Dir, Edge2);
		const float Det = static_cast<float>(FVector::DotProduct(Edge1, P));
		if (FMath::Abs(Det) < KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const float InvDet = 1.0f / Det;
		const FVector TVec = Origin - V0;
		const float U = static_cast<float>(FVector::DotProduct(TVec, P)) * InvDet;
		if (U < 0.0f || U > 1.0f)
		{
			return false;
		}
		const FVector Q = FVector::CrossProduct(TVec, Edge1);
		const float V = static_cast<float>(FVector::DotProduct(Dir, Q)) * InvDet;
		if (V < 0.0f || U + V > 1.0f)
		{
			return false;
		}
		OutT = static_cast<float>(FVector::DotProduct(Edge2, Q)) * InvDet;
		return OutT > KINDA_SMALL_NUMBER;
	}

	static void CollectVisibleSpans(
		const AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* SplineComp,
		TArray<TPair<double, double>>& OutSpans)
	{
		OutSpans.Reset();
		if (Road)
		{
			Road->GetJunctionMeshGenerationSpans(OutSpans);
		}
		if (OutSpans.Num() > 0)
		{
			return;
		}
		const double Len = SplineComp ? SplineComp->GetSplineLength() : 0.0;
		if (Len > KINDA_SMALL_NUMBER)
		{
			OutSpans.Add(TPair<double, double>(0.0, Len));
		}
	}

	static bool FindVisibleSpanAtS(
		const AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* SplineComp,
		double WorldS,
		double& OutS0,
		double& OutS1)
	{
		TArray<TPair<double, double>> Spans;
		CollectVisibleSpans(Road, SplineComp, Spans);
		for (const TPair<double, double>& Span : Spans)
		{
			if (WorldS >= Span.Key - 1.0 && WorldS <= Span.Value + 1.0)
			{
				OutS0 = Span.Key;
				OutS1 = Span.Value;
				return true;
			}
		}
		return false;
	}

	static bool SameVisibleSpan(
		const AMoKuEditSplineActor* Road,
		const UMetaLandRoadSplineComponent* SplineComp,
		double WorldA,
		double WorldB)
	{
		double A0 = 0.0;
		double A1 = 0.0;
		double B0 = 0.0;
		double B1 = 0.0;
		return FindVisibleSpanAtS(Road, SplineComp, WorldA, A0, A1)
			&& FindVisibleSpanAtS(Road, SplineComp, WorldB, B0, B1)
			&& FMath::IsNearlyEqual(A0, B0, 1.0)
			&& FMath::IsNearlyEqual(A1, B1, 1.0);
	}

	static bool IsVisibleS(const AMoKuEditSplineActor* Road, const UMetaLandRoadSplineComponent* SplineComp, double WorldS)
	{
		double S0 = 0.0;
		double S1 = 0.0;
		return FindVisibleSpanAtS(Road, SplineComp, WorldS, S0, S1);
	}

	static int32 FindSectionAtS(const UMetaLandRoadSplineComponent* SplineComp, double WorldS)
	{
		if (!SplineComp)
		{
			return INDEX_NONE;
		}
		return SplineComp->GetRoadLayout().FindSectionAtSOffset(WorldS);
	}

	static float DistPointToSegment2D(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B,
		float& OutT)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		OutT = (LenSq > KINDA_SMALL_NUMBER)
			? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LenSq, 0.0f, 1.0f)
			: 0.0f;
		return FVector2D::Distance(Point, A + AB * OutT);
	}

	static bool PickRoadBedFromView(
		UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView* View,
		const FVector2D& Mouse,
		FRoadBedPick& OutPick)
	{
		if (!SplineComp || !View || SplineComp->GetNumberOfSplinePoints() < 2)
		{
			return false;
		}

		const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
		TArray<TPair<double, double>> Spans;
		CollectVisibleSpans(Road, SplineComp, Spans);
		if (Spans.Num() == 0)
		{
			return false;
		}

		float BestDist = TNumericLimits<float>::Max();
		FRoadBedPick Best;
		bool bHit = false;

		for (const TPair<double, double>& Span : Spans)
		{
			const double SpanLen = Span.Value - Span.Key;
			if (SpanLen <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const int32 Samples = FMath::Clamp(FMath::CeilToInt(SpanLen / 60.0) + 1, 2, 160);
			for (int32 Step = 0; Step < Samples; ++Step)
			{
				const double WorldS = FMath::Lerp(
					Span.Key,
					Span.Value,
					static_cast<double>(Step) / static_cast<double>(Samples - 1));
				FVector Left, Right, Center;
				if (!SplineComp->GetRoadEdgePointsAtS(WorldS, Left, Right, Center, ESplineCoordinateSpace::World))
				{
					continue;
				}

				FVector2D LeftPx;
				FVector2D RightPx;
				if (!View->WorldToPixel(Left, LeftPx) || !View->WorldToPixel(Right, RightPx))
				{
					continue;
				}

				float AcrossT = 0.5f;
				const float Dist = DistPointToSegment2D(Mouse, LeftPx, RightPx, AcrossT);
				const float HalfW = FMath::Max(FVector2D::Distance(LeftPx, RightPx) * 0.52f, 10.0f);
				if (Dist > HalfW || Dist >= BestDist)
				{
					continue;
				}

				BestDist = Dist;
				Best.WorldS = WorldS;
				Best.Alpha = AcrossT;
				Best.SpanS0 = Span.Key;
				Best.SpanS1 = Span.Value;
				Best.WorldPos = FMath::Lerp(Left, Right, AcrossT);
				Best.SectionIndex = FindSectionAtS(SplineComp, WorldS);
				bHit = true;
			}
		}

		if (bHit)
		{
			OutPick = Best;
		}
		return bHit;
	}

	static bool PickNearestSectionEdge(
		UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView* View,
		const FVector2D& Mouse,
		float MaxPx,
		EMetaLandRoadPropEdgeRef& OutEdge,
		bool& OutbCenter,
		FRoadBedPick& OutPick,
		bool bIncludeCenter)
	{
		if (!SplineComp || !View)
		{
			return false;
		}

		const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
		TArray<TPair<double, double>> Spans;
		CollectVisibleSpans(Road, SplineComp, Spans);
		float BestDist = MaxPx;
		bool bHit = false;
		OutbCenter = false;

		for (const TPair<double, double>& Span : Spans)
		{
			const double SpanLen = Span.Value - Span.Key;
			if (SpanLen <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const int32 Samples = FMath::Clamp(FMath::CeilToInt(SpanLen / 60.0) + 1, 2, 160);
			FVector2D PrevLPx;
			FVector2D PrevRPx;
			FVector2D PrevCPx;
			double PrevS = Span.Key;
			bool bHavePrev = false;
			FVector FirstL, FirstR, FirstC;
			if (SplineComp->GetRoadEdgePointsAtS(PrevS, FirstL, FirstR, FirstC, ESplineCoordinateSpace::World)
				&& View->WorldToPixel(FirstL, PrevLPx)
				&& View->WorldToPixel(FirstR, PrevRPx)
				&& View->WorldToPixel(FirstC, PrevCPx))
			{
				bHavePrev = true;
			}

			for (int32 Step = 1; Step < Samples; ++Step)
			{
				const double NextS = FMath::Lerp(
					Span.Key,
					Span.Value,
					static_cast<double>(Step) / static_cast<double>(Samples - 1));
				FVector NextL, NextR, NextC;
				FVector2D NextLPx, NextRPx, NextCPx;
				if (!SplineComp->GetRoadEdgePointsAtS(NextS, NextL, NextR, NextC, ESplineCoordinateSpace::World)
					|| !View->WorldToPixel(NextL, NextLPx)
					|| !View->WorldToPixel(NextR, NextRPx)
					|| !View->WorldToPixel(NextC, NextCPx))
				{
					bHavePrev = false;
					continue;
				}
				if (!bHavePrev)
				{
					PrevLPx = NextLPx;
					PrevRPx = NextRPx;
					PrevCPx = NextCPx;
					PrevS = NextS;
					bHavePrev = true;
					continue;
				}

				auto AcceptGuide = [&](float Dist, float AlongT, EMetaLandRoadPropEdgeRef Edge, bool bCenter, float Alpha)
				{
					if (Dist >= BestDist)
					{
						return;
					}
					BestDist = Dist;
					OutEdge = Edge;
					OutbCenter = bCenter;
					OutPick.WorldS = FMath::Lerp(PrevS, NextS, AlongT);
					OutPick.Alpha = Alpha;
					OutPick.SpanS0 = Span.Key;
					OutPick.SpanS1 = Span.Value;
					OutPick.SectionIndex = FindSectionAtS(SplineComp, OutPick.WorldS);
					bHit = true;
				};

				float AlongT = 0.0f;
				AcceptGuide(
					DistPointToSegment2D(Mouse, PrevLPx, NextLPx, AlongT),
					AlongT,
					EMetaLandRoadPropEdgeRef::SectionLeft,
					false,
					0.0f);
				AcceptGuide(
					DistPointToSegment2D(Mouse, PrevRPx, NextRPx, AlongT),
					AlongT,
					EMetaLandRoadPropEdgeRef::SectionRight,
					false,
					1.0f);
				if (bIncludeCenter)
				{
					AcceptGuide(
						DistPointToSegment2D(Mouse, PrevCPx, NextCPx, AlongT),
						AlongT,
						EMetaLandRoadPropEdgeRef::Free,
						true,
						0.5f);
				}

				PrevLPx = NextLPx;
				PrevRPx = NextRPx;
				PrevCPx = NextCPx;
				PrevS = NextS;
			}
		}

		return bHit;
	}

	static bool PickNearestAuthoredRail(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView* View,
		const FVector2D& Mouse,
		float MaxPx,
		FGuid& OutRailId,
		FRoadBedPick& OutPick)
	{
		if (!SplineComp || !View)
		{
			return false;
		}

		const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
		TArray<TPair<double, double>> Spans;
		CollectVisibleSpans(Road, SplineComp, Spans);
		if (Spans.Num() == 0)
		{
			return false;
		}

		float BestDist = MaxPx;
		bool bHit = false;
		const TArray<FMetaLandRoadLaneSection>& Sections = SplineComp->GetLaneSections();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FMetaLandRoadLaneAttribute* Track =
				Sections[SectionIndex].FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
			if (!Track)
			{
				continue;
			}

			const double SectionS0 = Sections[SectionIndex].SOffset;
			const double SectionS1 = Sections[SectionIndex].SOffsetEnd_Cached;
			for (const FMetaLandRoadPropRail& Rail : Track->Rails)
			{
				for (const TPair<double, double>& Span : Spans)
				{
					const double DrawS0 = FMath::Max(Span.Key, SectionS0);
					const double DrawS1 = FMath::Min(Span.Value, SectionS1);
					const double SpanLen = DrawS1 - DrawS0;
					if (SpanLen <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const int32 Samples = FMath::Clamp(FMath::CeilToInt(SpanLen / 60.0) + 1, 2, 160);
					FVector2D PrevPx;
					double PrevS = DrawS0;
					bool bHavePrev = false;
					for (int32 Step = 0; Step < Samples; ++Step)
					{
						const double WorldS = FMath::Lerp(
							DrawS0,
							DrawS1,
							static_cast<double>(Step) / static_cast<double>(Samples - 1));
						FVector World;
						FVector2D Pixel;
						if (!FMetaLandRoadPropSettingUtil::EvalRailWorld(SplineComp, WorldS, Rail, World)
							|| !View->WorldToPixel(World, Pixel))
						{
							bHavePrev = false;
							continue;
						}
						if (!bHavePrev)
						{
							PrevPx = Pixel;
							PrevS = WorldS;
							bHavePrev = true;
							continue;
						}

						float AlongT = 0.0f;
						const float Dist = DistPointToSegment2D(Mouse, PrevPx, Pixel, AlongT);
						if (Dist < BestDist)
						{
							BestDist = Dist;
							OutRailId = Rail.RailId;
							OutPick.WorldS = FMath::Lerp(PrevS, WorldS, AlongT);
							OutPick.Alpha = Rail.Alpha;
							OutPick.SpanS0 = Span.Key;
							OutPick.SpanS1 = Span.Value;
							OutPick.SectionIndex = SectionIndex;
							bHit = true;
						}

						PrevPx = Pixel;
						PrevS = WorldS;
					}
				}
			}
		}
		return bHit;
	}

	static bool PickRoadBed(
		UMetaLandRoadSplineComponent* SplineComp,
		const FVector& RayOrigin,
		const FVector& RayDir,
		FRoadBedPick& OutPick)
	{
		if (!SplineComp || SplineComp->GetNumberOfSplinePoints() < 2)
		{
			return false;
		}

		const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
		TArray<TPair<double, double>> Spans;
		CollectVisibleSpans(Road, SplineComp, Spans);
		if (Spans.Num() == 0)
		{
			return false;
		}

		float BestT = TNumericLimits<float>::Max();
		FRoadBedPick Best;
		bool bHit = false;

		for (const TPair<double, double>& Span : Spans)
		{
			const double SpanLen = Span.Value - Span.Key;
			if (SpanLen <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const int32 Samples = FMath::Clamp(FMath::CeilToInt(SpanLen / 80.0) + 1, 2, 128);
			FVector PrevL, PrevR, PrevC;
			double PrevS = Span.Key;
			bool bHavePrev = SplineComp->GetRoadEdgePointsAtS(
				PrevS, PrevL, PrevR, PrevC, ESplineCoordinateSpace::World);

			for (int32 Step = 1; Step < Samples; ++Step)
			{
				const double NextS = FMath::Lerp(
					Span.Key,
					Span.Value,
					static_cast<double>(Step) / static_cast<double>(Samples - 1));
				FVector NextL, NextR, NextC;
				if (!SplineComp->GetRoadEdgePointsAtS(
					NextS, NextL, NextR, NextC, ESplineCoordinateSpace::World))
				{
					bHavePrev = false;
					continue;
				}
				if (!bHavePrev)
				{
					PrevL = NextL;
					PrevR = NextR;
					PrevS = NextS;
					bHavePrev = true;
					continue;
				}

				auto AcceptHit = [&](float T, double S0, double S1)
				{
					if (T >= BestT)
					{
						return;
					}
					const FVector Hit = RayOrigin + RayDir * T;
					BestT = T;
					Best.WorldPos = Hit;
					Best.SpanS0 = Span.Key;
					Best.SpanS1 = Span.Value;
					const FVector Mid0 = FMath::Lerp(PrevL, PrevR, 0.5f);
					const FVector Mid1 = FMath::Lerp(NextL, NextR, 0.5f);
					const float AlongDen = FVector::DistSquared(Mid1, Mid0);
					const float Along = (AlongDen > KINDA_SMALL_NUMBER)
						? FMath::Clamp(FVector::DotProduct(Hit - Mid0, Mid1 - Mid0) / AlongDen, 0.0f, 1.0f)
						: 0.5f;
					Best.WorldS = FMath::Lerp(S0, S1, Along);
					FVector HitL, HitR, HitC;
					if (SplineComp->GetRoadEdgePointsAtS(
						Best.WorldS, HitL, HitR, HitC, ESplineCoordinateSpace::World))
					{
						const FVector AcrossHit = HitR - HitL;
						const float AcrossSq = AcrossHit.SizeSquared();
						Best.Alpha = (AcrossSq > KINDA_SMALL_NUMBER)
							? FMath::Clamp(FVector::DotProduct(Hit - HitL, AcrossHit) / AcrossSq, 0.0f, 1.0f)
							: 0.5f;
						Best.WorldPos = FMath::Lerp(HitL, HitR, Best.Alpha);
					}
					Best.SectionIndex = FindSectionAtS(SplineComp, Best.WorldS);
					bHit = true;
				};

				float T = 0.0f;
				if (RayHitsTriangle(RayOrigin, RayDir, PrevL, PrevR, NextR, T))
				{
					AcceptHit(T, PrevS, NextS);
				}
				if (RayHitsTriangle(RayOrigin, RayDir, PrevL, NextR, NextL, T))
				{
					AcceptHit(T, PrevS, NextS);
				}

				PrevL = NextL;
				PrevR = NextR;
				PrevS = NextS;
			}
		}

		if (bHit)
		{
			OutPick = Best;
		}
		return bHit;
	}

	static bool PickRoadBedFromClick(
		UMetaLandRoadSplineComponent* SplineComp,
		FEditorViewportClient* ViewportClient,
		const FViewportClick& Click,
		FRoadBedPick& OutPick)
	{
		if (ViewportClient && ViewportClient->Viewport)
		{
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				ViewportClient->Viewport,
				ViewportClient->GetScene(),
				ViewportClient->EngineShowFlags));
			if (FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily))
			{
				const FIntPoint ClickPos = Click.GetClickPos();
				if (PickRoadBedFromView(
					SplineComp,
					View,
					FVector2D(static_cast<float>(ClickPos.X), static_cast<float>(ClickPos.Y)),
					OutPick))
				{
					return true;
				}
			}
		}
		return PickRoadBed(SplineComp, Click.GetOrigin(), Click.GetDirection(), OutPick);
	}

	static void DrawRailOnSpan(
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadPropRail& Rail,
		double S0,
		double S1,
		const FLinearColor& Color,
		float Thickness);

	struct FPropKeyPick
	{
		int32 SectionIndex = INDEX_NONE;
		FGuid RailId;
		int32 KeyIndex = INDEX_NONE;
		double WorldS = -1.0;
	};

	static bool PickNearestPropKey(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FSceneView* View,
		const FVector2D& Mouse,
		float RadiusPx,
		FPropKeyPick& OutPick)
	{
		if (!SplineComp || !View)
		{
			return false;
		}

		const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
		float BestDist = RadiusPx;
		bool bHit = false;
		const TArray<FMetaLandRoadLaneSection>& Sections = SplineComp->GetLaneSections();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FMetaLandRoadLaneAttribute* Track =
				Sections[SectionIndex].FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
			if (!Track)
			{
				continue;
			}

			for (const FMetaLandRoadPropRail& Rail : Track->Rails)
			{
				for (int32 KeyIndex = 0; KeyIndex < Rail.Keys.Num(); ++KeyIndex)
				{
					const double WorldS = Sections[SectionIndex].SOffset + Rail.Keys[KeyIndex].SOffset;
					if (!IsVisibleS(Road, SplineComp, WorldS))
					{
						continue;
					}
					FVector World;
					if (!FMetaLandRoadPropSettingUtil::EvalRailWorld(SplineComp, WorldS, Rail, World))
					{
						continue;
					}
					FVector2D Pixel;
					if (!View->WorldToPixel(World, Pixel))
					{
						continue;
					}
					const float Dist = static_cast<float>(FVector2D::Distance(Mouse, Pixel));
					if (Dist < BestDist)
					{
						BestDist = Dist;
						OutPick.SectionIndex = SectionIndex;
						OutPick.RailId = Rail.RailId;
						OutPick.KeyIndex = KeyIndex;
						OutPick.WorldS = WorldS;
						bHit = true;
					}
				}
			}
		}
		return bHit;
	}

	static void DrawPropKeyMarker(
		FPrimitiveDrawInterface* PDI,
		const FSceneView* View,
		const FVector& World,
		bool bSelected,
		bool bHovered)
	{
		if (!PDI)
		{
			return;
		}
		(void)View;
		const FLinearColor Color = bSelected
			? FLinearColor::Yellow
			: (bHovered
				? FLinearColor(FMetaLandRoadEditorColors::HoverColor)
				: FLinearColor(FMetaLandRoadEditorColors::SplineColor));
		PDI->DrawPoint(World, Color, bSelected ? 16.0f : (bHovered ? 14.0f : 9.0f), SDPG_Foreground);
	}

	static void DrawRailOnSpan(
		FPrimitiveDrawInterface* PDI,
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadPropRail& Rail,
		double S0,
		double S1,
		const FLinearColor& Color,
		float Thickness)
	{
		const double SpanLen = S1 - S0;
		if (!PDI || !SplineComp || SpanLen <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const int32 Samples = FMath::Clamp(FMath::CeilToInt(SpanLen / 80.0) + 1, 2, 256);
		TArray<FVector> Poly;
		Poly.Reserve(Samples);
		for (int32 Step = 0; Step < Samples; ++Step)
		{
			const double WorldS = FMath::Lerp(S0, S1, static_cast<double>(Step) / static_cast<double>(Samples - 1));
			FVector World;
			if (FMetaLandRoadPropSettingUtil::EvalRailWorld(SplineComp, WorldS, Rail, World))
			{
				Lift(World);
				Poly.Add(World);
			}
		}
		for (int32 Index = 1; Index < Poly.Num(); ++Index)
		{
			PDI->DrawLine(Poly[Index - 1], Poly[Index], Color, SDPG_Foreground, Thickness, 0.0f, true);
		}
	}

	static int32 FindExistingLockedRail(
		const FMetaLandRoadLaneAttribute* Track,
		EMetaLandRoadPropEdgeRef EdgeRef)
	{
		if (!Track || EdgeRef == EMetaLandRoadPropEdgeRef::Free)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Track->Rails.Num(); ++Index)
		{
			if (Track->Rails[Index].EdgeRef == EdgeRef)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	static int32 FindExistingFreeRail(const FMetaLandRoadLaneAttribute* Track, float Alpha, float Tolerance = 0.06f)
	{
		if (!Track)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Track->Rails.Num(); ++Index)
		{
			const FMetaLandRoadPropRail& Rail = Track->Rails[Index];
			if (Rail.EdgeRef == EMetaLandRoadPropEdgeRef::Free
				&& FMath::Abs(Rail.Alpha - Alpha) <= Tolerance)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

void FMoKuEditSplinesComponentVisualizer::ClearPropSettingSelection()
{
	PropSectionIndex = INDEX_NONE;
	PropRailId.Invalidate();
	PropKeyIndex = INDEX_NONE;
	PropFocusWorldS = -1.0;
	PropCachedWidgetWorld = FVector::ZeroVector;
	CachedPropHoveredRailId.Invalidate();
	CachedPropHoveredEdgeRef = EMetaLandRoadPropEdgeRef::Free;
	bCachedPropHoveredCenter = false;
	bCachedPropHoveredRoad = false;
}

bool FMoKuEditSplinesComponentVisualizer::HasSelectedPropRail() const
{
	return PropSectionIndex != INDEX_NONE && PropRailId.IsValid();
}

bool FMoKuEditSplinesComponentVisualizer::HasSelectedPropKey() const
{
	return HasSelectedPropRail() && PropKeyIndex != INDEX_NONE;
}

void FMoKuEditSplinesComponentVisualizer::RefreshPropCachedWidget()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		PropCachedWidgetWorld = FVector::ZeroVector;
		return;
	}

	const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(PropSectionIndex);
	const FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	const FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		PropCachedWidgetWorld = FVector::ZeroVector;
		return;
	}

	double WorldS = PropFocusWorldS;
	if (Rail->Keys.IsValidIndex(PropKeyIndex))
	{
		WorldS = Section.SOffset + Rail->Keys[PropKeyIndex].SOffset;
		PropFocusWorldS = WorldS;
	}
	else if (WorldS < 0.0)
	{
		WorldS = Section.SOffset + FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 0.0) * 0.5;
	}

	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	double SpanS0 = 0.0;
	double SpanS1 = 0.0;
	if (MetaLandRoadPropOverlay::FindVisibleSpanAtS(Road, SplineComp, WorldS, SpanS0, SpanS1))
	{
		WorldS = FMath::Clamp(WorldS, SpanS0, SpanS1);
	}

	FVector World;
	if (FMetaLandRoadPropSettingUtil::EvalRailWorld(SplineComp, WorldS, *Rail, World))
	{
		MetaLandRoadPropOverlay::Lift(World);
		PropCachedWidgetWorld = World;
	}
}

void FMoKuEditSplinesComponentVisualizer::DrawPropSettingOverlay(
	const UMetaLandRoadSplineComponent* SplineComp,
	FPrimitiveDrawInterface* PDI,
	const FSceneView* View) const
{
	if (!SplineComp || !PDI)
	{
		return;
	}
	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());

	FMoKuEditSplinesComponentVisualizer::UpdateEditorViewportTick();

	const TArray<FMetaLandRoadLaneSection>& Sections = SplineComp->GetLaneSections();
	TArray<TPair<double, double>> VisibleSpans;
	MetaLandRoadPropOverlay::CollectVisibleSpans(Road, SplineComp, VisibleSpans);

	if (MetaLandRoadPropOverlay::IsPlaceGuideHeld()
		&& bCachedPropHoveredRoad
		&& !CachedPropHoveredRailId.IsValid())
	{
		FMetaLandRoadPropRail Preview;
		Preview.EdgeRef = CachedPropHoveredEdgeRef;
		Preview.Alpha = CachedPropHoveredAlpha;
		const FLinearColor PreviewColor = (CachedPropHoveredEdgeRef == EMetaLandRoadPropEdgeRef::SectionLeft
			|| CachedPropHoveredEdgeRef == EMetaLandRoadPropEdgeRef::SectionRight)
			? FLinearColor(0.2f, 0.85f, 1.0f, 1.0f)
			: FLinearColor(1.0f, 0.92f, 0.35f, 1.0f);
		MetaLandRoadPropOverlay::DrawRailOnSpan(
			PDI,
			SplineComp,
			Preview,
			CachedPropHoveredSpanS0,
			CachedPropHoveredSpanS1,
			PreviewColor,
			3.2f);
	}

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FMetaLandRoadLaneAttribute* Track =
			Sections[SectionIndex].FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
		if (!Track)
		{
			continue;
		}

		const double SectionS0 = Sections[SectionIndex].SOffset;
		const double SectionS1 = Sections[SectionIndex].SOffsetEnd_Cached;

		for (const FMetaLandRoadPropRail& Rail : Track->Rails)
		{
			const bool bCurrentRail = PropRailId == Rail.RailId && PropSectionIndex == SectionIndex;
			PDI->SetHitProxy(new HMetaLandRoadPropRailVisProxy(SplineComp, SectionIndex, Rail.RailId));
			const int32 KeySpanCount = FMath::Max(Rail.Keys.Num() - 1, 0);
			if (KeySpanCount <= 0)
			{
				for (const TPair<double, double>& Span : VisibleSpans)
				{
					const double DrawS0 = FMath::Max(Span.Key, SectionS0);
					const double DrawS1 = FMath::Min(Span.Value, SectionS1);
					if (DrawS1 - DrawS0 > KINDA_SMALL_NUMBER)
					{
						MetaLandRoadPropOverlay::DrawRailOnSpan(
							PDI,
							SplineComp,
							Rail,
							DrawS0,
							DrawS1,
							bCurrentRail
								? FLinearColor(1.0f, 0.85f, 0.2f, 1.0f)
								: FLinearColor(0.35f, 0.85f, 0.55f, 0.55f),
							bCurrentRail ? 3.5f : 2.0f);
					}
				}
			}
			else
			{
				for (int32 KeySpan = 0; KeySpan < KeySpanCount; ++KeySpan)
				{
					const double KeyS0 = SectionS0 + Rail.Keys[KeySpan].SOffset;
					const double KeyS1 = SectionS0 + Rail.Keys[KeySpan + 1].SOffset;
					const bool bSelectedKeySpan = bCurrentRail
						&& ((PropKeyIndex == KeySpan)
							|| (PropKeyIndex == KeySpan + 1 && PropKeyIndex == Rail.Keys.Num() - 1)
							|| (PropKeyIndex == INDEX_NONE
								&& PropFocusWorldS >= KeyS0 - 1.0
								&& PropFocusWorldS <= KeyS1 + 1.0));
					const bool bHoverKeySpan = CachedPropHoveredRailId == Rail.RailId
						&& CachedPropHoveredWorldS >= KeyS0 - 1.0
						&& CachedPropHoveredWorldS <= KeyS1 + 1.0;
					FLinearColor LineColor = FLinearColor(0.35f, 0.85f, 0.55f, bCurrentRail ? 0.7f : 0.45f);
					float Thickness = 2.0f;
					if (bCurrentRail && !bSelectedKeySpan)
					{
						LineColor = FLinearColor(0.75f, 0.55f, 0.18f, 0.85f);
						Thickness = 2.2f;
					}
					if (bSelectedKeySpan)
					{
						LineColor = FLinearColor(1.0f, 0.85f, 0.2f, 1.0f);
						Thickness = 3.8f;
					}
					if (bHoverKeySpan && !bSelectedKeySpan)
					{
						LineColor = FLinearColor(FMetaLandRoadEditorColors::HoverColor);
						Thickness = 3.4f;
					}
					for (const TPair<double, double>& Span : VisibleSpans)
					{
						const double DrawS0 = FMath::Max(KeyS0, Span.Key);
						const double DrawS1 = FMath::Min(KeyS1, Span.Value);
						if (DrawS1 - DrawS0 <= KINDA_SMALL_NUMBER)
						{
							continue;
						}
						MetaLandRoadPropOverlay::DrawRailOnSpan(
							PDI,
							SplineComp,
							Rail,
							DrawS0,
							DrawS1,
							LineColor,
							Thickness);
					}
				}
			}
			PDI->SetHitProxy(nullptr);

			for (int32 KeyIndex = 0; KeyIndex < Rail.Keys.Num(); ++KeyIndex)
			{
				const double WorldS = SectionS0 + Rail.Keys[KeyIndex].SOffset;
				if (!MetaLandRoadPropOverlay::IsVisibleS(Road, SplineComp, WorldS))
				{
					continue;
				}
				FVector World;
				if (!FMetaLandRoadPropSettingUtil::EvalRailWorld(SplineComp, WorldS, Rail, World))
				{
					continue;
				}
				MetaLandRoadPropOverlay::Lift(World);
				const bool bSelected = bCurrentRail && PropKeyIndex == KeyIndex;
				const bool bHovered = CachedPropHoveredRailId == Rail.RailId
					&& CachedPropHoveredKeyIndex == KeyIndex;
				PDI->SetHitProxy(new HMetaLandRoadPropKeyVisProxy(
					SplineComp, SectionIndex, Rail.RailId, KeyIndex, HPP_Foreground));
				MetaLandRoadPropOverlay::DrawPropKeyMarker(
					PDI, View, World, bSelected, bHovered && !bSelected);
				PDI->SetHitProxy(nullptr);
			}
		}
	}
}

void FMoKuEditSplinesComponentVisualizer::TickPropSettingHover()
{
	const bool PrevRoad = bCachedPropHoveredRoad;
	const double PrevS = CachedPropHoveredWorldS;
	const float PrevAlpha = CachedPropHoveredAlpha;
	const int32 PrevKey = CachedPropHoveredKeyIndex;
	const FGuid PrevRail = CachedPropHoveredRailId;
	const EMetaLandRoadPropEdgeRef PrevEdge = CachedPropHoveredEdgeRef;
	const bool PrevCenter = bCachedPropHoveredCenter;
	const int32 PrevSection = CachedPropHoveredSectionIndex;
	const double PrevSpan0 = CachedPropHoveredSpanS0;
	const double PrevSpan1 = CachedPropHoveredSpanS1;

	bCachedPropHoveredRoad = false;
	CachedPropHoveredWorldS = -1.0;
	CachedPropHoveredAlpha = 0.5f;
	CachedPropHoveredSectionIndex = INDEX_NONE;
	CachedPropHoveredSpanS0 = 0.0;
	CachedPropHoveredSpanS1 = 0.0;
	CachedPropHoveredKeyIndex = INDEX_NONE;
	CachedPropHoveredRailId.Invalidate();
	CachedPropHoveredEdgeRef = EMetaLandRoadPropEdgeRef::Free;
	bCachedPropHoveredCenter = false;

	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().AnyMenusVisible())
	{
		bCachedPropHoveredRoad = PrevRoad;
		CachedPropHoveredWorldS = PrevS;
		CachedPropHoveredAlpha = PrevAlpha;
		CachedPropHoveredKeyIndex = PrevKey;
		CachedPropHoveredRailId = PrevRail;
		CachedPropHoveredEdgeRef = PrevEdge;
		bCachedPropHoveredCenter = PrevCenter;
		CachedPropHoveredSectionIndex = PrevSection;
		CachedPropHoveredSpanS0 = PrevSpan0;
		CachedPropHoveredSpanS1 = PrevSpan1;
		return;
	}

	if (!GCurrentLevelEditingViewportClient || !GCurrentLevelEditingViewportClient->Viewport)
	{
		if (GEditor && PrevRoad)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
		return;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp)
	{
		if (GEditor && PrevRoad)
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
	static bool bPrevPlaceGuide = false;
	const bool bPlaceGuide = MetaLandRoadPropOverlay::IsPlaceGuideHeld();

	if (!bPlaceGuide)
	{
		MetaLandRoadPropOverlay::FPropKeyPick KeyPick;
		if (MetaLandRoadPropOverlay::PickNearestPropKey(
			SplineComp,
			View,
			Mouse,
			MetaLandRoadPropOverlay::KeyPickPx,
			KeyPick))
		{
			bCachedPropHoveredRoad = true;
			CachedPropHoveredKeyIndex = KeyPick.KeyIndex;
			CachedPropHoveredRailId = KeyPick.RailId;
			CachedPropHoveredSectionIndex = KeyPick.SectionIndex;
			CachedPropHoveredWorldS = KeyPick.WorldS;
			MetaLandRoadPropOverlay::FRoadBedPick BedPick;
			if (MetaLandRoadPropOverlay::PickRoadBedFromView(SplineComp, View, Mouse, BedPick))
			{
				CachedPropHoveredAlpha = BedPick.Alpha;
				CachedPropHoveredSpanS0 = BedPick.SpanS0;
				CachedPropHoveredSpanS1 = BedPick.SpanS1;
			}
		}
	}

	if (CachedPropHoveredKeyIndex == INDEX_NONE)
	{
		MetaLandRoadPropOverlay::FRoadBedPick BedPick;
		if (!MetaLandRoadPropOverlay::PickRoadBedFromView(SplineComp, View, Mouse, BedPick))
		{
			if (GEditor && PrevRoad)
			{
				GEditor->RedrawLevelEditingViewports(false);
			}
			return;
		}

		bCachedPropHoveredRoad = true;
		CachedPropHoveredWorldS = BedPick.WorldS;
		CachedPropHoveredAlpha = BedPick.Alpha;
		CachedPropHoveredSectionIndex = BedPick.SectionIndex;
		CachedPropHoveredSpanS0 = BedPick.SpanS0;
		CachedPropHoveredSpanS1 = BedPick.SpanS1;

		if (bPlaceGuide)
		{
			EMetaLandRoadPropEdgeRef EdgeRef = EMetaLandRoadPropEdgeRef::Free;
			bool bCenterGuide = false;
			MetaLandRoadPropOverlay::FRoadBedPick EdgePick;
			if (MetaLandRoadPropOverlay::PickNearestSectionEdge(
				SplineComp,
				View,
				Mouse,
				MetaLandRoadPropOverlay::GuidePickPx,
				EdgeRef,
				bCenterGuide,
				EdgePick,
				false))
			{
				CachedPropHoveredWorldS = EdgePick.WorldS;
				CachedPropHoveredAlpha = EdgePick.Alpha;
				CachedPropHoveredSectionIndex = EdgePick.SectionIndex;
				CachedPropHoveredSpanS0 = EdgePick.SpanS0;
				CachedPropHoveredSpanS1 = EdgePick.SpanS1;
				CachedPropHoveredEdgeRef = EdgeRef;
			}
		}
		else
		{
			FGuid RailId;
			MetaLandRoadPropOverlay::FRoadBedPick RailPick;
			if (MetaLandRoadPropOverlay::PickNearestAuthoredRail(
				SplineComp,
				View,
				Mouse,
				MetaLandRoadPropOverlay::RailPickPx,
				RailId,
				RailPick))
			{
				CachedPropHoveredRailId = RailId;
				CachedPropHoveredWorldS = RailPick.WorldS;
				CachedPropHoveredAlpha = RailPick.Alpha;
				CachedPropHoveredSectionIndex = RailPick.SectionIndex;
				CachedPropHoveredSpanS0 = RailPick.SpanS0;
				CachedPropHoveredSpanS1 = RailPick.SpanS1;
			}
		}
	}

	if (GEditor && (PrevRoad != bCachedPropHoveredRoad
		|| !FMath::IsNearlyEqual(PrevS, CachedPropHoveredWorldS, 8.0)
		|| !FMath::IsNearlyEqual(PrevAlpha, CachedPropHoveredAlpha, 0.02f)
		|| PrevKey != CachedPropHoveredKeyIndex
		|| PrevRail != CachedPropHoveredRailId
		|| PrevEdge != CachedPropHoveredEdgeRef
		|| PrevCenter != bCachedPropHoveredCenter
		|| bPrevPlaceGuide != bPlaceGuide))
	{
		bPrevPlaceGuide = bPlaceGuide;
		GEditor->RedrawLevelEditingViewports(false);
	}
	else
	{
		bPrevPlaceGuide = bPlaceGuide;
	}
}

void FMoKuEditSplinesComponentVisualizer::GeneratePropSettingContextMenuSections(FMenuBuilder& InMenuBuilder) const
{
	FMoKuEditSplinesComponentVisualizer* MutableThis =
		const_cast<FMoKuEditSplinesComponentVisualizer*>(this);
	InMenuBuilder.BeginSection("PropSettingKeys", LOCTEXT("PropSettingMenu", "PropSetting"));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("AddPropKey", "Add Key"),
		LOCTEXT("AddPropKeyTip", "Add a span key on the selected rail at the hover position."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(MutableThis, &FMoKuEditSplinesComponentVisualizer::OnAddPropKey),
			FCanExecuteAction::CreateLambda([this]()
			{
				return HasSelectedPropRail()
					&& (CachedPropHoveredWorldS >= 0.0 || PropFocusWorldS >= 0.0);
			})));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("DeletePropKey", "Delete Key"),
		LOCTEXT("DeletePropKeyTip", "Delete the selected span key. Begin and End keys stay."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(MutableThis, &FMoKuEditSplinesComponentVisualizer::OnDeletePropKey),
			FCanExecuteAction::CreateLambda([this]()
			{
				return HasSelectedPropKey();
			})));
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("DeletePropRail", "Delete Rail"),
		LOCTEXT("DeletePropRailTip", "Delete the selected rail and its keys."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(MutableThis, &FMoKuEditSplinesComponentVisualizer::OnDeletePropRail),
			FCanExecuteAction::CreateLambda([this]()
			{
				return HasSelectedPropRail();
			})));
	InMenuBuilder.EndSection();
}

void FMoKuEditSplinesComponentVisualizer::OnAddPropKey()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !HasSelectedPropRail() || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return;
	}

	double WorldS = CachedPropHoveredWorldS;
	if (WorldS < 0.0)
	{
		WorldS = PropFocusWorldS;
	}
	if (WorldS < 0.0)
	{
		return;
	}

	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	if (!MetaLandRoadPropOverlay::IsVisibleS(Road, SplineComp, WorldS))
	{
		return;
	}

	FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(PropSectionIndex);
	FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddPropKey", "Add PropSetting Key"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	PropKeyIndex = Rail->AddSpanKey(WorldS - Section.SOffset);
	PropFocusWorldS = WorldS;
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	RefreshPropCachedWidget();
	EnsureNativeEditTranslateWidget();
	FMetaLandRoadEditorModule::Get().SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
	FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMoKuEditSplinesComponentVisualizer::OnDeletePropKey()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !HasSelectedPropKey() || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return;
	}

	FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(PropSectionIndex);
	FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeletePropKey", "Delete PropSetting Key"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	if (!Rail->RemoveSpanKey(PropKeyIndex))
	{
		return;
	}

	PropKeyIndex = INDEX_NONE;
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	RefreshPropCachedWidget();
	FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FMoKuEditSplinesComponentVisualizer::OnDeletePropRail()
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !HasSelectedPropRail() || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return;
	}

	FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(PropSectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	const int32 RailIndex = Track ? Track->FindRailIndex(PropRailId) : INDEX_NONE;
	if (RailIndex == INDEX_NONE)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeletePropRail", "Delete PropSetting Rail"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	Track->Rails.RemoveAt(RailIndex);
	ClearPropSettingSelection();
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

bool FMoKuEditSplinesComponentVisualizer::CommitPropLockedRail(
	UMetaLandRoadSplineComponent* SplineComp,
	FEditorViewportClient* ViewportClient,
	int32 SectionIndex,
	EMetaLandRoadPropEdgeRef EdgeRef,
	double WorldS)
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex)
		|| (EdgeRef != EMetaLandRoadPropEdgeRef::SectionLeft
			&& EdgeRef != EMetaLandRoadPropEdgeRef::SectionRight))
	{
		return false;
	}

	FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	FMetaLandRoadLaneAttribute* Track = Section.PinAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	if (!Track)
	{
		return false;
	}

	const int32 Existing = MetaLandRoadPropOverlay::FindExistingLockedRail(Track, EdgeRef);
	if (Existing != INDEX_NONE)
	{
		PropSectionIndex = SectionIndex;
		PropRailId = Track->Rails[Existing].RailId;
		PropKeyIndex = INDEX_NONE;
		PropFocusWorldS = WorldS;
		RefreshPropCachedWidget();
		EnsureNativeEditTranslateWidget(ViewportClient);
		FMetaLandRoadEditorModule::Get().SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
		FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return true;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddPropEdgeRail", "Add PropSetting Edge Rail"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	FMetaLandRoadPropRail Rail;
	Rail.RailId = FGuid::NewGuid();
	Rail.Name = (EdgeRef == EMetaLandRoadPropEdgeRef::SectionLeft)
		? FName(TEXT("SectionLeft"))
		: FName(TEXT("SectionRight"));
	Rail.EdgeRef = EdgeRef;
	Rail.Alpha = (EdgeRef == EMetaLandRoadPropEdgeRef::SectionLeft) ? 0.0f : 1.0f;
	Rail.EnsureSpanKeys(FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 0.0));
	Track->Rails.Add(Rail);

	PropSectionIndex = SectionIndex;
	PropRailId = Rail.RailId;
	PropKeyIndex = INDEX_NONE;
	PropFocusWorldS = WorldS;
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	RefreshPropCachedWidget();
	EnsureNativeEditTranslateWidget(ViewportClient);
	FMetaLandRoadEditorModule::Get().SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
	FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::CommitPropFreeRail(
	UMetaLandRoadSplineComponent* SplineComp,
	FEditorViewportClient* ViewportClient,
	int32 SectionIndex,
	float Alpha,
	double WorldS)
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(SectionIndex);
	FMetaLandRoadLaneAttribute* Track = Section.PinAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	if (!Track)
	{
		return false;
	}

	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const int32 Existing = MetaLandRoadPropOverlay::FindExistingFreeRail(Track, ClampedAlpha);
	if (Existing != INDEX_NONE)
	{
		PropSectionIndex = SectionIndex;
		PropRailId = Track->Rails[Existing].RailId;
		PropKeyIndex = INDEX_NONE;
		PropFocusWorldS = WorldS;
		RefreshPropCachedWidget();
		EnsureNativeEditTranslateWidget(ViewportClient);
		FMetaLandRoadEditorModule::Get().SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
		FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return true;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddPropCenterRail", "Add PropSetting Center Rail"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	FMetaLandRoadPropRail Rail;
	Rail.RailId = FGuid::NewGuid();
	Rail.Name = FName(TEXT("Center"));
	Rail.EdgeRef = EMetaLandRoadPropEdgeRef::Free;
	Rail.Alpha = ClampedAlpha;
	Rail.EnsureSpanKeys(FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 0.0));
	Track->Rails.Add(Rail);

	PropSectionIndex = SectionIndex;
	PropRailId = Rail.RailId;
	PropKeyIndex = INDEX_NONE;
	PropFocusWorldS = WorldS;
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	RefreshPropCachedWidget();
	EnsureNativeEditTranslateWidget(ViewportClient);
	FMetaLandRoadEditorModule::Get().SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
	FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::HandlePropSettingInputDelta(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	const FVector& DeltaTranslate)
{
	(void)ViewportClient;
	(void)Viewport;
	if (!FMetaLandRoadEditorModule::Get().ShouldDrawPropSettingOverlay()
		|| !HasSelectedPropRail()
		|| DeltaTranslate.IsNearlyZero())
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return true;
	}

	FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(PropSectionIndex);
	FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		return true;
	}

	const FVector NewWorld = PropCachedWidgetWorld + DeltaTranslate;
	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());

	if (HasSelectedPropKey())
	{
		if (PropKeyIndex == 0 || PropKeyIndex == Rail->Keys.Num() - 1)
		{
			RefreshPropCachedWidget();
			return true;
		}

		const float InputKey = SplineComp->FindInputKeyClosestToWorldLocation(NewWorld);
		double WorldS = SplineComp->GetDistanceAlongSplineAtSplineInputKey(InputKey);
		double SpanS0 = Section.SOffset;
		double SpanS1 = Section.SOffsetEnd_Cached;
		if (MetaLandRoadPropOverlay::FindVisibleSpanAtS(Road, SplineComp, PropFocusWorldS >= 0.0 ? PropFocusWorldS : WorldS, SpanS0, SpanS1))
		{
			WorldS = FMath::Clamp(WorldS, SpanS0, SpanS1);
		}

		const FScopedTransaction Transaction(LOCTEXT("MovePropKey", "Move PropSetting Key"));
		SplineComp->Modify();
		const double MaxS = FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 0.0);
		Rail->SetSpanKeyS(PropKeyIndex, WorldS - Section.SOffset, MaxS);
		const int32 NewIndex = Rail->FindSpanKey(WorldS - Section.SOffset, 2.0);
		if (NewIndex != INDEX_NONE)
		{
			PropKeyIndex = NewIndex;
		}
		PropFocusWorldS = WorldS;
		SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
		RefreshPropCachedWidget();
		FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
		return true;
	}

	double WorldS = PropFocusWorldS;
	if (WorldS < 0.0)
	{
		WorldS = CachedPropHoveredWorldS;
	}
	if (WorldS < 0.0)
	{
		WorldS = Section.SOffset + FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 0.0) * 0.5;
	}

	FVector Left, Right, Center;
	if (!SplineComp->GetRoadEdgePointsAtS(WorldS, Left, Right, Center, ESplineCoordinateSpace::World))
	{
		return true;
	}

	const FVector Across = Right - Left;
	const float AcrossSq = Across.SizeSquared();
	const float Alpha = (AcrossSq > KINDA_SMALL_NUMBER)
		? FMath::Clamp(FVector::DotProduct(NewWorld - Left, Across) / AcrossSq, 0.0f, 1.0f)
		: 0.5f;

	const FScopedTransaction Transaction(LOCTEXT("MovePropRail", "Move PropSetting Rail"));
	SplineComp->Modify();
	if (Alpha <= MetaLandRoadPropOverlay::EdgeSnap)
	{
		Rail->EdgeRef = EMetaLandRoadPropEdgeRef::SectionLeft;
		Rail->Alpha = 0.0f;
	}
	else if (Alpha >= 1.0f - MetaLandRoadPropOverlay::EdgeSnap)
	{
		Rail->EdgeRef = EMetaLandRoadPropEdgeRef::SectionRight;
		Rail->Alpha = 1.0f;
	}
	else
	{
		Rail->EdgeRef = EMetaLandRoadPropEdgeRef::Free;
		Rail->Alpha = Alpha;
	}
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	RefreshPropCachedWidget();
	FMetaLandRoadEditorModule::Get().BroadcastPropSettingEditorRefresh();
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::HandlePropSettingClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click)
{
	const bool bLeft = Click.GetKey() == EKeys::LeftMouseButton;
	const bool bRight = Click.GetKey() == EKeys::RightMouseButton;
	if (!bLeft && !bRight)
	{
		return false;
	}

	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp && HitProxy && HitProxy->IsA(HComponentVisProxy::StaticGetType()))
	{
		SplineComp = Cast<UMetaLandRoadSplineComponent>(
			const_cast<UActorComponent*>(static_cast<HComponentVisProxy*>(HitProxy)->Component.Get()));
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
	if (!SplineComp)
	{
		return false;
	}

	SetActiveForSpline(SplineComp);
	FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();

	if (InViewportClient && InViewportClient->Viewport)
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			InViewportClient->Viewport,
			InViewportClient->GetScene(),
			InViewportClient->EngineShowFlags));
		if (FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily))
		{
			const FIntPoint ClickPos = Click.GetClickPos();
			MetaLandRoadPropOverlay::FPropKeyPick KeyPick;
			if (MetaLandRoadPropOverlay::PickNearestPropKey(
				SplineComp,
				View,
				FVector2D(static_cast<float>(ClickPos.X), static_cast<float>(ClickPos.Y)),
				MetaLandRoadPropOverlay::KeyPickPx,
				KeyPick))
			{
				PropSectionIndex = KeyPick.SectionIndex;
				PropRailId = KeyPick.RailId;
				PropKeyIndex = KeyPick.KeyIndex;
				PropFocusWorldS = KeyPick.WorldS;
				RefreshPropCachedWidget();
				EnsureNativeEditTranslateWidget(InViewportClient);
				Module.BroadcastPropSettingEditorRefresh();
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
		}
	}

	if (InViewportClient && InViewportClient->Viewport)
	{
		FSceneViewFamilyContext RailViewFamily(FSceneViewFamily::ConstructionValues(
			InViewportClient->Viewport,
			InViewportClient->GetScene(),
			InViewportClient->EngineShowFlags));
		if (FSceneView* View = InViewportClient->CalcSceneView(&RailViewFamily))
		{
			const FIntPoint ClickPos = Click.GetClickPos();
			CachedPropHoveredRailId.Invalidate();
			FGuid RailId;
			MetaLandRoadPropOverlay::FRoadBedPick RailPick;
			if (MetaLandRoadPropOverlay::PickNearestAuthoredRail(
				SplineComp,
				View,
				FVector2D(static_cast<float>(ClickPos.X), static_cast<float>(ClickPos.Y)),
				MetaLandRoadPropOverlay::RailPickPx,
				RailId,
				RailPick))
			{
				CachedPropHoveredRailId = RailId;
				CachedPropHoveredWorldS = RailPick.WorldS;
				CachedPropHoveredAlpha = RailPick.Alpha;
				CachedPropHoveredSectionIndex = RailPick.SectionIndex;
				CachedPropHoveredSpanS0 = RailPick.SpanS0;
				CachedPropHoveredSpanS1 = RailPick.SpanS1;
				bCachedPropHoveredRoad = true;
			}
		}
	}

	MetaLandRoadPropOverlay::FRoadBedPick BedPick;
	const bool bOnRoad = MetaLandRoadPropOverlay::PickRoadBedFromClick(
		SplineComp, InViewportClient, Click, BedPick);

	if (bOnRoad && !CachedPropHoveredRailId.IsValid())
	{
		CachedPropHoveredWorldS = BedPick.WorldS;
		CachedPropHoveredAlpha = BedPick.Alpha;
		CachedPropHoveredSectionIndex = BedPick.SectionIndex;
		CachedPropHoveredSpanS0 = BedPick.SpanS0;
		CachedPropHoveredSpanS1 = BedPick.SpanS1;
		bCachedPropHoveredRoad = true;
	}

	if (bLeft
		&& bOnRoad
		&& MetaLandRoadPropOverlay::IsPlaceGuideHeld()
		&& !CachedPropHoveredRailId.IsValid()
		&& Module.GetPropPlaceMode() != FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan)
	{
		const int32 SectionIndex = (CachedPropHoveredSectionIndex != INDEX_NONE)
			? CachedPropHoveredSectionIndex
			: BedPick.SectionIndex;
		const double WorldS = (CachedPropHoveredWorldS >= 0.0) ? CachedPropHoveredWorldS : BedPick.WorldS;
		const bool bCommitted =
			(CachedPropHoveredEdgeRef == EMetaLandRoadPropEdgeRef::SectionLeft
				|| CachedPropHoveredEdgeRef == EMetaLandRoadPropEdgeRef::SectionRight)
			? CommitPropLockedRail(SplineComp, InViewportClient, SectionIndex, CachedPropHoveredEdgeRef, WorldS)
			: CommitPropFreeRail(
				SplineComp,
				InViewportClient,
				SectionIndex,
				(CachedPropHoveredWorldS >= 0.0) ? CachedPropHoveredAlpha : BedPick.Alpha,
				WorldS);
		if (bCommitted)
		{
			return true;
		}
	}

	if (HitProxy && HitProxy->IsA(HMetaLandRoadPropKeyVisProxy::StaticGetType()))
	{
		const HMetaLandRoadPropKeyVisProxy* KeyProxy = static_cast<HMetaLandRoadPropKeyVisProxy*>(HitProxy);
		PropSectionIndex = KeyProxy->SectionIndex;
		PropRailId = KeyProxy->RailId;
		PropKeyIndex = KeyProxy->KeyIndex;
		if (SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
		{
			const FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(PropSectionIndex);
			const FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
			const FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
			if (Rail && Rail->Keys.IsValidIndex(PropKeyIndex))
			{
				PropFocusWorldS = Section.SOffset + Rail->Keys[PropKeyIndex].SOffset;
			}
		}
		RefreshPropCachedWidget();
		EnsureNativeEditTranslateWidget(InViewportClient);
		Module.BroadcastPropSettingEditorRefresh();
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

	if (CachedPropHoveredRailId.IsValid() && CachedPropHoveredSectionIndex != INDEX_NONE)
	{
		PropSectionIndex = CachedPropHoveredSectionIndex;
		PropRailId = CachedPropHoveredRailId;
		PropFocusWorldS = bOnRoad ? BedPick.WorldS : CachedPropHoveredWorldS;
		if (CachedPropHoveredKeyIndex != INDEX_NONE)
		{
			PropKeyIndex = CachedPropHoveredKeyIndex;
		}
		else if (Module.GetPropPlaceMode() != FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan)
		{
			PropKeyIndex = INDEX_NONE;
		}
		RefreshPropCachedWidget();
		EnsureNativeEditTranslateWidget(InViewportClient);
		Module.BroadcastPropSettingEditorRefresh();
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

	if (HitProxy && HitProxy->IsA(HMetaLandRoadPropRailVisProxy::StaticGetType()))
	{
		const HMetaLandRoadPropRailVisProxy* RailProxy = static_cast<HMetaLandRoadPropRailVisProxy*>(HitProxy);
		PropSectionIndex = RailProxy->SectionIndex;
		PropRailId = RailProxy->RailId;
		if (Module.GetPropPlaceMode() != FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan)
		{
			PropKeyIndex = INDEX_NONE;
		}
		if (bOnRoad)
		{
			PropFocusWorldS = BedPick.WorldS;
		}
		else if (CachedPropHoveredWorldS >= 0.0)
		{
			PropFocusWorldS = CachedPropHoveredWorldS;
		}
		RefreshPropCachedWidget();
		EnsureNativeEditTranslateWidget(InViewportClient);
		Module.BroadcastPropSettingEditorRefresh();
	}

	if (bLeft && Module.GetPropPlaceMode() == FMetaLandRoadEditorModule::EPropPlaceMode::AddSpan)
	{
		if (!HasSelectedPropRail() || !bOnRoad)
		{
			return true;
		}
		if (!SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
		{
			return true;
		}

		FMetaLandRoadLaneSection& Section = SplineComp->GetLaneSection(PropSectionIndex);
		FMetaLandRoadLaneAttribute* Track = Section.FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
		FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
		if (!Rail)
		{
			return true;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddPropSpan", "Add PropSetting Span"));
		SplineComp->Modify();
		if (AActor* Owner = SplineComp->GetOwner())
		{
			Owner->Modify();
		}

		PropKeyIndex = Rail->AddSpanKey(BedPick.WorldS - Section.SOffset);
		PropFocusWorldS = BedPick.WorldS;
		SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
		RefreshPropCachedWidget();
		EnsureNativeEditTranslateWidget(InViewportClient);
		Module.SetPropPlaceMode(FMetaLandRoadEditorModule::EPropPlaceMode::Select);
		Module.BroadcastPropSettingEditorRefresh();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return true;
	}

	if (bOnRoad)
	{
		PropFocusWorldS = BedPick.WorldS;
		if (HasSelectedPropRail()
			&& SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex)
			&& BedPick.SectionIndex == PropSectionIndex)
		{
			PropKeyIndex = INDEX_NONE;
			RefreshPropCachedWidget();
		}
		Module.BroadcastPropSettingEditorRefresh();
		if (bRight && HasSelectedPropRail())
		{
			return FinishPickContextMenu(InViewportClient, true);
		}
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
		return true;
	}

	if (bRight && HasSelectedPropRail())
	{
		return FinishPickContextMenu(InViewportClient, true);
	}

	if (bLeft)
	{
		PropKeyIndex = INDEX_NONE;
		Module.BroadcastPropSettingEditorRefresh();
		return true;
	}

	return false;
}

int32 FMoKuEditSplinesComponentVisualizer::ResolvePropSpanKeyIndex(
	const FMetaLandRoadPropRail& Rail,
	double SectionS0) const
{
	if (Rail.Keys.IsValidIndex(PropKeyIndex))
	{
		return PropKeyIndex == Rail.Keys.Num() - 1 && Rail.Keys.Num() >= 2
			? Rail.Keys.Num() - 2
			: PropKeyIndex;
	}
	if (Rail.Keys.Num() == 0)
	{
		return INDEX_NONE;
	}
	if (Rail.Keys.Num() == 1)
	{
		return 0;
	}
	const double LocalS = PropFocusWorldS - SectionS0;
	for (int32 Index = 0; Index < Rail.Keys.Num() - 1; ++Index)
	{
		if (LocalS >= Rail.Keys[Index].SOffset - 1.0
			&& LocalS <= Rail.Keys[Index + 1].SOffset + 1.0)
		{
			return Index;
		}
	}
	return 0;
}

bool FMoKuEditSplinesComponentVisualizer::GetSelectedPropSettingDetails(FMetaLandRoadPropSettingDetails& OutDetails) const
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return false;
	}

	const FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(PropSectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	const FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		return false;
	}

	OutDetails.RailName = Rail->Name;
	OutDetails.EdgeRef = Rail->EdgeRef;
	OutDetails.Alpha = Rail->Alpha;
	const int32 SpanKey = ResolvePropSpanKeyIndex(*Rail, SplineComp->GetLaneSection(PropSectionIndex).SOffset);
	if (Rail->Keys.IsValidIndex(SpanKey))
	{
		const FMetaLandRoadPropRailKey& Key = Rail->Keys[SpanKey];
		OutDetails.SOffset = Key.SOffset;
		OutDetails.Layout = Key.Layout;
		OutDetails.SpacingCm = Key.SpacingCm;
		OutDetails.Mesh = Key.Mesh;
		OutDetails.ZOffsetCm = Key.ZOffsetCm;
	}
	return true;
}

bool FMoKuEditSplinesComponentVisualizer::SetSelectedPropSettingDetails(const FMetaLandRoadPropSettingDetails& InDetails)
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return false;
	}

	FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(PropSectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("EditPropSetting", "Edit PropSetting"));
	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}
	Rail->EdgeRef = InDetails.EdgeRef;
	Rail->Alpha = InDetails.Alpha;
	const int32 WriteIndex = ResolvePropSpanKeyIndex(*Rail, SplineComp->GetLaneSection(PropSectionIndex).SOffset);
	if (Rail->Keys.IsValidIndex(WriteIndex))
	{
		FMetaLandRoadPropRailKey& Written = Rail->Keys[WriteIndex];
		Written.Layout = InDetails.Layout;
		Written.SpacingCm = InDetails.SpacingCm;
		Written.Mesh = InDetails.Mesh;
		Written.ZOffsetCm = InDetails.ZOffsetCm;
	}
	SplineComp->GetRoadLayoutMutable().UpdateAttributesVersion();
	RefreshPropCachedWidget();
	UMetaLandRoadPropInstanceComponent::RefreshForRoad(
		Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()));
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
	return true;
}

FText FMoKuEditSplinesComponentVisualizer::GetSelectedPropSettingTitle() const
{
	UMetaLandRoadSplineComponent* SplineComp = GetEditedMetaLandRoadSpline();
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(PropSectionIndex))
	{
		return LOCTEXT("PropSettingIdle", "PropSetting — hover a remaining-road span");
	}
	const FMetaLandRoadLaneAttribute* Track =
		SplineComp->GetLaneSection(PropSectionIndex).FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
	const FMetaLandRoadPropRail* Rail = Track ? Track->FindRail(PropRailId) : nullptr;
	if (!Rail)
	{
		return LOCTEXT("PropSettingNoRail", "PropSetting — hover to select, Shift+move for place guide");
	}
	if (Rail->Keys.IsValidIndex(PropKeyIndex))
	{
		return FText::Format(
			LOCTEXT("PropSettingKeyTitle", "{0}  Key {1}"),
			FText::FromName(Rail->Name),
			FText::AsNumber(PropKeyIndex));
	}
	return FText::Format(
		LOCTEXT("PropSettingRailTitle", "{0}  — hover select, Shift+move place, RMB Add Key / Delete Rail"),
		FText::FromName(Rail->Name));
}

#undef LOCTEXT_NAMESPACE
