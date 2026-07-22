// Fill out your copyright notice in the Description page of Project Settings.


#include "RoadBrushActorInterface.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(RoadBrushActorInterface)

URoadBrushActorInterface::URoadBrushActorInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
IRoadBrushActorInterface::FRoadBrushActorChangedEvent& IRoadBrushActorInterface::GetOnRoadBrushActorChangedEvent()
{
	static IRoadBrushActorInterface::FRoadBrushActorChangedEvent RoadBrushActorChangedEvent;
	return RoadBrushActorChangedEvent;
}

#endif 
