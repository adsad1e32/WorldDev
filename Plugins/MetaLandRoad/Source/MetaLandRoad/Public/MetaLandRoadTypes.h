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

	/** World-cm per asphalt UV tile (T_Asphalt_D). Continuous along/across spline — not MetaRoad lane-mark UVs. */
	constexpr float DefaultAsphaltUvTileCm = 400.0f;

	/**
	 * MetaRoad mesh subdivision (UMetaRoadSettings defaults):
	 * - BuildLinearApproximation: uniform steps in spline **input-key** space from S0 to S1
	 * - ReparamStepsPerSegment = 20, MinNumStepsPerSection = 20
	 * - Each lane = quad strip between inner/outer offset curves (EvalLaneROffset + EvalROffset)
	 * Not used: ConvertSplineToPolyLine arc-length sampling, angle-based corner insert, fixed HalfWidth ribbon.
	 */
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
	float SplineParam = 0.0f;

	FMetaLandRoadSplinePositionLinearApproximation() = default;

	FMetaLandRoadSplinePositionLinearApproximation(const FVector& InPosition, float InSplineParam)
		: Position(InPosition)
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

	const struct FMetaLandRoadLaneSection& GetStartSection() const;
	struct FMetaLandRoadLaneSection& GetStartSection();
	const struct FMetaLandRoadLaneSection& GetEndSection() const;
	struct FMetaLandRoadLaneSection& GetEndSection();

	double GetStartOffset() const;
	double GetEndOffset() const { return SOffsetEnd_Cached; }

	void Trim(bool bFitWidth);

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

	bool IsSectionValid() const;
	const FMetaLandRoadLane& GetLaneByIndex(int LaneIndex) const;
	FMetaLandRoadLane& GetLaneByIndex(int LaneIndex);
	bool CheckLaneIndex(int LaneIndex) const;
	void Trim(bool bFitWidth);

	double EvalLaneROffset(int LaneIndex, double InSOffset, double Alpha = 1.0) const;

	bool operator<(const FMetaLandRoadLaneSection& Other) const { return SOffset < Other.SOffset; }
	bool IsSectionEmpty() const { return Left.IsEmpty() && Right.IsEmpty(); }

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

	TPair<FMetaLandRoadLaneSection&, FMetaLandRoadLaneSection&> GetLeftRightSections(int SectionIndex);
	TPair<const FMetaLandRoadLaneSection&, const FMetaLandRoadLaneSection&> GetLeftRightSections(int SectionIndex) const;

	TPair<TArray<FMetaLandRoadLane>&, TArray<FMetaLandRoadLane>&> GetLeftRightLanes(int SectionIndex);
	TPair<const TArray<FMetaLandRoadLane>&, const TArray<FMetaLandRoadLane>&> GetLeftRightLanes(int SectionIndex) const;

	uint64 GetLayoutVersion() const { return LayoutVersion; }
	uint64 GetAttributesVersion() const { return AttributesVersion; }
	void UpdateAttributesVersion() { ++AttributesVersion; }
	void UpdateLayoutVersion() { ++LayoutVersion; }

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
