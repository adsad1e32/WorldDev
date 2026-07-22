// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SplineRoadBasicSettings.h"
#include "Components/DynamicMeshComponent.h"
#include "RoadBrushActorInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class URoadBrushActorInterface : public UInterface
{
		GENERATED_UINTERFACE_BODY()
};

class IRoadBrushActorInterface
{
		GENERATED_IINTERFACE_BODY()

		virtual bool AffectsLandscape() const = 0;
	#if WITH_EDITOR
		virtual const FSplineRoadHeightmapSettings& GetRoadHeightmapSettings() const = 0;

		virtual const TMap<FName, FRoadBodyWeightmapSettings>& GetLayerWeightmapSettings() const = 0;

		virtual TObjectPtr<UPrimitiveComponent> GetBrushRenderableComponents() const = 0;

		struct FRoadBrushActorChangedEventParams
		{
			FRoadBrushActorChangedEventParams(IRoadBrushActorInterface* InRoadBrushActor, const FPropertyChangedEvent& InPropertyChangedEvent = FPropertyChangedEvent(/*InProperty = */nullptr))
				: RoadBrushActor(InRoadBrushActor)
				, PropertyChangedEvent(InPropertyChangedEvent)
			{
			}

			/** The Road brush actor that has changed */
			IRoadBrushActorInterface* RoadBrushActor = nullptr;

			/** Provides some additional context about how the Road brush actor data has changed (property, type of change...) */
			FPropertyChangedEvent PropertyChangedEvent;

			/** Indicates that property related to the Road brush actor's visual shape has changed */
			bool bShapeOrPositionChanged = false;

			/** Indicates that a property affecting the terrain weightmaps has changed */
			bool bWeightmapSettingsChanged = false;

			/** Indicates user initiated Parameter change */
			bool bUserTriggered = false;
		};


		DECLARE_EVENT_OneParam(IRoadBrushActorInterface, FRoadBrushActorChangedEvent, const FRoadBrushActorChangedEventParams&);
		static FRoadBrushActorChangedEvent& GetOnRoadBrushActorChangedEvent();

		void BroadcastRoadBrushActorChangedEvent(const FRoadBrushActorChangedEventParams& InParams)
		{
			if (GIsEditor)
			{
				FRoadBrushActorChangedEvent& Event = GetOnRoadBrushActorChangedEvent();
				if (Event.IsBound())
				{
					Event.Broadcast(InParams);
				}
			}
		}
	#endif 
};
