#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "StructUtils/InstancedStruct.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadTypes.generated.h"

enum { METALANDROAD_LANE_INDEX_NONE = 0 };
enum { LANE_INDEX_NONE = METALANDROAD_LANE_INDEX_NONE };

namespace MetaLandRoad
{
	constexpr double DefaultRoadLaneWidth = 512.0;

	/**
	 * World-cm per asphalt UV tile (T_Asphalt_D). Continuous along/across spline — not MetaRoad lane-mark UVs.
	 * Was 400 (repeats every 4m → barcode stripes on thin road edges); 1500 ≈ 15m per tile.
	 */
	constexpr float DefaultAsphaltUvTileCm = 1500.0f;

	/**
	 * Fallback mesh step when layout half-width is unknown.
	 * Live loft uses MeshRibbonStepCm(HalfW, R) — not this constant.
	 */
	constexpr double DefaultMeshSampleCm = 90.0;
	/** One-way reserved along-length vs half-width. 1.75 left 90° outer corners short. */
	constexpr double CornerFilletRadiusScale = 2.6;
	/** Mesh fillet ignores turns below this. Guidance step stays under half of it. */
	constexpr double CornerFilletSkipAngleRad = 18.0 * PI / 180.0;
	/** Explode guard only — step count still comes from length / chord-step. */
	constexpr int32 LeaveArriveSampleSegCap = 256;
	/** Preview rails only. A-bake writes authoring keys, not this sample cap. */
	constexpr int32 LeaveArrivePreviewSegCap = 64;

	/**
	 * Polyline step from live R and half-width.
	 * Sag ε = 0.08×HalfW (chord stays inside inner keep).
	 * Sweep ≤ half the fillet-skip so baked Linear keys are not rewritten as body fillets.
	 */
	inline double LeaveArriveChordStepCm(double RadiusCm, double HalfWidthCm)
	{
		const double HalfW = FMath::Max(HalfWidthCm, 1.0);
		const double R = FMath::Max(FMath::Max(RadiusCm, HalfW), 1.0);
		const double StepSweep = R * (CornerFilletSkipAngleRad * 0.5);
		const double StepSag = FMath::Sqrt(8.0 * (HalfW * 0.08) * R);
		return FMath::Max(FMath::Min(StepSweep, StepSag), 1.0);
	}

	/**
	 * Road-strip ring spacing from width + local centerline radius.
	 * Straight: ~0.45×HalfW. Curve: chord sag ≤ 0.08×HalfW (same as LeaveArrive).
	 * Floor 0.15×HalfW so a tight bend cannot explode rings.
	 */
	inline double MeshRibbonStepCm(double HalfWidthCm, double RadiusCm)
	{
		const double HalfW = FMath::Max(HalfWidthCm, 50.0);
		const double StraightCm = HalfW * 0.45;
		if (RadiusCm <= KINDA_SMALL_NUMBER || !FMath::IsFinite(RadiusCm))
		{
			return StraightCm;
		}
		const double CurveCm = LeaveArriveChordStepCm(RadiusCm, HalfW);
		return FMath::Clamp(CurveCm, HalfW * 0.15, StraightCm);
	}

	inline int32 LeaveArriveSampleSegs(double LengthCm, double StepCm)
	{
		const int32 Need = FMath::Max(
			2,
			FMath::CeilToInt(FMath::Max(LengthCm, 1.0) / FMath::Max(StepCm, 1.0)));
		return FMath::Min(Need, LeaveArriveSampleSegCap);
	}

	/** Centerline radius from live half-width and turn angle. Inner keep = HalfW * tan(θ/2) * (1+sinθ). */
	inline double CornerFilletRadiusCm(double HalfWidthCm, double TurnAngleRad)
	{
		const double HalfW = FMath::Max(HalfWidthCm, 1.0);
		const double Angle = FMath::Clamp(TurnAngleRad, 0.0, PI);
		const double TanHalf = FMath::Tan(FMath::Clamp(Angle * 0.5, 0.0, 1.15));
		const double Spread = 1.0 + FMath::Sin(Angle);
		return HalfW * (1.0 + TanHalf) * Spread;
	}

	inline double CornerFilletNeedAlongCm(double HalfWidthCm, double TurnAngleRad)
	{
		const double TanHalf = FMath::Tan(FMath::Clamp(TurnAngleRad * 0.5, 0.0, 1.15));
		return CornerFilletRadiusCm(HalfWidthCm, TurnAngleRad) * TanHalf;
	}

	/** Floor: centerline R = HalfW so inner R ≥ 0. Shorter → rails cross. */
	inline double CornerFilletNeedAlongMinCm(double HalfWidthCm, double TurnAngleRad)
	{
		const double TanHalf = FMath::Max(
			FMath::Tan(FMath::Clamp(TurnAngleRad * 0.5, 0.0, 1.15)),
			0.08);
		return FMath::Max(HalfWidthCm, 1.0) * TanHalf;
	}

	/**
	 * Width + angle → along-leg. Target = NeedAlongCm; floor = NeedAlongMinCm.
	 * Use nearly the whole shorter leg (not 45%). MouthBudget < 0 = ignore.
	 */
	inline double CornerFilletNeedAlongFitCm(
		double HalfWidthCm,
		double TurnAngleRad,
		double NeighborMinCm,
		double MouthBudgetCm = -1.0)
	{
		const double NeedStd = CornerFilletNeedAlongCm(HalfWidthCm, TurnAngleRad);
		const double NeedMin = CornerFilletNeedAlongMinCm(HalfWidthCm, TurnAngleRad);
		double FitMax = FMath::Max(NeighborMinCm - 8.0, 1.0);
		if (MouthBudgetCm > 1.0)
		{
			FitMax = FMath::Min(FitMax, MouthBudgetCm);
		}
		if (FitMax >= NeedMin)
		{
			return FMath::Clamp(NeedStd, NeedMin, FitMax);
		}
		return FitMax;
	}

	/** Path turn: leave→arrive, or leave→chord→arrive if the chord bends more. */
	inline double CornerTurnAngleRad(
		const FVector& LeaveDir,
		const FVector& ArriveDir,
		const FVector& ChordDir)
	{
		auto Ang = [](const FVector& A, const FVector& B) -> double
		{
			const FVector X = A.GetSafeNormal2D();
			const FVector Y = B.GetSafeNormal2D();
			if (X.IsNearlyZero() || Y.IsNearlyZero())
			{
				return 0.0;
			}
			return FMath::Acos(FMath::Clamp(FVector::DotProduct(X, Y), -1.0, 1.0));
		};
		return FMath::Clamp(
			FMath::Max(Ang(LeaveDir, ArriveDir), Ang(LeaveDir, ChordDir) + Ang(ChordDir, ArriveDir)),
			0.0,
			PI);
	}

	/**
	 * No-overlap radius, then grow if the chord is shorter than 2×NeedAlong
	 * (Fig3: diffuse first, fuse to target after the inner stays clear).
	 */
	inline double CornerDiffuseRadiusCm(
		double HalfWidthCm,
		double TurnAngleRad,
		double ChordLenCm)
	{
		const double HalfW = FMath::Max(HalfWidthCm, 1.0);
		const double ClearR = CornerFilletRadiusCm(HalfW, TurnAngleRad);
		const double Need = CornerFilletNeedAlongCm(HalfW, TurnAngleRad);
		const double Chord = FMath::Max(ChordLenCm, 1.0);
		if (Chord >= Need * 2.0)
		{
			return ClearR;
		}
		return ClearR * FMath::Clamp((2.0 * Need) / Chord, 1.0, 3.4);
	}

	/** Share of the fillet window that stays Hermite before mixing into the circle. */
	inline double CornerFilletLeadFrac(double TurnAngleRad)
	{
		return 0.28 + 0.16 * FMath::Sin(FMath::Clamp(TurnAngleRad, 0.0, PI));
	}

	/** Cursor must sit on the mouth bar, not a world-radius around it. */
	inline double MouthAdsorbAcceptCm(double MouthWidthCm)
	{
		return FMath::Max(MouthWidthCm, 1.0) * 0.38;
	}
	/**
	 * Shorter leftover spans are hub slivers (through-arm Approach/Depart not fully
	 * merged) or postage-stamp tips. Owner: GetJunctionMeshGenerationSpans.
	 */
	constexpr double MinKeepRoadSpanCm = 90.0;
	/** Blend loft Right to the mouth bar on the last ring only. 80cm twisted the tip into the hub. */
	constexpr double MouthRightAlignCm = 16.0;
	/** S0/S1 counts as CutS for mouth-align. Wide windows treated the whole C-bend as a mouth. */
	constexpr double CutSAlignWindowCm = 8.0;
	constexpr int32 DefaultReparamStepsPerSegment = 20;
	constexpr int32 DefaultMinNumStepsPerSection = 20;
	/** Alt+T editor overlay — denser than mesh so width keys / curves match face fill and boundary lines. */
	constexpr int32 LaneEditReparamStepsPerSegment = 40;
	constexpr int32 LaneEditMinNumStepsPerSection = 32;

	METALANDROAD_API void TrimCurveInRang(FRichCurve& Curve, double Time0, double Time1, bool bFitBorders);
	METALANDROAD_API void EnsureLaneWidthCurveCubic(FRichCurve& Width);
	METALANDROAD_API FQuat InvertRotation(const FQuat& Quat);

	FORCEINLINE int LaneIndex2SeqIndex(int LeftNum, int LaneIndex)
	{
		check(LaneIndex != 0);
		return LaneIndex + LeftNum + (LaneIndex > 0 ? -1 : 0);
	}

	FORCEINLINE int SeqIndex2LaneIndex(int LeftNum, int SeqIndex)
	{
		check(SeqIndex >= 0);
		return SeqIndex - LeftNum + (SeqIndex >= LeftNum ? +1 : 0);
	}
}

class AMoKuEditSplineActor;
class UMetaLandRoadSplineComponent;
class UMetaLandRoadConnection;
class UMetaLandRoadLaneConnection;
struct FMetaLandRoadLayout;

/** Sample point along a spline with lateral offset applied (MetaRoad-compatible). */
struct METALANDROAD_API FMetaLandRoadSplinePositionLinearApproximation
{
	FVector Position = FVector::ZeroVector;
	FVector PathCenter = FVector::ZeroVector;
	float SplineParam = 0.0f;

	FMetaLandRoadSplinePositionLinearApproximation() = default;

	FMetaLandRoadSplinePositionLinearApproximation(const FVector& InPosition, float InSplineParam)
		: Position(InPosition)
		, PathCenter(InPosition)
		, SplineParam(InSplineParam)
	{
	}
};

UENUM(BlueprintType)
enum class EMetaLandRoadDriveableLaneType : uint8
{
	None,
	Driving,
	Shoulder,
	Border,
	Stop,
	Biking,
	Restricted,
	Parking,
	Median,
	RoadWorks,
	Tram,
	Sidewalk,
};

UENUM(BlueprintType)
enum class EMetaLandRoadLaneDirection : uint8
{
	Default,
	Invert,
};

UENUM(BlueprintType)
enum class EMetaLandRoadLaneSectionSide : uint8
{
	Both,
	Left,
	Right
};

namespace MetaLandRoad
{
	METALANDROAD_API bool HasLeftSide(EMetaLandRoadLaneSectionSide Side);
	METALANDROAD_API bool HasRightSide(EMetaLandRoadLaneSectionSide Side);
	METALANDROAD_API float DistancePointToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B);
	METALANDROAD_API double ResolveSectionEndS(const UMetaLandRoadSplineComponent* SplineComp, int32 SectionIndex);
	/** Previous boundary S on the same face track (Left/Right splits do not block each other). */
	METALANDROAD_API double FindPrevDistinctSectionBoundaryS(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		double SameBoundaryTolerance = 0.1);
	/** Next boundary S on the same face track (Left/Right splits do not block each other). */
	METALANDROAD_API double FindNextDistinctSectionBoundaryS(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		double SameBoundaryTolerance = 0.1);
	/** Clamp a boundary S to spline length and adjacent distinct boundaries (Alt+E drag). */
	METALANDROAD_API double ClampSectionBoundaryS(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SectionIndex,
		double CandidateS,
		double MinSectionLength = 10.0,
		double SameBoundaryTolerance = 0.1);
}

UENUM(BlueprintType)
enum class EMetaLandRoadDirection : uint8
{
	LeftHand,
	RightHand
};

/** Which end of a lane span the user picked (路尾 / 闸口 / 路口 arm 统一). */
UENUM(BlueprintType)
enum class EMetaLandRoadMouthSide : uint8
{
	Start,
	End,
};

/** Draw finish policy (P1+). */
UENUM(BlueprintType)
enum class EMetaLandRoadDrawFinishMode : uint8
{
	Direct,
	Blend,
	Junction,
};

/** EndConnection width mix: how far back from the tip to lerp toward the target lane mouth width. */
UENUM(BlueprintType)
enum class EMetaLandRoadEndWidthBlendSpan : uint8
{
	/** Smooth over the last 2 spline spans (tip ← previous ← previous-1). Default. */
	LastTwoSegments UMETA(DisplayName = "Last 2 Segments"),
	/** Smooth over the last 3 spline spans. */
	LastThreeSegments UMETA(DisplayName = "Last 3 Segments"),
	/** Lerp width from road start to tip (entire extend path). */
	EntireRoad UMETA(DisplayName = "Entire Road"),
};

/** User lane-pick anchor for continue-draw / connect. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadDrawAnchor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	TWeakObjectPtr<UMetaLandRoadSplineComponent> SourceSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	int32 SectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	int32 LaneIndex = METALANDROAD_LANE_INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	double AnchorS = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	EMetaLandRoadMouthSide MouthSide = EMetaLandRoadMouthSide::End;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	TArray<int32> BundledLaneIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	bool bFromSuccessor = true;

	bool IsValid() const;
};

/** World Builder edit context — decoupled from UE actor selection. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadEditTarget
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	TWeakObjectPtr<AMoKuEditSplineActor> RoadActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	TWeakObjectPtr<UMetaLandRoadSplineComponent> Spline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	FMetaLandRoadDrawAnchor PendingAnchor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	bool bHasPendingAnchor = false;
};

METALANDROAD_API bool MetaLandRoadBuildDrawAnchorFromLaneConnection(
	const UMetaLandRoadLaneConnection& LaneConnection,
	FMetaLandRoadDrawAnchor& OutAnchor);

METALANDROAD_API void MetaLandRoadLogDrawAnchor(const TCHAR* Context, const FMetaLandRoadDrawAnchor& Anchor);

/** Unified viewport edit mode for FMoKuEditSplinesComponentVisualizer (mutually exclusive). */
UENUM(BlueprintType)
enum class EMetaLandRoadEditorVisMode : uint8
{
	/** Default: road mesh only, no section or native spline edit overlay. */
	Standard,
	/** Alt+W: native control-point path + rings + translate gizmo. */
	NativeEdit,
	/** Alt+E: section center lines, boundary hover, RMB Add Section. */
	Section,
	/** Reserved: connection editing. */
	Connection,
	/** Alt+T: per-lane boundary edit (no main center line). */
	Lane,
	/** Reserved: pin attribute types and edit keys along S. */
	Attribute,
};

/** Interactive draw tool: detected junction shape (MetaLandRoad editor mode only). */
UENUM(BlueprintType)
enum class EMetaLandRoadJunctionTopology : uint8
{
	Unknown,
	T,
	Cross,
	MultiWay,
};

/**
 * Which flank of a through-road CS participates in the junction cover.
 * T-junction: Left OR Right from interaction / stub side (spline lane sense).
 * Cross: Both.
 */
UENUM(BlueprintType)
enum class EMetaLandRoadJunctionConnectSide : uint8
{
	Both UMETA(DisplayName = "Both"),
	Left UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right"),
};

/** One road arm attached to a junction node inside the interactive editor mode session. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadJunctionArmRef
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	TWeakObjectPtr<UMetaLandRoadSplineComponent> Spline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double IntersectDistanceS = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double CutDistanceS = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double DepartCutS = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double ForwardIndent = 600.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	bool bThroughArm = false;

	/** T through-arm: only this half of Left→Center→Right CS is meshed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	EMetaLandRoadJunctionConnectSide ConnectSide = EMetaLandRoadJunctionConnectSide::Both;

	/**
	 * Lane indices on this arm that connect into the junction (from per-lane ConnectTo).
	 * Empty = full road mouth. Non-empty proper subset = 2-lane extend opens only that width.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	TArray<int32> ConnectedLaneIndices;
};

/** Persisted junction node (UMetaLandRoadJunctionComponent on AMoKuEditIntersectionActor). */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadJunctionNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	FGuid Id;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	FVector AnchorWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	TArray<FMetaLandRoadJunctionArmRef> Arms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	EMetaLandRoadJunctionTopology Topology = EMetaLandRoadJunctionTopology::Unknown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double ForwardIndent = 600.0;

	/**
	 * Multiplier on auto CAD fillet indent (cut farther from intersect = larger hub).
	 * Indent scale on corner clear. 1.15 default; cook clamps to [0.9, 1.5].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Junction, meta = (ClampMin = "0.9", ClampMax = "1.5", UIMin = "0.9", UIMax = "1.5"))
	float CutExpandScale = 1.15f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	int32 LayoutVersion = 0;
};

USTRUCT(BlueprintType, Blueprintable)
struct METALANDROAD_API FMetaLandRoadLaneInstance
{
	GENERATED_BODY()

	virtual ~FMetaLandRoadLaneInstance() = default;
};

USTRUCT(BlueprintType, Blueprintable)
struct METALANDROAD_API FMetaLandRoadLaneDriving : public FMetaLandRoadLaneInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	EMetaLandRoadDriveableLaneType DriveableLaneType = EMetaLandRoadDriveableLaneType::Driving;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	FName MaterialProfile = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bInvertUV0 = false;
};

USTRUCT(BlueprintType, Blueprintable)
struct METALANDROAD_API FMetaLandRoadLaneSidewalk : public FMetaLandRoadLaneInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bIsSoftBorder = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bInsideCurb = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bOutsideCurb = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bBeginCurb = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane, meta = (EditCondition = "bBeginCurb", EditConditionHides))
	FRuntimeFloatCurve BeginCapCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bEndCurb = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane, meta = (EditCondition = "bEndCurb", EditConditionHides))
	FRuntimeFloatCurve EndCapCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	FName MaterialProfile = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	FName CurbProfile = TEXT("Default");
};

USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadPosition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road)
	FQuat Quat = FQuat::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road)
	double SOffset = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road)
	double ROffset = 0.0;
};

USTRUCT(BlueprintType, Blueprintable)
struct METALANDROAD_API FMetaLandRoadLane
{
	GENERATED_BODY()

	friend FMetaLandRoadLayout;

	FMetaLandRoadLane();

	UPROPERTY(EditAnywhere, NoClear, BlueprintReadWrite, Category = RoadLane, Export)
	TInstancedStruct<FMetaLandRoadLaneInstance> LaneInstance;

	UPROPERTY(EditAnywhere, NoClear, BlueprintReadWrite, Category = RoadLane)
	TMap<FName, FMetaLandRoadLaneAttribute> Attributes;

	UPROPERTY(EditAnywhere, Category = RoadLane)
	FRichCurve Width;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	EMetaLandRoadLaneDirection Direction = EMetaLandRoadLaneDirection::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	bool bSkipProcedureGeneration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	TMap<FString, FString> StringTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	TMap<FString, double> FloatTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadLane)
	FString Label;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = RoadLane)
	TObjectPtr<UMetaLandRoadLaneConnection> PredecessorConnection;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = RoadLane)
	TObjectPtr<UMetaLandRoadLaneConnection> SuccessorConnection;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = RoadLane)
	double SOffsetEnd_Cached = 0.0;

	bool IsLaneValid() const;
	bool IsForwardLane() const;

	int GetStartSectionIndex() const { return StartSectionIndex; }
	int GetEndSectionIndex() const { return EndSectionIndex; }
	int GetLaneIndex() const { return LaneIndex; }

	double GetStartOffset() const;
	double GetEndOffset() const { return SOffsetEnd_Cached; }

	void Trim(bool bFitWidth);

	FMetaLandRoadLaneAttribute* PinAttribute(FName TypeId);
	bool UnpinAttribute(FName TypeId);
	FMetaLandRoadLaneAttribute* FindAttribute(FName TypeId);
	const FMetaLandRoadLaneAttribute* FindAttribute(FName TypeId) const;
	bool EvalAttributeAt(FName TypeId, double SOffset, TInstancedStruct<FMetaLandRoadLaneAttributeValue>& OutValue) const;

private:
	TWeakPtr<FMetaLandRoadLayout*> OwnedRoadLayout;

	UPROPERTY(VisibleAnywhere, Category = RoadLane, Transient)
	int StartSectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = RoadLane, Transient)
	int EndSectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = RoadLane, Transient)
	int LaneIndex = METALANDROAD_LANE_INDEX_NONE;
};

/** Profile clone for branch road spawn (P1). Declared after FMetaLandRoadLane for UHT. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadDrawSeed
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	FMetaLandRoadDrawAnchor Anchor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneAnchor)
	TArray<FMetaLandRoadLane> ProfileLanes;
};

USTRUCT(BlueprintType, Blueprintable)
struct METALANDROAD_API FMetaLandRoadLaneSection
{
	GENERATED_BODY()

	friend FMetaLandRoadLayout;

	FMetaLandRoadLaneSection();
	~FMetaLandRoadLaneSection();

	FMetaLandRoadLaneSection(FMetaLandRoadLaneSection&& Other) noexcept;
	FMetaLandRoadLaneSection& operator=(FMetaLandRoadLaneSection&& Other) noexcept;
	FMetaLandRoadLaneSection(const FMetaLandRoadLaneSection& Other) noexcept;
	FMetaLandRoadLaneSection& operator=(const FMetaLandRoadLaneSection& Other) noexcept;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneSection)
	EMetaLandRoadLaneSectionSide Side = EMetaLandRoadLaneSectionSide::Both;

	UPROPERTY(EditAnywhere, NoClear, BlueprintReadWrite, Export, Category = LaneSection)
	TArray<FMetaLandRoadLane> Left;

	UPROPERTY(EditAnywhere, NoClear, BlueprintReadWrite, Export, Category = LaneSection)
	TArray<FMetaLandRoadLane> Right;

	UPROPERTY(EditAnywhere, NoClear, BlueprintReadWrite, Category = LaneSection)
	TMap<FName, FMetaLandRoadLaneAttribute> Attributes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneSection)
	double SOffset = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = LaneSection)
	double SOffsetEnd_Cached = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneSection)
	FString Label;

	const FMetaLandRoadLane& GetLaneByIndex(int LaneIndex) const;
	FMetaLandRoadLane& GetLaneByIndex(int LaneIndex);
	bool CheckLaneIndex(int LaneIndex) const;
	void Trim(bool bFitWidth);

	FMetaLandRoadLaneAttribute* PinAttribute(FName TypeId);
	bool UnpinAttribute(FName TypeId);
	FMetaLandRoadLaneAttribute* FindAttribute(FName TypeId);
	const FMetaLandRoadLaneAttribute* FindAttribute(FName TypeId) const;
	bool EvalAttributeAt(FName TypeId, double InSOffset, TInstancedStruct<FMetaLandRoadLaneAttributeValue>& OutValue) const;
	bool EvalLandscapeAt(double InSOffset, FMetaLandRoadLandscapeAttributeValue& OutValue) const;

	double EvalLaneROffset(int LaneIndex, double InSOffset, double Alpha = 1.0) const;

	bool operator<(const FMetaLandRoadLaneSection& Other) const { return SOffset < Other.SOffset; }

private:
	TWeakPtr<FMetaLandRoadLayout*> OwnedRoadLayout;

	UPROPERTY(VisibleAnywhere, Category = LaneSection, Transient)
	int SectionIndex = INDEX_NONE;
};

USTRUCT(BlueprintType, Blueprintable)
struct METALANDROAD_API FMetaLandRoadLayout
{
	GENERATED_BODY()

	FMetaLandRoadLayout();
	FMetaLandRoadLayout(FMetaLandRoadLayout&& Other) noexcept;
	FMetaLandRoadLayout& operator=(FMetaLandRoadLayout&& Other) noexcept;
	FMetaLandRoadLayout(const FMetaLandRoadLayout& Other) noexcept;
	FMetaLandRoadLayout& operator=(const FMetaLandRoadLayout& Other) noexcept;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RoadLayout)
	TArray<FMetaLandRoadLaneSection> Sections;

	UPROPERTY(EditAnywhere, Category = RoadLayout)
	FRichCurve ROffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RoadLayout)
	EMetaLandRoadDirection Direction = EMetaLandRoadDirection::LeftHand;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FilledInstance, meta = (DisplayName = "Instance"))
	TInstancedStruct<FMetaLandRoadLaneInstance> FilledInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FilledInstance)
	double FilledInstanceTexAngle = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FilledInstance)
	double FilledInstanceTexScale = 1.0;

	void UpdateLayout(UMetaLandRoadSplineComponent* OwnedRoadSpline);
	void UpdateBounds(double SplineLength);
	void TrimSections(double SplineLength, double Tolerance, UMetaLandRoadSplineComponent* OwnedRoadSpline);

	double EvalROffset(double S) const;
	int FindSideSection(int SectionIndex, EMetaLandRoadLaneSectionSide Side) const;
	int FindSectionAtSOffset(double S) const;
	/** Host at S, then the Left/Right section that owns LaneIndex (CutS L|R pair). */
	int FindSectionForLaneAtS(double S, int LaneIndex) const;

	uint64 GetLayoutVersion() const { return LayoutVersion; }
	uint64 GetAttributesVersion() const { return AttributesVersion; }
	void UpdateAttributesVersion() { ++AttributesVersion; }
	void UpdateLayoutVersion() { ++LayoutVersion; }

	/** Span section → lane-data section (side-aware). Layout query — not viewport pick. */
	int32 ResolveLaneSectionIndex(int32 SpanSectionIndex, int32 LaneIndex) const;

private:
	TSharedPtr<FMetaLandRoadLayout*> ThisShared;
	uint64 LayoutVersion = 0;
	uint64 AttributesVersion = 0;
};

/**
 * Road-level connection at the start or end of UMetaLandRoadSplineComponent.
 */
UCLASS()
class METALANDROAD_API UMetaLandRoadConnection : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = RoadConnection)
	TSoftObjectPtr<UMetaLandRoadLaneConnection> OuterLaneConnection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RoadConnection)
	FString Label;

	UPROPERTY()
	FGuid LaneConnectionGuid;

	UMetaLandRoadSplineComponent* GetOwnedRoadSpline() const;
	bool IsConnectionValid() const;
	bool IsConnected() const;
	bool IsSuccessorConnection() const;
	bool IsPredecessorConnection() const;
	void Disconnect();
	bool CanConnectTo(const UMetaLandRoadLaneConnection* OtherLaneConnection) const;
	void ConnectTo(UMetaLandRoadLaneConnection* OtherLaneConnection);
};

/**
 * Lane-level connection at the start or end of each FMetaLandRoadLane.
 */
UCLASS(BlueprintType)
class METALANDROAD_API UMetaLandRoadLaneConnection : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient, DuplicateTransient)
	TSet<TWeakObjectPtr<UMetaLandRoadConnection>> OuterRoadConnections;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LaneConnection)
	FString Label;

	UPROPERTY(Transient)
	FTransform CachedTransform;

	UPROPERTY()
	FGuid Guid;

	UPROPERTY(VisibleAnywhere, Category = LaneConnection, Transient)
	TWeakObjectPtr<UMetaLandRoadSplineComponent> OwnedRoadSpline;

	UPROPERTY(VisibleAnywhere, Category = LaneConnection, Transient)
	int SectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = LaneConnection, Transient)
	int LaneIndex = METALANDROAD_LANE_INDEX_NONE;

	const FMetaLandRoadLane& GetOwnedRoadLane() const;
	FMetaLandRoadLane& GetOwnedRoadLane();
	bool IsConnectionValid() const;
	bool IsConnected() const;
	void DisconnectAll();
	FTransform EvalTransform() const;
};

namespace MetaLandRoad
{
	METALANDROAD_API int32 ResolveLaneSectionIndexForSpan(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SpanSectionIndex,
		int32 LaneIndex);
	METALANDROAD_API bool IsLaneValidOnSpanSection(
		const UMetaLandRoadSplineComponent* SplineComp,
		int32 SpanSectionIndex,
		int32 LaneIndex);
	METALANDROAD_API double ClampMouthAnchorSToVisibleMesh(
		const UMetaLandRoadSplineComponent* SplineComp,
		double PreferredAnchorS,
		EMetaLandRoadMouthSide MouthSide);
	/** Remaining-road span tips (CutS / free ends). EndSnap / overlay / mix. Empty spans → 0 and Length. */
	METALANDROAD_API void CollectVisibleMouthStations(
		const UMetaLandRoadSplineComponent* SplineComp,
		TArray<TPair<double, EMetaLandRoadMouthSide>>& OutStations);
	/**
	 * Free-end-only stations (0 / Length if that side still has mesh).
	 * EndSnap pick uses CollectVisibleMouthStations — free-end-only wrapped to the far mouth.
	 */
	METALANDROAD_API void CollectEndSnapMouthStations(
		const UMetaLandRoadSplineComponent* SplineComp,
		TArray<TPair<double, EMetaLandRoadMouthSide>>& OutStations);
	METALANDROAD_API bool ResolveMouthOutwardTangent(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadDrawAnchor& Anchor,
		FVector& OutOutwardTangent);
	METALANDROAD_API bool ComputeAnchorMouthRailFrame(
		const UMetaLandRoadSplineComponent* SplineComp,
		const FMetaLandRoadDrawAnchor& Anchor,
		FVector& OutTipLeft,
		FVector& OutTipRight,
		FVector& OutTipCenter,
		FVector& OutOutwardTangent);
}
