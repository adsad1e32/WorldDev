#pragma once

#include "CoreMinimal.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadInteractiveTool.h"
#include "Components/SplineComponent.h"
#include "Engine/EngineTypes.h"

class UMetaLandRoadInteractiveTool;
class UMetaLandRoadEditorMode;
class UMetaLandRoadSplineComponent;
class UMoKuEditSplinesComponent;
class AMoKuEditSplineActor;
class AActor;
class UWorld;
class FPrimitiveDrawInterface;
struct FMetaLandRoadDrawAnchor;
struct FMetaLandRoadEditTarget;
struct FHitResult;
struct FCollisionQueryParams;

struct FEndSnapTargetLanePick
{
	UMetaLandRoadSplineComponent* Spline = nullptr;
	int32 Lane = METALANDROAD_LANE_INDEX_NONE;
	int32 Section = INDEX_NONE;
	EMetaLandRoadMouthSide Mouth = EMetaLandRoadMouthSide::End;
	double MouthS = 0.0;

	bool IsValid() const
	{
		return Spline && Lane != METALANDROAD_LANE_INDEX_NONE;
	}
};

struct FLaneExtendPointFreeze
{
	FVector WorldLocation = FVector::ZeroVector;
	FVector ArriveWorld = FVector::ZeroVector;
	FVector LeaveWorld = FVector::ZeroVector;
	TEnumAsByte<ESplinePointType::Type> PointType = ESplinePointType::Curve;
	double DistanceS = 0.0;
};

inline constexpr float GuidanceMeshSurfaceZOffset = 2.0f;
inline constexpr float MinParallelGuidanceExtendCm = 150.0f;
inline constexpr float GuidanceSampleStepCm = 80.0f;
inline constexpr float GuidancePDILiftCm = 2.0f;
inline constexpr float GuidanceLineWidth = 28.0f;
inline constexpr int32 MaxGuidanceBakeMeshKeys = 5;

extern const FLinearColor GuidanceLeftRailColor;
extern const FLinearColor GuidanceRightRailColor;
extern const FLinearColor GuidanceErrorRailColor;
extern const FName MetaLandRoadLaneBranchTag;

UMetaLandRoadEditorMode* ResolveMetaLandRoadEditorMode(const UMetaLandRoadInteractiveTool* Tool);

int32 CountDrawAnchorBundleLanes(const FMetaLandRoadDrawAnchor& Anchor);

void CollectSortedBundleLaneIndices(
	const FMetaLandRoadDrawAnchor& Anchor,
	TArray<int32>& OutLaneIndices);

bool FindEndSnapTargetLanePick(
	UWorld* World,
	AMoKuEditSplineActor* ActiveRoad,
	const FVector& CursorWorld,
	float SnapDistCm,
	FEndSnapTargetLanePick& OutPick);

/** Turn a proximity mouth pick into EndSnap ghost + arrive-into-mouth tangent. */
bool BuildEndSnapFromLanePick(
	const FEndSnapTargetLanePick& Pick,
	int32 DesiredLaneCount,
	bool bSelectAllMouthLanes,
	FMetaLandRoadDrawAnchor& OutAnchor,
	FVector& OutGhostLeft,
	FVector& OutGhostRight,
	FVector& OutSnapPos,
	FVector& OutArriveTangent);

/** Out of the road: Begin = -(P1-P0), End = (PLast-PPrev). Not spline Leave/Arrive. */
FVector MouthOutwardFromSplinePoints(
	const USplineComponent* Spline,
	bool bFromBegin);

/** Snap the draw tip onto the XY crossing of From→Cursor with another road. */
bool TrySnapDrawToRoadCrossing(
	AMoKuEditSplineActor* DrawingRoad,
	const FVector& FromPos,
	const FVector& Cursor,
	UWorld* World,
	float ReachCm,
	FVector& OutCross);

bool AddContinueDrawSplinePoint(
	AMoKuEditSplineActor* Road,
	const FVector& WorldPos,
	bool bInsertAtStart,
	double MouthS = -1.0,
	const FVector& MouthCenter = FVector::ZeroVector,
	bool bKeepWorldZ = false);

/** A: keep C-session authoring keys (original + Ctrl adds), rewrite spline, no preview-sample keys. */
void ReconstructContinueDrawAuthoringSpline(AMoKuEditSplineActor* Road);

/** Freeze the confirmed-side tangent; size only the new-span handle from width and angle. */
void ApplyWidthAwareNewSpanTangents(
	UMoKuEditSplinesComponent* Spline,
	int32 JointIndex,
	int32 NewTipIndex);

/** C EndSnap finish: insert a new tip if needed, never move original body points. */
bool PinContinueDrawTipToTargetMouth(
	AMoKuEditSplineActor* Road,
	UMoKuEditSplinesComponent* Spline,
	bool& bAttachAtSplineStart,
	int32 OriginalNumPoints,
	const FVector& TargetCenter,
	const FVector& TargetOutward,
	const TArray<FVector>* PreviewCenterline = nullptr,
	double PreviewMouthS = -1.0,
	const FVector& PreviewMouthCenter = FVector::ZeroVector,
	bool bKeepAuthoringKeys = false);

void StabilizeLaneExtendMouthPoint(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 MouthPointIndex,
	const FVector& MouthWorld,
	const FVector& MouthOutward,
	double MouthWidthCm = 0.0);

bool ResolveLaneEdgeConnectionSnap(
	AMoKuEditSplineActor* SnapRoad,
	const FVector& ApproachFrom,
	const FVector& CursorWorld,
	float SnapDistCm,
	int32 DesiredLaneCount,
	FVector& OutSnapPos,
	FVector& OutArriveTangent,
	float& OutSampleDist,
	FMetaLandRoadDrawAnchor* OutAnchor = nullptr,
	FVector* OutGhostLeft = nullptr,
	FVector* OutGhostRight = nullptr);

void HideEditorSplineRibbon(UMoKuEditSplinesComponent* SplineComponent);

void ApplyGuidanceSurfaceZ(FVector& Point);

bool ProjectPointOntoLandscapeSurface(
	UWorld* World,
	FVector& InOutPoint,
	const AActor* ExtraIgnoreActor = nullptr);

float ResolveGuidancePlaneZOnSurface(
	UWorld* World,
	const FVector& TipCenter,
	const AActor* IgnoreActor);

int32 MapSpawnedBranchLaneIndex(int32 SourceLaneIndex, const TArray<int32>& SourceLanes);

void TransferEditTargetToSpawnedBranch(
	UMetaLandRoadEditorMode* RoadMode,
	const FMetaLandRoadEditTarget& SourceTarget,
	AMoKuEditSplineActor* SpawnedActor);

void ApplyAsymmetricTangentBezierConnection(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 StartIndex,
	const FVector& EndPos,
	const FVector& LeaveDir,
	const FVector& ArriveDir);

void SoftenLaneExtendCornerJoint(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 JointIndex);

void ApplyLaneExtendHermiteSegmentHandles(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 StartIndex,
	int32 EndIndex,
	const FVector& StartTangent,
	const FVector& EndTangent,
	bool bPreserveStartArrive);

void CaptureLaneExtendPointFreezes(
	const UMoKuEditSplinesComponent* SplineComponent,
	int32 IndexBegin,
	int32 IndexEndExclusive,
	TArray<FLaneExtendPointFreeze>& OutFreezes);

void RestoreLaneExtendPointFreezes(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 IndexBegin,
	const TArray<FLaneExtendPointFreeze>& Freezes,
	int32 TipIndexKeepNewLeave);

/** CutS / remaining-road span tips — C-extend may reshape the body, not these. */
void CollectJunctionInterfaceStations(
	const UMoKuEditSplinesComponent* Spline,
	TArray<double>& OutStations);

void RestoreJunctionInterfaceFreezes(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 IndexBegin,
	const TArray<FLaneExtendPointFreeze>& Freezes);

void ClearNonMouthCustomTangents(
	UMoKuEditSplinesComponent* SplineComponent,
	int32 MouthPointIndex,
	int32 PreserveStubIndex = INDEX_NONE,
	int32 PreserveSoftJointIndex = INDEX_NONE);

/** One corner plan for preview rails and A-bake. Left/right are offsets of this centerline. */
struct FLeaveArrivePlan
{
	enum class EKind : uint8
	{
		None,
		CornerFillet,
		CornerSharp,
		OffsetS,
		PoseHermite,
		LeadOutHermite,
		DubinsCSC
	};

	EKind Kind = EKind::None;
	FVector P0 = FVector::ZeroVector;
	FVector P1 = FVector::ZeroVector;
	FVector Leave = FVector::ForwardVector;
	FVector Arrive = FVector::ForwardVector;
	FVector Corner = FVector::ZeroVector;
	FVector FilletA = FVector::ZeroVector;
	FVector FilletB = FVector::ZeroVector;
	FVector FilletCenter = FVector::ZeroVector;
	/** DubinsCSC: real mouths. P0/P1 are the inset Dubins ends when LeaveLen/ArriveLen > 0. */
	FVector Stub0 = FVector::ZeroVector;
	FVector Stub1 = FVector::ZeroVector;
	double SweepSign = 1.0;
	double LeaveLen = 0.0;
	double ArriveLen = 0.0;
	double Radius = 0.0;
	double MinRadius = 0.0;
	double Handle0 = 0.0;
	double Handle1 = 0.0;
	/** Dubins word: 0=LSL 1=LSR 2=RSL 3=RSR. Lengths t,p,q are in units of Radius. */
	int32 DubinsWord = 0;
	double DubinsT = 0.0;
	double DubinsP = 0.0;
	double DubinsQ = 0.0;
};

struct FCornerCurveDebug
{
	FLeaveArrivePlan::EKind Kind = FLeaveArrivePlan::EKind::None;
	double LeaveLen = 0.0;
	double ArriveLen = 0.0;
	double Radius = 0.0;
	double MinRadius = 0.0;
	double HalfWidth0 = 0.0;
	double HalfWidth1 = 0.0;
	int32 CenterPts = 0;
	int32 BakeKeys = 0;
	int32 CrossHits = 0;
	bool bInvalid = false;

	const TCHAR* KindName() const
	{
		switch (Kind)
		{
		case FLeaveArrivePlan::EKind::CornerFillet: return TEXT("Fillet");
		case FLeaveArrivePlan::EKind::CornerSharp: return TEXT("Sharp");
		case FLeaveArrivePlan::EKind::OffsetS: return TEXT("OffsetS");
		case FLeaveArrivePlan::EKind::PoseHermite: return TEXT("Hermite");
		case FLeaveArrivePlan::EKind::LeadOutHermite: return TEXT("LeadOut");
		case FLeaveArrivePlan::EKind::DubinsCSC: return TEXT("Dubins");
		default: return TEXT("None");
		}
	}
};

/** Mouth cannot face the other mouth, or the CSC hook would invert / detour. */
bool IsLeaveArrivePlanUnusable(const FLeaveArrivePlan& Plan, double HalfWidthCm);

/** Dubins CSC: shortest same-side, then any CSC. Red only if rails invert.
 *  HalfWidthCm > 0: outward mouth stubs along Leave/Arrive, then CSC from insets. */
bool TryMakeLeaveArrivePlan(
	const FVector& P0,
	const FVector& P1,
	const FVector& LeaveDir,
	const FVector& ArriveDir,
	double MinRadiusCm,
	FLeaveArrivePlan& OutPlan,
	double HalfWidthCm = 0.0);

void SampleLeaveArrivePlan(
	const FLeaveArrivePlan& Plan,
	double HalfWidthCm,
	TArray<FVector>& OutSamples,
	int32 MaxSegs = INDEX_NONE);

void KeyLeaveArrivePlan(const FLeaveArrivePlan& Plan, TArray<FVector>& OutKeys);

void BuildLaneWidthGuidanceRails(
	const FVector& TipLeft,
	const FVector& TipRight,
	const FVector& TipCenter,
	const FVector& TipTangent,
	const FVector& Cursor,
	bool bUseHermite,
	const FVector& EndTangent,
	double RampStubLengthCm,
	double SampleStepCm,
	TArray<FVector>& OutLeft,
	TArray<FVector>& OutRight,
	const FVector* OptionalEndLeft = nullptr,
	const FVector* OptionalEndRight = nullptr,
	TArray<FVector>* OutCenter = nullptr,
	FCornerCurveDebug* OutDebug = nullptr);

double ResolveDrawTipSForGuidance(
	const UMetaLandRoadSplineComponent* Spline,
	bool bForward);

/** Visible mesh tip S (CutS / remaining-road span), not hub-interior spline ends. */
double ResolveEditableTipS(
	const AMoKuEditSplineActor* RoadActor,
	const USplineComponent* Spline,
	bool bForward);

bool ResolveDrawTipLaneEdges(
	const AMoKuEditSplineActor* RoadActor,
	const UMetaLandRoadSplineComponent* Spline,
	double TipS,
	bool bForward,
	const FMetaLandRoadEditTarget* EditTarget,
	FVector& OutLeft,
	FVector& OutRight,
	FVector& OutCenter,
	bool bForceFullRoadMouth = false);

bool ResolveCtrlDrawRayHit(
	UWorld* World,
	const FRay& WorldRay,
	FHitResult& OutHit,
	AActor*& OutHitActor,
	bool bPreferRoadOrJunction = true);

/** Closest authored centerline to the ray (screen-pointing at a road). */
bool TryPickClosestRoadToRay(
	UWorld* World,
	const FRay& WorldRay,
	AMoKuEditSplineActor*& OutRoad,
	FVector& OutCenterline,
	double& OutS,
	float& OutRayDist);

/** Road bed along the ray (no physics). DynamicMesh roads have no collision. */
bool TryPickRoadBedAlongRay(
	UWorld* World,
	const FRay& WorldRay,
	AMoKuEditSplineActor*& OutRoad,
	FVector& OutCenterline,
	double& OutS,
	float& OutRayT);

/** Click XY sits on a road bed (centerline within half-width). No physics. */
bool TryPickRoadCenterlineNearPoint(
	UWorld* World,
	const FVector& WorldPos,
	AMoKuEditSplineActor* IgnoreRoad,
	AMoKuEditSplineActor*& OutRoad,
	FVector& OutCenterline,
	double& OutS);

FVector ProjectOntoParallelLaneExtend(
	const FVector& Origin,
	const FVector& ExtendTangent,
	const FVector& WorldPoint,
	bool bClampForwardOnly = true);

void RefreshRoadMeshSuppressJunction(AMoKuEditSplineActor* RoadActor);

bool IsLaneBranchRoad(const AMoKuEditSplineActor* RoadActor);

bool RestoreLaneDrawStartAnchorFromBranch(
	UMetaLandRoadSplineComponent* BranchSpline,
	const FMetaLandRoadDrawAnchor& BranchTipAnchor,
	FMetaLandRoadDrawAnchor& OutStartAnchor);

bool ResolveGuidanceCursorOnPlane(
	const FRay& WorldRay,
	float PlaneZ,
	FVector& OutWorldPos);

FVector ResolveGuidanceCursorForRay(
	const FRay& WorldRay,
	const UMetaLandRoadEditorMode* RoadMode,
	const AMoKuEditSplineActor* ActiveRoad,
	EMoKuEditState DrawState,
	float StoredPlaneZ,
	bool bHasStoredPlaneZ,
	float FallbackZ,
	const FVector& TraceHitPos,
	bool bHasTraceHit,
	UWorld* /*WorldUnused*/ = nullptr);
