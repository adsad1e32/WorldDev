#pragma once

#include "Components/SplineComponent.h"
#include "MetaLandRoadSplinePointTypes.generated.h"

enum class EMetaLandRoadComputeArcMode
{
	AdjStartTangent,
	AdjEndTangent,
	AdjStartPos,
	AdjEndPos
};

UENUM(BlueprintType)
enum class EMetaLandRoadSplinePointType : uint8
{
	Linear = ESplinePointType::Linear,
	Curve = ESplinePointType::Curve,
	Constant = ESplinePointType::Constant,
	CurveClamped = ESplinePointType::CurveClamped,
	CurveCustomTangent = ESplinePointType::CurveCustomTangent,
	Arc
};

UENUM()
enum class EMetaLandRoadSplinePointTypeOverride : uint8
{
	Inherited,
	Arc
};
