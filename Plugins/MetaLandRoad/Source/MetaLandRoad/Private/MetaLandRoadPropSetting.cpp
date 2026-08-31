#include "MetaLandRoadPropSetting.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadPropLibrary.h"
#include "MetaLandRoadSplineComponent.h"

namespace
{
	void ApplyDefaultFenceKey(FMetaLandRoadPropRailKey& Key)
	{
		if (const FMetaLandRoadPropAssetRow* Row = FMetaLandRoadPropLibrary::FindDefaultFenceRow())
		{
			if (Key.Mesh.IsNull())
			{
				Key.Mesh = Row->Mesh;
			}
			if (Key.SpacingCm <= KINDA_SMALL_NUMBER)
			{
				Key.SpacingCm = Row->DefaultSpacingCm;
			}
		}
	}
}

float FMetaLandRoadPropRail::ResolveAlpha() const
{
	switch (EdgeRef)
	{
	case EMetaLandRoadPropEdgeRef::SectionLeft:
	case EMetaLandRoadPropEdgeRef::LaneLeft:
		return 0.0f;
	case EMetaLandRoadPropEdgeRef::SectionRight:
	case EMetaLandRoadPropEdgeRef::LaneRight:
		return 1.0f;
	default:
		return FMath::Clamp(Alpha, 0.0f, 1.0f);
	}
}

void FMetaLandRoadPropRail::EnsureSpanKeys(double EndS)
{
	const double ClampedEnd = FMath::Max(EndS, 0.0);
	if (Keys.Num() == 0)
	{
		FMetaLandRoadPropRailKey Begin;
		Begin.SOffset = 0.0;
		Begin.Layout = EMetaLandRoadPropLayout::EvenSpacing;
		ApplyDefaultFenceKey(Begin);
		Keys.Add(Begin);
	}

	Keys[0].SOffset = 0.0;
	if (ClampedEnd <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (Keys.Num() == 1)
	{
		FMetaLandRoadPropRailKey End = Keys[0];
		End.SOffset = ClampedEnd;
		Keys.Add(End);
		return;
	}

	Keys.Last().SOffset = ClampedEnd;
	for (int32 Index = 1; Index < Keys.Num() - 1; ++Index)
	{
		Keys[Index].SOffset = FMath::Clamp(Keys[Index].SOffset, 0.0, ClampedEnd);
	}
	Keys.Sort([](const FMetaLandRoadPropRailKey& A, const FMetaLandRoadPropRailKey& B)
	{
		return A.SOffset < B.SOffset;
	});
	Keys[0].SOffset = 0.0;
	Keys.Last().SOffset = ClampedEnd;
}

int32 FMetaLandRoadPropRail::FindSpanKey(double SOffset, double Tolerance) const
{
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		if (FMath::Abs(Keys[Index].SOffset - SOffset) <= Tolerance)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FMetaLandRoadPropRail::AddSpanKey(double SOffset, double DuplicateTolerance)
{
	const double ClampedS = FMath::Max(SOffset, 0.0);
	const int32 Existing = FindSpanKey(ClampedS, DuplicateTolerance);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	FMetaLandRoadPropRailKey NewKey;
	NewKey.SOffset = ClampedS;
	int32 Hold = 0;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		if (Keys[Index].SOffset <= ClampedS)
		{
			Hold = Index;
		}
	}
	if (Keys.IsValidIndex(Hold))
	{
		NewKey.Layout = Keys[Hold].Layout;
		NewKey.SpacingCm = Keys[Hold].SpacingCm;
		NewKey.Mesh = Keys[Hold].Mesh;
		NewKey.ZOffsetCm = Keys[Hold].ZOffsetCm;
	}

	Keys.Add(NewKey);
	Keys.Sort([](const FMetaLandRoadPropRailKey& A, const FMetaLandRoadPropRailKey& B)
	{
		return A.SOffset < B.SOffset;
	});
	return FindSpanKey(ClampedS, DuplicateTolerance);
}

bool FMetaLandRoadPropRail::RemoveSpanKey(int32 KeyIndex)
{
	if (!Keys.IsValidIndex(KeyIndex) || Keys.Num() <= 2)
	{
		return false;
	}
	if (KeyIndex == 0 || KeyIndex == Keys.Num() - 1)
	{
		return false;
	}
	Keys.RemoveAt(KeyIndex);
	return true;
}

bool FMetaLandRoadPropRail::SetSpanKeyS(int32 KeyIndex, double NewS, double MaxS)
{
	if (!Keys.IsValidIndex(KeyIndex) || KeyIndex == 0 || KeyIndex == Keys.Num() - 1)
	{
		return false;
	}
	Keys[KeyIndex].SOffset = FMath::Clamp(NewS, 0.0, FMath::Max(MaxS, 0.0));
	Keys.Sort([](const FMetaLandRoadPropRailKey& A, const FMetaLandRoadPropRailKey& B)
	{
		return A.SOffset < B.SOffset;
	});
	return true;
}

bool FMetaLandRoadPropSettingUtil::EvalRailWorld(
	const UMetaLandRoadSplineComponent* Spline,
	double WorldS,
	const FMetaLandRoadPropRail& Rail,
	FVector& OutWorld)
{
	if (!Spline)
	{
		return false;
	}

	FVector Left, Right, Center;
	if (!Spline->GetRoadEdgePointsAtS(WorldS, Left, Right, Center, ESplineCoordinateSpace::World))
	{
		return false;
	}

	OutWorld = FMath::Lerp(Left, Right, Rail.ResolveAlpha());
	return true;
}

int32 FMetaLandRoadPropSettingUtil::QuantizeS(double WorldS)
{
	return FMath::RoundToInt(WorldS);
}
