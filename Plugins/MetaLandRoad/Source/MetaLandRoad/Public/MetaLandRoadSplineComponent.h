#pragma once

#include "Components/SplineComponent.h"
#include "Templates/Function.h"
#include "MetaLandRoadSplinePointTypes.h"
#include "MetaLandRoadTypes.h"
#include "MetaLandRoadSplineComponent.generated.h"

class FPrimitiveDrawInterface;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/**
 * MetaLandRoad spline with lane-section layout (MetaRoad-compatible data model).
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent, DisplayName = "MetaLandRoad Spline"))
class METALANDROAD_API UMetaLandRoadSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UMetaLandRoadSplineComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnRegister() override;
	virtual void PostLoad() override;
	virtual void UpdateSpline() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road, meta = (DisplayName = "Show Lane Visualization"))
	bool bShowLaneVisualization = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road, meta = (ClampMin = "25.0", UIMin = "25.0", UIMax = "1000.0", DisplayName = "Lane Viz Sample Step (cm)"))
	float LaneVisualizationSampleStep = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road, meta = (DisplayName = "Lane Viz Height Offset (cm)"))
	float LaneVisualizationHeightOffset = 15.0f;

	/** World cm per asphalt UV tile along/across the road. Larger = less repetition on top and thin side edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road, meta = (ClampMin = "50.0", ClampMax = "10000.0", UIMin = "200.0", UIMax = "4000.0", DisplayName = "Asphalt UV Tile (cm)"))
	float AsphaltUvTileCm = MetaLandRoad::DefaultAsphaltUvTileCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Road, meta = (DisplayName = "Auto Maintain 2-Section Midpoint"))
	bool bAutoMaintainMidpointSectionSplit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Road)
	FMetaLandRoadLayout RoadLayout;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = Road)
	TObjectPtr<UMetaLandRoadConnection> PredecessorConnection;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = Road)
	TObjectPtr<UMetaLandRoadConnection> SuccessorConnection;

	UPROPERTY(VisibleAnywhere, Category = Spline)
	TArray<EMetaLandRoadSplinePointTypeOverride> PointTypes;

	UFUNCTION(BlueprintCallable, Category = Spline)
	EMetaLandRoadSplinePointType GetRoadSplinePointType(int32 PointIndex) const;

	UFUNCTION(BlueprintCallable, Category = Spline)
	void SetRoadSplinePointType(int32 PointIndex, EMetaLandRoadSplinePointType Mode, bool bUpdateSpline = true);

	UFUNCTION(BlueprintCallable, Category = Spline)
	void UpdateAutoTangents(int32 EditingPointIndex = -1);

	virtual void UpdateSpline(int32 EditingPointIndex);

	UFUNCTION(BlueprintCallable, Category = Road)
	virtual void UpdateRoadLayout();

	UFUNCTION(BlueprintCallable, Category = Road)
	void UpdateLaneSectionBounds();

	UFUNCTION(BlueprintCallable, Category = Road)
	void TrimLaneSections(double Tolerance = 0.1);

	/** Find lane section containing the given spline input key. */
	UFUNCTION(BlueprintCallable, Category = Road)
	int32 FindRoadSectionOnSplineKey(float SplineKey) const;

	/**
	 * Split the lane section at a spline input key (MetaRoad-compatible).
	 * @return Index of the newly created section, or INDEX_NONE on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = Road)
	virtual int32 SplitSection(float SplineKey, EMetaLandRoadLaneSectionSide Side = EMetaLandRoadLaneSectionSide::Both);

	UFUNCTION(BlueprintCallable, Category = Road)
	int32 SplitSectionAtDistanceAlongSpline(double DistanceAlongSpline, EMetaLandRoadLaneSectionSide Side = EMetaLandRoadLaneSectionSide::Both);

	UFUNCTION(BlueprintCallable, Category = Road)
	int32 SplitSectionAtSplinePoint(int32 PointIndex, EMetaLandRoadLaneSectionSide Side = EMetaLandRoadLaneSectionSide::Both);

	/** Debug/editor entry: split at spline input key, new section keeps LEFT lanes only. Set breakpoint here. */
	UFUNCTION(BlueprintCallable, Category = "Road|Debug")
	int32 EditorAddLeftSection(float SplineKey);

	/** Debug/editor entry: split at spline input key, new section keeps RIGHT lanes only. Set breakpoint here. */
	UFUNCTION(BlueprintCallable, Category = "Road|Debug")
	int32 EditorAddRightSection(float SplineKey);

	UFUNCTION(BlueprintCallable, Category = "Road|Debug")
	int32 EditorAddLeftSectionAtDistanceAlongSpline(double DistanceAlongSpline);

	UFUNCTION(BlueprintCallable, Category = "Road|Debug")
	int32 EditorAddRightSectionAtDistanceAlongSpline(double DistanceAlongSpline);

	struct FMetaLandRoadLaneRange
	{
		double StartS = 0.0;
		double EndS = 0.0;
	};

	FMetaLandRoadLaneRange GetLaneRange(int SectionIndex, int LaneIndex) const;
	FVector EvalLanePosition(int SectionIndex, int LaneIndex, double S, double Alpha, ESplineCoordinateSpace::Type CoordinateSpace) const;
	float ClosestKeyToSegment(float Key1, float Key2, const FVector& A1, const FVector& A2) const;
	float ClosestKeyToSegment2(float S1, float S2, const FVector& A1, const FVector& A2) const;

	UFUNCTION(BlueprintCallable, Category = Road)
	const FMetaLandRoadLayout& GetRoadLayout() const { return RoadLayout; }

	UFUNCTION(BlueprintCallable, Category = Road)
	const TArray<FMetaLandRoadLaneSection>& GetLaneSections() const { return RoadLayout.Sections; }

	TArray<FMetaLandRoadLaneSection>& GetLaneSectionsMutable() { return RoadLayout.Sections; }

	FMetaLandRoadLayout& GetRoadLayoutMutable() { return RoadLayout; }

	UFUNCTION(BlueprintCallable, Category = Road)
	int GetLaneSectionsNum() const { return RoadLayout.Sections.Num(); }

	UFUNCTION(BlueprintCallable, Category = Road)
	const FMetaLandRoadLaneSection& GetLaneSection(int Index) const { return RoadLayout.Sections[Index]; }

	FMetaLandRoadLaneSection& GetLaneSection(int Index) { return RoadLayout.Sections[Index]; }

	const FMetaLandRoadLane* GetRoadLane(int SectionIndex, int LaneIndex) const;
	FMetaLandRoadLane* GetRoadLane(int SectionIndex, int LaneIndex);

	UMetaLandRoadConnection* GetPredecessorConnection() const { return PredecessorConnection; }
	UMetaLandRoadConnection* GetSuccessorConnection() const { return SuccessorConnection; }

	double EvalROffset(double S) const { return RoadLayout.EvalROffset(S); }

	/** Extra mesh samples near sharp turns so corners don't look like a single V kink. */
	int32 EstimateCornerExtraSteps(double S0Param, double S1Param) const
	{
		const int32 NumPoints = GetNumberOfSplinePoints();
		if (NumPoints < 2)
		{
			return 0;
		}

		int32 Extra = 0;

		// 2-point Hermite: no mid joint — measure leave vs chord (was always 0 → faceted arc).
		if (NumPoints == 2)
		{
			const FVector P0 = GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
			const FVector P1 = GetLocationAtSplinePoint(1, ESplineCoordinateSpace::Local);
			const FVector Chord = (P1 - P0).GetSafeNormal();
			const FVector Leave0 = GetLeaveTangentAtSplinePoint(0, ESplineCoordinateSpace::Local).GetSafeNormal();
			if (!Chord.IsNearlyZero() && !Leave0.IsNearlyZero())
			{
				const float Dot = FMath::Clamp(FVector::DotProduct(Leave0, Chord), -1.0f, 1.0f);
				const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
				if (AngleDeg > 12.0f)
				{
					Extra = AngleDeg > 45.0f ? 5 : (AngleDeg > 25.0f ? 4 : 3);
				}
			}
			return FMath::Clamp(Extra, 0, 6);
		}

		const int32 KeyStart = FMath::Max(FMath::FloorToInt(static_cast<float>(S0Param)), 1);
		const int32 KeyEnd = FMath::Min(FMath::CeilToInt(static_cast<float>(S1Param)), NumPoints - 2);
		for (int32 KeyIdx = KeyStart; KeyIdx <= KeyEnd; ++KeyIdx)
		{
			const FVector Prev = GetLocationAtSplinePoint(KeyIdx - 1, ESplineCoordinateSpace::Local);
			const FVector Cur = GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::Local);
			const FVector Next = GetLocationAtSplinePoint(KeyIdx + 1, ESplineCoordinateSpace::Local);
			const FVector InDir = (Cur - Prev).GetSafeNormal();
			const FVector OutDir = (Next - Cur).GetSafeNormal();
			if (InDir.IsNearlyZero() || OutDir.IsNearlyZero())
			{
				continue;
			}
			const float Dot = FMath::Clamp(FVector::DotProduct(InDir, OutDir), -1.0f, 1.0f);
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
			if (AngleDeg > 18.0f)
			{
				Extra = FMath::Max(Extra, AngleDeg > 50.0f ? 4 : (AngleDeg > 30.0f ? 3 : 2));
			}
		}
		return FMath::Clamp(Extra, 0, 6);
	}

	/** Convert Linear keys to Curve so bends subdivide instead of hard corners. */
	void EnsureCurvePointTypesForSmoothCorners(const TArray<int32>& KeyIndices, bool bUpdateSplineAfter = true)
	{
		bool bChanged = false;
		for (const int32 KeyIdx : KeyIndices)
		{
			if (KeyIdx < 0 || KeyIdx >= GetNumberOfSplinePoints())
			{
				continue;
			}
			if (GetSplinePointType(KeyIdx) == ESplinePointType::Linear
				|| GetSplinePointType(KeyIdx) == ESplinePointType::Constant)
			{
				SetSplinePointType(KeyIdx, ESplinePointType::Curve, false);
				bChanged = true;
			}
		}
		if (bChanged && bUpdateSplineAfter)
		{
			UpdateSpline();
		}
	}

	void EnsureAllCurvePointTypesForSmoothCorners(bool bUpdateSplineAfter = true)
	{
		TArray<int32> Keys;
		const int32 Num = GetNumberOfSplinePoints();
		Keys.Reserve(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			Keys.Add(i);
		}
		EnsureCurvePointTypesForSmoothCorners(Keys, bUpdateSplineAfter);
	}

	void BuildLinearApproximation(
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& OutPoints,
		const TFunction<double(double)>& RightOffsetFunc,
		double S0,
		double S1,
		int32 InReparamStepsPerSegment,
		int32 MinNumSteps,
		ESplineCoordinateSpace::Type CoordinateSpace) const;

	/** Build a combined road mesh by triangulating each lane separately (MetaRoad-style). */
	virtual bool GenerateCombinedLaneDynamicMesh(
		UE::Geometry::FDynamicMesh3& OutMesh,
		float RoadHeightOffset = 0.0f,
		bool bGenerateUVs = true);

	/** Total half-width (left/right outer edge distance from center) at distance S. */
	double GetRoadHalfWidthAtS(double S) const;

	/** Outer left/right edge and center at distance S along the road. */
	bool GetRoadEdgePointsAtS(
		double S,
		FVector& OutLeft,
		FVector& OutRight,
		FVector& OutCenter,
		ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World) const;

	/**
	 * Full cross-section vertices at S, ordered left-outer → right-outer
	 * (one point per lane outer edge + center). Matches road mesh cut point indices.
	 */
	bool GetRoadCrossSectionPointsAtS(
		double S,
		TArray<FVector>& OutPoints,
		ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World) const;

	/**
	 * Cross-section for a contiguous ConnectedLaneIndices subset at S.
	 * Empty / invalid set falls back to full GetRoadCrossSectionPointsAtS.
	 */
	bool GetRoadCrossSectionPointsForLaneIndicesAtS(
		double S,
		const TArray<int32>& LaneIndices,
		TArray<FVector>& OutPoints,
		ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World) const;

	/** Half-width of the ConnectedLaneIndices mouth (or full road if empty). */
	double GetRoadHalfWidthForLaneIndicesAtS(double S, const TArray<int32>& LaneIndices) const;

	UFUNCTION(BlueprintCallable, Category = Road)
	void RefreshLaneVisualization();

	/** Center-line polyline through every spline point (per-segment InputKey, not index-as-key). */
	void BuildCenterLineWorldPolyline(TArray<FVector>& OutPoints, int32 StepsPerSegment = 32) const;
	float GetSplinePointInputKey(int32 PointIndex) const;
	float GetMainCenterLineHeightOffset() const;
	void DrawMainCenterLinePDI(FPrimitiveDrawInterface* PDI, const FLinearColor& Color, float Thickness) const;

	bool EvalMouthTransform(const FMetaLandRoadDrawAnchor& Anchor, FTransform& OutTransform) const;

	bool BuildDrawAnchorForLaneMouth(
		int32 SpanSectionIndex,
		int32 LaneIndex,
		double AnchorS,
		EMetaLandRoadMouthSide MouthSide,
		FMetaLandRoadDrawAnchor& OutAnchor) const;

	/**
	 * MetaRoad FitSplineToBegin/EndConnection: pin tip control point to Anchor mouth + outward tangent.
	 * bAtSplineStart=true → point 0 (draw from Lane Successor); false → last point (finish at target mouth).
	 */
	void FitSplineToAnchor(const FMetaLandRoadDrawAnchor& Anchor, bool bAtSplineStart);

	/**
	 * MetaRoad FromConnection: Road Predecessor/Successor ↔ source Lane Successor/Predecessor.
	 * Returns true if at least the primary lane link was written.
	 */
	bool ConnectRoadToDrawAnchor(const FMetaLandRoadDrawAnchor& SourceAnchor, bool bAttachAtSplineStart);

	/**
	 * [KEY-ALGO] Mouth-to-mouth blend. Min(N,M)==1 → Left[0]+Right[0] width mix, ROffset=0.
	 * Center line stays in the mesh middle; head/tail align to both mouths.
	 */
	bool BlendEndWidthToTargetMouth(
		const FMetaLandRoadDrawAnchor& TargetAnchor,
		EMetaLandRoadEndWidthBlendSpan SpanMode = EMetaLandRoadEndWidthBlendSpan::LastTwoSegments,
		double BlendLengthCm = 0.0);

protected:
	void FixUpSegments();
	int32 GetNextPoint(int32 PointIndex) const;
	int32 GetPrevPoint(int32 PointIndex) const;
	bool AdjustArcSegment(int32 PointIndex, EMetaLandRoadComputeArcMode Mode);
};
