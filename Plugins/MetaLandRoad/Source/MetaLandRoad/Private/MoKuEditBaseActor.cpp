#include "MoKuEditBaseActor.h"
#include  "EditSplineRoadInfo.h"
#include "WorldEditorSettings.h"

#define LOCTEXT_NAMESPACE "MoKuEditBaseActor"

bool AMoKuEditBaseActor::bSuppressLandscapeBrushNotify = false;

AMoKuEditBaseActor::AMoKuEditBaseActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAffectsLandscape(true)
{
}

bool AMoKuEditBaseActor::AffectsLandscape() const
{
	return bAffectsLandscape;
}

bool AMoKuEditBaseActor::AffectsWeightmapLayer(FName LayerName) const
{
	if (!bAffectsLandscape || LayerName.IsNone())
	{
		return false;
	}
	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayer = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	return LayerName == RoadLayer;
}

float AMoKuEditBaseActor::GetRoadWeightFalloffWidth() const
{
	return FMath::Max(RoadWeightFalloffWidth, 1.0f);
}

bool AMoKuEditBaseActor::EnsureDefaultTerrainInfluenceSettings()
{
	if (!IsValid(this)
		|| bIsEditorPreviewActor
		|| HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate()
		|| IsActorBeingDestroyed()
		|| !bAffectsLandscape)
	{
		return false;
	}

	bool bChanged = false;
	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayerName = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	if (const FRoadBodyWeightmapSettings* OldRoad = RoadBodyWeightmapSettings.Find(RoadLayerName))
	{
		if (OldRoad->FalloffWidth > KINDA_SMALL_NUMBER)
		{
			RoadWeightFalloffWidth = OldRoad->FalloffWidth;
			bChanged = true;
		}
	}
	if (RoadBodyWeightmapSettings.Num() > 0)
	{
		RoadBodyWeightmapSettings.Empty();
		bChanged = true;
	}

	if (!FMath::IsFinite(RoadWeightFalloffWidth) || RoadWeightFalloffWidth <= KINDA_SMALL_NUMBER)
	{
		RoadWeightFalloffWidth = 180.0f;
		bChanged = true;
	}
	else
	{
		const float Clamped = FMath::Clamp(RoadWeightFalloffWidth, 1.0f, 2500.0f);
		if (!FMath::IsNearlyEqual(Clamped, RoadWeightFalloffWidth))
		{
			RoadWeightFalloffWidth = Clamped;
			bChanged = true;
		}
	}

	return bChanged;
}

void AMoKuEditBaseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateBrushMaterial();
}

void AMoKuEditBaseActor::PostLoad()
{
	Super::PostLoad();
	EnsureDefaultTerrainInfluenceSettings();
}

void AMoKuEditBaseActor::PostEditUndo()
{
	Super::PostEditUndo();
}

void AMoKuEditBaseActor::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void AMoKuEditBaseActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FOnRoadBodyChangedParams Params(PropertyChangedEvent);
	Params.bUserTriggered = true;
	OnPostEditChangeProperty(Params);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void AMoKuEditBaseActor::OnPostEditChangeProperty(FOnRoadBodyChangedParams& InOutOnWaterBodyChangedParams)
{
	const FPropertyChangedEvent& PropertyChangedEvent = InOutOnWaterBodyChangedParams.PropertyChangedEvent;
	if (PropertyChangedEvent.MemberProperty)
	{
		const FName MemberName = PropertyChangedEvent.MemberProperty->GetFName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(AMoKuEditBaseActor, RoadWeightFalloffWidth))
		{
			EnsureDefaultTerrainInfluenceSettings();
			InOutOnWaterBodyChangedParams.bWeightmapSettingsChanged = true;
			InOutOnWaterBodyChangedParams.bShapeOrPositionChanged = true;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(AMoKuEditBaseActor, bAffectsLandscape))
		{
			EnsureDefaultTerrainInfluenceSettings();
			InOutOnWaterBodyChangedParams.bShapeOrPositionChanged = true;
		}
	}
	OnRoadBodyChanged(InOutOnWaterBodyChangedParams);
}

void AMoKuEditBaseActor::InitSceneManagement()
{
	if (!SceneManagement.IsValid() || !SceneManagement.Get())
	{
		UWorld* World = GetWorld();
		if (World)
		{
			for (TActorIterator<AWorldSceneManagement> It(World); It; ++It)
			{
				AWorldSceneManagement* FoundActorManager = *It;
				if (FoundActorManager)
				{
					SceneManagement = FoundActorManager;
					break;
				}
			}
		}
	}
}

void AMoKuEditBaseActor::Destroyed()
{
	if (AWorldSceneManagement* Manager = SceneManagement.Get())
	{
		Manager->HandleRoadActorRemoved(this);
	}
	else if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AWorldSceneManagement> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				(*It)->HandleRoadActorRemoved(this);
				break;
			}
		}
	}

	Super::Destroyed();
}

void AMoKuEditBaseActor::UpdateBrushMaterial()
{
}

void AMoKuEditBaseActor::InvalidateLandscapeDistanceFieldCache()
{
	InitSceneManagement();
	if (AWorldSceneManagement* Manager = SceneManagement.Get())
	{
		Manager->InvalidateBrushDistanceFieldCache();
	}
}

void AMoKuEditBaseActor::NotifyLandscapeBrushChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged, bool bUserTriggered)
{
	if (!bAffectsLandscape || bSuppressLandscapeBrushNotify)
	{
		return;
	}
	EnsureDefaultTerrainInfluenceSettings();
	InitSceneManagement();
	if (bShapeOrPositionChanged || bWeightmapSettingsChanged)
	{
		InvalidateLandscapeDistanceFieldCache();
	}
	FOnRoadBodyChangedParams Params;
	Params.PropertyChangedEvent.ChangeType = EPropertyChangeType::ValueSet;
	Params.bShapeOrPositionChanged = bShapeOrPositionChanged;
	Params.bWeightmapSettingsChanged = bWeightmapSettingsChanged;
	Params.bUserTriggered = bUserTriggered;
	OnRoadBodyChanged(Params);
}

void AMoKuEditBaseActor::PostActorCreated()
{
	Super::PostActorCreated();
	NotifyLandscapeBrushChanged(/*bShapeOrPositionChanged=*/true, /*bWeightmapSettingsChanged=*/true, /*bUserTriggered=*/false);
}

void AMoKuEditBaseActor::OnRoadBodyChanged(const FOnRoadBodyChangedParams& InParams)
{
	if (bSuppressLandscapeBrushNotify)
	{
		return;
	}
	IRoadBrushActorInterface::FRoadBrushActorChangedEventParams Params(this, InParams.PropertyChangedEvent);
	Params.bUserTriggered = InParams.bUserTriggered;
	Params.bShapeOrPositionChanged = InParams.bShapeOrPositionChanged;
	Params.bWeightmapSettingsChanged = InParams.bWeightmapSettingsChanged;
	this->BroadcastRoadBrushActorChangedEvent(Params);
}

#undef LOCTEXT_NAMESPACE
