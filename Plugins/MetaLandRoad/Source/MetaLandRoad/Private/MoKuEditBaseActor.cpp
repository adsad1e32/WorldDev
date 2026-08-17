#include "MoKuEditBaseActor.h"
#include "LevelEditor.h"
#include "MetaLandRoadEditorMode.h"
#include "EditorModeManager.h"
#include  "EditSplineRoadInfo.h"
#include "LandscapeEditLayer.h"
#include "WorldEditorSettings.h"
#include "UObject/SoftObjectPath.h"

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

bool AMoKuEditBaseActor::EnsureDefaultTerrainInfluenceSettings()
{
	// BlueprintCompileReinstancer / preview actors can hit LevelActorAdded while TMaps are unsafe.
	if (!IsValid(this)
		|| bIsEditorPreviewActor
		|| HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate()
		|| IsActorBeingDestroyed()
		|| !bAffectsLandscape)
	{
		return false;
	}

	bool bDistanceFieldDirty = false;

	const UWorldEditorSettings* Settings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayerName = Settings ? Settings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	const FName ShoulderLayerName = Settings ? Settings->GetDefaultShoulderWeightmapLayerName() : FName(TEXT("Dirty"));

	static const FSoftObjectPath DefaultNoisePath(TEXT("/Game/Textures/BaseTextures/T_RoadWeightNoise.T_RoadWeightNoise"));

	// Only repair broken values. Never overwrite artist Falloff / Curl / Blur / Influence.
	auto SanitizeWeightEntry = [](FName LayerName, FRoadBodyWeightmapSettings& WeightSettings) -> bool
	{
		bool bChanged = false;
		if (!FMath::IsFinite(WeightSettings.TextureInfluence)
			|| WeightSettings.TextureInfluence < 0.0f
			|| WeightSettings.TextureInfluence > 1.0f + KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Layer '%s' TextureInfluence=%.1f invalid — clamp to 0..1"),
				*LayerName.ToString(), WeightSettings.TextureInfluence);
			WeightSettings.TextureInfluence = FMath::IsFinite(WeightSettings.TextureInfluence)
				? FMath::Clamp(WeightSettings.TextureInfluence, 0.0f, 1.0f)
				: 0.0f;
			bChanged = true;
		}
		if (!FMath::IsFinite(WeightSettings.TextureTiling) || WeightSettings.TextureTiling < 0.1f)
		{
			WeightSettings.TextureTiling = 4.0f;
			bChanged = true;
		}
		else
		{
			const float ClampedTiling = FMath::Clamp(WeightSettings.TextureTiling, 0.1f, 64.0f);
			if (!FMath::IsNearlyEqual(ClampedTiling, WeightSettings.TextureTiling))
			{
				WeightSettings.TextureTiling = ClampedTiling;
				bChanged = true;
			}
		}

		if (!FMath::IsFinite(WeightSettings.FalloffWidth) || WeightSettings.FalloffWidth <= KINDA_SMALL_NUMBER)
		{
			WeightSettings.FalloffWidth = 400.f;
			bChanged = true;
		}
		if (!FMath::IsFinite(WeightSettings.EdgeOffset) || FMath::Abs(WeightSettings.EdgeOffset) > 5000.f)
		{
			WeightSettings.EdgeOffset = 0.f;
			bChanged = true;
		}
		return bChanged;
	};

	// Narrow AA rim only — large Falloff (400–800) softens across the whole carriageway → gray mix.
	constexpr float RoadFalloffDefault = 180.f;
	if (RoadLayerName != NAME_None && !RoadBodyWeightmapSettings.Contains(RoadLayerName))
	{
		FRoadBodyWeightmapSettings RoadWeight;
		RoadWeight.FalloffWidth = RoadFalloffDefault;
		RoadWeight.EdgeOffset = 0.f;
		RoadWeight.TextureInfluence = 0.0f;
		RoadWeight.ModulationTexture = TSoftObjectPtr<UTexture2D>(DefaultNoisePath);
		RoadBodyWeightmapSettings.Add(RoadLayerName, RoadWeight);
		bDistanceFieldDirty = true;
	}

	FRoadFalloffSettings& Falloff = RoadHeightmapSettings.FalloffSettings;
	// Height FalloffWidth is artist-owned. Only seed when empty / non-finite.
	if (!FMath::IsFinite(Falloff.FalloffWidth) || Falloff.FalloffWidth <= KINDA_SMALL_NUMBER)
	{
		Falloff.FalloffWidth = 800.f;
		bDistanceFieldDirty = true;
	}
	if (!FMath::IsFinite(Falloff.ZOffset) || FMath::Abs(Falloff.ZOffset) > 5000.f)
	{
		Falloff.ZOffset = 0.f;
		bDistanceFieldDirty = true;
	}
	if (!FMath::IsFinite(Falloff.EdgeOffset) || FMath::Abs(Falloff.EdgeOffset) > 5000.f)
	{
		Falloff.EdgeOffset = 0.f;
		bDistanceFieldDirty = true;
	}
	if (!FMath::IsFinite(Falloff.FalloffAngle) || Falloff.FalloffAngle <= KINDA_SMALL_NUMBER)
	{
		Falloff.FalloffAngle = 45.f;
		bDistanceFieldDirty = true;
	}

	// Dirty / shoulder: wider falloff + outward EdgeOffset (slopes), not pavement core.
	if (ShoulderLayerName != NAME_None && ShoulderLayerName != RoadLayerName
		&& !RoadBodyWeightmapSettings.Contains(ShoulderLayerName))
	{
		FRoadBodyWeightmapSettings ShoulderWeight;
		ShoulderWeight.FalloffWidth = FMath::Clamp(Falloff.FalloffWidth, 400.f, 2500.f);
		ShoulderWeight.EdgeOffset = ShoulderWeight.FalloffWidth * 0.5f;
		ShoulderWeight.TextureInfluence = 0.0f;
		ShoulderWeight.ModulationTexture = TSoftObjectPtr<UTexture2D>(DefaultNoisePath);
		RoadBodyWeightmapSettings.Add(ShoulderLayerName, ShoulderWeight);
		bDistanceFieldDirty = true;
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Added shoulder weight layer '%s' Falloff=%.0f EdgeOffset=%.0f (slopes)"),
			*ShoulderLayerName.ToString(), ShoulderWeight.FalloffWidth, ShoulderWeight.EdgeOffset);
	}

	FRoadBrushEffectBlurring& Blur = RoadHeightmapSettings.Effects.Blurring;
	if (Blur.Radius < 0 || Blur.Radius > 32)
	{
		Blur.Radius = 5;
		bDistanceFieldDirty = true;
	}

	FRoadBrushEffectDisplacement& Displacement = RoadHeightmapSettings.Effects.Displacement;
	if (!Displacement.Texture.ToSoftObjectPath().IsValid())
	{
		Displacement.Texture = TSoftObjectPtr<UTexture2D>(DefaultNoisePath);
	}
	if (!FMath::IsFinite(Displacement.DisplacementTiling) || Displacement.DisplacementTiling <= KINDA_SMALL_NUMBER)
	{
		Displacement.DisplacementTiling = 2.0f;
	}
	if (!FMath::IsFinite(Displacement.WeightmapInfluence)
		|| Displacement.WeightmapInfluence < 0.0f
		|| Displacement.WeightmapInfluence > 1.0f)
	{
		Displacement.WeightmapInfluence = FMath::IsFinite(Displacement.WeightmapInfluence)
			? FMath::Clamp(Displacement.WeightmapInfluence, 0.0f, 1.0f)
			: 0.0f;
	}

	FRoadBrushEffectSmoothBlending& Smooth = RoadHeightmapSettings.Effects.SmoothBlending;
	if (!FMath::IsFinite(Smooth.InnerSmoothDistance) || Smooth.InnerSmoothDistance < 0.01f)
	{
		Smooth.InnerSmoothDistance = 32.0f;
		bDistanceFieldDirty = true;
	}
	if (!FMath::IsFinite(Smooth.OuterSmoothDistance) || Smooth.OuterSmoothDistance < 0.01f)
	{
		Smooth.OuterSmoothDistance = 128.0f;
		bDistanceFieldDirty = true;
	}
	// One-time undo of previous auto-forced Smooth (768/128) that flattened distance falloff into a bowl.
	if (FMath::IsNearlyEqual(Smooth.OuterSmoothDistance, 768.0f, 1.0f)
		&& Smooth.InnerSmoothDistance >= 64.0f)
	{
		Smooth.InnerSmoothDistance = 32.0f;
		Smooth.OuterSmoothDistance = 128.0f;
		bDistanceFieldDirty = true;
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Reset over-forced SmoothBlending to 32/128 so FalloffWidth distance works"));
	}

	for (TPair<FName, FRoadBodyWeightmapSettings>& Pair : RoadBodyWeightmapSettings)
	{
		bDistanceFieldDirty |= SanitizeWeightEntry(Pair.Key, Pair.Value);
	}

	return bDistanceFieldDirty;
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
		if (MemberName == GET_MEMBER_NAME_CHECKED(AMoKuEditBaseActor, RoadBodyWeightmapSettings))
		{
			// Repair broken Influence/NaN only — do not rewrite Falloff artist values.
			EnsureDefaultTerrainInfluenceSettings();
			InOutOnWaterBodyChangedParams.bWeightmapSettingsChanged = true;
			InOutOnWaterBodyChangedParams.bShapeOrPositionChanged = true;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(AMoKuEditBaseActor, RoadHeightmapSettings)
			|| MemberName == GET_MEMBER_NAME_CHECKED(AMoKuEditBaseActor, bAffectsLandscape))
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

void AMoKuEditBaseActor::NotifyLandscapeBrushChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged, bool bUserTriggered)
{
	if (!bAffectsLandscape || bSuppressLandscapeBrushNotify)
	{
		return;
	}
	EnsureDefaultTerrainInfluenceSettings();
	InitSceneManagement();
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
