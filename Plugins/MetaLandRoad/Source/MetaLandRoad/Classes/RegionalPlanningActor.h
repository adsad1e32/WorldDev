#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "Components/DynamicMeshComponent.h"


#include "RegionalPlanningActor.generated.h"




UCLASS()
class ARegionalPlanningActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	virtual void OnConstruction(const FTransform& Transform){};

public:
	UPROPERTY(Category = RegionalPlanningActor,VisibleAnywhere)
	TObjectPtr<UDynamicMeshComponent> RegionalGrid;

};