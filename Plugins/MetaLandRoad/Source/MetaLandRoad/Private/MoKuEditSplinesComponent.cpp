#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadProfile.h"
#include "Misc/ScopeExit.h"

namespace MoKuEditSplinesComponentPrivate
{
	static bool ShouldRefreshRoadMeshFromPropertyChange(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
		{
			return false;
		}

		if (!PropertyChangedEvent.Property)
		{
			return false;
		}

		const FName PropertyName = PropertyChangedEvent.GetPropertyName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, RoadLayout))
		{
			return true;
		}

		if (PropertyChangedEvent.MemberProperty)
		{
			const FName MemberName = PropertyChangedEvent.MemberProperty->GetFName();
			if (MemberName == GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves)
				|| MemberName == GET_MEMBER_NAME_CHECKED(UMetaLandRoadSplineComponent, RoadLayout))
			{
				return true;
			}
		}

		return false;
	}
}

UMoKuEditSplinesComponent::UMoKuEditSplinesComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMoKuEditSplinesComponent::OnRegister()
{
	Super::OnRegister();

	if (!ParentActor)
	{
		ParentActor = Cast<AMoKuEditSplineActor>(GetOwner());
	}

	if (ParentActor && !bIsRefreshingRoadMesh)
	{
		ParentActor->RefreshDynamicRoadMesh();
	}
}

#if WITH_EDITOR
void UMoKuEditSplinesComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (ParentActor
		&& !bIsRefreshingRoadMesh
		&& !bDeferRoadMeshRefresh
		&& MoKuEditSplinesComponentPrivate::ShouldRefreshRoadMeshFromPropertyChange(PropertyChangedEvent))
	{
		ParentActor->RefreshDynamicRoadMesh();
	}
}

void UMoKuEditSplinesComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (ParentActor
		&& !bIsRefreshingRoadMesh
		&& !bDeferRoadMeshRefresh
		&& MoKuEditSplinesComponentPrivate::ShouldRefreshRoadMeshFromPropertyChange(PropertyChangedEvent))
	{
		ParentActor->RefreshDynamicRoadMesh();
	}
}

void UMoKuEditSplinesComponent::PostLoad()
{
	Super::PostLoad();
}

void UMoKuEditSplinesComponent::PostEditUndo()
{
	Super::PostEditUndo();
	if (UMetaLandRoadSplineComponent* RoadSpline = Cast<UMetaLandRoadSplineComponent>(this))
	{
		TGuardValue<bool> SuppressJunctionRecook(
			AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh, true);
		MetaLandRoadProfile::RecookLocalJunctionsForSpline(RoadSpline);
		MetaLandRoadProfile::RefreshConnectedRoadMeshes(RoadSpline);
	}
}
#endif

void UMoKuEditSplinesComponent::UpdateRoadLayout()
{
	Super::UpdateRoadLayout();
	// During junction Recook, SplitSection/Purge call this — must not Refresh→Recook again.
	if (ParentActor
		&& !bIsRefreshingRoadMesh
		&& !bDeferRoadMeshRefresh
		&& !AMoKuEditSplineActor::bSuppressJunctionRecookFromRoadRefresh)
	{
		ParentActor->RefreshDynamicRoadMesh();
	}
}

void UMoKuEditSplinesComponent::AddSplinePointLocation(const FVector& InLocationPos, const int32 Index)
{
	Modify();
	const int32 ClampedIndex = FMath::Clamp(Index, 0, GetNumberOfSplinePoints());
	AddSplinePointAtIndex(InLocationPos, ClampedIndex, ESplineCoordinateSpace::World, true);
}

void UMoKuEditSplinesComponent::UpdateProceduralPoints(const FMoKuSplineInterpPoint& InPoint)
{
	ProceduralPoints.Add(InPoint);
}

void UMoKuEditSplinesComponent::UpdateSplineMesh()
{
}

void UMoKuEditSplinesComponent::DistributeSegmentToSpline(TArray<UStaticMesh*> InStaticMeshList)
{
}

void UMoKuEditSplinesComponent::DistributeSegmentByDist(float Distance)
{
}
