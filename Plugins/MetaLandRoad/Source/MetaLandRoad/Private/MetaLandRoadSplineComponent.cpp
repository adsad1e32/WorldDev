#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadModule.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditIntersectionActor.h"
#include "Components/LineBatchComponent.h"
#include "SceneManagement.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

static FVector FlattenHorizontalRight(const FVector& Right)
{
	FVector Flat(Right.X, Right.Y, 0.0);
	if (!Flat.Normalize())
	{
		return FVector::YAxisVector;
	}
	return Flat;
}

static FVector HorizontalRightFromTravel(const FVector& Travel)
{
	return FlattenHorizontalRight(FVector::CrossProduct(FVector::UpVector, Travel));
}

#if 0
static FVector HorizontalRightFromTravel(const FVector& Travel)
{
	return FlattenHorizontalRight(FVector::CrossProduct(FVector::UpVector, Travel));
}

static bool IntersectOffsetLines2D(
	const FVector& Center,
	const FVector& InDir,
	const FVector& OutDir,
	double Offset,
	FVector& OutMiter)
{
	const FVector RightIn = HorizontalRightFromTravel(InDir);
	const FVector RightOut = HorizontalRightFromTravel(OutDir);
	const double A11 = InDir.X;
	const double A12 = -OutDir.X;
	const double A21 = InDir.Y;
	const double A22 = -OutDir.Y;
	const double Det = A11 * A22 - A12 * A21;
	if (FMath::Abs(Det) < 1.0e-8)
	{
		return false;
	}

	const double Bx = (RightOut.X - RightIn.X) * Offset;
	const double By = (RightOut.Y - RightIn.Y) * Offset;
	const double T = (Bx * A22 - A12 * By) / Det;
	OutMiter = Center + RightIn * Offset + InDir * T;
	OutMiter.Z = Center.Z;
	return true;
}

/** Sweep offset of one authored-center sample. PathCenter stays on the spline. */
static FVector SweepRailPoint(
	const FVector& PrevCenter,
	const FVector& Center,
	const FVector& NextCenter,
	double Off,
	const FVector& FallbackRight,
	double HalfW)
{
	FVector InDir = (Center - PrevCenter).GetSafeNormal2D();
	FVector OutDir = (NextCenter - Center).GetSafeNormal2D();
	if (InDir.IsNearlyZero())
	{
		InDir = OutDir;
	}
	if (OutDir.IsNearlyZero())
	{
		OutDir = InDir;
	}
	FVector Right = HorizontalRightFromTravel((InDir + OutDir).GetSafeNormal2D());
	if (Right.IsNearlyZero())
	{
		Right = FallbackRight;
	}
	if (!FallbackRight.IsNearlyZero() && FVector::DotProduct(Right, FallbackRight) < 0.0)
	{
		Right = -Right;
	}

	if (InDir.IsNearlyZero() && OutDir.IsNearlyZero())
	{
		return Center + Right * Off;
	}

	const double Cross = InDir.X * OutDir.Y - InDir.Y * OutDir.X;
	const double Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(InDir, OutDir), -1.0, 1.0));
	const bool bInner = (Off * Cross) < 0.0;
	double UseOff = Off;
	if (bInner && Turn > 1.0e-3)
	{
		const double Chord = FVector::Dist2D(PrevCenter, NextCenter);
		const double SinTurn = FMath::Sin(Turn);
		const double Radius = (SinTurn > 1.0e-6 && Chord > 1.0)
			? (Chord / (2.0 * SinTurn))
			: 0.0;
		const double MinKeep = FMath::Max(HalfW * 0.08, 25.0);
		if (Radius > 1.0)
		{
			const double MaxInner = FMath::Max(Radius - MinKeep, MinKeep);
			if (FMath::Abs(UseOff) > MaxInner)
			{
				UseOff = FMath::Sign(UseOff) * MaxInner;
			}
		}
	}

	FVector Miter = FVector::ZeroVector;
	if (Turn > MetaLandRoad::CornerFilletSkipAngleRad
		&& IntersectOffsetLines2D(Center, InDir, OutDir, UseOff, Miter))
	{
		FVector Delta = Miter - Center;
		Delta.Z = 0.0;
		const FVector Side = Right * (UseOff >= 0.0 ? 1.0 : -1.0);
		if (FVector::DotProduct(Delta, Side) < 0.0)
		{
			return Center + Right * UseOff;
		}
		const double MaxLen = FMath::Abs(UseOff) / FMath::Max(FMath::Sin(Turn * 0.5), 0.20);
		if (Delta.Size2D() > MaxLen)
		{
			Delta = Delta.GetSafeNormal2D() * MaxLen;
		}
		FVector Out = Center;
		Out.X += Delta.X;
		Out.Y += Delta.Y;
		Out.Z = Center.Z;
		return Out;
	}
	return Center + Right * UseOff;
}

static void UncrossSweepRail(const TArray<FVector>& Centers, TArray<FVector>& Rails)
{
	if (Centers.Num() != Rails.Num() || Rails.Num() < 2)
	{
		return;
	}
	for (int32 i = 1; i < Rails.Num(); ++i)
	{
		FVector CenterMove = Centers[i] - Centers[i - 1];
		CenterMove.Z = 0.0;
		FVector RailMove = Rails[i] - Rails[i - 1];
		RailMove.Z = 0.0;
		if (CenterMove.SizeSquared2D() < 1.0)
		{
			continue;
		}
		if (FVector::DotProduct(RailMove, CenterMove) >= 0.0 && RailMove.SizeSquared2D() >= 0.25)
		{
			continue;
		}
		const FVector Step = CenterMove.GetSafeNormal2D() * FMath::Max(CenterMove.Size2D(), 1.0);
		Rails[i] = Rails[i - 1] + Step;
		Rails[i].Z = Centers[i].Z;
	}
}

struct FCornerFillet
{
	double SEnter = 0.0;
	double SLeave = 0.0;
	FVector CircleCenter = FVector::ZeroVector;
	FVector T1 = FVector::ZeroVector;
	FVector T2 = FVector::ZeroVector;
	FVector InDir = FVector::ForwardVector;
	FVector OutDir = FVector::ForwardVector;
	double Radius = 1.0;
	double MinInnerRadius = 40.0;
	double NeedAlong = 0.0;
	double LeadFrac = 0.22;
	double AngleEnter = 0.0;
	double AngleLeave = 0.0;
	FVector RightEnter = FVector::ZeroVector;
	FVector RightLeave = FVector::ZeroVector;
};

static FVector EvalFilletHermitePoint(
	const FVector& A,
	const FVector& B,
	const FVector& DirA,
	const FVector& DirB,
	double HandleA,
	double HandleB,
	double Alpha)
{
	const double T = FMath::Clamp(Alpha, 0.0, 1.0);
	const double T2 = T * T;
	const double T3 = T2 * T;
	const FVector M0 = DirA * HandleA;
	const FVector M1 = DirB * HandleB;
	FVector Point = A * (2.0 * T3 - 3.0 * T2 + 1.0)
		+ M0 * (T3 - 2.0 * T2 + T)
		+ B * (-2.0 * T3 + 3.0 * T2)
		+ M1 * (T3 - T2);
	Point.Z = FMath::Lerp(A.Z, B.Z, static_cast<float>(T));
	return Point;
}

static FVector EvalFilletHermiteTangent(
	const FVector& A,
	const FVector& B,
	const FVector& DirA,
	const FVector& DirB,
	double HandleA,
	double HandleB,
	double Alpha)
{
	const double T = FMath::Clamp(Alpha, 0.0, 1.0);
	const double T2 = T * T;
	const FVector M0 = DirA * HandleA;
	const FVector M1 = DirB * HandleB;
	const FVector D = A * (6.0 * T2 - 6.0 * T)
		+ M0 * (3.0 * T2 - 4.0 * T + 1.0)
		+ B * (-6.0 * T2 + 6.0 * T)
		+ M1 * (3.0 * T2 - 2.0 * T);
	return D.GetSafeNormal2D();
}

static void EvalFilletCircleFrame(
	const FCornerFillet& Fillet,
	double CircleU,
	FVector& OutCenter,
	FVector& OutRight,
	FVector* OutTangent = nullptr)
{
	const double U = FMath::Clamp(CircleU, 0.0, 1.0);
	const double Ang = FMath::Lerp(Fillet.AngleEnter, Fillet.AngleLeave, U);
	OutCenter = Fillet.CircleCenter
		+ FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * Fillet.Radius;
	OutCenter.Z = FMath::Lerp(Fillet.T1.Z, Fillet.T2.Z, U);

	FVector Tangent(-FMath::Sin(Ang), FMath::Cos(Ang), 0.0);
	if (Fillet.AngleLeave < Fillet.AngleEnter)
	{
		Tangent = -Tangent;
	}
	if (OutTangent)
	{
		*OutTangent = Tangent.GetSafeNormal2D();
	}
	OutRight = HorizontalRightFromTravel(Tangent);
	if (FVector::DotProduct(OutRight, Fillet.RightEnter) < 0.0)
	{
		OutRight = -OutRight;
	}
}
#endif

/** Layout half-width only. Must not call GetRoadEdgePointsAtS / EvalLanePosition. */
static double LayoutHalfWidthAtS(const UMetaLandRoadSplineComponent* Spline, double S)
{
	if (!Spline || Spline->GetLaneSectionsNum() == 0)
	{
		return MetaLandRoad::DefaultRoadLaneWidth;
	}

	const FMetaLandRoadLayout& Layout = Spline->GetRoadLayout();
	const int32 SectionIndex = Layout.FindSectionAtSOffset(S);
	if (SectionIndex == INDEX_NONE)
	{
		return MetaLandRoad::DefaultRoadLaneWidth;
	}

	double HalfW = 0.0;
	const int32 LeftSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
	if (LeftSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& LeftSection = Spline->GetLaneSection(LeftSectionIndex);
		if (!LeftSection.Left.IsEmpty())
		{
			HalfW = FMath::Max(
				HalfW,
				FMath::Abs(LeftSection.EvalLaneROffset(-LeftSection.Left.Num(), S, 1.0) + Spline->EvalROffset(S)));
		}
	}

	const int32 RightSectionIndex = Layout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);
	if (RightSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& RightSection = Spline->GetLaneSection(RightSectionIndex);
		if (!RightSection.Right.IsEmpty())
		{
			HalfW = FMath::Max(
				HalfW,
				FMath::Abs(RightSection.EvalLaneROffset(RightSection.Right.Num(), S, 1.0) + Spline->EvalROffset(S)));
		}
	}

	return FMath::Max(HalfW, 80.0);
}

/** Real CutS / trim-zone mouths only. Spline 0 / Length are caps, not body-corner bans. */
static bool IsProtectedMouthStation(const UMetaLandRoadSplineComponent* Spline, double KeyS)
{
	if (!Spline)
	{
		return true;
	}

	auto NearCutS = [KeyS](double CutS)
	{
		return CutS >= 0.0 && FMath::Abs(KeyS - CutS) <= 80.0;
	};

	if (const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
	{
		if (NearCutS(Road->StartJunctionMeshTrimS) || NearCutS(Road->EndJunctionMeshTrimS))
		{
			return true;
		}
		for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
		{
			if (NearCutS(Zone.ApproachCutS) || NearCutS(Zone.DepartCutS))
			{
				return true;
			}
		}
	}
	return false;
}

#if 0
static bool FilletWindowHitsMouth(
	const UMetaLandRoadSplineComponent* Spline,
	double KeyS,
	double NeedAlong)
{
	if (!Spline)
	{
		return true;
	}

	(void)NeedAlong;
	return IsProtectedMouthStation(Spline, KeyS);
}

/** Longest NeedAlong that stays off spline ends and CutS. Preview does not own this. */
static double MaxNeedAlongBeforeMouth(const UMetaLandRoadSplineComponent* Spline, double KeyS)
{
	if (!Spline)
	{
		return 0.0;
	}

	const double Length = Spline->GetSplineLength();
	double Budget = FMath::Min(KeyS - 8.0, Length - 8.0 - KeyS);
	auto LimitByCutS = [KeyS, &Budget](double CutS)
	{
		if (CutS >= 0.0)
		{
			Budget = FMath::Min(Budget, FMath::Abs(KeyS - CutS) - 40.0);
		}
	};

	if (const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
	{
		LimitByCutS(Road->StartJunctionMeshTrimS);
		LimitByCutS(Road->EndJunctionMeshTrimS);
		for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
		{
			LimitByCutS(Zone.ApproachCutS);
			LimitByCutS(Zone.DepartCutS);
		}
		TArray<TPair<double, double>> Spans;
		Road->GetJunctionMeshGenerationSpans(Spans);
		for (const TPair<double, double>& Span : Spans)
		{
			LimitByCutS(Span.Key);
			LimitByCutS(Span.Value);
		}
	}
	return FMath::Max(Budget, 0.0);
}
#endif

static bool IsCutSStation(const UMetaLandRoadSplineComponent* Spline, double S)
{
	if (!Spline)
	{
		return false;
	}
	const double Length = Spline->GetSplineLength();
	const double TipW = MetaLandRoad::CutSAlignWindowCm;
	if (S <= TipW || S >= Length - TipW)
	{
		return true;
	}
	auto Near = [S](double CutS)
	{
		return CutS >= 0.0 && FMath::Abs(S - CutS) <= MetaLandRoad::CutSAlignWindowCm;
	};
	if (const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
	{
		if (Near(Road->StartJunctionMeshTrimS) || Near(Road->EndJunctionMeshTrimS))
		{
			return true;
		}
		for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
		{
			if (Near(Zone.ApproachCutS) || Near(Zone.DepartCutS))
			{
				return true;
			}
		}
	}
	return false;
}

#if 0
static bool TryEvalFilletFrame(
	const TArray<FCornerFillet>& Fillets,
	double S,
	FVector& OutCenter,
	FVector& OutRight)
{
	for (const FCornerFillet& Fillet : Fillets)
	{
		if (S < Fillet.SEnter || S > Fillet.SLeave)
		{
			continue;
		}

		const double Span = FMath::Max(Fillet.SLeave - Fillet.SEnter, 1.0);
		const double U = FMath::Clamp((S - Fillet.SEnter) / Span, 0.0, 1.0);
		if (U <= 0.02)
		{
			OutCenter = Fillet.T1;
			OutRight = Fillet.RightEnter;
			return true;
		}
		if (U >= 0.98)
		{
			OutCenter = Fillet.T2;
			OutRight = Fillet.RightLeave.IsNearlyZero() ? Fillet.RightEnter : Fillet.RightLeave;
			return true;
		}

		const double Lead = FMath::Clamp(Fillet.LeadFrac, 0.12, 0.46);
		const double HandleExtend = FMath::Max(Fillet.NeedAlong * Lead * 1.45, Fillet.Radius * Lead * 1.2);
		const double HandleArrive = FMath::Max(Fillet.NeedAlong * Lead * 0.70, Fillet.Radius * Lead * 0.65);
		if (U < Lead)
		{
			FVector CirclePt = Fillet.T1;
			FVector CircleRight = Fillet.RightEnter;
			FVector CircleTan = Fillet.InDir;
			EvalFilletCircleFrame(Fillet, Lead, CirclePt, CircleRight, &CircleTan);
			const double Local = U / Lead;
			OutCenter = EvalFilletHermitePoint(
				Fillet.T1, CirclePt, Fillet.InDir, CircleTan, HandleExtend, HandleArrive, Local);
			const FVector Travel = EvalFilletHermiteTangent(
				Fillet.T1, CirclePt, Fillet.InDir, CircleTan, HandleExtend, HandleArrive, Local);
			OutRight = HorizontalRightFromTravel(Travel.IsNearlyZero() ? Fillet.InDir : Travel);
			if (FVector::DotProduct(OutRight, Fillet.RightEnter) < 0.0)
			{
				OutRight = -OutRight;
			}
			return true;
		}
		if (U > 1.0 - Lead)
		{
			FVector CirclePt = Fillet.T2;
			FVector CircleRight = Fillet.RightLeave;
			FVector CircleTan = Fillet.OutDir;
			EvalFilletCircleFrame(Fillet, 1.0 - Lead, CirclePt, CircleRight, &CircleTan);
			const double Local = (U - (1.0 - Lead)) / Lead;
			OutCenter = EvalFilletHermitePoint(
				CirclePt, Fillet.T2, CircleTan, Fillet.OutDir, HandleArrive, HandleExtend, Local);
			const FVector Travel = EvalFilletHermiteTangent(
				CirclePt, Fillet.T2, CircleTan, Fillet.OutDir, HandleArrive, HandleExtend, Local);
			const FVector Prefer = Fillet.RightLeave.IsNearlyZero() ? Fillet.RightEnter : Fillet.RightLeave;
			OutRight = HorizontalRightFromTravel(Travel.IsNearlyZero() ? Fillet.OutDir : Travel);
			if (FVector::DotProduct(OutRight, Prefer) < 0.0)
			{
				OutRight = -OutRight;
			}
			return true;
		}

		const double CircleU = (U - Lead) / FMath::Max(1.0 - 2.0 * Lead, 0.08);
		EvalFilletCircleFrame(Fillet, CircleU, OutCenter, OutRight);
		return true;
	}
	return false;
}

/** Keep the offset rail on the path side of the fillet core. Crossing the center still has large Dist. */
static FVector ClampOffsetToFilletInnerKeep(
	const TArray<FCornerFillet>& Fillets,
	double S,
	const FVector& PathCenter,
	const FVector& Point)
{
	FVector Out = Point;
	for (const FCornerFillet& Fillet : Fillets)
	{
		if (S < Fillet.SEnter || S > Fillet.SLeave)
		{
			continue;
		}

		const double Span = FMath::Max(Fillet.SLeave - Fillet.SEnter, 1.0);
		const double U = FMath::Clamp((S - Fillet.SEnter) / Span, 0.0, 1.0);
		const double Lead = FMath::Clamp(Fillet.LeadFrac, 0.12, 0.46);
		if (U < Lead || U > 1.0 - Lead)
		{
			return Out;
		}

		FVector Radial = PathCenter - Fillet.CircleCenter;
		Radial.Z = 0.0;
		if (Radial.SizeSquared2D() < 1.0)
		{
			Radial = FVector(FMath::Cos(Fillet.AngleEnter), FMath::Sin(Fillet.AngleEnter), 0.0);
		}

		FVector Delta = Out - Fillet.CircleCenter;
		Delta.Z = 0.0;
		const double MinR = FMath::Max(Fillet.MinInnerRadius, 40.0);
		const bool bCrossedCore = FVector::DotProduct(Delta, Radial) < 0.0;
		if (!bCrossedCore && Delta.Size2D() >= MinR)
		{
			return Out;
		}

		Delta = Radial.GetSafeNormal2D() * MinR;
		Out.X = Fillet.CircleCenter.X + Delta.X;
		Out.Y = Fillet.CircleCenter.Y + Delta.Y;
		return Out;
	}
	return Out;
}

/** Parked: loft no longer replaces the authored centerline. Ask before deleting. */
static thread_local int32 GCollectCornerFilletsDepth = 0;

static void CollectCornerFillets(
	const UMetaLandRoadSplineComponent* Spline,
	double S0,
	double S1,
	ESplineCoordinateSpace::Type CoordinateSpace,
	TArray<FCornerFillet>& OutFillets)
{
	OutFillets.Reset();
	if (!Spline || GCollectCornerFilletsDepth > 0)
	{
		return;
	}
	TGuardValue<int32> DepthGuard(GCollectCornerFilletsDepth, 1);

	const int32 NumPts = Spline->GetNumberOfSplinePoints();
	if (NumPts < 3)
	{
		return;
	}

	const double SpanMin = FMath::Min(S0, S1);
	const double SpanMax = FMath::Max(S0, S1);
	for (int32 KeyIdx = 1; KeyIdx <= NumPts - 2; ++KeyIdx)
	{
		const double KeyS = Spline->GetDistanceAlongSplineAtSplinePoint(KeyIdx);
		if (KeyS < SpanMin - 10.0 || KeyS > SpanMax + 10.0)
		{
			continue;
		}
		if (IsProtectedMouthStation(Spline, KeyS))
		{
			continue;
		}

		const FVector Prev = Spline->GetLocationAtSplinePoint(KeyIdx - 1, CoordinateSpace);
		const FVector Cur = Spline->GetLocationAtSplinePoint(KeyIdx, CoordinateSpace);
		const FVector Next = Spline->GetLocationAtSplinePoint(KeyIdx + 1, CoordinateSpace);
		const FVector InDir = (Cur - Prev).GetSafeNormal2D();
		const FVector OutDir = (Next - Cur).GetSafeNormal2D();
		if (InDir.IsNearlyZero() || OutDir.IsNearlyZero())
		{
			continue;
		}

		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(InDir, OutDir), -1.0, 1.0));
		if (Angle < MetaLandRoad::CornerFilletSkipAngleRad || Angle > FMath::DegreesToRadians(170.0))
		{
			continue;
		}

		const double HalfW = LayoutHalfWidthAtS(Spline, KeyS);
		const double TanHalf = FMath::Max(
			FMath::Tan(FMath::Clamp(Angle * 0.5, 0.0, 1.15)),
			0.08);
		const double MouthBudget = MaxNeedAlongBeforeMouth(Spline, KeyS);
		const double SegIn = FMath::Max(FVector::Dist2D(Prev, Cur), 1.0);
		const double SegOut = FMath::Max(FVector::Dist2D(Cur, Next), 1.0);
		const double NeedAlong = MetaLandRoad::CornerFilletNeedAlongFitCm(
			HalfW,
			Angle,
			FMath::Min(SegIn, SegOut),
			MouthBudget);
		const double Radius = NeedAlong / TanHalf;
		if (NeedAlong < 20.0)
		{
			continue;
		}

		FVector T1 = Cur - InDir * NeedAlong;
		FVector T2 = Cur + OutDir * NeedAlong;
		T1.Z = Cur.Z;
		T2.Z = Cur.Z;

		const bool bLeftTurn = (InDir.X * OutDir.Y - InDir.Y * OutDir.X) > 0.0;
		const FVector RightIn = HorizontalRightFromTravel(InDir);
		const FVector RightOut = HorizontalRightFromTravel(OutDir);
		const FVector InwardIn = bLeftTurn ? -RightIn : RightIn;
		const FVector InwardOut = bLeftTurn ? -RightOut : RightOut;
		const FVector C1 = T1 + InwardIn * Radius;
		const FVector C2 = T2 + InwardOut * Radius;
		if (FVector::Dist2D(C1, C2) > 8.0)
		{
			continue;
		}

		FCornerFillet Fillet;
		Fillet.CircleCenter = (C1 + C2) * 0.5;
		Fillet.CircleCenter.Z = Cur.Z;
		Fillet.T1 = T1;
		Fillet.T2 = T2;
		Fillet.InDir = InDir;
		Fillet.OutDir = OutDir;
		Fillet.Radius = Radius;
		Fillet.NeedAlong = NeedAlong;
		Fillet.LeadFrac = MetaLandRoad::CornerFilletLeadFrac(Angle);
		Fillet.MinInnerRadius = (Radius > HalfW + 1.0)
			? FMath::Max(Radius - HalfW, HalfW * TanHalf)
			: FMath::Max(Radius * 0.15, 25.0);
		const double WindowAlong = (MouthBudget > 1.0)
			? FMath::Min(NeedAlong, MouthBudget)
			: NeedAlong;
		Fillet.SEnter = KeyS - WindowAlong;
		Fillet.SLeave = KeyS + WindowAlong;
		Fillet.RightEnter = RightIn;
		Fillet.RightLeave = RightOut;
		Fillet.AngleEnter = FMath::Atan2(T1.Y - Fillet.CircleCenter.Y, T1.X - Fillet.CircleCenter.X);
		Fillet.AngleLeave = FMath::Atan2(T2.Y - Fillet.CircleCenter.Y, T2.X - Fillet.CircleCenter.X);
		if (bLeftTurn)
		{
			while (Fillet.AngleLeave <= Fillet.AngleEnter)
			{
				Fillet.AngleLeave += 2.0 * PI;
			}
		}
		else
		{
			while (Fillet.AngleLeave >= Fillet.AngleEnter)
			{
				Fillet.AngleLeave -= 2.0 * PI;
			}
		}
		bool bOverlaps = false;
		for (int32 Existing = 0; Existing < OutFillets.Num(); ++Existing)
		{
			const FCornerFillet& Other = OutFillets[Existing];
			if (Fillet.SLeave < Other.SEnter + 8.0 || Fillet.SEnter > Other.SLeave - 8.0)
			{
				continue;
			}
			bOverlaps = true;
			if ((Fillet.SLeave - Fillet.SEnter) > (Other.SLeave - Other.SEnter))
			{
				OutFillets[Existing] = Fillet;
			}
			break;
		}
		if (!bOverlaps)
		{
			OutFillets.Add(Fillet);
		}
	}
}
#endif

static float SumFaceWidths(const TArray<float>& Widths)
{
	float Sum = 0.0f;
	for (const float W : Widths)
	{
		Sum += W;
	}
	return Sum;
}

static void CollectUniqueFaceWidths(
	const UMetaLandRoadSplineComponent* Spline,
	TArray<float>& OutLeftWidths,
	TArray<float>& OutRightWidths)
{
	OutLeftWidths.Reset();
	OutRightWidths.Reset();
	if (!Spline)
	{
		return;
	}

	TSet<int32> Seen;
	for (int32 SectionIndex = 0; SectionIndex < Spline->GetLaneSectionsNum(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
		for (int32 i = 0; i < Section.Left.Num(); ++i)
		{
			const int32 LaneIndex = Section.Left[i].GetLaneIndex();
			const int32 Key = (LaneIndex != 0) ? LaneIndex : -(1000 * (SectionIndex + 1) + i + 1);
			if (Seen.Contains(Key))
			{
				continue;
			}
			Seen.Add(Key);
			OutLeftWidths.Add(FMath::Max(Section.Left[i].Width.Eval(0.f), 1.0f));
		}
		for (int32 i = 0; i < Section.Right.Num(); ++i)
		{
			const int32 LaneIndex = Section.Right[i].GetLaneIndex();
			const int32 Key = (LaneIndex != 0) ? LaneIndex : (1000 * (SectionIndex + 1) + i + 1);
			if (Seen.Contains(Key))
			{
				continue;
			}
			Seen.Add(Key);
			OutRightWidths.Add(FMath::Max(Section.Right[i].Width.Eval(0.f), 1.0f));
		}
	}
}

static FMetaLandRoadLane MakeBlendWidthLane(float StartW, float EndW, float LengthCm)
{
	FMetaLandRoadLane Lane;
	FMetaLandRoadLaneDriving DrivingLane;
	DrivingLane.DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;
	Lane.LaneInstance.InitializeAs<FMetaLandRoadLaneDriving>(DrivingLane);
	Lane.Width.Reset();
	const FKeyHandle H0 = Lane.Width.AddKey(0.0f, FMath::Max(StartW, 8.0f));
	Lane.Width.SetKeyInterpMode(H0, ERichCurveInterpMode::RCIM_Linear);
	const FKeyHandle H1 = Lane.Width.AddKey(FMath::Max(LengthCm, 1.0f), FMath::Max(EndW, 8.0f));
	Lane.Width.SetKeyInterpMode(H1, ERichCurveInterpMode::RCIM_Linear);
	return Lane;
}

static float SumLaneWidthsAtTime(const TArray<FMetaLandRoadLane>& Face, float Time)
{
	float Sum = 0.0f;
	for (const FMetaLandRoadLane& Lane : Face)
	{
		Sum += FMath::Max(Lane.Width.Eval(Time), 1.0f);
	}
	return Sum;
}

static void WriteLaneBodyToTipWidth(
	FMetaLandRoadLane& Lane,
	float SectionLen,
	float BodyW,
	float TipW,
	bool bTipAtStart)
{
	Lane.Width.Reset();
	const float StartW = FMath::Max(bTipAtStart ? TipW : BodyW, 8.0f);
	const float EndW = FMath::Max(bTipAtStart ? BodyW : TipW, 8.0f);
	const FKeyHandle H0 = Lane.Width.AddKey(0.0f, StartW);
	Lane.Width.SetKeyInterpMode(H0, ERichCurveInterpMode::RCIM_Linear);
	const FKeyHandle H1 = Lane.Width.AddKey(FMath::Max(SectionLen, 1.0f), EndW);
	Lane.Width.SetKeyInterpMode(H1, ERichCurveInterpMode::RCIM_Linear);
}

// =============================================================================
// [KEY-ALGO] ApplyMinLaneLeftRightWidthBlend
// 2合1 / N合1：按最小车道数=1 写成 Left[0]+Right[0]，只混 width。
// ROffset≡0，粉中线 = mesh 几何中心，头尾不再被 EvalLaneROffset 推偏。
// =============================================================================
static void ApplyMinLaneLeftRightWidthBlend(
	UMetaLandRoadSplineComponent* Spline,
	float StartLeftW,
	float StartRightW,
	float EndLeftW,
	float EndRightW)
{
	if (!Spline)
	{
		return;
	}

	const float Len = static_cast<float>(FMath::Max(Spline->GetSplineLength(), 1.0));
	TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSectionsMutable();
	if (Sections.Num() == 0)
	{
		Sections.AddDefaulted();
	}

	FMetaLandRoadLaneSection& Host = Sections[0];
	Host.Side = EMetaLandRoadLaneSectionSide::Both;
	Host.SOffset = 0.0;
	Host.Left.Reset();
	Host.Right.Reset();
	Host.Left.Add(MakeBlendWidthLane(StartLeftW, EndLeftW, Len));
	Host.Right.Add(MakeBlendWidthLane(StartRightW, EndRightW, Len));
	if (Sections.Num() > 1)
	{
		Sections.SetNum(1);
	}

	FMetaLandRoadLayout& Layout = Spline->GetRoadLayoutMutable();
	Layout.ROffset.Reset();
	const FKeyHandle R0 = Layout.ROffset.AddKey(0.0f, 0.0f);
	Layout.ROffset.SetKeyInterpMode(R0, ERichCurveInterpMode::RCIM_Linear);
	const FKeyHandle R1 = Layout.ROffset.AddKey(Len, 0.0f);
	Layout.ROffset.SetKeyInterpMode(R1, ERichCurveInterpMode::RCIM_Linear);
	Spline->UpdateLaneSectionBounds();
	Spline->UpdateRoadLayout();
}

#if WITH_EDITOR
static void RedrawNativeEditViewport(UMetaLandRoadSplineComponent* Spline)
{
	if (Spline && GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}
#endif

using namespace UE::Geometry;

namespace MetaLandRoadLaneMeshPrivate
{
	constexpr int32 NumPointPerSegment = MetaLandRoad::DefaultReparamStepsPerSegment;
	constexpr int32 MinNumStepsPerSection = MetaLandRoad::DefaultMinNumStepsPerSection;

	static int32 ComputeScaledMinStepsForSpan(
		double S0,
		double S1,
		double TotalSplineLength,
		int32 GlobalMinSteps = MinNumStepsPerSection)
	{
		if (TotalSplineLength <= KINDA_SMALL_NUMBER)
		{
			return FMath::Max(2, GlobalMinSteps);
		}

		const double SpanLength = FMath::Max(S1 - S0, 0.0);
		const double Ratio = SpanLength / TotalSplineLength;
		return FMath::Clamp(
			FMath::RoundToInt(static_cast<float>(GlobalMinSteps * Ratio)),
			4,
			FMath::Max(GlobalMinSteps, 4));
	}

	/**
	 * Inner rail shares Outer S and the same PathCenter (sweep frame).
	 * Offset along the outer lateral so inner does not re-eval a raw V.
	 */
	static void BuildInnerRingMatchingOuterS(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 OuterLaneIndex,
		int32 InnerLaneIndex,
		double InnerAlpha,
		double S0,
		double S1,
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& OuterPoints,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& OutInner)
	{
		OutInner.Reset();
		if (!Component || OuterPoints.Num() < 2)
		{
			return;
		}

		OutInner.SetNum(OuterPoints.Num());
		for (int32 i = 0; i < OuterPoints.Num(); ++i)
		{
			double S = Component->GetDistanceAlongSplineAtSplineInputKey(OuterPoints[i].SplineParam);
			if (i == 0)
			{
				S = S0;
			}
			else if (i + 1 == OuterPoints.Num())
			{
				S = S1;
			}

			const double InnerOff = Component->GetLaneSection(SectionIndex).EvalLaneROffset(InnerLaneIndex, S, InnerAlpha)
				+ Component->EvalROffset(S);
			const double OuterOff = Component->GetLaneSection(SectionIndex).EvalLaneROffset(OuterLaneIndex, S, 1.0)
				+ Component->EvalROffset(S);
			FVector Lateral = OuterPoints[i].Position - OuterPoints[i].PathCenter;
			Lateral.Z = 0.0;
			if (Lateral.SizeSquared2D() <= 1.0 && i > 0)
			{
				Lateral = OutInner[i - 1].Position - OutInner[i - 1].PathCenter;
				Lateral.Z = 0.0;
			}
			if (Lateral.SizeSquared2D() > 1.0 && FMath::Abs(OuterOff) > 1.0)
			{
				OutInner[i].Position = OuterPoints[i].PathCenter + Lateral * (InnerOff / OuterOff);
				OutInner[i].Position.Z = OuterPoints[i].Position.Z;
			}
			else
			{
				OutInner[i].Position = OuterPoints[i].PathCenter;
				OutInner[i].Position.Z = OuterPoints[i].Position.Z;
			}
			OutInner[i].SplineParam = Component->GetInputKeyValueAtDistanceAlongSpline(S);
			OutInner[i].PathCenter = OuterPoints[i].PathCenter;
		}
	}

	void SnapLaneApproximationEndpoint(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 LaneIndex,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& Points,
		double S,
		bool bStart)
	{
		if (!Component || Points.Num() == 0)
		{
			return;
		}
		const int32 Idx = bStart ? 0 : Points.Num() - 1;
		const FVector RawCenter = Component->GetLocationAtDistanceAlongSpline(
			S, ESplineCoordinateSpace::Local);
		if (FVector::Dist2D(RawCenter, Points[Idx].PathCenter) > 15.0)
		{
			return;
		}
		const FVector Center = RawCenter;
		const FVector Right = FlattenHorizontalRight(
			Component->GetRightVectorAtDistanceAlongSpline(S, ESplineCoordinateSpace::Local));
		const int32 Sec = SectionIndex;
		double Off = Component->EvalROffset(S);
		if (Component->GetLaneSections().IsValidIndex(Sec)
			&& LaneIndex != LANE_INDEX_NONE)
		{
			Off += Component->GetLaneSection(Sec).EvalLaneROffset(LaneIndex, S, 1.0);
		}
		Points[Idx].SplineParam = Component->GetInputKeyValueAtDistanceAlongSpline(S);
		Points[Idx].PathCenter = Component->ApplyLinkedJunctionHeightToPosition(
			Center, S, ESplineCoordinateSpace::Local);
		Points[Idx].Position = Component->ApplyLinkedJunctionHeightToPosition(
			Center + Right * Off, S, ESplineCoordinateSpace::Local);
	}

	bool BuildLaneRoadMesh(
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& InnerSide,
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& OuterSide,
		const UMetaLandRoadSplineComponent* Component,
		int32 LaneIndex,
		const FMetaLandRoadLaneSection& LaneSection,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		if (InnerSide.Num() != OuterSide.Num() || InnerSide.Num() < 2)
		{
			return false;
		}

		const int32 N = InnerSide.Num();
		const int32 BaseVertexID = OutMesh.VertexCount();
		const FVector HeightOffset(0.0, 0.0, RoadHeightOffset);
		const float UvTileCm = FMath::Max(
			Component ? Component->AsphaltUvTileCm : MetaLandRoad::DefaultAsphaltUvTileCm,
			50.0f);

		// Asphalt UVs: U = across, V = along spline (world cm / tile). Larger tile = fewer side-edge stripes.
		auto MakeAsphaltUV = [Component, UvTileCm](const FVector& LocalPos, float SplineParam) -> FVector2f
		{
			const double S = Component->GetDistanceAlongSplineAtSplineInputKey(SplineParam);
			const FVector Center = Component->GetLocationAtDistanceAlongSpline(
				S, ESplineCoordinateSpace::Local);
			const FVector Right = Component->GetRightVectorAtDistanceAlongSpline(
				S, ESplineCoordinateSpace::Local);
			const float LateralCm = static_cast<float>(FVector::DotProduct(LocalPos - Center, Right));
			return FVector2f(LateralCm / UvTileCm, static_cast<float>(S) / UvTileCm);
		};

		// Inner strip first, outer strip second (MetaRoad BuildRoadMesh order).
		for (int32 i = 0; i < N; ++i)
		{
			FVertexInfo InnerVertex;
			InnerVertex.Position = InnerSide[i].Position + HeightOffset;
			const FVector2f InnerUV = bGenerateUVs
				? MakeAsphaltUV(InnerSide[i].Position, InnerSide[i].SplineParam)
				: FVector2f::Zero();
			if (bGenerateUVs)
			{
				InnerVertex.UV = InnerUV;
				InnerVertex.bHaveUV = true;
			}
			const int32 InnerVid = OutMesh.AppendVertex(InnerVertex);
			if (bGenerateUVs && OutMesh.HasVertexUVs())
			{
				OutMesh.SetVertexUV(InnerVid, InnerUV);
			}
		}

		for (int32 i = 0; i < N; ++i)
		{
			FVertexInfo OuterVertex;
			OuterVertex.Position = OuterSide[i].Position + HeightOffset;
			const FVector2f OuterUV = bGenerateUVs
				? MakeAsphaltUV(OuterSide[i].Position, OuterSide[i].SplineParam)
				: FVector2f::Zero();
			if (bGenerateUVs)
			{
				OuterVertex.UV = OuterUV;
				OuterVertex.bHaveUV = true;
			}
			const int32 OuterVid = OutMesh.AppendVertex(OuterVertex);
			if (bGenerateUVs && OutMesh.HasVertexUVs())
			{
				OutMesh.SetVertexUV(OuterVid, OuterUV);
			}
		}

		// DynamicMesh is single-sided. Right-hand lanes use one winding; left-hand lanes need the opposite
		// so both sides face up in the preview mesh (MetaRoad SceneProxy uses a different facing).
		for (int32 i = 0; i < N - 1; ++i)
		{
			const double InnerWidthSq = FVector::DistSquared(InnerSide[i].Position, OuterSide[i].Position);
			const double NextInnerWidthSq = FVector::DistSquared(InnerSide[i + 1].Position, OuterSide[i + 1].Position);
			if (InnerWidthSq < 1.0e-4 && NextInnerWidthSq < 1.0e-4)
			{
				continue;
			}

			if (LaneIndex < 0)
			{
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i, BaseVertexID + i + 1, BaseVertexID + i + N));
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i + N, BaseVertexID + i + 1, BaseVertexID + i + 1 + N));
			}
			else
			{
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i, BaseVertexID + i + N, BaseVertexID + i + 1));
				OutMesh.AppendTriangle(FIndex3i(BaseVertexID + i + N, BaseVertexID + i + 1 + N, BaseVertexID + i + 1));
			}
		}

		return true;
	}

	bool AppendLaneMeshForSection(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 OuterLaneIndex,
		int32 InnerLaneIndex,
		double InnerAlpha,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SectionIndex);
		if ((S1 - S0) < 10.0) // <10cm: MinSteps packs rings into a fan band
		{
			return false;
		}

		TArray<FMetaLandRoadSplinePositionLinearApproximation> OuterPoints;
		Component->BuildLinearApproximation(
			OuterPoints,
			[Component, SectionIndex, OuterLaneIndex](double S)
			{
				return Component->GetLaneSection(SectionIndex).EvalLaneROffset(OuterLaneIndex, S, 1.0)
					+ Component->EvalROffset(S);
			},
			S0,
			S1,
			NumPointPerSegment,
			MinNumSteps,
			ESplineCoordinateSpace::Local);

		if (OuterPoints.Num() < 2)
		{
			return false;
		}

		SnapLaneApproximationEndpoint(Component, SectionIndex, OuterLaneIndex, OuterPoints, S0, true);
		SnapLaneApproximationEndpoint(Component, SectionIndex, OuterLaneIndex, OuterPoints, S1, false);

		TArray<FMetaLandRoadSplinePositionLinearApproximation> InnerPoints;
		BuildInnerRingMatchingOuterS(
			Component,
			SectionIndex,
			OuterLaneIndex,
			InnerLaneIndex,
			InnerAlpha,
			S0,
			S1,
			OuterPoints,
			InnerPoints);
		if (InnerPoints.Num() != OuterPoints.Num())
		{
			return false;
		}

		return BuildLaneRoadMesh(
			InnerPoints,
			OuterPoints,
			Component,
			OuterLaneIndex,
			Section,
			RoadHeightOffset,
			bGenerateUVs,
			OutMesh);
	}

	void AppendLanesForSide(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 PreSectionIndex,
		bool bHasSide,
		bool bUseCurrentSectionLanes,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& /*PrevPoints*/,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh,
		int32 LaneSign)
	{
		const int32 LaneSectionIndex = bUseCurrentSectionLanes ? SectionIndex : PreSectionIndex;
		if (LaneSectionIndex < 0)
		{
			return;
		}

		const FMetaLandRoadLaneSection& LaneSection = Component->GetLaneSection(LaneSectionIndex);
		const TArray<FMetaLandRoadLane>& Lanes = LaneSign > 0 ? LaneSection.Right : LaneSection.Left;
		if (!bHasSide || Lanes.IsEmpty())
		{
			return;
		}

		const int32 OuterLaneIndex = LaneSign > 0 ? Lanes.Num() : -Lanes.Num();
		AppendLaneMeshForSection(
			Component,
			LaneSectionIndex,
			OuterLaneIndex,
			LANE_INDEX_NONE,
			0.0,
			S0,
			S1,
			MinNumSteps,
			RoadHeightOffset,
			bGenerateUVs,
			OutMesh);
	}

}

namespace MetaLandRoadSplinePointPrivate
{
	static double CalcTangentMultiplier(const float InRadius, const float InRotInc)
	{
		static constexpr double A = 0.5;
		static constexpr double A2 = A * A;
		static constexpr double A3 = A2 * A;

		const FVector T0 = FVector::ForwardVector;
		const FVector T1 = T0.RotateAngleAxis(InRotInc, FVector::UpVector);
		const FVector P0 = FVector::RightVector * InRadius;
		const FVector P1 = P0.RotateAngleAxis(InRotInc, FVector::UpVector);

		const FVector ActualVal = FMath::CubicInterp(P0, T0, P1, T1, A);
		const FVector ExpectedVal = P0.RotateAngleAxis(InRotInc * A, FVector::UpVector);
		const double Diff = (ActualVal.X - ExpectedVal.X);

		static constexpr double C1 = (A3 - (2 * A2) + A);
		static constexpr double C2 = (A3 - A2);
		const double PartialInterp = -1.0 * ((C1 * T0.X) + (C2 * T1.X));

		return FMath::IsNearlyZero(PartialInterp) ? 1.0 : ((Diff / PartialInterp) + 1.0);
	}
}

UMetaLandRoadSplineComponent::UMetaLandRoadSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHiddenInGame = true;
	bDrawDebug = false;

	PredecessorConnection = CreateDefaultSubobject<UMetaLandRoadConnection>(TEXT("PredecessorConnection"));
	SuccessorConnection = CreateDefaultSubobject<UMetaLandRoadConnection>(TEXT("SuccessorConnection"));

	if (SplineCurves.Position.Points.Num() >= 2)
	{
		SplineCurves.Position.Points[1].OutVal = FVector(1000.0, 0.0, 0.0);
	}
	UpdateSpline();
}

void UMetaLandRoadSplineComponent::FixUpSegments()
{
	if (GetNumberOfSplinePoints() != PointTypes.Num())
	{
		PointTypes.Reset();
		PointTypes.SetNum(GetNumberOfSplinePoints());
	}
}

int32 UMetaLandRoadSplineComponent::GetNextPoint(int32 PointIndex) const
{
	if (IsClosedLoop())
	{
		return (PointIndex + 1) % GetNumberOfSplinePoints();
	}
	if (PointIndex < (GetNumberOfSplinePoints() - 2))
	{
		return PointIndex + 1;
	}
	return INDEX_NONE;
}

int32 UMetaLandRoadSplineComponent::GetPrevPoint(int32 PointIndex) const
{
	if (IsClosedLoop())
	{
		return (PointIndex == 0) ? GetNumberOfSplinePoints() - 1 : PointIndex - 1;
	}
	if (PointIndex > 0)
	{
		return PointIndex - 1;
	}
	return INDEX_NONE;
}

EMetaLandRoadSplinePointType UMetaLandRoadSplineComponent::GetRoadSplinePointType(int32 PointIndex) const
{
	if (PointIndex >= 0 && PointIndex < SplineCurves.Position.Points.Num())
	{
		if (PointTypes.IsValidIndex(PointIndex) && PointTypes[PointIndex] == EMetaLandRoadSplinePointTypeOverride::Arc)
		{
			return EMetaLandRoadSplinePointType::Arc;
		}
		return static_cast<EMetaLandRoadSplinePointType>(GetSplinePointType(PointIndex));
	}

	return EMetaLandRoadSplinePointType::Constant;
}

void UMetaLandRoadSplineComponent::SetRoadSplinePointType(int32 PointIndex, EMetaLandRoadSplinePointType Mode, bool bUpdateSpline)
{
	FixUpSegments();

	if (PointIndex >= 0 && PointIndex < SplineCurves.Position.Points.Num())
	{
		if (Mode == EMetaLandRoadSplinePointType::Arc)
		{
			PointTypes[PointIndex] = EMetaLandRoadSplinePointTypeOverride::Arc;
			SplineCurves.Position.Points[PointIndex].InterpMode = CIM_CurveUser;
			const int32 NextPoint = GetNextPoint(PointIndex);
			if (NextPoint >= 0)
			{
				SplineCurves.Position.Points[NextPoint].InterpMode = CIM_CurveUser;
			}
		}
		else
		{
			PointTypes[PointIndex] = EMetaLandRoadSplinePointTypeOverride::Inherited;
			SplineCurves.Position.Points[PointIndex].InterpMode = ConvertSplinePointTypeToInterpCurveMode(
				static_cast<ESplinePointType::Type>(Mode));
		}

		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}

bool UMetaLandRoadSplineComponent::AdjustArcSegment(int32 PointIndex, EMetaLandRoadComputeArcMode Mode)
{
	check(PointIndex >= 0 && PointIndex < GetNumberOfSplineSegments());

	bool bResult = true;

	const int32 StartPointIndex = PointIndex;
	const int32 EndPointIndex = (PointIndex + 1) % GetNumberOfSplinePoints();

	FInterpCurvePointVector& StartPoint = SplineCurves.Position.Points[StartPointIndex];
	FInterpCurvePointVector& EndPoint = SplineCurves.Position.Points[EndPointIndex];

	FVector StartToEnd = EndPoint.OutVal - StartPoint.OutVal;
	FVector StartToEndDir = StartToEnd.GetSafeNormal();
	FVector StartDir;
	FVector EndDir;

	if (Mode == EMetaLandRoadComputeArcMode::AdjStartTangent || Mode == EMetaLandRoadComputeArcMode::AdjEndTangent)
	{
		StartToEnd = EndPoint.OutVal - StartPoint.OutVal;
		StartToEndDir = StartToEnd.GetSafeNormal();
		if (Mode == EMetaLandRoadComputeArcMode::AdjStartTangent)
		{
			EndDir = -GetDirectionAtSplinePoint(EndPointIndex, ESplineCoordinateSpace::Local);
			StartDir = FMath::GetReflectionVector(EndDir, StartToEndDir);
		}
		else
		{
			StartDir = GetDirectionAtSplinePoint(StartPointIndex, ESplineCoordinateSpace::Local);
			EndDir = FMath::GetReflectionVector(StartDir, -StartToEndDir);
		}
	}
	else
	{
		StartDir = GetDirectionAtSplinePoint(StartPointIndex, ESplineCoordinateSpace::Local);
		EndDir = -GetDirectionAtSplinePoint(EndPointIndex, ESplineCoordinateSpace::Local);
		FVector Intersection;
		if (FMath::SegmentIntersection2D(
			StartPoint.OutVal,
			StartPoint.OutVal + StartDir * 1.0e7f,
			EndPoint.OutVal,
			EndPoint.OutVal + EndDir * 1.0e7f,
			Intersection))
		{
			const double A = (Intersection - StartPoint.OutVal).Size();
			const double B = (Intersection - EndPoint.OutVal).Size();
			if (Mode == EMetaLandRoadComputeArcMode::AdjStartPos)
			{
				StartPoint.OutVal += StartDir * (A - B);
			}
			else
			{
				EndPoint.OutVal += EndDir * (B - A);
			}
			StartToEnd = EndPoint.OutVal - StartPoint.OutVal;
			StartToEndDir = StartToEnd.GetSafeNormal();
		}
		else
		{
			bResult = false;
		}
	}

	const FVector StartRightVector(FVector2D(StartDir.X, StartDir.Y).GetRotated(90.0), StartDir.Z);
	const double CosA = StartRightVector.CosineAngle2D(StartToEndDir);

	if (StartDir.CosineAngle2D(StartToEndDir) < 0.0)
	{
		bResult = false;
	}

	const double Radius = (StartToEnd.Size2D() / 2.0) / CosA;
	const double Ang = (180.0 - FMath::Acos(CosA) * 2.0 / PI * 180.0);
	const double TangentMult = MetaLandRoadSplinePointPrivate::CalcTangentMultiplier(static_cast<float>(Radius), static_cast<float>(-Ang));

	StartPoint.LeaveTangent = StartDir * TangentMult;
	StartPoint.ArriveTangent = StartDir * StartPoint.ArriveTangent.Size() * (TangentMult >= 0.0 ? 1.0 : -1.0);

	EndPoint.LeaveTangent = -EndDir * EndPoint.LeaveTangent.Size() * (TangentMult >= 0.0 ? 1.0 : -1.0);
	EndPoint.ArriveTangent = -EndDir * TangentMult;

	return bResult;
}

void UMetaLandRoadSplineComponent::UpdateAutoTangents(int32 EditingPointIndex)
{
	if (GetNumberOfSplineSegments() <= 0)
	{
		return;
	}

	FixUpSegments();

	for (int32 PointIndex = 0; PointIndex < GetNumberOfSplineSegments(); ++PointIndex)
	{
		if (PointTypes.IsValidIndex(PointIndex) && PointTypes[PointIndex] == EMetaLandRoadSplinePointTypeOverride::Arc)
		{
			SplineCurves.Position.Points[PointIndex].InterpMode = CIM_CurveUser;
			const int32 NextPointIndex = GetNextPoint(PointIndex);
			if (NextPointIndex >= 0)
			{
				SplineCurves.Position.Points[NextPointIndex].InterpMode = CIM_CurveUser;
			}
		}
	}

	if (EditingPointIndex == INDEX_NONE)
	{
		for (int32 PointIndex = 0; PointIndex < GetNumberOfSplineSegments(); ++PointIndex)
		{
			if (GetRoadSplinePointType(PointIndex) == EMetaLandRoadSplinePointType::Arc)
			{
				const int32 NextPointIndex = GetNextPoint(PointIndex);
				const int32 PrePointIndex = GetPrevPoint(PointIndex);

				const bool bPinStartTangent = PrePointIndex >= 0
					&& GetRoadSplinePointType(PrePointIndex) == EMetaLandRoadSplinePointType::Linear;
				const bool bPinEndTangent = NextPointIndex >= 0
					&& GetRoadSplinePointType(NextPointIndex) == EMetaLandRoadSplinePointType::Linear;

				if (bPinStartTangent && bPinEndTangent)
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
				else if (bPinStartTangent)
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
				else if (bPinEndTangent)
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjStartTangent);
				}
				else
				{
					AdjustArcSegment(PointIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
			}
		}
	}
	else
	{
		int32 NumSegments = IsClosedLoop() ? GetNumberOfSplineSegments() - 1 : GetNumberOfSplineSegments() - EditingPointIndex;
		for (int32 i = 0; i < NumSegments; ++i)
		{
			const int32 SegmentIndex = (i + EditingPointIndex) % GetNumberOfSplineSegments();
			if (GetRoadSplinePointType(SegmentIndex) == EMetaLandRoadSplinePointType::Arc)
			{
				const int32 NextSegment = GetNextPoint(SegmentIndex);
				const int32 PreSegment = GetPrevPoint(SegmentIndex);

				const bool bPinStartTangent = PreSegment >= 0
					&& GetRoadSplinePointType(PreSegment) == EMetaLandRoadSplinePointType::Linear;
				const bool bPinEndTangent = NextSegment >= 0
					&& GetRoadSplinePointType(NextSegment) == EMetaLandRoadSplinePointType::Linear;

				if (bPinStartTangent && bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndPos);
					break;
				}
				else if (bPinStartTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
				else if (bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndPos);
					break;
				}
				else
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjEndTangent);
				}
			}
			else
			{
				break;
			}
		}

		NumSegments = IsClosedLoop() ? GetNumberOfSplineSegments() - 1 : EditingPointIndex;
		for (int32 i = 0; i < NumSegments; ++i)
		{
			int32 SegmentIndex = EditingPointIndex - 1 - i;
			if (SegmentIndex < 0)
			{
				SegmentIndex += GetNumberOfSplineSegments();
			}

			if (GetRoadSplinePointType(SegmentIndex) == EMetaLandRoadSplinePointType::Arc)
			{
				const int32 NextSegment = GetNextPoint(SegmentIndex);
				const int32 PreSegment = GetPrevPoint(SegmentIndex);

				const bool bPinStartTangent = PreSegment >= 0
					&& GetRoadSplinePointType(PreSegment) == EMetaLandRoadSplinePointType::Linear;
				const bool bPinEndTangent = NextSegment >= 0
					&& GetRoadSplinePointType(NextSegment) == EMetaLandRoadSplinePointType::Linear;

				if (bPinStartTangent && bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartPos);
					break;
				}
				else if (bPinStartTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartPos);
					break;
				}
				else if (bPinEndTangent)
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartTangent);
				}
				else
				{
					AdjustArcSegment(SegmentIndex, EMetaLandRoadComputeArcMode::AdjStartTangent);
				}
			}
			else
			{
				break;
			}
		}
	}
}

void UMetaLandRoadSplineComponent::OnRegister()
{
	Super::OnRegister();
	if (GetLaneSectionsNum() > 0)
	{
		UpdateRoadLayout();
	}
	else
	{
		UpdateLaneSectionBounds();
	}
	RefreshLaneVisualization();
}

void UMetaLandRoadSplineComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatch = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		{
			LineBatch->ClearBatch(static_cast<uint32>(GetUniqueID()));
		}
	}

	Super::OnUnregister();
}

void UMetaLandRoadSplineComponent::PostLoad()
{
	Super::PostLoad();
	FixUpSegments();
	if (GetLaneSectionsNum() > 0)
	{
		UpdateRoadLayout();
	}
	else
	{
		UpdateLaneSectionBounds();
	}
	RefreshLaneVisualization();
}

void UMetaLandRoadSplineComponent::UpdateSpline()
{
	EnsureAllCurvePointTypesForSmoothCorners(false);
	FixUpSegments();
	UpdateAutoTangents();
	Super::UpdateSpline();
	UpdateLaneSectionBounds();
	if (const UMoKuEditSplinesComponent* MoKu = Cast<UMoKuEditSplinesComponent>(this);
		MoKu && MoKu->bDeferRoadMeshRefresh)
	{
		return;
	}
	RefreshLaneVisualization();
#if WITH_EDITOR
	// Spline changed while editing — redraw ComponentVisualizer PDI overlay.
	RedrawNativeEditViewport(this);
#endif
}

void UMetaLandRoadSplineComponent::UpdateSplineKeepTangents(bool bUpdateSectionBounds)
{
	FixUpSegments();
	USplineComponent::UpdateSpline();
	if (bUpdateSectionBounds)
	{
		UpdateLaneSectionBounds();
	}
}

bool UMetaLandRoadSplineComponent::ApplyAngleAwareCornerTangent(
	int32 PointIndex,
	bool bUpdateSplineAfter,
	int32* OutNetPointsAdded,
	bool bAllowInsert)
{
	if (OutNetPointsAdded)
	{
		*OutNetPointsAdded = 0;
	}

	const int32 NumPts = GetNumberOfSplinePoints();
	if (PointIndex <= 0 || PointIndex >= NumPts - 1)
	{
		return false;
	}

	const FVector Prev = GetLocationAtSplinePoint(PointIndex - 1, ESplineCoordinateSpace::World);
	const FVector Cur = GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
	const FVector Next = GetLocationAtSplinePoint(PointIndex + 1, ESplineCoordinateSpace::World);
	const FVector InDir = (Cur - Prev).GetSafeNormal2D();
	const FVector OutDir = (Next - Cur).GetSafeNormal2D();
	if (InDir.IsNearlyZero() || OutDir.IsNearlyZero())
	{
		return false;
	}

	const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(InDir, OutDir), -1.0, 1.0));
	if (Angle < MetaLandRoad::CornerFilletSkipAngleRad)
	{
		if (GetSplinePointType(PointIndex) == ESplinePointType::Linear
			|| GetSplinePointType(PointIndex) == ESplinePointType::Constant)
		{
			SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
			if (bUpdateSplineAfter)
			{
				UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
			}
			return true;
		}
		return false;
	}
	if (Angle > FMath::DegreesToRadians(170.0))
	{
		return false;
	}

	const double KeyS = GetDistanceAlongSplineAtSplinePoint(PointIndex);
	if (IsProtectedMouthStation(this, KeyS)
		|| !IsSplinePointOnDisplaySpan(PointIndex)
		|| !IsSplinePointOnDisplaySpan(PointIndex - 1)
		|| !IsSplinePointOnDisplaySpan(PointIndex + 1))
	{
		return false;
	}

	(void)bAllowInsert;
	if (GetSplinePointType(PointIndex) != ESplinePointType::Curve)
	{
		SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
	}
	if (bUpdateSplineAfter)
	{
		UpdateSplineKeepTangents(/*bUpdateSectionBounds*/ false);
	}
	return true;
}

float UMetaLandRoadSplineComponent::GetSplinePointInputKey(int32 PointIndex) const
{
	if (!SplineCurves.Position.Points.IsValidIndex(PointIndex))
	{
		return 0.0f;
	}
	return SplineCurves.Position.Points[PointIndex].InVal;
}

bool UMetaLandRoadSplineComponent::IsSplinePointOnDisplaySpan(int32 PointIndex, double TolCm) const
{
	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(GetOwner());
	if (!Road || !SplineCurves.Position.Points.IsValidIndex(PointIndex))
	{
		return true;
	}
	return Road->ContainsJunctionMeshStation(
		GetDistanceAlongSplineAtSplinePoint(PointIndex),
		FMath::Max(TolCm, 1.0));
}

bool UMetaLandRoadSplineComponent::IsSplineSegmentOnDisplaySpan(int32 SegmentIndex, double TolCm) const
{
	const int32 NumPts = GetNumberOfSplinePoints();
	const int32 I1 = IsClosedLoop() ? ((SegmentIndex + 1) % NumPts) : (SegmentIndex + 1);
	return IsSplinePointOnDisplaySpan(SegmentIndex, TolCm)
		&& IsSplinePointOnDisplaySpan(I1, TolCm);
}

void UMetaLandRoadSplineComponent::BuildCenterLineWorldPolyline(
	TArray<FVector>& OutPoints,
	int32 StepsPerSegment) const
{
	OutPoints.Reset();
	const int32 NumPts = GetNumberOfSplinePoints();
	const int32 NumSeg = GetNumberOfSplineSegments();
	if (NumPts < 2 || NumSeg < 1)
	{
		return;
	}

	for (int32 Seg = 0; Seg < NumSeg; ++Seg)
	{
		const int32 I0 = Seg;
		const int32 I1 = IsClosedLoop() ? ((Seg + 1) % NumPts) : (Seg + 1);
		if (!SplineCurves.Position.Points.IsValidIndex(I0)
			|| !SplineCurves.Position.Points.IsValidIndex(I1)
			|| !IsSplineSegmentOnDisplaySpan(Seg))
		{
			continue;
		}

		const bool bLinearSeg =
			GetSplinePointType(I0) == ESplinePointType::Linear
			&& GetSplinePointType(I1) == ESplinePointType::Linear;
		const int32 Steps = bLinearSeg ? 1 : FMath::Clamp(StepsPerSegment, 4, 64);
		const float Key0 = GetSplinePointInputKey(I0);
		const float Key1 = IsClosedLoop() && I1 == 0
			? GetSplinePointInputKey(I0) + 1.0f
			: GetSplinePointInputKey(I1);
		const bool bContinueChain = OutPoints.Num() > 0
			&& Seg > 0
			&& IsSplineSegmentOnDisplaySpan(Seg - 1);
		const int32 StartStep = bContinueChain ? 1 : 0;
		for (int32 Step = StartStep; Step <= Steps; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
			const float Key = FMath::Lerp(Key0, Key1, Alpha);
			if (Step == 0)
			{
				OutPoints.Add(GetLocationAtSplinePoint(I0, ESplineCoordinateSpace::World));
			}
			else if (Step == Steps)
			{
				OutPoints.Add(GetLocationAtSplinePoint(I1, ESplineCoordinateSpace::World));
			}
			else
			{
				OutPoints.Add(GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World));
			}
		}
	}
}

float UMetaLandRoadSplineComponent::GetMainCenterLineHeightOffset() const
{
	return 2.0f;
}

void UMetaLandRoadSplineComponent::DrawMainCenterLinePDI(
	FPrimitiveDrawInterface* PDI,
	const FLinearColor& Color,
	float Thickness) const
{
	if (!PDI || GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	const FVector Raise(0.0f, 0.0f, GetMainCenterLineHeightOffset());
	const int32 NumPts = GetNumberOfSplinePoints();
	const int32 NumSeg = GetNumberOfSplineSegments();
	for (int32 Seg = 0; Seg < NumSeg; ++Seg)
	{
		if (!IsSplineSegmentOnDisplaySpan(Seg))
		{
			continue;
		}
		const int32 I0 = Seg;
		const int32 I1 = IsClosedLoop() ? ((Seg + 1) % NumPts) : (Seg + 1);
		const bool bLinearSeg =
			GetSplinePointType(I0) == ESplinePointType::Linear
			&& GetSplinePointType(I1) == ESplinePointType::Linear;
		const int32 Steps = bLinearSeg ? 1 : FMath::Clamp(32, 4, 64);
		const float Key0 = GetSplinePointInputKey(I0);
		const float Key1 = IsClosedLoop() && I1 == 0
			? GetSplinePointInputKey(I0) + 1.0f
			: GetSplinePointInputKey(I1);
		FVector Prev = GetLocationAtSplinePoint(I0, ESplineCoordinateSpace::World) + Raise;
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
			const FVector Cur = ((Step == Steps)
				? GetLocationAtSplinePoint(I1, ESplineCoordinateSpace::World)
				: GetLocationAtSplineInputKey(
					FMath::Lerp(Key0, Key1, Alpha), ESplineCoordinateSpace::World))
				+ Raise;
			PDI->DrawLine(Prev, Cur, Color, SDPG_Foreground, Thickness, 0.0f, true);
			Prev = Cur;
		}
	}
}

void UMetaLandRoadSplineComponent::UpdateSpline(int32 EditingPointIndex)
{
	EnsureAllCurvePointTypesForSmoothCorners(false);
	FixUpSegments();
	UpdateAutoTangents(EditingPointIndex);
	Super::UpdateSpline();
	UpdateLaneSectionBounds();
	if (const UMoKuEditSplinesComponent* MoKu = Cast<UMoKuEditSplinesComponent>(this);
		MoKu && MoKu->bDeferRoadMeshRefresh)
	{
		return;
	}
	RefreshLaneVisualization();
#if WITH_EDITOR
	RedrawNativeEditViewport(this);
#endif
}

void UMetaLandRoadSplineComponent::UpdateRoadLayout()
{
	RoadLayout.UpdateLayout(this);
	UpdateLaneSectionBounds();
	RefreshLaneVisualization();
}

void UMetaLandRoadSplineComponent::UpdateLaneSectionBounds()
{
	RoadLayout.UpdateBounds(GetSplineLength());
}

void UMetaLandRoadSplineComponent::TrimLaneSections(double Tolerance)
{
	RoadLayout.TrimSections(GetSplineLength(), Tolerance, this);
	RoadLayout.UpdateLayout(this);
}

void UMetaLandRoadSplineComponent::RefreshLaneVisualization()
{
#if WITH_EDITOR
	if (FMetaLandRoadEditorHooks::RefreshLaneVisualization)
	{
		FMetaLandRoadEditorHooks::RefreshLaneVisualization(this);
	}
#endif
}

#if WITH_EDITOR
void UMetaLandRoadSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, RoadLayout)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, bShowLaneVisualization)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, LaneVisualizationSampleStep)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, LaneVisualizationHeightOffset))
	{
		UpdateRoadLayout();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, AsphaltUvTileCm))
	{
		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(GetOwner()))
		{
			RoadActor->RefreshDynamicRoadMesh();
		}
	}
}
#endif

const FMetaLandRoadLane* UMetaLandRoadSplineComponent::GetRoadLane(int SectionIndex, int LaneIndex) const
{
	if (SectionIndex >= 0 && SectionIndex < GetLaneSectionsNum())
	{
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		if (LaneIndex > 0 && LaneIndex <= Section.Right.Num())
		{
			return &Section.Right[LaneIndex - 1];
		}
		if (LaneIndex < 0 && -LaneIndex <= Section.Left.Num())
		{
			return &Section.Left[-LaneIndex - 1];
		}
	}
	return nullptr;
}

FMetaLandRoadLane* UMetaLandRoadSplineComponent::GetRoadLane(int SectionIndex, int LaneIndex)
{
	return const_cast<FMetaLandRoadLane*>(static_cast<const UMetaLandRoadSplineComponent*>(this)->GetRoadLane(SectionIndex, LaneIndex));
}

bool UMetaLandRoadSplineComponent::GetRoadEdgePointsAtS(
	double S,
	FVector& OutLeft,
	FVector& OutRight,
	FVector& OutCenter,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	if (GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const int32 SectionIndex = RoadLayout.FindSectionAtSOffset(S);
	if (SectionIndex == INDEX_NONE)
	{
		return false;
	}

	OutCenter = EvalLanePosition(SectionIndex, LANE_INDEX_NONE, S, 0.0, CoordinateSpace);
	const float Key = GetInputKeyValueAtDistanceAlongSpline(S);
	const FVector RightVector = FlattenHorizontalRight(GetRightVectorAtSplineInputKey(Key, CoordinateSpace));
	const double FallbackHalfWidth = MetaLandRoad::DefaultRoadLaneWidth * 2.0;

	// Always resolve Left AND Right faces via FindSideSection. Dual-lane L|R markers
	// share the same SOffset; FindSectionAtSOffset may land on only-Left or only-Right —
	// collapsing the missing face to Center was poisoning junction mouths (EdgeR==Center).
	OutLeft = OutCenter - RightVector * FallbackHalfWidth;
	OutRight = OutCenter + RightVector * FallbackHalfWidth;

	const int32 LeftSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
	const int32 RightSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);

	bool bHaveLeft = false;
	if (LeftSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& LeftSection = GetLaneSection(LeftSectionIndex);
		if (!LeftSection.Left.IsEmpty())
		{
			OutLeft = EvalLanePosition(
				LeftSectionIndex, -LeftSection.Left.Num(), S, 1.0, CoordinateSpace);
			bHaveLeft = true;
		}
	}

	bool bHaveRight = false;
	if (RightSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& RightSection = GetLaneSection(RightSectionIndex);
		if (!RightSection.Right.IsEmpty())
		{
			OutRight = EvalLanePosition(
				RightSectionIndex, RightSection.Right.Num(), S, 1.0, CoordinateSpace);
			bHaveRight = true;
		}
	}

	// One-sided bundle (e.g. 2×Left centered via ROffset): mesh spans outermost α=1
	// to innermost α=0 — NOT Center. Collapsing the missing face to Center halved mouth
	// width and laterally misaligned Fig1 cover vs road body.
	if (!bHaveLeft && bHaveRight)
	{
		OutLeft = EvalLanePosition(RightSectionIndex, 1, S, 0.0, CoordinateSpace);
	}
	if (!bHaveRight && bHaveLeft)
	{
		OutRight = EvalLanePosition(LeftSectionIndex, -1, S, 0.0, CoordinateSpace);
	}

	return bHaveLeft || bHaveRight;
}

bool UMetaLandRoadSplineComponent::GetRoadCrossSectionPointsAtS(
	double S,
	TArray<FVector>& OutPoints,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	OutPoints.Reset();
	if (GetLaneSectionsNum() == 0)
	{
		return false;
	}

	const int32 HostSectionIndex = RoadLayout.FindSectionAtSOffset(S);
	if (HostSectionIndex == INDEX_NONE)
	{
		return false;
	}

	auto AppendUnique = [&OutPoints](const FVector& Point)
	{
		if (OutPoints.Num() > 0 && FVector::DistSquared(OutPoints.Last(), Point) < 1.0)
		{
			return;
		}
		OutPoints.Add(Point);
	};

	auto AppendSideLanes = [this, S, CoordinateSpace, &AppendUnique](int32 SectionIndex, bool bLeftSide)
	{
		if (SectionIndex == INDEX_NONE)
		{
			return;
		}
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		if (bLeftSide)
		{
			for (int32 LaneCount = Section.Left.Num(); LaneCount >= 1; --LaneCount)
			{
				AppendUnique(EvalLanePosition(SectionIndex, -LaneCount, S, 1.0, CoordinateSpace));
			}
		}
		else
		{
			for (int32 LaneCount = 1; LaneCount <= Section.Right.Num(); ++LaneCount)
			{
				AppendUnique(EvalLanePosition(SectionIndex, LaneCount, S, 1.0, CoordinateSpace));
			}
		}
	};

	const FMetaLandRoadLaneSection& HostSection = GetLaneSection(HostSectionIndex);
	const int32 LeftSectionIndex = RoadLayout.FindSideSection(HostSectionIndex, EMetaLandRoadLaneSectionSide::Left);
	const int32 RightSectionIndex = RoadLayout.FindSideSection(HostSectionIndex, EMetaLandRoadLaneSectionSide::Right);
	const bool bHaveLeftLanes = LeftSectionIndex != INDEX_NONE
		&& !GetLaneSection(LeftSectionIndex).Left.IsEmpty();
	const bool bHaveRightLanes = RightSectionIndex != INDEX_NONE
		&& !GetLaneSection(RightSectionIndex).Right.IsEmpty();

	// Mouth CS = section Left/Right lane edges only (α=1 outer → α=0 inner).
	// Never insert Center between faces — Center as mid made GetEdgeLanePair treat
	// EdgeC as lane-inner → red-V / fan strips / stripW=half-road (section L/R mix-up).
	if (bHaveLeftLanes)
	{
		AppendSideLanes(LeftSectionIndex, true);
		AppendUnique(EvalLanePosition(LeftSectionIndex, -1, S, 0.0, CoordinateSpace));
	}
	if (bHaveRightLanes)
	{
		AppendUnique(EvalLanePosition(RightSectionIndex, 1, S, 0.0, CoordinateSpace));
		AppendSideLanes(RightSectionIndex, false);
	}
	(void)HostSection;

	if (OutPoints.Num() < 2)
	{
		FVector Left;
		FVector Right;
		FVector Center;
		if (!GetRoadEdgePointsAtS(S, Left, Right, Center, CoordinateSpace))
		{
			return false;
		}
		OutPoints.Reset();
		OutPoints.Add(Left);
		// Do not insert Center between L/R — one-sided mouths already use α=0 as the
		// opposite edge; L-C-R here reintroduced the red-V notch on junction covers.
		OutPoints.Add(Right);
		(void)Center;
	}

	return OutPoints.Num() >= 2;
}

bool UMetaLandRoadSplineComponent::GetRoadCrossSectionPointsForLaneIndicesAtS(
	double S,
	const TArray<int32>& LaneIndices,
	TArray<FVector>& OutPoints,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	OutPoints.Reset();
	if (LaneIndices.Num() == 0)
	{
		return GetRoadCrossSectionPointsAtS(S, OutPoints, CoordinateSpace);
	}

	TArray<int32> SortedLanes;
	SortedLanes.Reserve(LaneIndices.Num());
	for (const int32 LaneIndex : LaneIndices)
	{
		if (LaneIndex != LANE_INDEX_NONE)
		{
			SortedLanes.AddUnique(LaneIndex);
		}
	}
	if (SortedLanes.Num() == 0)
	{
		return GetRoadCrossSectionPointsAtS(S, OutPoints, CoordinateSpace);
	}
	SortedLanes.Sort();

	const int32 HostSectionIndex = RoadLayout.FindSectionAtSOffset(S);
	if (HostSectionIndex == INDEX_NONE)
	{
		return false;
	}

	auto AppendUnique = [&OutPoints](const FVector& Point)
	{
		if (OutPoints.Num() > 0 && FVector::DistSquared(OutPoints.Last(), Point) < 1.0)
		{
			return;
		}
		OutPoints.Add(Point);
	};

	auto ResolveSectionForLane = [this, HostSectionIndex](int32 LaneIndex) -> int32
	{
		const int32 Resolved = MetaLandRoad::ResolveLaneSectionIndexForSpan(
			this, HostSectionIndex, LaneIndex);
		return Resolved;
	};

	TArray<int32> LeftLanes;
	TArray<int32> RightLanes;
	for (const int32 LaneIndex : SortedLanes)
	{
		if (LaneIndex < 0)
		{
			LeftLanes.Add(LaneIndex);
		}
		else
		{
			RightLanes.Add(LaneIndex);
		}
	}

	// Left outer → inner (most-negative first), matching full-CS order.
	for (int32 Index = 0; Index < LeftLanes.Num(); ++Index)
	{
		const int32 LaneIndex = LeftLanes[Index];
		const int32 SectionIndex = ResolveSectionForLane(LaneIndex);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}
		AppendUnique(EvalLanePosition(SectionIndex, LaneIndex, S, 1.0, CoordinateSpace));
	}

	if (LeftLanes.Num() > 0 && RightLanes.Num() > 0)
	{
		// Face inners (alpha0), not Center — same section Left/Right rule as full CS.
		const int32 LeftInnerLane = LeftLanes.Last();
		const int32 LeftInnerSec = ResolveSectionForLane(LeftInnerLane);
		if (LeftInnerSec != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(LeftInnerSec, LeftInnerLane, S, 0.0, CoordinateSpace));
		}
		const int32 RightInnerLane = RightLanes[0];
		const int32 RightInnerSec = ResolveSectionForLane(RightInnerLane);
		if (RightInnerSec != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(RightInnerSec, RightInnerLane, S, 0.0, CoordinateSpace));
		}
	}
	else if (LeftLanes.Num() > 0)
	{
		const int32 InnerLane = LeftLanes.Last();
		const int32 SectionIndex = ResolveSectionForLane(InnerLane);
		if (SectionIndex != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(SectionIndex, InnerLane, S, 0.0, CoordinateSpace));
		}
	}
	else if (RightLanes.Num() > 0)
	{
		const int32 InnerLane = RightLanes[0];
		const int32 SectionIndex = ResolveSectionForLane(InnerLane);
		if (SectionIndex != INDEX_NONE)
		{
			AppendUnique(EvalLanePosition(SectionIndex, InnerLane, S, 0.0, CoordinateSpace));
		}
	}

	for (const int32 LaneIndex : RightLanes)
	{
		const int32 SectionIndex = ResolveSectionForLane(LaneIndex);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}
		AppendUnique(EvalLanePosition(SectionIndex, LaneIndex, S, 1.0, CoordinateSpace));
	}

	if (OutPoints.Num() < 2)
	{
		return GetRoadCrossSectionPointsAtS(S, OutPoints, CoordinateSpace);
	}
	return true;
}

double UMetaLandRoadSplineComponent::GetRoadHalfWidthForLaneIndicesAtS(
	double S,
	const TArray<int32>& LaneIndices) const
{
	if (LaneIndices.Num() == 0)
	{
		return GetRoadHalfWidthAtS(S);
	}

	TArray<FVector> MouthCS;
	if (!GetRoadCrossSectionPointsForLaneIndicesAtS(S, LaneIndices, MouthCS, ESplineCoordinateSpace::World)
		|| MouthCS.Num() < 2)
	{
		return GetRoadHalfWidthAtS(S);
	}

	// Mouth half-width = half of subset edge span. Dist-to-centerline overstates off-center
	// bundles and understates centered ones used for junction indent / custom lane mouths.
	return FMath::Max(FVector::Dist2D(MouthCS[0], MouthCS.Last()) * 0.5, 1.0);
}

double UMetaLandRoadSplineComponent::GetRoadHalfWidthAtS(double S) const
{
	FVector Left;
	FVector Right;
	FVector Center;
	if (!GetRoadEdgePointsAtS(S, Left, Right, Center, ESplineCoordinateSpace::World))
	{
		return MetaLandRoad::DefaultRoadLaneWidth;
	}

	return FMath::Max(FVector::Dist2D(Left, Center), FVector::Dist2D(Right, Center));
}

bool UMetaLandRoadSplineComponent::GenerateCombinedLaneDynamicMesh(
	FDynamicMesh3& OutMesh,
	float RoadHeightOffset,
	bool bGenerateUVs)
{
	using namespace MetaLandRoadLaneMeshPrivate;

	if (GetLaneSectionsNum() == 0)
	{
		return false;
	}

	if (bGenerateUVs)
	{
		OutMesh.EnableVertexUVs(FVector2f::Zero());
		OutMesh.EnableAttributes();
	}

	const double SplineLength = GetSplineLength();
	if (SplineLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TArray<TPair<double, double>> MeshSpans;
	if (const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(GetOwner()))
	{
		RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
	}
	// Stamp already falls back to full length when MinKeep drops every span.
	// Overlay also draws [0, Length]. Empty spans here left lane lines + terrain, no mesh.
	if (MeshSpans.Num() == 0)
	{
		MeshSpans.Emplace(0.0, SplineLength);
	}

	if (MeshSpans.Num() > 1)
	{
		TArray<TPair<double, double>> MergedSpans;
		MergedSpans.Add(MeshSpans[0]);
		for (int32 SpanIdx = 1; SpanIdx < MeshSpans.Num(); ++SpanIdx)
		{
			if (MeshSpans[SpanIdx].Key <= MergedSpans.Last().Value + 1.0)
			{
				MergedSpans.Last().Value = FMath::Max(MergedSpans.Last().Value, MeshSpans[SpanIdx].Value);
			}
			else
			{
				MergedSpans.Add(MeshSpans[SpanIdx]);
			}
		}
		MeshSpans = MoveTemp(MergedSpans);
	}

	/**
	 * Face loft breaks when this face's lane count changes (appear/disappear or
	 * Both→Both add/remove). Empty opposite face still resolves via FindSideSection
	 * after the break — adsorb / mouth CS are unchanged.
	 * Zero-length L|R markers share S with the real span; they are not break hosts.
	 */
	auto FaceLaneCount = [](const FMetaLandRoadLaneSection& Section, bool bLeftFace) -> int32
	{
		if (bLeftFace)
		{
			return MetaLandRoad::HasLeftSide(Section.Side) ? Section.Left.Num() : 0;
		}
		return MetaLandRoad::HasRightSide(Section.Side) ? Section.Right.Num() : 0;
	};

	auto CollectSideBreaks = [this, &FaceLaneCount](bool bLeftFace, double MeshSMin, double MeshSMax) -> TArray<double>
	{
		TArray<double> Breaks;
		Breaks.Add(MeshSMin);
		bool bHavePrevCount = false;
		int32 PrevCount = 0;
		for (int32 SectionIndex = 0; SectionIndex < GetLaneSectionsNum(); ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
			if (Section.SOffsetEnd_Cached <= Section.SOffset + KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const int32 Count = FaceLaneCount(Section, bLeftFace);
			if (bHavePrevCount
				&& Count != PrevCount
				&& Section.SOffset > MeshSMin + KINDA_SMALL_NUMBER
				&& Section.SOffset < MeshSMax - KINDA_SMALL_NUMBER)
			{
				Breaks.Add(Section.SOffset);
			}
			PrevCount = Count;
			bHavePrevCount = true;
		}
		Breaks.Add(MeshSMax);
		Breaks.Sort();

		TArray<double> Unique;
		Unique.Reserve(Breaks.Num());
		for (double S : Breaks)
		{
			if (Unique.Num() == 0 || !FMath::IsNearlyEqual(Unique.Last(), S, 1.0))
			{
				Unique.Add(S);
			}
		}
		return Unique;
	};

	auto ResolveLaneSectionForFace = [this](double SMid, bool bLeftFace) -> int32
	{
		int32 HostSection = RoadLayout.FindSectionAtSOffset(SMid);
		if (HostSection == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		return RoadLayout.FindSideSection(
			HostSection,
			bLeftFace ? EMetaLandRoadLaneSectionSide::Left : EMetaLandRoadLaneSectionSide::Right);
	};

	for (const TPair<double, double>& Span : MeshSpans)
	{
		const double MeshSMin = Span.Key;
		const double MeshSMax = Span.Value;
		if ((MeshSMax - MeshSMin) < MetaLandRoad::MinKeepRoadSpanCm)
		{
			continue;
		}

		auto BuildFaceSpans = [&](bool bLeftFace, int32 LaneSign)
		{
			const TArray<double> Breaks = CollectSideBreaks(bLeftFace, MeshSMin, MeshSMax);

			for (int32 BreakIdx = 0; BreakIdx + 1 < Breaks.Num(); ++BreakIdx)
			{
				const double S0 = Breaks[BreakIdx];
				const double S1 = Breaks[BreakIdx + 1];
				if ((S1 - S0) < KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 LaneSectionIndex = ResolveLaneSectionForFace(0.5 * (S0 + S1), bLeftFace);
				if (LaneSectionIndex == INDEX_NONE)
				{
					continue;
				}

				const int32 SectionMinSteps = ComputeScaledMinStepsForSpan(S0, S1, SplineLength, 4);
				TArray<FMetaLandRoadSplinePositionLinearApproximation> UnusedPrev;
				AppendLanesForSide(
					this,
					LaneSectionIndex,
					INDEX_NONE,
					true,
					true,
					UnusedPrev,
					S0,
					S1,
					SectionMinSteps,
					RoadHeightOffset,
					bGenerateUVs,
					OutMesh,
					LaneSign);
			}
		};

		BuildFaceSpans(/*bLeftFace=*/false, /*LaneSign=*/+1);
		BuildFaceSpans(/*bLeftFace=*/true, /*LaneSign=*/-1);
	}

	if (OutMesh.TriangleCount() > 0)
	{
		if (AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(GetOwner()))
		{
			if (UDynamicMeshComponent* MeshComp = RoadActor->DynamicMeshComponent)
			{
				MeshComp->SetRelativeLocation(FVector::ZeroVector);
				MeshComp->SetRelativeRotation(FRotator::ZeroRotator);
				const FTransform SplineToMesh = MeshComp->GetComponentTransform().Inverse() * GetComponentTransform();
				if (!SplineToMesh.Equals(FTransform::Identity, 0.05))
				{
					for (const int32 Vid : OutMesh.VertexIndicesItr())
					{
						OutMesh.SetVertex(Vid, SplineToMesh.TransformPosition(OutMesh.GetVertex(Vid)));
					}
				}
			}
		}
	}

	return OutMesh.TriangleCount() > 0;
}


namespace MetaLandRoadSplinePrivate
{
	static void ShiftWidthCurve(FRichCurve& Width, double SDelta)
	{
		for (FRichCurveKey& Key : Width.Keys)
		{
			Key.Time += SDelta;
		}
	}

	static void ShiftLaneAttribute(FMetaLandRoadLaneAttribute& Attribute, double SDelta)
	{
		for (FMetaLandRoadLaneAttributeKey& Key : Attribute.Keys)
		{
			Key.SOffset += SDelta;
		}
	}

	static void ShiftLane(FMetaLandRoadLane& OldLane, FMetaLandRoadLane& NewLane, double SDelta)
	{
		ShiftWidthCurve(NewLane.Width, SDelta);
		for (TPair<FName, FMetaLandRoadLaneAttribute>& AttributePair : NewLane.Attributes)
		{
			ShiftLaneAttribute(AttributePair.Value, SDelta);
		}

		if (OldLane.IsForwardLane())
		{
			NewLane.SuccessorConnection = OldLane.SuccessorConnection;
			OldLane.SuccessorConnection = nullptr;
			NewLane.PredecessorConnection = nullptr;
		}
		else
		{
			NewLane.PredecessorConnection = OldLane.PredecessorConnection;
			OldLane.PredecessorConnection = nullptr;
			NewLane.SuccessorConnection = nullptr;
		}
	}
}

int32 UMetaLandRoadSplineComponent::FindRoadSectionOnSplineKey(float SplineKey) const
{
	for (int32 SectionIndex = 0; SectionIndex < GetLaneSectionsNum(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		const float StartKey = GetInputKeyValueAtDistanceAlongSpline(Section.SOffset);
		const float EndKey = GetInputKeyValueAtDistanceAlongSpline(Section.SOffsetEnd_Cached);
		if (SplineKey >= StartKey && SplineKey <= EndKey)
		{
			return SectionIndex;
		}
	}

	return INDEX_NONE;
}

int32 UMetaLandRoadSplineComponent::SplitSectionAtDistanceAlongSpline(double DistanceAlongSpline, EMetaLandRoadLaneSectionSide Side)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	return SplitSection(SplineKey, Side);
}

int32 UMetaLandRoadSplineComponent::SplitSectionAtSplinePoint(int32 PointIndex, EMetaLandRoadLaneSectionSide Side)
{
	if (!GetSplinePointsPosition().Points.IsValidIndex(PointIndex))
	{
		return INDEX_NONE;
	}

	return SplitSection(static_cast<float>(PointIndex), Side);
}

int32 UMetaLandRoadSplineComponent::EditorAddLeftSection(float SplineKey)
{
	return SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Left);
}

int32 UMetaLandRoadSplineComponent::EditorAddRightSection(float SplineKey)
{
	return SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Right);
}

int32 UMetaLandRoadSplineComponent::EditorAddLeftSectionAtDistanceAlongSpline(double DistanceAlongSpline)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	return EditorAddLeftSection(SplineKey);
}

int32 UMetaLandRoadSplineComponent::EditorAddRightSectionAtDistanceAlongSpline(double DistanceAlongSpline)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	return EditorAddRightSection(SplineKey);
}

// =============================================================================
// [KEY-ALGO] SplitSection
// Alt+E: Add Section / Add Left Section / Add Right Section 的唯一写入入口。
// Side=Left|Right 只切那一面；对面幅继续走，两端应对齐同一条样条 S。
// 依赖 EnsureBothFacesOnRoad：对面幅为空时这里只能插入空 section。
// =============================================================================
int32 UMetaLandRoadSplineComponent::SplitSection(float SplineKey, EMetaLandRoadLaneSectionSide Side)
{
	MetaLandRoadProfile::EnsureBothFacesOnRoad(this);

	const int32 SectionIndex = FindRoadSectionOnSplineKey(SplineKey);
	if (SectionIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("SplitSection: no section contains spline key %f"), SplineKey);
		return INDEX_NONE;
	}

	const double SOffset = GetDistanceAlongSplineAtSplineInputKey(SplineKey);
	const double SplineLength = GetSplineLength();
	if (SOffset <= KINDA_SMALL_NUMBER || SOffset >= SplineLength - KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("SplitSection: invalid split distance %f on spline length %f"), SOffset, SplineLength);
		return INDEX_NONE;
	}

	// Same S is OK for opposite faces (Left then Right). Reject only when the new Side
	// overlaps an existing face at that S (Both overlaps everything; Left↔Left, Right↔Right).
	auto SideOverlapsFace = [](EMetaLandRoadLaneSectionSide Existing, EMetaLandRoadLaneSectionSide Incoming)
	{
		if (Existing == EMetaLandRoadLaneSectionSide::Both
			|| Incoming == EMetaLandRoadLaneSectionSide::Both)
		{
			return true;
		}
		return Existing == Incoming;
	};
	for (const FMetaLandRoadLaneSection& ExistingSection : RoadLayout.Sections)
	{
		if (FMath::IsNearlyEqual(ExistingSection.SOffset, SOffset, KINDA_SMALL_NUMBER)
			&& SideOverlapsFace(ExistingSection.Side, Side))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SplitSection: overlapping %d face already exists at distance %f"),
				static_cast<int32>(Side),
				SOffset);
			return INDEX_NONE;
		}
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(NSLOCTEXT("MetaLandRoad", "SplitSection", "Split Lane Section"), !GIsTransacting);
	Modify();
#endif

	bAutoMaintainMidpointSectionSplit = false;

	FMetaLandRoadLaneSection& SourceSection = RoadLayout.Sections[SectionIndex];

	FMetaLandRoadLaneSection SectionToAdd;
	SectionToAdd.SOffset = SOffset;
	SectionToAdd.Side = Side;
	SectionToAdd.Attributes = SourceSection.Attributes;

	int32 LeftSectionIndex = INDEX_NONE;
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Left)
	{
		LeftSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Left);
		if (LeftSectionIndex != INDEX_NONE)
		{
			SectionToAdd.Left = RoadLayout.Sections[LeftSectionIndex].Left;
		}
	}

	int32 RightSectionIndex = INDEX_NONE;
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Right)
	{
		RightSectionIndex = RoadLayout.FindSideSection(SectionIndex, EMetaLandRoadLaneSectionSide::Right);
		if (RightSectionIndex != INDEX_NONE)
		{
			SectionToAdd.Right = RoadLayout.Sections[RightSectionIndex].Right;
		}
	}

	RoadLayout.Sections.Insert(SectionToAdd, SectionIndex + 1);
	UpdateLaneSectionBounds();
	UpdateRoadLayout();

	FMetaLandRoadLaneSection& NewSection = RoadLayout.Sections[SectionIndex + 1];

	for (int32 LaneIndex = 0; LaneIndex < NewSection.Left.Num(); ++LaneIndex)
	{
		if (LeftSectionIndex == INDEX_NONE)
		{
			break;
		}

		FMetaLandRoadLaneSection& OldSection = RoadLayout.Sections[LeftSectionIndex];
		MetaLandRoadSplinePrivate::ShiftLane(
			OldSection.Left[LaneIndex],
			NewSection.Left[LaneIndex],
			OldSection.SOffset - SOffset);
	}

	for (int32 LaneIndex = 0; LaneIndex < NewSection.Right.Num(); ++LaneIndex)
	{
		if (RightSectionIndex == INDEX_NONE)
		{
			break;
		}

		FMetaLandRoadLaneSection& OldSection = RoadLayout.Sections[RightSectionIndex];
		MetaLandRoadSplinePrivate::ShiftLane(
			OldSection.Right[LaneIndex],
			NewSection.Right[LaneIndex],
			OldSection.SOffset - SOffset);
	}

	const int32 CenterSectionIndex = FMath::Max(LeftSectionIndex, RightSectionIndex);
	if (CenterSectionIndex != INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& OldSection = RoadLayout.Sections[CenterSectionIndex];
		for (TPair<FName, FMetaLandRoadLaneAttribute>& AttributePair : NewSection.Attributes)
		{
			MetaLandRoadSplinePrivate::ShiftLaneAttribute(AttributePair.Value, OldSection.SOffset - SOffset);
		}
	}

	// Only trim the side(s) that were actually split. Full Section::Trim also rewrote the
	// continued side's Width (e.g. Right when adding Left-only) and opened mesh V-gaps.
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Left)
	{
		if (LeftSectionIndex != INDEX_NONE)
		{
			for (FMetaLandRoadLane& Lane : RoadLayout.Sections[LeftSectionIndex].Left)
			{
				Lane.Trim(true);
			}
		}
		for (FMetaLandRoadLane& Lane : NewSection.Left)
		{
			Lane.Trim(true);
		}
	}
	if (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Right)
	{
		if (RightSectionIndex != INDEX_NONE)
		{
			for (FMetaLandRoadLane& Lane : RoadLayout.Sections[RightSectionIndex].Right)
			{
				Lane.Trim(true);
			}
		}
		for (FMetaLandRoadLane& Lane : NewSection.Right)
		{
			Lane.Trim(true);
		}
	}
	UpdateRoadLayout();
	MarkRenderStateDirty();

	return SectionIndex + 1;
}

UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange UMetaLandRoadSplineComponent::GetLaneRange(int SectionIndex, int LaneIndex) const
{
	if (LaneIndex == LANE_INDEX_NONE)
	{
		const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
		FMetaLandRoadLaneRange Range;
		Range.StartS = Section.SOffset;
		Range.EndS = Section.SOffsetEnd_Cached;
		return Range;
	}

	const int32 LaneSectionIndex = MetaLandRoad::ResolveLaneSectionIndexForSpan(
		this, SectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE || !RoadLayout.Sections.IsValidIndex(LaneSectionIndex))
	{
		return FMetaLandRoadLaneRange();
	}

	const FMetaLandRoadLaneSection& LaneSection = GetLaneSection(LaneSectionIndex);
	if (!LaneSection.CheckLaneIndex(LaneIndex))
	{
		return FMetaLandRoadLaneRange();
	}

	const FMetaLandRoadLane& Lane = LaneSection.GetLaneByIndex(LaneIndex);
	FMetaLandRoadLaneRange Range;
	Range.StartS = Lane.GetStartOffset();
	Range.EndS = Lane.GetEndOffset();
	return Range;
}

bool UMetaLandRoadSplineComponent::BuildDrawAnchorForLaneMouth(
	int32 SpanSectionIndex,
	int32 LaneIndex,
	double AnchorS,
	EMetaLandRoadMouthSide MouthSide,
	FMetaLandRoadDrawAnchor& OutAnchor) const
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	if (!MetaLandRoad::IsLaneValidOnSpanSection(this, SpanSectionIndex, LaneIndex))
	{
		return false;
	}

	OutAnchor.SourceSpline = const_cast<UMetaLandRoadSplineComponent*>(this);
	OutAnchor.SectionIndex = SpanSectionIndex;
	OutAnchor.LaneIndex = LaneIndex;
	OutAnchor.AnchorS = AnchorS;
	OutAnchor.MouthSide = MouthSide;
	OutAnchor.bFromSuccessor = MouthSide == EMetaLandRoadMouthSide::End;
	OutAnchor.BundledLaneIndices.Reset();
	return OutAnchor.IsValid();
}

bool UMetaLandRoadSplineComponent::EvalMouthTransform(
	const FMetaLandRoadDrawAnchor& Anchor,
	FTransform& OutTransform) const
{
	if (!Anchor.IsValid() || Anchor.SourceSpline.Get() != this)
	{
		return false;
	}

	const int32 LaneSectionIndex = MetaLandRoad::ResolveLaneSectionIndexForSpan(
		this, Anchor.SectionIndex, Anchor.LaneIndex);
	if (LaneSectionIndex == INDEX_NONE || !GetLaneSections().IsValidIndex(LaneSectionIndex))
	{
		return false;
	}

	if (!GetLaneSection(LaneSectionIndex).CheckLaneIndex(Anchor.LaneIndex))
	{
		return false;
	}

	FMetaLandRoadDrawAnchor VisibleAnchor = Anchor;
	VisibleAnchor.AnchorS = MetaLandRoad::ClampMouthAnchorSToVisibleMesh(
		this, Anchor.AnchorS, Anchor.MouthSide);

	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector OutwardTangent;
	if (!MetaLandRoad::ComputeAnchorMouthRailFrame(
		this, VisibleAnchor, TipLeft, TipRight, TipCenter, OutwardTangent))
	{
		return false;
	}

	const FVector Tangent = OutwardTangent.IsNearlyZero()
		? FVector::ForwardVector
		: OutwardTangent.GetSafeNormal();
	const FVector UpVector = GetUpVectorAtDistanceAlongSpline(
		VisibleAnchor.AnchorS, ESplineCoordinateSpace::World).GetSafeNormal();
	OutTransform = FTransform(Tangent.ToOrientationQuat(), TipCenter);
	if (!UpVector.IsNearlyZero())
	{
		OutTransform.SetRotation(FRotationMatrix::MakeFromXZ(Tangent, UpVector).ToQuat());
	}

	return true;
}

void UMetaLandRoadSplineComponent::FitSplineToAnchor(const FMetaLandRoadDrawAnchor& Anchor, bool bAtSplineStart)
{
	UMetaLandRoadSplineComponent* SourceSpline = Anchor.SourceSpline.Get();
	if (!IsValid(SourceSpline) || !Anchor.IsValid() || GetNumberOfSplinePoints() < 1)
	{
		return;
	}

	FVector MouthLoc = FVector::ZeroVector;
	FVector MouthOutward = FVector::ForwardVector;
	FVector TipLeft;
	FVector TipRight;
	FVector TipCenter;
	FVector OutwardTangent;
	if (MetaLandRoad::ComputeAnchorMouthRailFrame(
		SourceSpline, Anchor, TipLeft, TipRight, TipCenter, OutwardTangent))
	{
		MouthLoc = TipCenter;
		MouthOutward = OutwardTangent.GetSafeNormal2D();
	}
	else
	{
		FTransform MouthTransform = FTransform::Identity;
		if (!SourceSpline->EvalMouthTransform(Anchor, MouthTransform))
		{
			return;
		}
		MouthLoc = MouthTransform.GetLocation();
		MouthOutward = MouthTransform.GetRotation().GetForwardVector().GetSafeNormal2D();
	}
	if (MouthOutward.IsNearlyZero())
	{
		MouthOutward = FVector::ForwardVector;
	}

	Modify();
	const int32 NumPts = GetNumberOfSplinePoints();
	const int32 TipIndex = bAtSplineStart ? 0 : (NumPts - 1);

	// Start-mouth freeze during continue draw only. EndConnection must always pin the tip.
	if (bAtSplineStart && NumPts > 2)
	{
		return;
	}

	const double TipS = bAtSplineStart ? 0.0 : GetSplineLength();
	// EndConnection: size tangents from TARGET mouth width (subset bundle), not the still-narrow tip.
	TArray<int32> MouthLaneIndices;
	MouthLaneIndices.Add(Anchor.LaneIndex);
	for (const int32 BundledLaneIndex : Anchor.BundledLaneIndices)
	{
		MouthLaneIndices.AddUnique(BundledLaneIndex);
	}
	const double TargetMouthHalf = SourceSpline->GetRoadHalfWidthForLaneIndicesAtS(
		Anchor.AnchorS, MouthLaneIndices);
	const double HalfWidthCm = FMath::Max3(
		GetRoadHalfWidthAtS(TipS),
		TargetMouthHalf,
		MetaLandRoad::DefaultRoadLaneWidth * 0.5);
	// Wider mouths need longer approach chord or the tip looks short / angled off the rails.
	const double MinAlongCm = FMath::Clamp(HalfWidthCm * 0.55, 80.0, 450.0);
	const FVector MouthPinned = MouthLoc;
	SetLocationAtSplinePoint(TipIndex, MouthPinned, ESplineCoordinateSpace::World, false);

	// Begin leave = MouthOutward; End arrive into mouth = -MouthOutward (MetaRoad FitSplineToEnd).
	const FVector TipDir = bAtSplineStart ? MouthOutward : (-MouthOutward);

	if (NumPts >= 2)
	{
		const int32 NeighborIndex = bAtSplineStart ? 1 : (TipIndex - 1);
		FVector NeighborLoc = GetLocationAtSplinePoint(NeighborIndex, ESplineCoordinateSpace::World);

		// Half-space nudge so tip leave/arrive is not a kink against the mouth tangent.
		// Start: neighbor must sit along +MouthOutward. End: along +MouthOutward (approach from outside).
		if (NumPts <= 2 || !bAtSplineStart)
		{
			const double DistToNeighbor = FVector::Dist2D(MouthPinned, NeighborLoc);
			const double Along = FVector::DotProduct(NeighborLoc - MouthPinned, MouthOutward);
			// Far opposite mouth of a 2-point connector — do not pull it onto this outward.
			if (DistToNeighbor <= MinAlongCm * 2.0 && Along < MinAlongCm * 0.35)
			{
				const FVector Lateral = NeighborLoc - MouthPinned - MouthOutward * Along;
				NeighborLoc = MouthPinned + MouthOutward * MinAlongCm + Lateral;
				NeighborLoc.Z = GetLocationAtSplinePoint(NeighborIndex, ESplineCoordinateSpace::World).Z;
				SetLocationAtSplinePoint(NeighborIndex, NeighborLoc, ESplineCoordinateSpace::World, false);
			}
		}

		const double ChordToNeighbor = FVector::Dist2D(MouthPinned, NeighborLoc);
		const double TangentSize = FMath::Max(ChordToNeighbor, MinAlongCm);
		SetSplinePointType(TipIndex, ESplinePointType::CurveCustomTangent, false);
		if (bAtSplineStart)
		{
			const FVector MouthArrive = GetArriveTangentAtSplinePoint(TipIndex, ESplineCoordinateSpace::World);
			SetTangentsAtSplinePoint(
				TipIndex,
				MouthArrive,
				TipDir * TangentSize,
				ESplineCoordinateSpace::World,
				false);
		}
		else
		{
			// MetaRoad FitSplineToEnd: Arrive + Leave both along connection into the mouth.
			SetTangentsAtSplinePoint(
				TipIndex,
				TipDir * TangentSize,
				TipDir * TangentSize,
				ESplineCoordinateSpace::World,
				false);
			// Long 2-point mouth-to-mouth: keep the opposite Bezier leave. Only
			// rewrite the neighbor on a short approach stub.
			if (ChordToNeighbor <= MinAlongCm * 2.0)
			{
				SetSplinePointType(NeighborIndex, ESplinePointType::CurveCustomTangent, false);
				const FVector ToTip = (MouthPinned - NeighborLoc).GetSafeNormal2D();
				if (!ToTip.IsNearlyZero())
				{
					const FVector NeighborArrive = GetArriveTangentAtSplinePoint(
						NeighborIndex, ESplineCoordinateSpace::World);
					SetTangentsAtSplinePoint(
						NeighborIndex,
						NeighborArrive.IsNearlyZero() ? (-ToTip * TangentSize) : NeighborArrive,
						ToTip * TangentSize,
						ESplineCoordinateSpace::World,
						false);
				}
			}
		}
	}
	else
	{
		SetSplinePointType(TipIndex, ESplinePointType::CurveCustomTangent, false);
		SetTangentAtSplinePoint(
			TipIndex,
			TipDir * 200.0,
			ESplineCoordinateSpace::World,
			false);
	}

	UpdateSpline();
}

// =============================================================================
// [KEY-ALGO] BlendEndWidthToTargetMouth
// 2合1：MinLane=1 → Left[0]+Right[0] 只混 width，ROffset≡0。
// 粉中线 = mesh 几何中心，头尾口对齐，不再用 EvalLaneROffset 的融合偏移。
// =============================================================================
bool UMetaLandRoadSplineComponent::BlendEndWidthToTargetMouth(
	const FMetaLandRoadDrawAnchor& TargetAnchor,
	EMetaLandRoadEndWidthBlendSpan SpanMode,
	double BlendLengthCm)
{
	UMetaLandRoadSplineComponent* TargetSpline = TargetAnchor.SourceSpline.Get();
	if (!IsValid(TargetSpline) || !TargetAnchor.IsValid() || GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	TArray<int32> TargetLaneIndices;
	TArray<float> TargetLaneWidths;
	if (!MetaLandRoadProfile::GetDrawAnchorBundleLaneWidths(
		TargetSpline, TargetAnchor, TargetLaneIndices, TargetLaneWidths))
	{
		return false;
	}

	TArray<float> TargetLeftWidths;
	TArray<float> TargetRightWidths;
	for (int32 Index = 0; Index < TargetLaneIndices.Num(); ++Index)
	{
		const float W = FMath::Max(TargetLaneWidths.IsValidIndex(Index) ? TargetLaneWidths[Index] : 1.0f, 1.0f);
		if (TargetLaneIndices[Index] > 0)
		{
			TargetRightWidths.Add(W);
		}
		else if (TargetLaneIndices[Index] < 0)
		{
			TargetLeftWidths.Add(W);
		}
	}

	TArray<float> SourceLeftWidths;
	TArray<float> SourceRightWidths;
	CollectUniqueFaceWidths(this, SourceLeftWidths, SourceRightWidths);

	const int32 SourceLaneCount = SourceLeftWidths.Num() + SourceRightWidths.Num();
	const int32 TargetLaneCount = TargetLeftWidths.Num() + TargetRightWidths.Num();

	const float SourceMouthSum = FMath::Max(
		SumFaceWidths(SourceLeftWidths) + SumFaceWidths(SourceRightWidths), 1.0f);
	const float TargetMouthWidthSum = FMath::Max(
		SumFaceWidths(TargetLeftWidths) + SumFaceWidths(TargetRightWidths), 1.0f);

	auto LogicalLaneCount = [](int32 FaceCount, float TotalW) -> int32
	{
		if (FaceCount <= 1)
		{
			return 1;
		}
		// 1-lane that EnsureBothFaces split into Left+Right halves still counts as 1.
		if (FaceCount == 2
			&& TotalW <= static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth) * 1.35f)
		{
			return 1;
		}
		return FaceCount;
	};
	const int32 MinLaneCount = FMath::Max(
		1,
		FMath::Min(
			LogicalLaneCount(SourceLaneCount, SourceMouthSum),
			LogicalLaneCount(TargetLaneCount, TargetMouthWidthSum)));

	(void)SpanMode;
	(void)BlendLengthCm;

	// Native roads may already have authored multi-section widths (thin then thick).
	// Collapsing to one Start→End blend is what turns 图二 into the 图一 blob.
	if (GetLaneSectionsNum() > 1)
	{
		return false;
	}

	if (MinLaneCount <= 1)
	{
		// 2合1 / N合1：按最小车道=1 的中线切开，左右各 1 条，只混 width。
		// 左右半宽相同 → EvalLaneROffset 的 0 就是 mesh 几何中心，不再靠 ROffset 融合。
		const float StartHalf = SourceMouthSum * 0.5f;
		const float EndHalf = TargetMouthWidthSum * 0.5f;
		ApplyMinLaneLeftRightWidthBlend(this, StartHalf, StartHalf, EndHalf, EndHalf);
	}
	else if (SourceLeftWidths.Num() > 0 && SourceRightWidths.Num() > 0)
	{
		FMetaLandRoadLayout& Layout = GetRoadLayoutMutable();
		Layout.ROffset.Reset();
		Layout.ROffset.AddKey(0.0f, 0.0f);
		UpdateLaneSectionBounds();
		UpdateRoadLayout();
	}

	UpdateLaneSectionBounds();
	UpdateSpline();
	return true;
}

void UMetaLandRoadSplineComponent::StretchTipSectionToSplineLength(bool bAttachAtSplineStart)
{
	TArray<FMetaLandRoadLaneSection>& Sections = GetLaneSectionsMutable();
	if (Sections.Num() == 0)
	{
		return;
	}

	const double Len = GetSplineLength();
	if (bAttachAtSplineStart)
	{
		Sections[0].SOffset = 0.0;
		Sections[0].SOffsetEnd_Cached = (Sections.Num() == 1) ? Len : Sections[1].SOffset;
	}
	else
	{
		if (Sections.Num() == 1)
		{
			Sections[0].SOffset = 0.0;
		}
		Sections.Last().SOffsetEnd_Cached = Len;
	}
}

void UMetaLandRoadSplineComponent::IsolateTipSectionAtMouthS(double MouthS)
{
	TArray<FMetaLandRoadLaneSection>& Sections = GetLaneSectionsMutable();
	if (Sections.Num() == 0)
	{
		return;
	}

	const double Len = GetSplineLength();
	const double CutS = FMath::Clamp(MouthS, 10.0, FMath::Max(Len - 10.0, 10.0));
	FMetaLandRoadLaneSection& Last = Sections.Last();
	if (FMath::Abs(Last.SOffset - CutS) <= 25.0)
	{
		Last.SOffsetEnd_Cached = Len;
		return;
	}

	Last.SOffsetEnd_Cached = CutS;
	FMetaLandRoadLaneSection TipSection = Last;
	TipSection.SOffset = CutS;
	TipSection.SOffsetEnd_Cached = Len;
	Sections.Add(MoveTemp(TipSection));
}

bool UMetaLandRoadSplineComponent::BlendTipSectionWidthToTargetMouth(
	const FMetaLandRoadDrawAnchor& TargetAnchor,
	bool bAttachAtSplineStart)
{
	UMetaLandRoadSplineComponent* TargetSpline = TargetAnchor.SourceSpline.Get();
	if (!IsValid(TargetSpline) || !TargetAnchor.IsValid())
	{
		return false;
	}

	TArray<int32> TargetLaneIndices;
	TArray<float> TargetLaneWidths;
	if (!MetaLandRoadProfile::GetDrawAnchorBundleLaneWidths(
		TargetSpline, TargetAnchor, TargetLaneIndices, TargetLaneWidths))
	{
		return false;
	}

	float TargetLeftSum = 0.0f;
	float TargetRightSum = 0.0f;
	for (int32 Index = 0; Index < TargetLaneIndices.Num(); ++Index)
	{
		const float W = FMath::Max(
			TargetLaneWidths.IsValidIndex(Index) ? TargetLaneWidths[Index] : 1.0f,
			1.0f);
		if (TargetLaneIndices[Index] > 0)
		{
			TargetRightSum += W;
		}
		else if (TargetLaneIndices[Index] < 0)
		{
			TargetLeftSum += W;
		}
	}
	const float TargetSum = FMath::Max(TargetLeftSum + TargetRightSum, 1.0f);

	TArray<FMetaLandRoadLaneSection>& Sections = GetLaneSectionsMutable();
	if (Sections.Num() == 0)
	{
		return false;
	}

	StretchTipSectionToSplineLength(bAttachAtSplineStart);
	FMetaLandRoadLaneSection& Section = bAttachAtSplineStart ? Sections[0] : Sections.Last();
	const float SectionLen = static_cast<float>(
		FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 1.0));
	const float TipTime = bAttachAtSplineStart ? 0.0f : SectionLen;
	const float BodyTime = bAttachAtSplineStart ? SectionLen : 0.0f;

	const float SourceLeft = FMath::Max(SumLaneWidthsAtTime(Section.Left, TipTime), 1.0f);
	const float SourceRight = FMath::Max(SumLaneWidthsAtTime(Section.Right, TipTime), 1.0f);
	const float SourceSum = SourceLeft + SourceRight;
	if (FMath::Abs(SourceSum - TargetSum) < 8.0f)
	{
		return true;
	}

	const float LeftScale = (TargetLeftSum > 1.0f && Section.Left.Num() > 0)
		? (TargetLeftSum / SourceLeft)
		: (TargetSum / SourceSum);
	const float RightScale = (TargetRightSum > 1.0f && Section.Right.Num() > 0)
		? (TargetRightSum / SourceRight)
		: (TargetSum / SourceSum);

	Modify();
	for (FMetaLandRoadLane& Lane : Section.Left)
	{
		WriteLaneBodyToTipWidth(
			Lane,
			SectionLen,
			Lane.Width.Eval(BodyTime),
			Lane.Width.Eval(TipTime) * LeftScale,
			bAttachAtSplineStart);
	}
	for (FMetaLandRoadLane& Lane : Section.Right)
	{
		WriteLaneBodyToTipWidth(
			Lane,
			SectionLen,
			Lane.Width.Eval(BodyTime),
			Lane.Width.Eval(TipTime) * RightScale,
			bAttachAtSplineStart);
	}

	return true;
}

bool UMetaLandRoadSplineComponent::ConnectRoadToDrawAnchor(
	const FMetaLandRoadDrawAnchor& SourceAnchor,
	bool bAttachAtSplineStart,
	bool bRebuildLayouts)
{
	UMetaLandRoadSplineComponent* SourceSpline = SourceAnchor.SourceSpline.Get();
	if (!IsValid(SourceSpline) || !SourceAnchor.IsValid() || SourceSpline == this)
	{
		return false;
	}

	if (bRebuildLayouts)
	{
		SourceSpline->GetRoadLayoutMutable().UpdateLayout(SourceSpline);
		GetRoadLayoutMutable().UpdateLayout(this);
	}

	TArray<int32> SourceLaneIndices;
	SourceLaneIndices.Add(SourceAnchor.LaneIndex);
	for (int32 BundledLaneIndex : SourceAnchor.BundledLaneIndices)
	{
		SourceLaneIndices.AddUnique(BundledLaneIndex);
	}
	SourceLaneIndices.Sort([](int32 A, int32 B)
	{
		return FMath::Abs(A) < FMath::Abs(B);
	});

	UMetaLandRoadConnection* PrimaryRoadConnection = bAttachAtSplineStart
		? GetPredecessorConnection()
		: GetSuccessorConnection();
	if (!IsValid(PrimaryRoadConnection))
	{
		return false;
	}

	auto ResolveSourceLaneConnection = [&SourceAnchor, SourceSpline](int32 LaneIndex) -> UMetaLandRoadLaneConnection*
	{
		const FMetaLandRoadLane* SourceLane = SourceSpline->GetRoadLane(
			SourceAnchor.SectionIndex, LaneIndex);
		if (!SourceLane)
		{
			return nullptr;
		}
		if (SourceAnchor.MouthSide == EMetaLandRoadMouthSide::End || SourceAnchor.bFromSuccessor)
		{
			return SourceLane->SuccessorConnection;
		}
		return SourceLane->PredecessorConnection;
	};

	auto TryConnectLane = [](UMetaLandRoadConnection* RoadConnection, UMetaLandRoadLaneConnection* SourceLaneConnection) -> bool
	{
		if (!IsValid(RoadConnection) || !IsValid(SourceLaneConnection) || !SourceLaneConnection->IsConnectionValid())
		{
			return false;
		}
		if (!RoadConnection->CanConnectTo(SourceLaneConnection))
		{
			if (RoadConnection->IsConnected()
				&& RoadConnection->OuterLaneConnection.Get() == SourceLaneConnection)
			{
				return true;
			}
			return false;
		}
		RoadConnection->ConnectTo(SourceLaneConnection);
		return RoadConnection->IsConnected();
	};

	bool bAnyConnected = false;
	bool bUsedPrimaryConnection = false;
	for (int32 SourceLaneIndex : SourceLaneIndices)
	{
		UMetaLandRoadLaneConnection* SourceLaneConnection = ResolveSourceLaneConnection(SourceLaneIndex);
		if (!SourceLaneConnection)
		{
			continue;
		}

		UMetaLandRoadConnection* RoadConnection = nullptr;
		if (!bUsedPrimaryConnection)
		{
			RoadConnection = PrimaryRoadConnection;
			bUsedPrimaryConnection = true;
		}
		else
		{
			RoadConnection = NewObject<UMetaLandRoadConnection>(this, NAME_None, RF_Transactional);
		}

		if (TryConnectLane(RoadConnection, SourceLaneConnection))
		{
			bAnyConnected = true;
		}
	}

	return bAnyConnected;
}

#if 0
static void AppendFilletSampleParams(
	const UMetaLandRoadSplineComponent* Spline,
	const TArray<FCornerFillet>& Fillets,
	double S0,
	double S1,
	TArray<float>& InOutParams)
{
	if (!Spline || Fillets.Num() == 0)
	{
		return;
	}

	for (const FCornerFillet& Fillet : Fillets)
	{
		const double Win0 = FMath::Max(S0, Fillet.SEnter);
		const double Win1 = FMath::Min(S1, Fillet.SLeave);
		if (Win1 <= Win0 + 1.0)
		{
			continue;
		}

		const double SampleCm = FMath::Max(Fillet.Radius * 0.10, Fillet.MinInnerRadius * 0.20);
		const int32 Extra = FMath::Clamp(FMath::CeilToInt((Win1 - Win0) / SampleCm), 8, 48);
		for (int32 StepIndex = 0; StepIndex <= Extra; ++StepIndex)
		{
			const double S = FMath::Lerp(Win0, Win1, static_cast<double>(StepIndex) / static_cast<double>(Extra));
			InOutParams.Add(Spline->GetInputKeyValueAtDistanceAlongSpline(S));
		}
	}
}
#endif

static void AppendCornerKeySampleParams(
	const UMetaLandRoadSplineComponent* Spline,
	double S0,
	double S1,
	TArray<float>& InOutParams)
{
	if (!Spline)
	{
		return;
	}
	const int32 NumPts = Spline->GetNumberOfSplinePoints();
	const double StepCm = FMath::Max(MetaLandRoad::DefaultMeshSampleCm * 0.35, 20.0);
	for (int32 KeyIdx = 1; KeyIdx <= NumPts - 2; ++KeyIdx)
	{
		const double KeyS = Spline->GetDistanceAlongSplineAtSplinePoint(KeyIdx);
		if (KeyS < S0 - 1.0 || KeyS > S1 + 1.0)
		{
			continue;
		}
		const FVector Prev = Spline->GetLocationAtSplinePoint(KeyIdx - 1, ESplineCoordinateSpace::Local);
		const FVector Cur = Spline->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::Local);
		const FVector Next = Spline->GetLocationAtSplinePoint(KeyIdx + 1, ESplineCoordinateSpace::Local);
		const FVector InDir = (Cur - Prev).GetSafeNormal2D();
		const FVector OutDir = (Next - Cur).GetSafeNormal2D();
		if (InDir.IsNearlyZero() || OutDir.IsNearlyZero())
		{
			continue;
		}
		const double Angle = FMath::Acos(
			FMath::Clamp(FVector::DotProduct(InDir, OutDir), -1.0, 1.0));
		if (Angle < MetaLandRoad::CornerFilletSkipAngleRad)
		{
			continue;
		}
		const int32 Extra = Angle > FMath::DegreesToRadians(50.0) ? 8
			: (Angle > FMath::DegreesToRadians(30.0) ? 5 : 2);
		InOutParams.Add(Spline->GetInputKeyValueAtDistanceAlongSpline(KeyS));
		for (int32 Step = 1; Step <= Extra; ++Step)
		{
			const double Delta = StepCm * static_cast<double>(Step);
			if (KeyS - Delta >= S0)
			{
				InOutParams.Add(Spline->GetInputKeyValueAtDistanceAlongSpline(KeyS - Delta));
			}
			if (KeyS + Delta <= S1)
			{
				InOutParams.Add(Spline->GetInputKeyValueAtDistanceAlongSpline(KeyS + Delta));
			}
		}
	}
}

void UMetaLandRoadSplineComponent::BuildLinearApproximation(
	TArray<FMetaLandRoadSplinePositionLinearApproximation>& OutPoints,
	const TFunction<double(double)>& RightOffsetFunc,
	double S0,
	double S1,
	int32 InReparamStepsPerSegment,
	int32 MinNumSteps,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	OutPoints.Reset();

	const double S0_Param = GetInputKeyValueAtDistanceAlongSpline(S0);
	const double S1_Param = GetInputKeyValueAtDistanceAlongSpline(S1);
	if (S1_Param <= S0_Param + KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double SpanLenCm = FMath::Max(S1 - S0, KINDA_SMALL_NUMBER);
	const double HalfW = LayoutHalfWidthAtS(this, (S0 + S1) * 0.5);
	const double TargetSampleCm = MetaLandRoad::MeshRibbonStepCm(HalfW, /*RadiusCm=*/0.0);
	int32 NumStep = FMath::Max(2, FMath::CeilToInt(SpanLenCm / TargetSampleCm));
	NumStep = FMath::Clamp(NumStep, 2, MetaLandRoad::LeaveArriveSampleSegCap);
	(void)InReparamStepsPerSegment;
	(void)MinNumSteps;

	const bool bAlignStart = IsCutSStation(this, S0);
	const bool bAlignEnd = IsCutSStation(this, S1);
	const FVector StartRight = FlattenHorizontalRight(
		GetRightVectorAtDistanceAlongSpline(S0, CoordinateSpace));
	const FVector EndRight = FlattenHorizontalRight(
		GetRightVectorAtDistanceAlongSpline(S1, CoordinateSpace));
	const FVector StartTravel = GetTangentAtDistanceAlongSpline(S0, CoordinateSpace).GetSafeNormal2D();
	const FVector EndTravel = GetTangentAtDistanceAlongSpline(S1, CoordinateSpace).GetSafeNormal2D();
	const double AlignCm = FMath::Min(
		LayoutHalfWidthAtS(this, (S0 + S1) * 0.5) * 0.05,
		MetaLandRoad::MouthRightAlignCm);

	TArray<float> Params;
	Params.Reserve(NumStep + 64);
	for (int32 i = 0; i <= NumStep; ++i)
	{
		const double SampleS = FMath::Lerp(S0, S1, static_cast<double>(i) / static_cast<double>(NumStep));
		Params.Add(static_cast<float>(GetInputKeyValueAtDistanceAlongSpline(SampleS)));
	}
	AppendCornerKeySampleParams(this, S0, S1, Params);
	for (int32 SectionIndex = 0; SectionIndex < GetLaneSectionsNum(); ++SectionIndex)
	{
		const double SectionS = GetLaneSection(SectionIndex).SOffset;
		if (SectionS > S0 + 1.0 && SectionS < S1 - 1.0)
		{
			Params.Add(static_cast<float>(GetInputKeyValueAtDistanceAlongSpline(SectionS)));
		}
	}
	Params.Sort();

	TArray<float> UniqueParams;
	UniqueParams.Reserve(Params.Num());
	const float ParamEps = 0.002f;
	for (float Param : Params)
	{
		Param = FMath::Clamp(Param, static_cast<float>(S0_Param), static_cast<float>(S1_Param));
		if (UniqueParams.Num() == 0 || FMath::Abs(Param - UniqueParams.Last()) > ParamEps)
		{
			UniqueParams.Add(Param);
		}
	}

	for (int32 Pass = 0; Pass < 3 && UniqueParams.Num() < MetaLandRoad::LeaveArriveSampleSegCap; ++Pass)
	{
		bool bGrew = false;
		TArray<float> Refined;
		Refined.Reserve(UniqueParams.Num() * 2);
		Refined.Add(UniqueParams[0]);
		for (int32 i = 1; i < UniqueParams.Num(); ++i)
		{
			const float P0 = Refined.Last();
			const float P1 = UniqueParams[i];
			const double SegS0 = GetDistanceAlongSplineAtSplineInputKey(P0);
			const double SegS1 = GetDistanceAlongSplineAtSplineInputKey(P1);
			const double SpanCm = SegS1 - SegS0;
			const FVector T0 = GetTangentAtSplineInputKey(P0, CoordinateSpace).GetSafeNormal2D();
			const FVector T1 = GetTangentAtSplineInputKey(P1, CoordinateSpace).GetSafeNormal2D();
			const double Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(T0, T1), -1.0, 1.0));
			const double RadiusCm = (Turn > 1.0e-4 && SpanCm > 1.0)
				? (SpanCm / Turn)
				: 0.0;
			double NeedCm = MetaLandRoad::MeshRibbonStepCm(HalfW, RadiusCm);
			if (RadiusCm > 1.0)
			{
				const double OuterStep = (RadiusCm + HalfW) * FMath::DegreesToRadians(5.0);
				NeedCm = FMath::Min(NeedCm, FMath::Max(OuterStep, HalfW * 0.08));
			}
			if (SpanCm > NeedCm * 1.15)
			{
				const int32 Cuts = FMath::Clamp(
					FMath::CeilToInt(SpanCm / NeedCm) - 1,
					1,
					24);
				for (int32 Cut = 1; Cut <= Cuts; ++Cut)
				{
					const double MidS = FMath::Lerp(
						SegS0,
						SegS1,
						static_cast<double>(Cut) / static_cast<double>(Cuts + 1));
					Refined.Add(static_cast<float>(GetInputKeyValueAtDistanceAlongSpline(MidS)));
				}
				bGrew = true;
			}
			Refined.Add(P1);
		}
		UniqueParams = MoveTemp(Refined);
		if (!bGrew)
		{
			break;
		}
	}

	struct FMeshSample
	{
		FVector Center = FVector::ZeroVector;
		FVector Right = FVector::ZeroVector;
		FVector Travel = FVector::ZeroVector;
		double S = 0.0;
		float Param = 0.0f;
	};
	TArray<FMeshSample> Samples;
	Samples.Reserve(UniqueParams.Num());
	for (const float Param : UniqueParams)
	{
		const double SampleS = GetDistanceAlongSplineAtSplineInputKey(Param);
		FVector Center = GetLocationAtSplineInputKey(Param, CoordinateSpace);
		FVector Travel = GetTangentAtSplineInputKey(Param, CoordinateSpace).GetSafeNormal2D();
		FVector Right = FlattenHorizontalRight(
			GetRightVectorAtSplineInputKey(Param, CoordinateSpace));
		if (Samples.Num() > 0 && FVector::DotProduct(Samples.Last().Right, Right) < 0.0)
		{
			Right = -Right;
		}
		if (bAlignStart && (SampleS - S0) < AlignCm
			&& FVector::DotProduct(Travel, StartTravel) > 0.85)
		{
			const double U = FMath::Clamp(1.0 - (SampleS - S0) / AlignCm, 0.0, 1.0);
			const double W = U * U * (3.0 - 2.0 * U);
			FVector TipRight = StartRight;
			if (FVector::DotProduct(TipRight, Right) < 0.0)
			{
				TipRight = -TipRight;
			}
			Right = FlattenHorizontalRight(FMath::Lerp(Right, TipRight, W));
		}
		if (bAlignEnd && (S1 - SampleS) < AlignCm
			&& FVector::DotProduct(Travel, EndTravel) > 0.85)
		{
			const double U = FMath::Clamp(1.0 - (S1 - SampleS) / AlignCm, 0.0, 1.0);
			const double W = U * U * (3.0 - 2.0 * U);
			FVector TipRight = EndRight;
			if (FVector::DotProduct(TipRight, Right) < 0.0)
			{
				TipRight = -TipRight;
			}
			Right = FlattenHorizontalRight(FMath::Lerp(Right, TipRight, W));
		}
		FMeshSample Sample;
		Sample.Center = Center;
		Sample.Right = Right;
		Sample.Travel = Travel;
		Sample.S = SampleS;
		Sample.Param = Param;
		Samples.Add(Sample);
	}

	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		const FVector Prev = (i > 0) ? Samples[i - 1].Center : Samples[i].Center;
		const FVector Next = (i + 1 < Samples.Num()) ? Samples[i + 1].Center : Samples[i].Center;
		FVector Travel = (Next - Prev).GetSafeNormal2D();
		if (Travel.IsNearlyZero())
		{
			Travel = Samples[i].Travel;
		}
		FVector Right = HorizontalRightFromTravel(Travel);
		if (i > 0 && FVector::DotProduct(Samples[i - 1].Right, Right) < 0.0)
		{
			Right = -Right;
		}
		if (bAlignStart && (Samples[i].S - S0) < AlignCm
			&& FVector::DotProduct(Travel, StartTravel) > 0.85)
		{
			const double U = FMath::Clamp(1.0 - (Samples[i].S - S0) / AlignCm, 0.0, 1.0);
			const double W = U * U * (3.0 - 2.0 * U);
			FVector TipRight = StartRight;
			if (FVector::DotProduct(TipRight, Right) < 0.0)
			{
				TipRight = -TipRight;
			}
			Right = FlattenHorizontalRight(FMath::Lerp(Right, TipRight, W));
		}
		if (bAlignEnd && (S1 - Samples[i].S) < AlignCm
			&& FVector::DotProduct(Travel, EndTravel) > 0.85)
		{
			const double U = FMath::Clamp(1.0 - (S1 - Samples[i].S) / AlignCm, 0.0, 1.0);
			const double W = U * U * (3.0 - 2.0 * U);
			FVector TipRight = EndRight;
			if (FVector::DotProduct(TipRight, Right) < 0.0)
			{
				TipRight = -TipRight;
			}
			Right = FlattenHorizontalRight(FMath::Lerp(Right, TipRight, W));
		}
		Samples[i].Travel = Travel;
		Samples[i].Right = Right;
	}

	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		const FVector Right = Samples[i].Right;
		const double Off = RightOffsetFunc(Samples[i].S);
		const FVector OffsetPoint = Samples[i].Center + Right * Off;
		OutPoints.Emplace(
			ApplyLinkedJunctionHeightToPosition(OffsetPoint, Samples[i].S, CoordinateSpace),
			Samples[i].Param);
		OutPoints.Last().PathCenter = ApplyLinkedJunctionHeightToPosition(
			Samples[i].Center, Samples[i].S, CoordinateSpace);
	}
}

FVector UMetaLandRoadSplineComponent::EvalLanePosition(int SectionIndex, int LaneIndex, double S, double Alpha, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float SParam = GetInputKeyValueAtDistanceAlongSpline(S);
	FVector Loc = GetLocationAtSplineInputKey(SParam, CoordinateSpace);
	FVector RVector = FlattenHorizontalRight(GetRightVectorAtDistanceAlongSpline(S, CoordinateSpace));

	double ROffset = EvalROffset(S);
	if (LaneIndex != LANE_INDEX_NONE)
	{
		ROffset += RoadLayout.Sections[SectionIndex].EvalLaneROffset(LaneIndex, S, Alpha);
	}

	const FMetaLandRoadLaneSection& Section = RoadLayout.Sections[SectionIndex];
	(void)Section;
	return ApplyLinkedJunctionHeightToPosition(Loc + RVector * ROffset, S, CoordinateSpace);
}

static float JunctionHeightSmootherstep01(float T)
{
	T = FMath::Clamp(T, 0.0f, 1.0f);
	return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
}

static double ResolveJunctionHeightBlendCm(
	const AMoKuEditIntersectionActor* Junction,
	const UMetaLandRoadSplineComponent* Spline)
{
	if (!Junction)
	{
		return 0.0;
	}

	if (Junction->JunctionData)
	{
		const FMetaLandRoadJunctionNode& Node = Junction->JunctionData->JunctionNode;
		for (const FMetaLandRoadJunctionArmRef& Arm : Node.Arms)
		{
			if (Arm.Spline.Get() == Spline && Arm.ForwardIndent > KINDA_SMALL_NUMBER)
			{
				return Arm.ForwardIndent;
			}
		}
		if (Node.ForwardIndent > KINDA_SMALL_NUMBER)
		{
			return Node.ForwardIndent;
		}
	}
	return 600.0;
}

struct FJunctionHeightBlendMouth
{
	double MouthS = 0.0;
	double BlendCm = 0.0;
	double HubWorldZ = 0.0;
};

static void AddJunctionHeightBlendMouth(
	TArray<FJunctionHeightBlendMouth>& OutMouths,
	const AMoKuEditIntersectionActor* Junction,
	const UMetaLandRoadSplineComponent* Spline,
	double MouthS,
	double SplineLength)
{
	if (!Junction || !Spline || MouthS < 0.0)
	{
		return;
	}

	const double BlendCm = FMath::Clamp(
		ResolveJunctionHeightBlendCm(Junction, Spline),
		50.0,
		FMath::Max(SplineLength, 50.0));
	for (const FJunctionHeightBlendMouth& Existing : OutMouths)
	{
		if (FMath::IsNearlyEqual(Existing.MouthS, MouthS, 5.0)
			&& FMath::IsNearlyEqual(Existing.HubWorldZ, Junction->GetActorLocation().Z, 1.0))
		{
			return;
		}
	}

	FJunctionHeightBlendMouth Mouth;
	Mouth.MouthS = MouthS;
	Mouth.BlendCm = BlendCm;
	Mouth.HubWorldZ = Junction->GetActorLocation().Z;
	OutMouths.Add(Mouth);
}

FVector UMetaLandRoadSplineComponent::ApplyLinkedJunctionHeightToPosition(
	const FVector& Position,
	double S,
	ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(GetOwner());
	if (!Road)
	{
		return Position;
	}

	const double SplineLength = GetSplineLength();
	TArray<FJunctionHeightBlendMouth> Mouths;
	AddJunctionHeightBlendMouth(
		Mouths, Road->StartOfJunction.Get(), this, Road->StartJunctionMeshTrimS, SplineLength);
	AddJunctionHeightBlendMouth(
		Mouths, Road->EndOfJunction.Get(), this, Road->EndJunctionMeshTrimS, SplineLength);
	for (const FMetaLandRoadJunctionTrimZone& Zone : Road->JunctionTrimZones)
	{
		AddJunctionHeightBlendMouth(Mouths, Zone.Junction.Get(), this, Zone.ApproachCutS, SplineLength);
		AddJunctionHeightBlendMouth(Mouths, Zone.Junction.Get(), this, Zone.DepartCutS, SplineLength);
	}
	if (Mouths.Num() == 0)
	{
		return Position;
	}

	float BestAlpha = 0.0f;
	double BestHubZ = Position.Z;
	for (const FJunctionHeightBlendMouth& Mouth : Mouths)
	{
		const double Dist = FMath::Abs(S - Mouth.MouthS);
		if (Dist >= Mouth.BlendCm)
		{
			continue;
		}
		const float Alpha = JunctionHeightSmootherstep01(
			static_cast<float>(1.0 - Dist / Mouth.BlendCm));
		if (Alpha > BestAlpha)
		{
			BestAlpha = Alpha;
			BestHubZ = Mouth.HubWorldZ;
		}
	}
	if (BestAlpha <= KINDA_SMALL_NUMBER)
	{
		return Position;
	}

	FVector WorldPos = (CoordinateSpace == ESplineCoordinateSpace::World)
		? Position
		: GetComponentTransform().TransformPosition(Position);
	WorldPos.Z = FMath::Lerp(WorldPos.Z, BestHubZ, static_cast<double>(BestAlpha));
	return (CoordinateSpace == ESplineCoordinateSpace::World)
		? WorldPos
		: GetComponentTransform().InverseTransformPosition(WorldPos);
}

float UMetaLandRoadSplineComponent::ClosestKeyToSegment(float Key1, float Key2, const FVector& A1, const FVector& A2) const
{
	if (Key2 <= Key1)
	{
		return Key1;
	}

	FVector SubsegmentStart = GetLocationAtSplineInputKey(Key1, ESplineCoordinateSpace::World);
	double ClosestDistance = TNumericLimits<double>::Max();
	FVector OutBestLocation = SubsegmentStart;

	const int32 NumSubdivisionsPerSegm = 16;
	int32 NumSubdivisions = NumSubdivisionsPerSegm * (Key2 - Key1) + .5f;
	if (NumSubdivisions <= 1)
	{
		NumSubdivisions = 2;
	}
	const float StepLen = (Key2 - Key1) / NumSubdivisions;

	for (int32 Step = 1; Step <= NumSubdivisions; Step++)
	{

		const float Key = Key1 + Step * StepLen;
		const FVector SubsegmentEnd = GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);

		FVector SplineClosest;
		FVector RayClosest;
		FMath::SegmentDistToSegmentSafe(SubsegmentStart, SubsegmentEnd, A1, A2, SplineClosest, RayClosest);

		const double Distance = FVector::DistSquared(SplineClosest, RayClosest);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			OutBestLocation = SplineClosest;
		}
		SubsegmentStart = SubsegmentEnd;
	}

	return FindInputKeyClosestToWorldLocation(OutBestLocation);
}

float UMetaLandRoadSplineComponent::ClosestKeyToSegment2(float S1, float S2, const FVector& A1, const FVector& A2) const
{
	const float Key1 = GetInputKeyValueAtDistanceAlongSpline(S1);
	const float Key2 = GetInputKeyValueAtDistanceAlongSpline(S2);
	return ClosestKeyToSegment(Key1, Key2, A1, A2);
}
