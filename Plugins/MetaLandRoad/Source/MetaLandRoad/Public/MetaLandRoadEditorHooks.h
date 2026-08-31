#pragma once

#include "CoreMinimal.h"

class AMoKuEditIntersectionActor;
class AMoKuEditSplineActor;
class UMetaLandRoadSplineComponent;

/**
 * Editor module binds these on startup. Runtime must not #include Editor headers.
 */
#if WITH_EDITOR
struct METALANDROAD_API FMetaLandRoadEditorHooks
{
	static TFunction<void(UMetaLandRoadSplineComponent*)> RefreshLaneVisualization;
	static TFunction<void(AMoKuEditIntersectionActor*, bool)> RecookJunction;
	static TFunction<bool(const UMetaLandRoadSplineComponent*, int32&, int32&)> TryGetSelectedLandscapeKey;
	static TFunction<void(AMoKuEditSplineActor*)> NotifySplineComponentSet;
};
#endif
