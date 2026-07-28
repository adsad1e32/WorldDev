#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadLaneVisualization.h"
#include "MetaLandRoadProfile.h"
#include "MetaLandRoadModule.h"
#include "Components/LineBatchComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

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
			2,
			GlobalMinSteps);
	}

	static void ResampleLinearApproximation(
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& Source,
		int32 TargetCount,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& OutPoints)
	{
		OutPoints.SetNum(TargetCount);
		if (Source.Num() < 2 || TargetCount < 2)
		{
			return;
		}

		for (int32 i = 0; i < TargetCount; ++i)
		{
			const float Alpha = static_cast<float>(i) / static_cast<float>(TargetCount - 1);
			const float SourceIndex = Alpha * static_cast<float>(Source.Num() - 1);
			const int32 Idx0 = FMath::Clamp(FMath::FloorToInt(SourceIndex), 0, Source.Num() - 2);
			const int32 Idx1 = Idx0 + 1;
			const float T = SourceIndex - static_cast<float>(Idx0);
			OutPoints[i].Position = FMath::Lerp(Source[Idx0].Position, Source[Idx1].Position, T);
			OutPoints[i].SplineParam = FMath::Lerp(Source[Idx0].SplineParam, Source[Idx1].SplineParam, T);
		}
	}

	static void AlignInnerPointsToOuter(
		UMetaLandRoadSplineComponent* Component,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& InnerPoints,
		const TArray<FMetaLandRoadSplinePositionLinearApproximation>& OuterPoints,
		double S0,
		double S1,
		int32 MinNumSteps)
	{
		if (InnerPoints.Num() == OuterPoints.Num())
		{
			return;
		}

		if (InnerPoints.Num() < 2)
		{
			Component->BuildLinearApproximation(
				InnerPoints,
				[Component](double S) { return Component->EvalROffset(S); },
				S0,
				S1,
				NumPointPerSegment,
				MinNumSteps,
				ESplineCoordinateSpace::Local);
		}

		if (InnerPoints.Num() != OuterPoints.Num())
		{
			ResampleLinearApproximation(InnerPoints, OuterPoints.Num(), InnerPoints);
		}
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
		const float UvTileCm = FMath::Max(MetaLandRoad::DefaultAsphaltUvTileCm, 1.0f);

		// Asphalt test UVs: continuous world-distance tiling (U = across, V = along spline).
		// MetaRoad lane-mark UVs (0..1 per lane) break tiling on T_Asphalt_D / MI_Asphalt01.
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
			if (InnerWidthSq < 1.0 && NextInnerWidthSq < 1.0)
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
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& PrevPoints,
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 LaneIndex,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SectionIndex);
		if ((S1 - S0) < KINDA_SMALL_NUMBER)
		{
			return false;
		}

		TArray<FMetaLandRoadSplinePositionLinearApproximation> Points;
		Component->BuildLinearApproximation(
			Points,
			[&Section, LaneIndex, Component](double S)
			{
				return Section.EvalLaneROffset(LaneIndex, S, 1.0) + Component->EvalROffset(S);
			},
			S0,
			S1,
			NumPointPerSegment,
			MinNumSteps,
			ESplineCoordinateSpace::Local);

		if (Points.Num() < 2)
		{
			return false;
		}

		TArray<FMetaLandRoadSplinePositionLinearApproximation> InnerPoints = PrevPoints;
		AlignInnerPointsToOuter(Component, InnerPoints, Points, S0, S1, MinNumSteps);

		const bool bBuilt = BuildLaneRoadMesh(
			InnerPoints,
			Points,
			Component,
			LaneIndex,
			Section,
			RoadHeightOffset,
			bGenerateUVs,
			OutMesh);

		PrevPoints = MoveTemp(Points);
		return bBuilt;
	}

	void AppendLanesForSide(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 PreSectionIndex,
		bool bHasSide,
		bool bUseCurrentSectionLanes,
		TArray<FMetaLandRoadSplinePositionLinearApproximation>& PrevPoints,
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

		for (int32 LaneArrayIndex = 0; LaneArrayIndex < Lanes.Num(); ++LaneArrayIndex)
		{
			const int32 LaneIndex = LaneSign > 0 ? (LaneArrayIndex + 1) : -(LaneArrayIndex + 1);
			AppendLaneMeshForSection(
				PrevPoints,
				Component,
				LaneSectionIndex,
				LaneIndex,
				S0,
				S1,
				MinNumSteps,
				RoadHeightOffset,
				bGenerateUVs,
				OutMesh);
		}
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
	bDrawDebug = true;

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
	FixUpSegments();
	UpdateAutoTangents();
	Super::UpdateSpline();
	UpdateLaneSectionBounds();
	RefreshLaneVisualization();
#if WITH_EDITOR
	// Spline changed while editing — redraw ComponentVisualizer PDI overlay.
	RedrawNativeEditViewport(this);
#endif
}

void UMetaLandRoadSplineComponent::UpdateSpline(int32 EditingPointIndex)
{
	FixUpSegments();
	UpdateAutoTangents(EditingPointIndex);
	Super::UpdateSpline();
	UpdateLaneSectionBounds();
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
	MetaLandRoadLaneVisualization::RefreshLaneVisualization(this);
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

	// Existing actors may still have Linear keys — convert before sampling mesh.
	EnsureAllCurvePointTypesForSmoothCorners();

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

	/**
	 * Independent face timelines:
	 * - Left face breaks only at Left/Both section SOffsets
	 * - Right face breaks only at Right/Both section SOffsets
	 * A Left-only split must NOT open a new right-face strip (and vice versa).
	 */
	auto CollectSideBreaks = [this, SplineLength](bool bLeftFace) -> TArray<double>
	{
		TArray<double> Breaks;
		Breaks.Add(0.0);
		for (int32 SectionIndex = 0; SectionIndex < GetLaneSectionsNum(); ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);
			if (Section.SOffset <= KINDA_SMALL_NUMBER
				|| Section.SOffset >= SplineLength - KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const bool bBreaksThisFace = bLeftFace
				? MetaLandRoad::HasLeftSide(Section.Side)
				: MetaLandRoad::HasRightSide(Section.Side);
			if (bBreaksThisFace)
			{
				Breaks.Add(Section.SOffset);
			}
		}
		Breaks.Add(SplineLength);
		Breaks.Sort();

		TArray<double> Unique;
		Unique.Reserve(Breaks.Num());
		for (double S : Breaks)
		{
			if (Unique.Num() == 0 || !FMath::IsNearlyEqual(Unique.Last(), S, KINDA_SMALL_NUMBER))
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

	auto BuildFaceSpans = [&](bool bLeftFace, int32 LaneSign)
	{
		const TArray<double> Breaks = CollectSideBreaks(bLeftFace);
		const int32 SectionMinSteps = MetaLandRoadLaneMeshPrivate::MinNumStepsPerSection;

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

			TArray<FMetaLandRoadSplinePositionLinearApproximation> CenterPoints;
			BuildLinearApproximation(
				CenterPoints,
				[this](double S) { return EvalROffset(S); },
				S0,
				S1,
				NumPointPerSegment,
				SectionMinSteps,
				ESplineCoordinateSpace::Local);

			if (CenterPoints.Num() < 2)
			{
				continue;
			}

			TArray<FMetaLandRoadSplinePositionLinearApproximation> PrevPoints = CenterPoints;
			AppendLanesForSide(
				this,
				LaneSectionIndex,
				INDEX_NONE,
				true,
				true,
				PrevPoints,
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
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddLeftSection ENTER  SplineKey=%f  Sections=%d  SplineLength=%f"),
		SplineKey,
		GetLaneSectionsNum(),
		GetSplineLength());
	const int32 Result = SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Left);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddLeftSection EXIT   Result=%d"), Result);
	return Result;
}

int32 UMetaLandRoadSplineComponent::EditorAddRightSection(float SplineKey)
{
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddRightSection ENTER SplineKey=%f  Sections=%d  SplineLength=%f"),
		SplineKey,
		GetLaneSectionsNum(),
		GetSplineLength());
	const int32 Result = SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Right);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddRightSection EXIT  Result=%d"), Result);
	return Result;
}

int32 UMetaLandRoadSplineComponent::EditorAddLeftSectionAtDistanceAlongSpline(double DistanceAlongSpline)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddLeftSectionAtDistanceAlongSpline  Distance=%f  SplineKey=%f"),
		DistanceAlongSpline,
		SplineKey);
	return EditorAddLeftSection(SplineKey);
}

int32 UMetaLandRoadSplineComponent::EditorAddRightSectionAtDistanceAlongSpline(double DistanceAlongSpline)
{
	const float SplineKey = GetInputKeyValueAtDistanceAlongSpline(DistanceAlongSpline);
	UE_LOG(LogTemp, Warning, TEXT("[MetaLandRoad] EditorAddRightSectionAtDistanceAlongSpline Distance=%f  SplineKey=%f"),
		DistanceAlongSpline,
		SplineKey);
	return EditorAddRightSection(SplineKey);
}

int32 UMetaLandRoadSplineComponent::SplitSection(float SplineKey, EMetaLandRoadLaneSectionSide Side)
{
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
	const FMetaLandRoadLaneSection& Section = GetLaneSection(SectionIndex);

	FMetaLandRoadLaneRange Range;
	if (LaneIndex == LANE_INDEX_NONE)
	{
		Range.StartS = Section.SOffset;
		Range.EndS = Section.SOffsetEnd_Cached;
	}
	else
	{
		const FMetaLandRoadLane& Lane = Section.GetLaneByIndex(LaneIndex);
		Range.StartS = Lane.GetStartOffset();
		Range.EndS = Lane.GetEndOffset();
	}
	return Range;
}

FVector UMetaLandRoadSplineComponent::EvalLanePosition(int SectionIndex, int LaneIndex, double S, double Alpha, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float SParam = GetInputKeyValueAtDistanceAlongSpline(S);
	const FVector Loc = GetLocationAtSplineInputKey(SParam, CoordinateSpace);

	double ROffset = EvalROffset(S);
	if (LaneIndex != LANE_INDEX_NONE)
	{
		ROffset += RoadLayout.Sections[SectionIndex].EvalLaneROffset(LaneIndex, S, Alpha);
	}

	const FMetaLandRoadLaneSection& Section = RoadLayout.Sections[SectionIndex];
	const float SectionStartKey = GetInputKeyValueAtDistanceAlongSpline(Section.SOffset);
	const float SectionEndKey = GetInputKeyValueAtDistanceAlongSpline(Section.SOffsetEnd_Cached);
	const bool bUseConstantOffsetRight = GetNumberOfSplinePoints() <= 2
		|| FMath::FloorToInt(SectionStartKey) == FMath::FloorToInt(SectionEndKey);
	const FVector RVector = bUseConstantOffsetRight
		? GetRightVectorAtDistanceAlongSpline(Section.SOffset, CoordinateSpace)
		: GetRightVectorAtDistanceAlongSpline(S, CoordinateSpace);
	return Loc + RVector * ROffset;
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
