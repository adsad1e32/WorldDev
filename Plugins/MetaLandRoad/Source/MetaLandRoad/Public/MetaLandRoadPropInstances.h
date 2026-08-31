#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "MetaLandRoadPropInstances.generated.h"

class AMoKuEditSplineActor;

/** Host for PropSetting HISM instances. Rebuild does not Recook the road or junction. */
UCLASS()
class METALANDROAD_API UMetaLandRoadPropInstanceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	static constexpr float ScaleZRange = 0.2f;

	static void RefreshForRoad(AMoKuEditSplineActor* Road);
	void Rebuild();
};
