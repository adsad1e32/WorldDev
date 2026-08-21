#pragma once

#include "MetaLandRoadSplineComponent.h"
#include "ComponentVisualizer.h"

#if WITH_EDITOR

struct METALANDROAD_API HMetaLandRoadSplineVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadSplineVisProxy(const UMetaLandRoadSplineComponent* InComponent, EHitProxyPriority InPriority = HPP_Wireframe)
		: HComponentVisProxy(InComponent, InPriority)
	{}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

struct METALANDROAD_API HMetaLandRoadSectionVisProxy : public HMetaLandRoadSplineVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadSectionVisProxy(const UMetaLandRoadSplineComponent* InComponent, int InSectionIndex, EHitProxyPriority InPriority = HPP_Wireframe)
		: HMetaLandRoadSplineVisProxy(InComponent, InPriority)
		, SectionIndex(InSectionIndex)
	{}

	int SectionIndex;

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

struct METALANDROAD_API HMetaLandRoadLaneVisProxy : public HMetaLandRoadSectionVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadLaneVisProxy(const UMetaLandRoadSplineComponent* InComponent, int InSectionIndex, int InLaneIndex, EHitProxyPriority InPriority = HPP_Wireframe)
		: HMetaLandRoadSectionVisProxy(InComponent, InSectionIndex, InPriority)
		, LaneIndex(InLaneIndex)
	{}

	int LaneIndex;

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

struct METALANDROAD_API HMetaLandRoadSectionKeyVisProxy : public HMetaLandRoadLaneVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadSectionKeyVisProxy(const UMetaLandRoadSplineComponent* InComponent, int InSectionIndex, EHitProxyPriority InPriority = HPP_Foreground)
		: HMetaLandRoadLaneVisProxy(InComponent, InSectionIndex, LANE_INDEX_NONE, InPriority)
	{}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}
};

/** Alt+T width edit: segment between two Width keys on a lane outer boundary. */
struct METALANDROAD_API HMetaLandRoadLaneWidthSegmentVisProxy : public HMetaLandRoadLaneVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadLaneWidthSegmentVisProxy(
		const UMetaLandRoadSplineComponent* InComponent,
		int InSectionIndex,
		int InLaneIndex,
		int InWidthIndex,
		EHitProxyPriority InPriority = HPP_Foreground)
		: HMetaLandRoadLaneVisProxy(InComponent, InSectionIndex, InLaneIndex, InPriority)
		, WidthIndex(InWidthIndex)
	{}

	int WidthIndex = INDEX_NONE;
};

/** Alt+T width edit: draggable Width key on lane outer boundary. */
struct METALANDROAD_API HMetaLandRoadLaneWidthKeyVisProxy : public HMetaLandRoadLaneWidthSegmentVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadLaneWidthKeyVisProxy(
		const UMetaLandRoadSplineComponent* InComponent,
		int InSectionIndex,
		int InLaneIndex,
		int InWidthIndex,
		EHitProxyPriority InPriority = HPP_Foreground)
		: HMetaLandRoadLaneWidthSegmentVisProxy(InComponent, InSectionIndex, InLaneIndex, InWidthIndex, InPriority)
	{}
};

/** Landscape attribute key on the section center-line (Standard overlay, not Alt+W/E/T). */
struct METALANDROAD_API HMetaLandRoadAttributeKeyVisProxy : public HMetaLandRoadSectionVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadAttributeKeyVisProxy(
		const UMetaLandRoadSplineComponent* InComponent,
		int InSectionIndex,
		int InKeyIndex,
		EHitProxyPriority InPriority = HPP_Foreground)
		: HMetaLandRoadSectionVisProxy(InComponent, InSectionIndex, InPriority)
		, KeyIndex(InKeyIndex)
	{}

	int KeyIndex = INDEX_NONE;

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}
};

/** Alt+W native spline edit base — mirrors engine HSplineVisProxy (not shared with section/lane proxies). */
struct METALANDROAD_API HMetaLandRoadNativeSplineVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadNativeSplineVisProxy(
		const UActorComponent* InComponent,
		EHitProxyPriority InPriority = HPP_Foreground)
		: HComponentVisProxy(InComponent, InPriority)
	{}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}
};

/** Native Alt+W control-point pick — mirrors engine HSplineKeyProxy. */
struct METALANDROAD_API HMetaLandRoadSplineKeyVisProxy : public HMetaLandRoadNativeSplineVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadSplineKeyVisProxy(
		const UActorComponent* InComponent,
		int32 InKeyIndex,
		EHitProxyPriority InPriority = HPP_Foreground)
		: HMetaLandRoadNativeSplineVisProxy(InComponent, InPriority)
		, KeyIndex(InKeyIndex)
	{}

	int32 KeyIndex = INDEX_NONE;

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}
};

/** Native Alt+W segment pick — mirrors engine HSplineSegmentProxy. */
struct METALANDROAD_API HMetaLandRoadSplineSegmentVisProxy : public HMetaLandRoadNativeSplineVisProxy
{
	DECLARE_HIT_PROXY();

	HMetaLandRoadSplineSegmentVisProxy(
		const UActorComponent* InComponent,
		int32 InSegmentIndex,
		EHitProxyPriority InPriority = HPP_Foreground)
		: HMetaLandRoadNativeSplineVisProxy(InComponent, InPriority)
		, SegmentIndex(InSegmentIndex)
	{}

	int32 SegmentIndex = INDEX_NONE;

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}
};

#endif // WITH_EDITOR
