#include "MetaLandRoadTypes.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadSplineComponent.h"
#include "MoKuEditSplineActor.h"

bool MetaLandRoad::HasLeftSide(EMetaLandRoadLaneSectionSide Side)
{
	return Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Left;
}

bool MetaLandRoad::HasRightSide(EMetaLandRoadLaneSectionSide Side)
{
	return Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Right;
}

float MetaLandRoad::DistancePointToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const float Denom = AB.SizeSquared();
	if (Denom <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::Distance(Point, A);
	}

	const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / Denom, 0.0f, 1.0f);
	return FVector2D::Distance(Point, A + AB * T);
}

double MetaLandRoad::ResolveSectionEndS(const UMetaLandRoadSplineComponent* SplineComp, int32 SectionIndex)
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return 0.0;
	}

	// Next distinct boundary on the same face track (Left/Right splits do not block each other).
	const double StartS = SplineComp->GetLaneSection(SectionIndex).SOffset;
	const double NextBoundaryS = FindNextDistinctSectionBoundaryS(SplineComp, SectionIndex);
	if (NextBoundaryS > StartS + KINDA_SMALL_NUMBER)
	{
		return NextBoundaryS;
	}
	return SplineComp->GetSplineLength();
}

static bool SectionBoundaryConstrainsSide(
	EMetaLandRoadLaneSectionSide BoundarySide,
	EMetaLandRoadLaneSectionSide MoverSide)
{
	if (BoundarySide == EMetaLandRoadLaneSectionSide::Both
		|| MoverSide == EMetaLandRoadLaneSectionSide::Both)
	{
		return true;
	}
	return BoundarySide == MoverSide;
}

static void CollectCoLocatedSectionSides(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	double SameBoundaryTolerance,
	TArray<EMetaLandRoadLaneSectionSide, TInlineAllocator<3>>& OutSides)
{
	OutSides.Reset();
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return;
	}

	const double CurrentS = SplineComp->GetLaneSection(SectionIndex).SOffset;
	for (const FMetaLandRoadLaneSection& Section : SplineComp->GetLaneSections())
	{
		if (FMath::IsNearlyEqual(Section.SOffset, CurrentS, SameBoundaryTolerance))
		{
			OutSides.AddUnique(Section.Side);
		}
	}
}

static bool SectionBlocksAnyMoverSide(
	EMetaLandRoadLaneSectionSide BoundarySide,
	const TArray<EMetaLandRoadLaneSectionSide, TInlineAllocator<3>>& MoverSides)
{
	for (EMetaLandRoadLaneSectionSide MoverSide : MoverSides)
	{
		if (SectionBoundaryConstrainsSide(BoundarySide, MoverSide))
		{
			return true;
		}
	}
	return false;
}

double MetaLandRoad::FindPrevDistinctSectionBoundaryS(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	double SameBoundaryTolerance)
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return 0.0;
	}

	const double CurrentS = SplineComp->GetLaneSection(SectionIndex).SOffset;
	TArray<EMetaLandRoadLaneSectionSide, TInlineAllocator<3>> MoverSides;
	CollectCoLocatedSectionSides(SplineComp, SectionIndex, SameBoundaryTolerance, MoverSides);

	double PrevS = 0.0;
	for (const FMetaLandRoadLaneSection& Section : SplineComp->GetLaneSections())
	{
		if (FMath::IsNearlyEqual(Section.SOffset, CurrentS, SameBoundaryTolerance))
		{
			continue;
		}
		if (Section.SOffset >= CurrentS - SameBoundaryTolerance)
		{
			continue;
		}
		if (!SectionBlocksAnyMoverSide(Section.Side, MoverSides))
		{
			continue;
		}
		PrevS = FMath::Max(PrevS, Section.SOffset);
	}
	return PrevS;
}

double MetaLandRoad::FindNextDistinctSectionBoundaryS(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	double SameBoundaryTolerance)
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return 0.0;
	}

	const double CurrentS = SplineComp->GetLaneSection(SectionIndex).SOffset;
	const double SplineLength = SplineComp->GetSplineLength();
	TArray<EMetaLandRoadLaneSectionSide, TInlineAllocator<3>> MoverSides;
	CollectCoLocatedSectionSides(SplineComp, SectionIndex, SameBoundaryTolerance, MoverSides);

	double NextS = SplineLength;
	for (const FMetaLandRoadLaneSection& Section : SplineComp->GetLaneSections())
	{
		if (FMath::IsNearlyEqual(Section.SOffset, CurrentS, SameBoundaryTolerance))
		{
			continue;
		}
		if (Section.SOffset <= CurrentS + SameBoundaryTolerance)
		{
			continue;
		}
		if (!SectionBlocksAnyMoverSide(Section.Side, MoverSides))
		{
			continue;
		}
		NextS = FMath::Min(NextS, Section.SOffset);
	}
	return NextS;
}

double MetaLandRoad::ClampSectionBoundaryS(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SectionIndex,
	double CandidateS,
	double MinSectionLength,
	double SameBoundaryTolerance)
{
	if (!SplineComp || !SplineComp->GetLaneSections().IsValidIndex(SectionIndex) || SectionIndex <= 0)
	{
		return CandidateS;
	}

	const double SplineLength = SplineComp->GetSplineLength();
	const double AbsoluteMinS = MinSectionLength;
	const double AbsoluteMaxS = FMath::Max(AbsoluteMinS, SplineLength - MinSectionLength);

	CandidateS = FMath::Clamp(CandidateS, AbsoluteMinS, AbsoluteMaxS);

	const double PrevBoundaryS = FindPrevDistinctSectionBoundaryS(
		SplineComp, SectionIndex, SameBoundaryTolerance);
	const double NextBoundaryS = FindNextDistinctSectionBoundaryS(
		SplineComp, SectionIndex, SameBoundaryTolerance);

	double MinS = FMath::Max(AbsoluteMinS, PrevBoundaryS + MinSectionLength);
	double MaxS = FMath::Min(AbsoluteMaxS, NextBoundaryS - MinSectionLength);
	MaxS = FMath::Max(MaxS, MinS);

	return FMath::Clamp(CandidateS, MinS, MaxS);
}

FQuat MetaLandRoad::InvertRotation(const FQuat& Quat){
	const FVector Direction = Quat.GetForwardVector();
	const FVector UpVector = Quat.GetUpVector();
	return FRotationMatrix::MakeFromXZ(-Direction, UpVector).ToQuat();
}

void MetaLandRoad::TrimCurveInRang(FRichCurve& Curve, double Time0, double Time1, bool bFitBorders)
{
	const float Value0 = Curve.Eval(Time0);
	const float Value1 = Curve.Eval(Time1);

	int32 StartKey = INDEX_NONE;
	int32 EndKey = INDEX_NONE;
	for (int32 KeyIndex = 0; KeyIndex < Curve.Keys.Num(); ++KeyIndex)
	{
		const float CurrentKeyTime = Curve.Keys[KeyIndex].Time;
		if (CurrentKeyTime <= Time0)
		{
			StartKey = KeyIndex;
		}
		if (CurrentKeyTime >= Time1)
		{
			EndKey = KeyIndex;
			break;
		}
	}

	if (EndKey != INDEX_NONE && (Curve.GetNumKeys() - 1 != EndKey))
	{
		Curve.Keys.SetNum(EndKey + 1);
	}

	if (StartKey != INDEX_NONE && StartKey != 0)
	{
		Curve.Keys.RemoveAt(0, StartKey);
	}

	if (Curve.GetNumKeys())
	{
		if (Curve.Keys[Curve.GetNumKeys() - 1].Time > Time1)
		{
			Curve.Keys[Curve.GetNumKeys() - 1].Time = Time1;
		}

		if (Curve.Keys[0].Time < Time0)
		{
			Curve.Keys[0].Time = Time0;
		}
	}

	if (bFitBorders)
	{
		if (Curve.GetNumKeys() == 0 || !FMath::IsNearlyEqual(Curve.Eval(Time0), Value0))
		{
			const FKeyHandle KeyHandle = Curve.UpdateOrAddKey(Time0, Value0);
			Curve.GetKey(KeyHandle).InterpMode = ERichCurveInterpMode::RCIM_Cubic;
			Curve.GetKey(KeyHandle).TangentMode = ERichCurveTangentMode::RCTM_Auto;
		}

		if (!FMath::IsNearlyEqual(Curve.Eval(Time1), Value1))
		{
			const FKeyHandle KeyHandle = Curve.UpdateOrAddKey(Time1, Value1);
			Curve.GetKey(KeyHandle).InterpMode = ERichCurveInterpMode::RCIM_Cubic;
			Curve.GetKey(KeyHandle).TangentMode = ERichCurveTangentMode::RCTM_Auto;
		}
	}

	Curve.AutoSetTangents();
}

void MetaLandRoad::EnsureLaneWidthCurveCubic(FRichCurve& Width)
{
	for (FRichCurveKey& Key : Width.Keys)
	{
		Key.InterpMode = ERichCurveInterpMode::RCIM_Cubic;
		Key.TangentMode = ERichCurveTangentMode::RCTM_Auto;
	}
	if (Width.GetNumKeys() > 0)
	{
		Width.AutoSetTangents();
	}
}

static bool IsCurveZero(const FRichCurve& Curve, float ErrorTolerance = 2.0f)
{
	if (Curve.GetNumKeys() == 0)
	{
		return FMath::IsNearlyZero(Curve.Eval(0.0f), ErrorTolerance);
	}

	const float FirstKeyTime = Curve.Keys[0].Time;
	const float LastKeyTime = Curve.Keys[Curve.Keys.Num() - 1].Time;
	const float TestPoints = 10.0f;
	const float SampleRate = (LastKeyTime - FirstKeyTime) / TestPoints;

	for (int32 i = 0; i <= 10; ++i)
	{
		const float Value = Curve.Eval(FirstKeyTime + i * SampleRate);
		if (!FMath::IsNearlyZero(Value, ErrorTolerance))
		{
			return false;
		}
	}

	return true;
}

FMetaLandRoadLaneSection::FMetaLandRoadLaneSection()
{
}

FMetaLandRoadLaneSection::~FMetaLandRoadLaneSection()
{
}

FMetaLandRoadLaneSection::FMetaLandRoadLaneSection(FMetaLandRoadLaneSection&& Other) noexcept
{
	Side = Other.Side;
	Left = MoveTemp(Other.Left);
	Right = MoveTemp(Other.Right);
	SOffset = Other.SOffset;
	SOffsetEnd_Cached = Other.SOffsetEnd_Cached;
	Label = MoveTemp(Other.Label);
	Attributes = MoveTemp(Other.Attributes);
}

FMetaLandRoadLaneSection& FMetaLandRoadLaneSection::operator=(FMetaLandRoadLaneSection&& Other) noexcept
{
	Side = Other.Side;
	Left = MoveTemp(Other.Left);
	Right = MoveTemp(Other.Right);
	SOffset = Other.SOffset;
	SOffsetEnd_Cached = Other.SOffsetEnd_Cached;
	Label = MoveTemp(Other.Label);
	Attributes = MoveTemp(Other.Attributes);
	OwnedRoadLayout = nullptr;
	SectionIndex = INDEX_NONE;
	return *this;
}

FMetaLandRoadLaneSection::FMetaLandRoadLaneSection(const FMetaLandRoadLaneSection& Other) noexcept
{
	Side = Other.Side;
	Left = Other.Left;
	Right = Other.Right;
	SOffset = Other.SOffset;
	SOffsetEnd_Cached = Other.SOffsetEnd_Cached;
	Attributes = Other.Attributes;

	for (FMetaLandRoadLane& Lane : Left)
	{
		Lane.PredecessorConnection = nullptr;
		Lane.SuccessorConnection = nullptr;
	}

	for (FMetaLandRoadLane& Lane : Right)
	{
		Lane.PredecessorConnection = nullptr;
		Lane.SuccessorConnection = nullptr;
	}
}

FMetaLandRoadLaneSection& FMetaLandRoadLaneSection::operator=(const FMetaLandRoadLaneSection& Other) noexcept
{
	Side = Other.Side;
	Left = Other.Left;
	Right = Other.Right;
	SOffset = Other.SOffset;
	SOffsetEnd_Cached = Other.SOffsetEnd_Cached;
	Attributes = Other.Attributes;
	OwnedRoadLayout = nullptr;
	SectionIndex = INDEX_NONE;

	for (FMetaLandRoadLane& Lane : Left)
	{
		Lane.PredecessorConnection = nullptr;
		Lane.SuccessorConnection = nullptr;
	}

	for (FMetaLandRoadLane& Lane : Right)
	{
		Lane.PredecessorConnection = nullptr;
		Lane.SuccessorConnection = nullptr;
	}

	return *this;
}

bool FMetaLandRoadLaneSection::CheckLaneIndex(int LaneIndex) const
{
	if (LaneIndex == 0)
	{
		return false;
	}
	if (LaneIndex > 0 && (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Right))
	{
		return (LaneIndex - 1) < Right.Num();
	}
	if (LaneIndex < 0 && (Side == EMetaLandRoadLaneSectionSide::Both || Side == EMetaLandRoadLaneSectionSide::Left))
	{
		return (-LaneIndex - 1) < Left.Num();
	}
	return false;
}

const FMetaLandRoadLane& FMetaLandRoadLaneSection::GetLaneByIndex(int LaneIndex) const
{
	check(LaneIndex != METALANDROAD_LANE_INDEX_NONE);
	if (LaneIndex > 0)
	{
		return Right[LaneIndex - 1];
	}
	return Left[-LaneIndex - 1];
}

FMetaLandRoadLane& FMetaLandRoadLaneSection::GetLaneByIndex(int LaneIndex)
{
	check(LaneIndex != METALANDROAD_LANE_INDEX_NONE);
	if (LaneIndex > 0)
	{
		return Right[LaneIndex - 1];
	}
	return Left[-LaneIndex - 1];
}

void FMetaLandRoadLaneSection::Trim(bool bFitWidth)
{
	for (FMetaLandRoadLane& Lane : Left)
	{
		Lane.Trim(bFitWidth);
	}
	for (FMetaLandRoadLane& Lane : Right)
	{
		Lane.Trim(bFitWidth);
	}
	for (TPair<FName, FMetaLandRoadLaneAttribute>& Attr : Attributes)
	{
		Attr.Value.Trim(0.0, SOffsetEnd_Cached - SOffset);
	}
}

FMetaLandRoadLaneAttribute* FMetaLandRoadLaneSection::PinAttribute(FName TypeId)
{
	return FMetaLandRoadLaneAttribute::PinOnMap(Attributes, TypeId, EMetaLandRoadAttributeHost::SectionCenter);
}

bool FMetaLandRoadLaneSection::UnpinAttribute(FName TypeId)
{
	return FMetaLandRoadLaneAttribute::UnpinFromMap(Attributes, TypeId);
}

FMetaLandRoadLaneAttribute* FMetaLandRoadLaneSection::FindAttribute(FName TypeId)
{
	return FMetaLandRoadLaneAttribute::FindOnMap(Attributes, TypeId);
}

const FMetaLandRoadLaneAttribute* FMetaLandRoadLaneSection::FindAttribute(FName TypeId) const
{
	return FMetaLandRoadLaneAttribute::FindOnMap(Attributes, TypeId);
}

bool FMetaLandRoadLaneSection::EvalAttributeAt(
	FName TypeId,
	double InSOffset,
	TInstancedStruct<FMetaLandRoadLaneAttributeValue>& OutValue) const
{
	if (const FMetaLandRoadLaneAttribute* Track = FindAttribute(TypeId))
	{
		return Track->EvalAt(InSOffset, OutValue);
	}
	return false;
}

bool FMetaLandRoadLaneSection::EvalLandscapeAt(double InSOffset, FMetaLandRoadLandscapeAttributeValue& OutValue) const
{
	if (const FMetaLandRoadLaneAttribute* Track = FindAttribute(FMetaLandRoadAttributeRegistry::TypeLandscape))
	{
		return Track->EvalAsLandscape(InSOffset, OutValue);
	}
	return false;
}

// =============================================================================
// [KEY-ALGO] EvalLaneROffset
// LaneIndex<0 = 左幅（负 R），>0 = 右幅（正 R）。Alpha 0=内侧(中线) 1=外侧。
// 左右 section 的世界位置都从这里算，对不齐先查这里和 ROffset。
// =============================================================================
double FMetaLandRoadLaneSection::EvalLaneROffset(int LaneIndex, double InSOffset, double Alpha) const
{
	const TArray<FMetaLandRoadLane>* LanesInfo = nullptr;
	if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
	{
		return 0.0;
	}
	if (LaneIndex > 0)
	{
		LanesInfo = &Right;
	}
	else
	{
		LanesInfo = &Left;
	}

	const int32 AbsLaneIndex = FMath::Abs(LaneIndex);
	if (AbsLaneIndex > LanesInfo->Num())
	{
		return EvalLaneROffset(LaneIndex >= 0 ? LanesInfo->Num() : -LanesInfo->Num(), InSOffset, Alpha);
	}

	double BorderOffset = 0.0;
	double PreBorderOffset = 0.0;
	for (int32 i = 0; i < AbsLaneIndex; ++i)
	{
		PreBorderOffset = BorderOffset;
		if ((*LanesInfo)[i].LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>())
		{
			continue;
		}
		const double LocalT = InSOffset - (*LanesInfo)[i].GetStartOffset();
		const float SampleT = static_cast<float>(FMath::Max(LocalT, 0.0));
		const double LaneW = FMath::Max(
			static_cast<double>((*LanesInfo)[i].Width.Eval(SampleT)),
			8.0);
		BorderOffset += LaneW;
	}

	return FMath::Lerp(PreBorderOffset, BorderOffset, Alpha) * (LaneIndex >= 0 ? 1.0 : -1.0);
}

FMetaLandRoadLayout::FMetaLandRoadLayout()
{
	ThisShared = MakeShared<FMetaLandRoadLayout*>(this);
}

FMetaLandRoadLayout::FMetaLandRoadLayout(FMetaLandRoadLayout&& Other) noexcept
{
	ThisShared = MakeShared<FMetaLandRoadLayout*>(this);
	Sections = MoveTemp(Other.Sections);
	ROffset = MoveTemp(Other.ROffset);
	Direction = Other.Direction;
	FilledInstance = MoveTemp(Other.FilledInstance);
	FilledInstanceTexAngle = Other.FilledInstanceTexAngle;
	FilledInstanceTexScale = Other.FilledInstanceTexScale;
	LayoutVersion = 0;
	AttributesVersion = 0;
	UpdateLayout(nullptr);
}

FMetaLandRoadLayout& FMetaLandRoadLayout::operator=(FMetaLandRoadLayout&& Other) noexcept
{
	Sections = MoveTemp(Other.Sections);
	ROffset = MoveTemp(Other.ROffset);
	Direction = Other.Direction;
	FilledInstance = Other.FilledInstance;
	FilledInstanceTexAngle = Other.FilledInstanceTexAngle;
	FilledInstanceTexScale = Other.FilledInstanceTexScale;
	LayoutVersion = 0;
	AttributesVersion = 0;
	UpdateLayout(nullptr);
	return *this;
}

FMetaLandRoadLayout::FMetaLandRoadLayout(const FMetaLandRoadLayout& Other) noexcept
{
	ThisShared = MakeShared<FMetaLandRoadLayout*>(this);
	Sections = Other.Sections;
	ROffset = Other.ROffset;
	Direction = Other.Direction;
	FilledInstance = Other.FilledInstance;
	FilledInstanceTexAngle = Other.FilledInstanceTexAngle;
	FilledInstanceTexScale = Other.FilledInstanceTexScale;
	LayoutVersion = 0;
	AttributesVersion = 0;
	UpdateLayout(nullptr);
}

FMetaLandRoadLayout& FMetaLandRoadLayout::operator=(const FMetaLandRoadLayout& Other) noexcept
{
	Sections = Other.Sections;
	ROffset = Other.ROffset;
	Direction = Other.Direction;
	FilledInstance = Other.FilledInstance;
	FilledInstanceTexAngle = Other.FilledInstanceTexAngle;
	FilledInstanceTexScale = Other.FilledInstanceTexScale;
	LayoutVersion = 0;
	AttributesVersion = 0;
	UpdateLayout(nullptr);
	return *this;
}

void FMetaLandRoadLayout::UpdateLayout(UMetaLandRoadSplineComponent* OwnedRoadSpline)
{
	for (FMetaLandRoadLaneSection& Section : Sections)
	{
		for (auto It = Section.Left.CreateIterator(); It; ++It)
		{
			if (IsCurveZero(It->Width))
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = Section.Right.CreateIterator(); It; ++It)
		{
			if (IsCurveZero(It->Width))
			{
				It.RemoveCurrent();
			}
		}
	}

	const auto UpdateConnection = [OwnedRoadSpline](TObjectPtr<UMetaLandRoadLaneConnection>& Connection, const FMetaLandRoadLane& RoadLane)
	{
		if (OwnedRoadSpline)
		{
			if (!IsValid(Connection))
			{
				Connection = NewObject<UMetaLandRoadLaneConnection>(OwnedRoadSpline, NAME_None, RF_Transactional);
			}
			Connection->OwnedRoadSpline = OwnedRoadSpline;
			Connection->SectionIndex = RoadLane.GetStartSectionIndex();
			Connection->LaneIndex = RoadLane.GetLaneIndex();
		}
		else
		{
			Connection = nullptr;
		}
	};

	int32 FinishedLeftSectionIndex = -1;
	int32 FinishedRightSectionIndex = -1;

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
		Section.OwnedRoadLayout = ThisShared;
		Section.SectionIndex = SectionIndex;

		if (Section.Side == EMetaLandRoadLaneSectionSide::Left)
		{
			Section.Right.Empty();
		}
		else if (Section.Side == EMetaLandRoadLaneSectionSide::Right)
		{
			Section.Left.Empty();
		}

		if (Section.Side == EMetaLandRoadLaneSectionSide::Both || Section.Side == EMetaLandRoadLaneSectionSide::Left)
		{
			for (int32 i = 0; i < Section.Left.Num(); ++i)
			{
				FMetaLandRoadLane& Lane = Section.Left[i];
				Lane.LaneIndex = -i - 1;
				Lane.StartSectionIndex = SectionIndex;
				Lane.EndSectionIndex = SectionIndex;
				Lane.OwnedRoadLayout = ThisShared;
				UpdateConnection(Lane.PredecessorConnection, Lane);
				UpdateConnection(Lane.SuccessorConnection, Lane);
			}
			FinishedLeftSectionIndex = SectionIndex;
		}
		else if (FinishedLeftSectionIndex >= 0)
		{
			for (int32 i = 0; i < Sections[FinishedLeftSectionIndex].Left.Num(); ++i)
			{
				Sections[FinishedLeftSectionIndex].Left[i].EndSectionIndex = SectionIndex;
			}
		}

		if (Section.Side == EMetaLandRoadLaneSectionSide::Both || Section.Side == EMetaLandRoadLaneSectionSide::Right)
		{
			for (int32 i = 0; i < Section.Right.Num(); ++i)
			{
				FMetaLandRoadLane& Lane = Section.Right[i];
				Lane.LaneIndex = i + 1;
				Lane.StartSectionIndex = SectionIndex;
				Lane.EndSectionIndex = SectionIndex;
				Lane.OwnedRoadLayout = ThisShared;
				UpdateConnection(Lane.PredecessorConnection, Lane);
				UpdateConnection(Lane.SuccessorConnection, Lane);
			}
			FinishedRightSectionIndex = SectionIndex;
		}
		else if (FinishedRightSectionIndex >= 0)
		{
			for (int32 i = 0; i < Sections[FinishedRightSectionIndex].Right.Num(); ++i)
			{
				Sections[FinishedRightSectionIndex].Right[i].EndSectionIndex = SectionIndex;
			}
		}
	}

	if (OwnedRoadSpline && OwnedRoadSpline->IsClosedLoop())
	{
		if (IsValid(OwnedRoadSpline->GetPredecessorConnection()))
		{
			OwnedRoadSpline->GetPredecessorConnection()->Disconnect();
		}
		if (IsValid(OwnedRoadSpline->GetSuccessorConnection()))
		{
			OwnedRoadSpline->GetSuccessorConnection()->Disconnect();
		}
	}

	++LayoutVersion;
}

void FMetaLandRoadLayout::UpdateBounds(double SplineLength)
{
	if (Sections.Num())
	{
		Sections[0].SOffset = 0.0;
		Sections.Last().SOffsetEnd_Cached = SplineLength;
	}

	// Keep cuts/split markers near junction mouths. 100cm clamp parked orange
	// section lines far inside the road while rails/mesh tip sit at CutS.
	static constexpr double MinSectionLength = 10.0;
	const double MaxInteriorS = FMath::Max(MinSectionLength, SplineLength - MinSectionLength);
	for (int32 SectionIndex = 1; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		Sections[SectionIndex].SOffset = FMath::Clamp(
			Sections[SectionIndex].SOffset,
			MinSectionLength,
			MaxInteriorS);
	}

	for (int32 i = 0; i < Sections.Num() - 1; ++i)
	{
		Sections[i].SOffsetEnd_Cached = Sections[i + 1].SOffset;
	}

	int32 FinishedLeftSectionIndex = -1;
	int32 FinishedRightSectionIndex = -1;

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		FMetaLandRoadLaneSection& Section = Sections[SectionIndex];

		if (Section.Side == EMetaLandRoadLaneSectionSide::Both || Section.Side == EMetaLandRoadLaneSectionSide::Left)
		{
			for (int32 i = 0; i < Section.Left.Num(); ++i)
			{
				Section.Left[i].SOffsetEnd_Cached = Section.SOffsetEnd_Cached;
			}
			FinishedLeftSectionIndex = SectionIndex;
		}
		else if (FinishedLeftSectionIndex >= 0)
		{
			for (int32 i = 0; i < Sections[FinishedLeftSectionIndex].Left.Num(); ++i)
			{
				Sections[FinishedLeftSectionIndex].Left[i].SOffsetEnd_Cached = Section.SOffsetEnd_Cached;
			}
		}

		if (Section.Side == EMetaLandRoadLaneSectionSide::Both || Section.Side == EMetaLandRoadLaneSectionSide::Right)
		{
			for (int32 i = 0; i < Section.Right.Num(); ++i)
			{
				Section.Right[i].SOffsetEnd_Cached = Section.SOffsetEnd_Cached;
			}
			FinishedRightSectionIndex = SectionIndex;
		}
		else if (FinishedRightSectionIndex >= 0)
		{
			for (int32 i = 0; i < Sections[FinishedRightSectionIndex].Right.Num(); ++i)
			{
				Sections[FinishedRightSectionIndex].Right[i].SOffsetEnd_Cached = Section.SOffsetEnd_Cached;
			}
		}
	}

	// Repair legacy profiles that stored cubic width keys (width drifts/collapses at road end).
	auto NormalizeLaneWidthCurve = [](FMetaLandRoadLane& Lane, double SectionLength)
	{
		const auto ResetToConstant = [&Lane](float Width)
		{
			Lane.Width.Reset();
			Lane.Width.SetDefaultValue(Width);
		};

		if (Lane.Width.GetNumKeys() == 0)
		{
			if (Lane.Width.GetDefaultValue() <= KINDA_SMALL_NUMBER)
			{
				ResetToConstant(static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth));
			}
			return;
		}

		const float StartWidth = Lane.Width.Eval(0.f);
		const float EndWidth = Lane.Width.Eval(static_cast<float>(FMath::Max(SectionLength, 0.0)));
		const float FallbackWidth = FMath::Max(
			StartWidth,
			static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth));

		if (Lane.Width.GetNumKeys() == 1 || StartWidth <= KINDA_SMALL_NUMBER)
		{
			ResetToConstant(FallbackWidth);
			return;
		}
		// Only wipe a collapsed cubic (end ≈ 0). N→1 tapers (2→1 ≈ 50%/lane) must stay.
		if (EndWidth < 8.0f && EndWidth < FallbackWidth * 0.05f)
		{
			ResetToConstant(FallbackWidth);
			return;
		}

		// Width keys stay cubic so Add/drag Width Key keeps a smooth boundary
		// (Linear here made every corner a polyline after UpdateRoadLayout).
		MetaLandRoad::EnsureLaneWidthCurveCubic(Lane.Width);
	};

	for (FMetaLandRoadLaneSection& Section : Sections)
	{
		const double SectionLength = FMath::Max(Section.SOffsetEnd_Cached - Section.SOffset, 0.0);
		for (FMetaLandRoadLane& Lane : Section.Left)
		{
			NormalizeLaneWidthCurve(Lane, SectionLength);
		}
		for (FMetaLandRoadLane& Lane : Section.Right)
		{
			NormalizeLaneWidthCurve(Lane, SectionLength);
		}
	}
}

void FMetaLandRoadLayout::TrimSections(double SplineLength, double Tolerance, UMetaLandRoadSplineComponent* OwnedRoadSpline)
{
	UpdateBounds(SplineLength);

	MetaLandRoad::TrimCurveInRang(ROffset, 0.0, SplineLength, false);

	bool bWasRemoved = false;
	do
	{
		bWasRemoved = false;
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num();)
		{
			const FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
			// Array-neighbor End_Cached is 0 for co-located Left|Right at the same S
			// (dual-lane face split). Use face-aware length so we do not delete a valid side.
			double FaceLen = Section.SOffsetEnd_Cached - Section.SOffset;
			if (OwnedRoadSpline)
			{
				FaceLen = MetaLandRoad::ResolveSectionEndS(OwnedRoadSpline, SectionIndex) - Section.SOffset;
			}

			const bool bPastEnd = Section.SOffset >= SplineLength;
			const bool bZeroSpan = FaceLen <= Tolerance;
			bool bCoLocatedOppositeFace = false;
			if (bZeroSpan
				&& SectionIndex + 1 < Sections.Num()
				&& FMath::Abs(Sections[SectionIndex + 1].SOffset - Section.SOffset) <= Tolerance
				&& Section.Side != EMetaLandRoadLaneSectionSide::Both
				&& Sections[SectionIndex + 1].Side != EMetaLandRoadLaneSectionSide::Both
				&& Section.Side != Sections[SectionIndex + 1].Side)
			{
				// Left@S + Right@S markers — keep both for dual-lane face tracks.
				bCoLocatedOppositeFace = true;
			}

			if (bPastEnd || (bZeroSpan && !bCoLocatedOppositeFace))
			{
				Sections.RemoveAt(SectionIndex);
				bWasRemoved = true;
				UpdateBounds(SplineLength);
				continue;
			}
			++SectionIndex;
		}
	} while (bWasRemoved);

	UpdateLayout(OwnedRoadSpline);

	for (FMetaLandRoadLaneSection& Section : Sections)
	{
		Section.Trim(false);
	}
}

double FMetaLandRoadLayout::EvalROffset(double S) const
{
	if (ROffset.GetNumKeys() == 0)
	{
		return 0.0;
	}
	return ROffset.Eval(S);
}

// =============================================================================
// [KEY-ALGO] FindSideSection
// 从 SpanSection 往回找真正拥有 Left 或 Right 车道数组的 section。
// Add Left/Right Section、口条、显示线都靠它取对面幅；找不到就对不齐。
// =============================================================================
int FMetaLandRoadLayout::FindSideSection(int SectionIndex, EMetaLandRoadLaneSectionSide Side) const
{
	if (Sections.Num() == 0)
	{
		return INDEX_NONE;
	}
	if (!Sections.IsValidIndex(SectionIndex))
	{
		SectionIndex = Sections.Num() - 1;
	}

	const int32 Origin = SectionIndex;
	auto SectionOwnsFace = [this, Side](int32 Index, bool bAllowZeroLength) -> bool
	{
		const FMetaLandRoadLaneSection& Candidate = Sections[Index];
		if (!bAllowZeroLength
			&& Candidate.SOffsetEnd_Cached <= Candidate.SOffset + KINDA_SMALL_NUMBER)
		{
			return false;
		}
		if (Candidate.Side != Side && Candidate.Side != EMetaLandRoadLaneSectionSide::Both)
		{
			return false;
		}
		if (Side == EMetaLandRoadLaneSectionSide::Left)
		{
			return MetaLandRoad::HasLeftSide(Candidate.Side) && !Candidate.Left.IsEmpty();
		}
		if (Side == EMetaLandRoadLaneSectionSide::Right)
		{
			return MetaLandRoad::HasRightSide(Candidate.Side) && !Candidate.Right.IsEmpty();
		}
		return !Candidate.Left.IsEmpty() || !Candidate.Right.IsEmpty();
	};

	auto SearchFromOrigin = [&](bool bAllowZeroLength) -> int32
	{
		for (int32 Index = Origin; Index >= 0; --Index)
		{
			if (SectionOwnsFace(Index, bAllowZeroLength))
			{
				return Index;
			}
		}
		const double HostS = Sections[Origin].SOffset;
		for (int32 Forward = Origin + 1; Forward < Sections.Num(); ++Forward)
		{
			if (!FMath::IsNearlyEqual(Sections[Forward].SOffset, HostS, 10.0))
			{
				break;
			}
			if (SectionOwnsFace(Forward, bAllowZeroLength))
			{
				return Forward;
			}
		}
		return INDEX_NONE;
	};

	const int32 RealSpan = SearchFromOrigin(false);
	if (RealSpan != INDEX_NONE)
	{
		return RealSpan;
	}
	// Add Left/Right Section often sits at one S (zero cached span) but still owns lanes.
	// Skipping it left overlay rails with no mesh.
	return SearchFromOrigin(true);
}

int FMetaLandRoadLayout::FindSectionForLaneAtS(double S, int LaneIndex) const
{
	const int32 Host = FindSectionAtSOffset(S);
	if (Host == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
	{
		return Host;
	}
	const int32 Resolved = ResolveLaneSectionIndex(Host, LaneIndex);
	if (Resolved == INDEX_NONE)
	{
		return Host;
	}
	const FMetaLandRoadLaneSection& ResolvedSection = Sections[Resolved];
	if (S > ResolvedSection.SOffsetEnd_Cached + 10.0
		&& Host != Resolved
		&& Sections.IsValidIndex(Host))
	{
		const FMetaLandRoadLaneSection& HostSection = Sections[Host];
		const bool bWantRight = LaneIndex > 0;
		const bool bHostHasFace = bWantRight
			? (MetaLandRoad::HasRightSide(HostSection.Side) && !HostSection.Right.IsEmpty())
			: (MetaLandRoad::HasLeftSide(HostSection.Side) && !HostSection.Left.IsEmpty());
		if (bHostHasFace)
		{
			return Host;
		}
	}
	return Resolved;
}

int32 FMetaLandRoadLayout::ResolveLaneSectionIndex(int32 SpanSectionIndex, int32 LaneIndex) const
{
	if (!Sections.IsValidIndex(SpanSectionIndex))
	{
		return INDEX_NONE;
	}

	if (LaneIndex == METALANDROAD_LANE_INDEX_NONE)
	{
		return SpanSectionIndex;
	}

	const EMetaLandRoadLaneSectionSide Side = LaneIndex > 0
		? EMetaLandRoadLaneSectionSide::Right
		: EMetaLandRoadLaneSectionSide::Left;

	const FMetaLandRoadLaneSection& SpanSection = Sections[SpanSectionIndex];
	if (LaneIndex > 0)
	{
		if (SpanSection.Right.Num() > 0 && MetaLandRoad::HasRightSide(SpanSection.Side)
			&& SpanSection.CheckLaneIndex(LaneIndex))
		{
			return SpanSectionIndex;
		}
	}
	else
	{
		if (SpanSection.Left.Num() > 0 && MetaLandRoad::HasLeftSide(SpanSection.Side)
			&& SpanSection.CheckLaneIndex(LaneIndex))
		{
			return SpanSectionIndex;
		}
	}

	int32 SideSection = FindSideSection(SpanSectionIndex, Side);
	while (SideSection >= 0)
	{
		const FMetaLandRoadLaneSection& Section = Sections[SideSection];
		if (Section.CheckLaneIndex(LaneIndex))
		{
			return SideSection;
		}

		if (SideSection == 0)
		{
			break;
		}

		const int32 PrevSideSection = FindSideSection(SideSection - 1, Side);
		if (PrevSideSection >= SideSection)
		{
			break;
		}
		SideSection = PrevSideSection;
	}

	return INDEX_NONE;
}

int FMetaLandRoadLayout::FindSectionAtSOffset(double S) const
{
	if (Sections.Num() == 0)
	{
		return INDEX_NONE;
	}

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = Sections[SectionIndex];
		// Co-located Left|Right markers share S; array End_Cached == SOffset (zero span).
		// Skip them as hosts — FindSideSection still resolves both faces from the real span.
		if (Section.SOffsetEnd_Cached <= Section.SOffset + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		// Inclusive end: tip S == CutS must stay on the approach section (not hub span).
		if (Section.SOffsetEnd_Cached >= S - KINDA_SMALL_NUMBER)
		{
			return SectionIndex;
		}
	}

	return Sections.Num() - 1;
}

FMetaLandRoadLane::FMetaLandRoadLane()
{
	Width.SetDefaultValue(static_cast<float>(MetaLandRoad::DefaultRoadLaneWidth));
}

bool FMetaLandRoadLane::IsLaneValid() const
{
	if (OwnedRoadLayout.IsValid())
	{
		const TArray<FMetaLandRoadLaneSection>& Sections = (*OwnedRoadLayout.Pin())->Sections;
		if (Sections.IsValidIndex(StartSectionIndex))
		{
			const FMetaLandRoadLaneSection& Section = Sections[StartSectionIndex];
			const FMetaLandRoadLane* Lane = nullptr;

			if (LaneIndex > 0 && LaneIndex <= Section.Right.Num())
			{
				Lane = &Section.Right[LaneIndex - 1];
			}
			else if (LaneIndex < 0 && -LaneIndex <= Section.Left.Num())
			{
				Lane = &Section.Left[-LaneIndex - 1];
			}

			if (Lane == this)
			{
				return true;
			}
		}
	}
	return false;
}

bool FMetaLandRoadLane::IsForwardLane() const
{
	const TSharedPtr<FMetaLandRoadLayout*> Layout = OwnedRoadLayout.Pin();
	if (!Layout.IsValid())
	{
		return LaneIndex > 0;
	}

	return (LaneIndex > 0) ^ (Direction == EMetaLandRoadLaneDirection::Invert) ^ ((*Layout)->Direction == EMetaLandRoadDirection::RightHand);
}

double FMetaLandRoadLane::GetStartOffset() const
{
	if (const TSharedPtr<FMetaLandRoadLayout*> Layout = OwnedRoadLayout.Pin())
	{
		if ((*Layout)->Sections.IsValidIndex(StartSectionIndex))
		{
			return (*Layout)->Sections[StartSectionIndex].SOffset;
		}
	}
	return 0.0;
}

void FMetaLandRoadLane::Trim(bool bFitWidth)
{
	const double SOffsetEnd = GetEndOffset() - GetStartOffset();
	MetaLandRoad::TrimCurveInRang(Width, 0.0, SOffsetEnd, bFitWidth);
	for (TPair<FName, FMetaLandRoadLaneAttribute>& Attr : Attributes)
	{
		Attr.Value.Trim(0.0, SOffsetEnd);
	}
}

FMetaLandRoadLaneAttribute* FMetaLandRoadLane::PinAttribute(FName TypeId)
{
	return FMetaLandRoadLaneAttribute::PinOnMap(Attributes, TypeId, EMetaLandRoadAttributeHost::Lane);
}

bool FMetaLandRoadLane::UnpinAttribute(FName TypeId)
{
	return FMetaLandRoadLaneAttribute::UnpinFromMap(Attributes, TypeId);
}

FMetaLandRoadLaneAttribute* FMetaLandRoadLane::FindAttribute(FName TypeId)
{
	return FMetaLandRoadLaneAttribute::FindOnMap(Attributes, TypeId);
}

const FMetaLandRoadLaneAttribute* FMetaLandRoadLane::FindAttribute(FName TypeId) const
{
	return FMetaLandRoadLaneAttribute::FindOnMap(Attributes, TypeId);
}

bool FMetaLandRoadLane::EvalAttributeAt(
	FName TypeId,
	double SOffset,
	TInstancedStruct<FMetaLandRoadLaneAttributeValue>& OutValue) const
{
	if (const FMetaLandRoadLaneAttribute* Track = FindAttribute(TypeId))
	{
		return Track->EvalAt(SOffset, OutValue);
	}
	return false;
}

UMetaLandRoadSplineComponent* UMetaLandRoadConnection::GetOwnedRoadSpline() const
{
	return Cast<UMetaLandRoadSplineComponent>(GetOuter());
}

bool UMetaLandRoadConnection::IsConnectionValid() const
{
	return IsValid(GetOwnedRoadSpline());
}

bool UMetaLandRoadConnection::IsSuccessorConnection() const
{
	const UMetaLandRoadSplineComponent* OwnedRoadSpline = GetOwnedRoadSpline();
	if (!IsValid(OwnedRoadSpline))
	{
		return false;
	}
	return OwnedRoadSpline->GetSuccessorConnection() == this;
}

bool UMetaLandRoadConnection::IsPredecessorConnection() const
{
	const UMetaLandRoadSplineComponent* OwnedRoadSpline = GetOwnedRoadSpline();
	if (!IsValid(OwnedRoadSpline))
	{
		return false;
	}
	return OwnedRoadSpline->GetPredecessorConnection() == this;
}

void UMetaLandRoadConnection::Disconnect()
{
	if (OuterLaneConnection.IsValid())
	{
		Modify();
		if (UMetaLandRoadLaneConnection* LaneConnection = OuterLaneConnection.Get())
		{
			LaneConnection->Modify();
			LaneConnection->OuterRoadConnections.Remove(this);
		}
		OuterLaneConnection = nullptr;
	}
}

bool UMetaLandRoadConnection::IsConnected() const
{
	UMetaLandRoadLaneConnection* LaneConnection = OuterLaneConnection.Get();
	return LaneConnection != nullptr && LaneConnection->IsConnectionValid();
}

const FMetaLandRoadLane& UMetaLandRoadLaneConnection::GetOwnedRoadLane() const
{
	checkf(OwnedRoadSpline.IsValid(), TEXT("Lane connection has no owned road spline"));
	return OwnedRoadSpline->GetLaneSection(SectionIndex).GetLaneByIndex(LaneIndex);
}

FMetaLandRoadLane& UMetaLandRoadLaneConnection::GetOwnedRoadLane()
{
	return OwnedRoadSpline->GetLaneSection(SectionIndex).GetLaneByIndex(LaneIndex);
}

bool UMetaLandRoadLaneConnection::IsConnectionValid() const
{
	if (!OwnedRoadSpline.IsValid())
	{
		return false;
	}

	const FMetaLandRoadLane* OwnedLane = OwnedRoadSpline->GetRoadLane(SectionIndex, LaneIndex);
	if (!OwnedLane)
	{
		return false;
	}

	if (!OwnedLane->IsLaneValid())
	{
		return false;
	}

	if (OwnedLane->PredecessorConnection != this && OwnedLane->SuccessorConnection != this)
	{
		return false;
	}

	return true;
}

bool UMetaLandRoadLaneConnection::IsConnected() const
{
	for (const TWeakObjectPtr<UMetaLandRoadConnection>& It : OuterRoadConnections)
	{
		if (It.IsValid() && It->OuterLaneConnection.LoadSynchronous() == this)
		{
			return true;
		}
	}
	return false;
}

void UMetaLandRoadLaneConnection::DisconnectAll()
{
	Modify();

	for (const TWeakObjectPtr<UMetaLandRoadConnection>& It : OuterRoadConnections)
	{
		if (It.IsValid())
		{
			if (It->OuterLaneConnection == this)
			{
				It->Modify();
				It->OuterLaneConnection = nullptr;
			}
		}
	}
	OuterRoadConnections.Reset();
}

bool FMetaLandRoadDrawAnchor::IsValid() const
{
	if (!SourceSpline.IsValid() || SectionIndex == INDEX_NONE || LaneIndex == METALANDROAD_LANE_INDEX_NONE)
	{
		return false;
	}

	const UMetaLandRoadSplineComponent* Spline = SourceSpline.Get();
	if (!::IsValid(Spline) || !Spline->GetLaneSections().IsValidIndex(SectionIndex))
	{
		return false;
	}

	if (!MetaLandRoad::IsLaneValidOnSpanSection(Spline, SectionIndex, LaneIndex))
	{
		return false;
	}

	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		Spline->GetLaneRange(SectionIndex, LaneIndex);
	if (AnchorS < Range.StartS - KINDA_SMALL_NUMBER || AnchorS > Range.EndS + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	return true;
}

void MetaLandRoadLogDrawAnchor(const TCHAR* Context, const FMetaLandRoadDrawAnchor& Anchor)
{
	(void)Context;
	(void)Anchor;
}

bool MetaLandRoadBuildDrawAnchorFromLaneConnection(
	const UMetaLandRoadLaneConnection& LaneConnection,
	FMetaLandRoadDrawAnchor& OutAnchor)
{
	OutAnchor = FMetaLandRoadDrawAnchor();
	UMetaLandRoadSplineComponent* Spline = LaneConnection.OwnedRoadSpline.Get();
	if (!IsValid(Spline) || LaneConnection.SectionIndex == INDEX_NONE
		|| LaneConnection.LaneIndex == METALANDROAD_LANE_INDEX_NONE)
	{
		return false;
	}

	const FMetaLandRoadLane* Lane = Spline->GetRoadLane(LaneConnection.SectionIndex, LaneConnection.LaneIndex);
	if (!Lane || !Lane->IsLaneValid())
	{
		return false;
	}

	OutAnchor.SourceSpline = Spline;
	OutAnchor.SectionIndex = LaneConnection.SectionIndex;
	OutAnchor.LaneIndex = LaneConnection.LaneIndex;

	if (Lane->SuccessorConnection == &LaneConnection)
	{
		OutAnchor.MouthSide = EMetaLandRoadMouthSide::End;
		OutAnchor.AnchorS = Lane->GetEndOffset();
		OutAnchor.bFromSuccessor = true;
	}
	else if (Lane->PredecessorConnection == &LaneConnection)
	{
		OutAnchor.MouthSide = EMetaLandRoadMouthSide::Start;
		OutAnchor.AnchorS = Lane->GetStartOffset();
		OutAnchor.bFromSuccessor = false;
	}
	else
	{
		return false;
	}

	OutAnchor.BundledLaneIndices.Reset();
	OutAnchor.BundledLaneIndices.Add(LaneConnection.LaneIndex);
	return OutAnchor.IsValid();
}

FTransform UMetaLandRoadLaneConnection::EvalTransform() const
{
	FMetaLandRoadDrawAnchor Anchor;
	if (!MetaLandRoadBuildDrawAnchorFromLaneConnection(*this, Anchor))
	{
		return FTransform::Identity;
	}

	FTransform MouthTransform = FTransform::Identity;
	if (Anchor.SourceSpline.IsValid()
		&& Anchor.SourceSpline->EvalMouthTransform(Anchor, MouthTransform))
	{
		return MouthTransform;
	}

	return FTransform::Identity;
}

bool UMetaLandRoadConnection::CanConnectTo(const UMetaLandRoadLaneConnection* OtherLaneConnection) const
{
	if (!OtherLaneConnection || !OtherLaneConnection->IsConnectionValid() || !IsConnectionValid())
	{
		return false;
	}

	if (IsConnected())
	{
		return false;
	}

	if (GetOwnedRoadSpline() == OtherLaneConnection->OwnedRoadSpline.Get())
	{
		return false;
	}

	return true;
}

void UMetaLandRoadConnection::ConnectTo(UMetaLandRoadLaneConnection* OtherLaneConnection)
{
	if (!CanConnectTo(OtherLaneConnection))
	{
		return;
	}

	Modify();
	OtherLaneConnection->Modify();
	Disconnect();
	OuterLaneConnection = OtherLaneConnection;
	OtherLaneConnection->OuterRoadConnections.Add(this);
}

int32 MetaLandRoad::ResolveLaneSectionIndexForSpan(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SpanSectionIndex,
	int32 LaneIndex)
{
	if (!SplineComp)
	{
		return INDEX_NONE;
	}
	return SplineComp->GetRoadLayout().ResolveLaneSectionIndex(SpanSectionIndex, LaneIndex);
}

bool MetaLandRoad::IsLaneValidOnSpanSection(
	const UMetaLandRoadSplineComponent* SplineComp,
	int32 SpanSectionIndex,
	int32 LaneIndex)
{
	if (!SplineComp || SpanSectionIndex == INDEX_NONE || LaneIndex == LANE_INDEX_NONE)
	{
		return false;
	}

	const int32 LaneSectionIndex = ResolveLaneSectionIndexForSpan(SplineComp, SpanSectionIndex, LaneIndex);
	if (LaneSectionIndex == INDEX_NONE || !SplineComp->GetLaneSections().IsValidIndex(LaneSectionIndex))
	{
		return false;
	}

	if (!SplineComp->GetLaneSection(LaneSectionIndex).CheckLaneIndex(LaneIndex))
	{
		return false;
	}

	const UMetaLandRoadSplineComponent::FMetaLandRoadLaneRange Range =
		SplineComp->GetLaneRange(SpanSectionIndex, LaneIndex);
	if (Range.EndS <= Range.StartS + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const double SpanS0 = SplineComp->GetLaneSection(SpanSectionIndex).SOffset;
	const double SpanS1 = ResolveSectionEndS(SplineComp, SpanSectionIndex);
	const double ClipS0 = FMath::Max(SpanS0, Range.StartS);
	const double ClipS1 = FMath::Min(SpanS1, Range.EndS);
	return ClipS1 > ClipS0 + KINDA_SMALL_NUMBER;
}

double MetaLandRoad::ClampMouthAnchorSToVisibleMesh(
	const UMetaLandRoadSplineComponent* SplineComp,
	double PreferredAnchorS,
	EMetaLandRoadMouthSide MouthSide)
{
	if (!SplineComp)
	{
		return PreferredAnchorS;
	}

	const double Length = SplineComp->GetSplineLength();
	double ClampedPreferred = FMath::Clamp(PreferredAnchorS, 0.0, Length);

	const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	if (!RoadActor)
	{
		return ClampedPreferred;
	}

	TArray<TPair<double, double>> MeshSpans;
	RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
	if (MeshSpans.Num() == 0)
	{
		return ClampedPreferred;
	}

	for (const TPair<double, double>& Span : MeshSpans)
	{
		if (ClampedPreferred >= Span.Key - 1.0 && ClampedPreferred <= Span.Value + 1.0)
		{
			return MouthSide == EMetaLandRoadMouthSide::Start
				? FMath::Max(Span.Key, FMath::Min(ClampedPreferred, Span.Value))
				: FMath::Min(Span.Value, FMath::Max(ClampedPreferred, Span.Key));
		}
	}

	if (MouthSide == EMetaLandRoadMouthSide::End)
	{
		double BestEnd = MeshSpans[0].Value;
		for (const TPair<double, double>& Span : MeshSpans)
		{
			if (Span.Value <= ClampedPreferred + 1.0)
			{
				BestEnd = Span.Value;
			}
		}
		return BestEnd;
	}

	double BestStart = MeshSpans.Last().Key;
	for (int32 Index = MeshSpans.Num() - 1; Index >= 0; --Index)
	{
		if (MeshSpans[Index].Key >= ClampedPreferred - 1.0)
		{
			BestStart = MeshSpans[Index].Key;
		}
	}
	return BestStart;
}

void MetaLandRoad::CollectVisibleMouthStations(
	const UMetaLandRoadSplineComponent* SplineComp,
	TArray<TPair<double, EMetaLandRoadMouthSide>>& OutStations)
{
	OutStations.Reset();
	if (!SplineComp)
	{
		return;
	}

	const double Length = SplineComp->GetSplineLength();
	auto AddUnique = [&OutStations](double S, EMetaLandRoadMouthSide Side)
	{
		for (const TPair<double, EMetaLandRoadMouthSide>& Existing : OutStations)
		{
			if (Existing.Value == Side && FMath::IsNearlyEqual(Existing.Key, S, 1.0))
			{
				return;
			}
		}
		OutStations.Emplace(S, Side);
	};

	const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner());
	TArray<TPair<double, double>> MeshSpans;
	if (RoadActor)
	{
		RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
	}
	if (MeshSpans.Num() == 0)
	{
		AddUnique(0.0, EMetaLandRoadMouthSide::Start);
		if (Length > 1.0)
		{
			AddUnique(Length, EMetaLandRoadMouthSide::End);
		}
		return;
	}

	for (const TPair<double, double>& Span : MeshSpans)
	{
		AddUnique(Span.Key, EMetaLandRoadMouthSide::Start);
		AddUnique(Span.Value, EMetaLandRoadMouthSide::End);
	}
}

void MetaLandRoad::CollectEndSnapMouthStations(
	const UMetaLandRoadSplineComponent* SplineComp,
	TArray<TPair<double, EMetaLandRoadMouthSide>>& OutStations)
{
	OutStations.Reset();
	if (!SplineComp)
	{
		return;
	}

	const double Length = SplineComp->GetSplineLength();
	TArray<TPair<double, double>> MeshSpans;
	if (const AMoKuEditSplineActor* RoadActor = Cast<AMoKuEditSplineActor>(SplineComp->GetOwner()))
	{
		RoadActor->GetJunctionMeshGenerationSpans(MeshSpans);
	}
	if (MeshSpans.Num() == 0)
	{
		OutStations.Emplace(0.0, EMetaLandRoadMouthSide::Start);
		if (Length > 1.0)
		{
			OutStations.Emplace(Length, EMetaLandRoadMouthSide::End);
		}
		return;
	}

	bool bCoverStart = false;
	bool bCoverEnd = false;
	double StubStartS = Length;
	double StubEndS = 0.0;
	for (const TPair<double, double>& Span : MeshSpans)
	{
		if (Span.Key <= 1.0)
		{
			bCoverStart = true;
		}
		if (Span.Value >= Length - 1.0)
		{
			bCoverEnd = true;
		}
		StubStartS = FMath::Min(StubStartS, Span.Key);
		StubEndS = FMath::Max(StubEndS, Span.Value);
	}

	if (bCoverStart)
	{
		OutStations.Emplace(0.0, EMetaLandRoadMouthSide::Start);
	}
	else if (StubStartS < Length)
	{
		OutStations.Emplace(StubStartS, EMetaLandRoadMouthSide::Start);
	}

	if (bCoverEnd)
	{
		OutStations.Emplace(Length, EMetaLandRoadMouthSide::End);
	}
	else if (StubEndS > 1.0)
	{
		OutStations.Emplace(StubEndS, EMetaLandRoadMouthSide::End);
	}
}

bool MetaLandRoad::ResolveMouthOutwardTangent(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FMetaLandRoadDrawAnchor& Anchor,
	FVector& OutOutwardTangent)
{
	OutOutwardTangent = FVector::ZeroVector;
	if (!SplineComp || !Anchor.IsValid() || Anchor.SourceSpline.Get() != SplineComp)
	{
		return false;
	}

	if (ResolveLaneSectionIndexForSpan(SplineComp, Anchor.SectionIndex, Anchor.LaneIndex) == INDEX_NONE)
	{
		return false;
	}

	const double MouthS = ClampMouthAnchorSToVisibleMesh(SplineComp, Anchor.AnchorS, Anchor.MouthSide);
	FVector AlongIncreasingS = SplineComp->GetDirectionAtDistanceAlongSpline(
		MouthS, ESplineCoordinateSpace::World).GetSafeNormal2D();
	if (AlongIncreasingS.IsNearlyZero())
	{
		const double Delta = FMath::Max(50.0, SplineComp->GetSplineLength() * 0.01);
		const double SBefore = FMath::Max(MouthS - Delta, 0.0);
		const double SAfter = FMath::Min(MouthS + Delta, SplineComp->GetSplineLength());
		AlongIncreasingS = (
			SplineComp->GetLocationAtDistanceAlongSpline(SAfter, ESplineCoordinateSpace::World)
			- SplineComp->GetLocationAtDistanceAlongSpline(SBefore, ESplineCoordinateSpace::World))
			.GetSafeNormal2D();
	}
	if (AlongIncreasingS.IsNearlyZero())
	{
		return false;
	}

	OutOutwardTangent = Anchor.MouthSide == EMetaLandRoadMouthSide::End
		? AlongIncreasingS
		: -AlongIncreasingS;
	return !OutOutwardTangent.IsNearlyZero();
}

static void CollectAnchorLaneIndices(const FMetaLandRoadDrawAnchor& Anchor, TArray<int32>& OutLanes)
{
	OutLanes.Reset();
	OutLanes.Add(Anchor.LaneIndex);
	for (int32 BundledLaneIndex : Anchor.BundledLaneIndices)
	{
		OutLanes.AddUnique(BundledLaneIndex);
	}
}

static int32 ResolveInnermostLaneIndex(const TArray<int32>& Lanes)
{
	if (Lanes.Num() == 0)
	{
		return LANE_INDEX_NONE;
	}

	int32 Innermost = Lanes[0];
	for (int32 LaneIndex : Lanes)
	{
		if (LaneIndex > 0)
		{
			Innermost = (Innermost > 0) ? FMath::Min(Innermost, LaneIndex) : LaneIndex;
		}
		else if (LaneIndex < 0)
		{
			Innermost = (Innermost < 0) ? FMath::Max(Innermost, LaneIndex) : LaneIndex;
		}
	}
	return Innermost;
}

static int32 ResolveOutermostLaneIndex(const TArray<int32>& Lanes)
{
	if (Lanes.Num() == 0)
	{
		return LANE_INDEX_NONE;
	}

	int32 Outermost = Lanes[0];
	for (int32 LaneIndex : Lanes)
	{
		if (LaneIndex > 0)
		{
			Outermost = FMath::Max(Outermost, LaneIndex);
		}
		else if (LaneIndex < 0)
		{
			Outermost = FMath::Min(Outermost, LaneIndex);
		}
	}
	return Outermost;
}

bool MetaLandRoad::ComputeAnchorMouthRailFrame(
	const UMetaLandRoadSplineComponent* SplineComp,
	const FMetaLandRoadDrawAnchor& Anchor,
	FVector& OutTipLeft,
	FVector& OutTipRight,
	FVector& OutTipCenter,
	FVector& OutOutwardTangent)
{
	OutTipLeft = OutTipRight = OutTipCenter = OutOutwardTangent = FVector::ZeroVector;
	if (!SplineComp || !Anchor.IsValid() || Anchor.SourceSpline.Get() != SplineComp)
	{
		return false;
	}

	if (ResolveLaneSectionIndexForSpan(SplineComp, Anchor.SectionIndex, Anchor.LaneIndex) == INDEX_NONE)
	{
		return false;
	}

	TArray<int32> Lanes;
	CollectAnchorLaneIndices(Anchor, Lanes);

	FMetaLandRoadDrawAnchor VisibleAnchor = Anchor;
	VisibleAnchor.AnchorS = ClampMouthAnchorSToVisibleMesh(SplineComp, Anchor.AnchorS, Anchor.MouthSide);
	const double S = VisibleAnchor.AnchorS;

	if (!ResolveMouthOutwardTangent(SplineComp, VisibleAnchor, OutOutwardTangent))
	{
		return false;
	}

	int32 LeftOuterLane = LANE_INDEX_NONE;
	int32 RightOuterLane = LANE_INDEX_NONE;
	for (const int32 LaneIndex : Lanes)
	{
		if (LaneIndex > 0)
		{
			RightOuterLane = (RightOuterLane == LANE_INDEX_NONE)
				? LaneIndex
				: FMath::Max(RightOuterLane, LaneIndex);
		}
		else if (LaneIndex < 0)
		{
			LeftOuterLane = (LeftOuterLane == LANE_INDEX_NONE)
				? LaneIndex
				: FMath::Min(LeftOuterLane, LaneIndex);
		}
	}

	FVector EdgeA;
	FVector EdgeB;
	if (LeftOuterLane != LANE_INDEX_NONE && RightOuterLane != LANE_INDEX_NONE)
	{
		const int32 LeftSec = ResolveLaneSectionIndexForSpan(SplineComp, Anchor.SectionIndex, LeftOuterLane);
		const int32 RightSec = ResolveLaneSectionIndexForSpan(SplineComp, Anchor.SectionIndex, RightOuterLane);
		if (LeftSec == INDEX_NONE || RightSec == INDEX_NONE)
		{
			return false;
		}
		EdgeA = SplineComp->EvalLanePosition(
			LeftSec, LeftOuterLane, S, 1.0, ESplineCoordinateSpace::World);
		EdgeB = SplineComp->EvalLanePosition(
			RightSec, RightOuterLane, S, 1.0, ESplineCoordinateSpace::World);
	}
	else
	{
		const int32 InnermostLane = ResolveInnermostLaneIndex(Lanes);
		const int32 OutermostLane = ResolveOutermostLaneIndex(Lanes);
		if (InnermostLane == LANE_INDEX_NONE || OutermostLane == LANE_INDEX_NONE)
		{
			return false;
		}
		const int32 InnerSec = ResolveLaneSectionIndexForSpan(SplineComp, Anchor.SectionIndex, InnermostLane);
		const int32 OuterSec = ResolveLaneSectionIndexForSpan(SplineComp, Anchor.SectionIndex, OutermostLane);
		if (InnerSec == INDEX_NONE || OuterSec == INDEX_NONE)
		{
			return false;
		}
		EdgeA = SplineComp->EvalLanePosition(
			InnerSec, InnermostLane, S, 0.0, ESplineCoordinateSpace::World);
		EdgeB = SplineComp->EvalLanePosition(
			OuterSec, OutermostLane, S, 1.0, ESplineCoordinateSpace::World);
	}

	OutTipCenter = (EdgeA + EdgeB) * 0.5;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, OutOutwardTangent).GetSafeNormal2D();
	if (FVector::DotProduct(EdgeA - OutTipCenter, Right) <= 0.0)
	{
		OutTipLeft = EdgeA;
		OutTipRight = EdgeB;
	}
	else
	{
		OutTipLeft = EdgeB;
		OutTipRight = EdgeA;
	}

	return true;
}
