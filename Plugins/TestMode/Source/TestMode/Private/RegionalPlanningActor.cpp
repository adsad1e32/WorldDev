#include "RegionalPlanningActor.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "RegionalPlanningActor"



ARegionalPlanningActor::ARegionalPlanningActor(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	RegionalGrid = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("RegionalGrid"));
	if (RegionalGrid)
	{
		RegionalGrid->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
	}
}

#undef LOCTEXT_NAMESPACE