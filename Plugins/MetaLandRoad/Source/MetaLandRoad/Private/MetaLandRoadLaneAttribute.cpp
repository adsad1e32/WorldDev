#include "MetaLandRoadLaneAttribute.h"
#include "Serialization/Archive.h"

const FName FMetaLandRoadAttributeRegistry::TypeMark(TEXT("Mark"));
const FName FMetaLandRoadAttributeRegistry::TypeSpeed(TEXT("Speed"));
const FName FMetaLandRoadAttributeRegistry::TypeCurbCut(TEXT("CurbCut"));
const FName FMetaLandRoadAttributeRegistry::TypeLandscape(TEXT("Landscape"));
const FName FMetaLandRoadAttributeRegistry::TypePolygon(TEXT("Polygon"));
const FName FMetaLandRoadAttributeRegistry::TypeSidewalkHeight(TEXT("SidewalkHeight"));
const FName FMetaLandRoadAttributeRegistry::TypePropSetting(TEXT("PropSetting"));

static TArray<FMetaLandRoadAttributeTypeDesc> GAttributeTypes;
static bool GAttributeTypesRegistered = false;

void FMetaLandRoadLaneAttributeValue::InterpolateWith(const FMetaLandRoadLaneAttributeValue& Other, float Alpha)
{
	(void)Other;
	(void)Alpha;
}

FMetaLandRoadLandscapeAttributeValue::FMetaLandRoadLandscapeAttributeValue()
{
	SideFalloff = 2000.0f;
	SideOffset = 0.0f;
	HeightOffset = -5.0f;
	SyncFalloffFromMetaRoadFields();
}

void FMetaLandRoadLandscapeAttributeValue::MigrateLegacyFields()
{
	const bool bFalloffLooksDefault =
		FMath::IsNearlyEqual(FalloffSettings.FalloffWidth, 800.0f, 0.1f)
		&& FMath::IsNearlyEqual(FalloffSettings.ZOffset, -5.0f, 0.1f)
		&& FMath::IsNearlyZero(FalloffSettings.EdgeOffset);
	if (!bFalloffLooksDefault)
	{
		SyncLegacyFromFalloff();
		return;
	}

	FalloffSettings.FalloffWidth = SideFalloff;
	FalloffSettings.EdgeOffset = SideOffset;
	FalloffSettings.ZOffset = HeightOffset;
	SyncLegacyFromFalloff();
}

void FMetaLandRoadLandscapeAttributeValue::SyncLegacyFromFalloff()
{
	SideFalloff = FalloffSettings.FalloffWidth;
	SideOffset = FalloffSettings.EdgeOffset;
	HeightOffset = FalloffSettings.ZOffset;
}

void FMetaLandRoadLandscapeAttributeValue::SyncFalloffFromMetaRoadFields()
{
	FalloffSettings.FalloffMode = ERoadBrushFalloffMode::Width;
	FalloffSettings.FalloffWidth = FMath::Max(SideFalloff, 1.0f);
	FalloffSettings.EdgeOffset = SideOffset;
	FalloffSettings.ZOffset = HeightOffset;
}

void FMetaLandRoadLandscapeAttributeValue::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		MigrateLegacyFields();
	}
}

void FMetaLandRoadLandscapeAttributeValue::InterpolateWith(const FMetaLandRoadLaneAttributeValue& Other, float Alpha)
{
	const FMetaLandRoadLandscapeAttributeValue& OtherValue = static_cast<const FMetaLandRoadLandscapeAttributeValue&>(Other);
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	LayerFactor = FMath::Lerp(LayerFactor, OtherValue.LayerFactor, T);
	SideFalloff = FMath::Lerp(SideFalloff, OtherValue.SideFalloff, T);
	SideOffset = FMath::Lerp(SideOffset, OtherValue.SideOffset, T);
	HeightOffset = FMath::Lerp(HeightOffset, OtherValue.HeightOffset, T);
	const float W0 = RoadWeightFalloffWidth >= 1.0f ? RoadWeightFalloffWidth : OtherValue.RoadWeightFalloffWidth;
	const float W1 = OtherValue.RoadWeightFalloffWidth >= 1.0f ? OtherValue.RoadWeightFalloffWidth : RoadWeightFalloffWidth;
	RoadWeightFalloffWidth = (W0 >= 1.0f && W1 >= 1.0f)
		? FMath::Lerp(W0, W1, T)
		: FMath::Max(W0, W1);
	SyncFalloffFromMetaRoadFields();
}

void FMetaLandRoadLandscapeAttributeValue::InterpolateCatmullRom(
	const FMetaLandRoadLandscapeAttributeValue& P0,
	const FMetaLandRoadLandscapeAttributeValue& P1,
	const FMetaLandRoadLandscapeAttributeValue& P2,
	const FMetaLandRoadLandscapeAttributeValue& P3,
	float T)
{
	const float ClampedT = FMath::Clamp(T, 0.0f, 1.0f);
	auto Sample = [ClampedT](float V0, float V1, float V2, float V3)
	{
		const float T2 = ClampedT * ClampedT;
		const float T3 = T2 * ClampedT;
		return 0.5f * ((2.0f * V1)
			+ (-V0 + V2) * ClampedT
			+ (2.0f * V0 - 5.0f * V1 + 4.0f * V2 - V3) * T2
			+ (-V0 + 3.0f * V1 - 3.0f * V2 + V3) * T3);
	};

	LayerFactor = Sample(P0.LayerFactor, P1.LayerFactor, P2.LayerFactor, P3.LayerFactor);
	SideFalloff = FMath::Max(0.1f, Sample(P0.SideFalloff, P1.SideFalloff, P2.SideFalloff, P3.SideFalloff));
	SideOffset = Sample(P0.SideOffset, P1.SideOffset, P2.SideOffset, P3.SideOffset);
	HeightOffset = Sample(P0.HeightOffset, P1.HeightOffset, P2.HeightOffset, P3.HeightOffset);
	auto SolidW = [](float W) { return W >= 1.0f ? W : 0.0f; };
	const float A1 = SolidW(P1.RoadWeightFalloffWidth);
	const float A2 = SolidW(P2.RoadWeightFalloffWidth);
	if (A1 >= 1.0f || A2 >= 1.0f)
	{
		const float A0 = SolidW(P0.RoadWeightFalloffWidth);
		const float A3 = SolidW(P3.RoadWeightFalloffWidth);
		const float S1 = A1 >= 1.0f ? A1 : A2;
		const float S2 = A2 >= 1.0f ? A2 : S1;
		const float S0 = A0 >= 1.0f ? A0 : S1;
		const float S3 = A3 >= 1.0f ? A3 : S2;
		RoadWeightFalloffWidth = FMath::Max(1.0f, Sample(S0, S1, S2, S3));
	}
	SyncFalloffFromMetaRoadFields();
}

void FMetaLandRoadSidewalkHeightAttributeValue::InterpolateWith(const FMetaLandRoadLaneAttributeValue& Other, float Alpha)
{
	const FMetaLandRoadSidewalkHeightAttributeValue& OtherValue = static_cast<const FMetaLandRoadSidewalkHeightAttributeValue&>(Other);
	Height = FMath::Lerp(Height, OtherValue.Height, FMath::Clamp(Alpha, 0.0f, 1.0f));
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadMarkAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypeMark;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "Mark", "Mark");
	Desc.Category = TEXT("Marking");
	Desc.IconName = TEXT("ClassIcon.Actor");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Stepped;
	Desc.bAllowOnLane = true;
	Desc.bAllowOnSectionCenter = false;
	return Desc;
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadSpeedAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypeSpeed;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "Speed", "Speed");
	Desc.Category = TEXT("Data");
	Desc.IconName = TEXT("ClassIcon.Actor");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Stepped;
	Desc.bAllowOnLane = true;
	Desc.bAllowOnSectionCenter = false;
	return Desc;
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadCurbCutAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypeCurbCut;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "CurbCut", "Curb Cut");
	Desc.Category = TEXT("Geometry");
	Desc.IconName = TEXT("GraphEditor.Curve_16x");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Stepped;
	Desc.bAllowOnLane = true;
	Desc.bAllowOnSectionCenter = true;
	return Desc;
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadLandscapeAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypeLandscape;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "Landscape", "Landscape");
	Desc.Category = TEXT("Landscape");
	Desc.IconName = TEXT("ClassIcon.Landscape");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Interpolated;
	Desc.bAllowOnLane = false;
	Desc.bAllowOnSectionCenter = true;
	return Desc;
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadPolygonAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypePolygon;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "Polygon", "Polygon");
	Desc.Category = TEXT("Marking");
	Desc.IconName = TEXT("Icons.Transform");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Stepped;
	Desc.bAllowOnLane = true;
	Desc.bAllowOnSectionCenter = false;
	return Desc;
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadSidewalkHeightAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypeSidewalkHeight;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "Sidewalk", "Sidewalk");
	Desc.Category = TEXT("Geometry");
	Desc.IconName = TEXT("ClassIcon.StaticMeshActor");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Interpolated;
	Desc.bAllowOnLane = true;
	Desc.bAllowOnSectionCenter = true;
	return Desc;
}

FMetaLandRoadAttributeTypeDesc FMetaLandRoadPropSettingAttributeValue::GetTypeDesc()
{
	FMetaLandRoadAttributeTypeDesc Desc;
	Desc.TypeId = FMetaLandRoadAttributeRegistry::TypePropSetting;
	Desc.DisplayName = NSLOCTEXT("MetaLandRoadAttribute", "PropSetting", "PropSetting");
	Desc.Category = TEXT("Prop");
	Desc.IconName = TEXT("ClassIcon.StaticMeshActor");
	Desc.ValueStruct = StaticStruct();
	Desc.EvalMode = EMetaLandRoadAttributeEvalMode::Stepped;
	Desc.bAllowOnLane = true;
	Desc.bAllowOnSectionCenter = true;
	return Desc;
}

bool FMetaLandRoadLaneAttributeKey::operator<(const FMetaLandRoadLaneAttributeKey& Other) const
{
	return SOffset < Other.SOffset;
}

void FMetaLandRoadAttributeRegistry::RegisterBuiltInTypes()
{
	if (GAttributeTypesRegistered)
	{
		return;
	}

	GAttributeTypesRegistered = true;
	RegisterType(FMetaLandRoadMarkAttributeValue::GetTypeDesc());
	RegisterType(FMetaLandRoadSpeedAttributeValue::GetTypeDesc());
	RegisterType(FMetaLandRoadCurbCutAttributeValue::GetTypeDesc());
	RegisterType(FMetaLandRoadLandscapeAttributeValue::GetTypeDesc());
	RegisterType(FMetaLandRoadPolygonAttributeValue::GetTypeDesc());
	RegisterType(FMetaLandRoadSidewalkHeightAttributeValue::GetTypeDesc());
	RegisterType(FMetaLandRoadPropSettingAttributeValue::GetTypeDesc());
}

bool FMetaLandRoadAttributeRegistry::RegisterType(const FMetaLandRoadAttributeTypeDesc& Desc)
{
	if (Desc.TypeId.IsNone() || Desc.ValueStruct == nullptr)
	{
		return false;
	}

	if (FMetaLandRoadAttributeTypeDesc* Existing = const_cast<FMetaLandRoadAttributeTypeDesc*>(FindType(Desc.TypeId)))
	{
		*Existing = Desc;
		return true;
	}

	GAttributeTypes.Add(Desc);
	return true;
}

const FMetaLandRoadAttributeTypeDesc* FMetaLandRoadAttributeRegistry::FindType(FName TypeId)
{
	RegisterBuiltInTypes();
	for (const FMetaLandRoadAttributeTypeDesc& Desc : GAttributeTypes)
	{
		if (Desc.TypeId == TypeId)
		{
			return &Desc;
		}
	}
	return nullptr;
}

const TArray<FMetaLandRoadAttributeTypeDesc>& FMetaLandRoadAttributeRegistry::GetTypes()
{
	RegisterBuiltInTypes();
	return GAttributeTypes;
}

TInstancedStruct<FMetaLandRoadLaneAttributeValue> FMetaLandRoadAttributeRegistry::MakeDefaultValue(FName TypeId)
{
	TInstancedStruct<FMetaLandRoadLaneAttributeValue> Value;
	if (const FMetaLandRoadAttributeTypeDesc* Desc = FindType(TypeId))
	{
		if (Desc->ValueStruct)
		{
			Value.InitializeAsScriptStruct(Desc->ValueStruct);
		}
	}
	return Value;
}

bool FMetaLandRoadAttributeRegistry::CanPinOn(FName TypeId, EMetaLandRoadAttributeHost Host)
{
	const FMetaLandRoadAttributeTypeDesc* Desc = FindType(TypeId);
	if (!Desc)
	{
		return false;
	}

	if (Host == EMetaLandRoadAttributeHost::Lane)
	{
		return Desc->bAllowOnLane;
	}

	return Desc->bAllowOnSectionCenter;
}

FMetaLandRoadPropRail* FMetaLandRoadLaneAttribute::FindRail(const FGuid& RailId)
{
	const int32 Index = FindRailIndex(RailId);
	return Rails.IsValidIndex(Index) ? &Rails[Index] : nullptr;
}

const FMetaLandRoadPropRail* FMetaLandRoadLaneAttribute::FindRail(const FGuid& RailId) const
{
	const int32 Index = FindRailIndex(RailId);
	return Rails.IsValidIndex(Index) ? &Rails[Index] : nullptr;
}

int32 FMetaLandRoadLaneAttribute::FindRailIndex(const FGuid& RailId) const
{
	for (int32 Index = 0; Index < Rails.Num(); ++Index)
	{
		if (Rails[Index].RailId == RailId)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void FMetaLandRoadLaneAttribute::Trim(double S0, double S1)
{
	if (TypeId == FMetaLandRoadAttributeRegistry::TypePropSetting)
	{
		for (FMetaLandRoadPropRail& Rail : Rails)
		{
			for (int32 KeyIndex = Rail.Keys.Num() - 1; KeyIndex >= 0; --KeyIndex)
			{
				if (Rail.Keys[KeyIndex].SOffset < S0 || Rail.Keys[KeyIndex].SOffset > S1)
				{
					if (KeyIndex != 0 && KeyIndex != Rail.Keys.Num() - 1)
					{
						Rail.Keys.RemoveAt(KeyIndex);
					}
				}
			}
			for (FMetaLandRoadPropRailKey& Key : Rail.Keys)
			{
				Key.SOffset = FMath::Clamp(Key.SOffset, S0, S1) - S0;
			}
			Rail.EnsureSpanKeys(S1 - S0);
		}
		return;
	}

	int32 StartKey = INDEX_NONE;
	int32 EndKey = INDEX_NONE;
	for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		const double KeyS = Keys[KeyIndex].SOffset;
		if (KeyS <= S0)
		{
			StartKey = KeyIndex;
		}
		if (KeyS >= S1)
		{
			EndKey = KeyIndex;
			break;
		}
	}

	if (EndKey != INDEX_NONE && (Keys.Num() - 1 != EndKey))
	{
		Keys.SetNum(EndKey + 1);
	}

	if (StartKey != INDEX_NONE && StartKey != 0)
	{
		Keys.RemoveAt(0, StartKey);
	}

	if (Keys.Num())
	{
		if (Keys[0].SOffset < S0)
		{
			Keys[0].SOffset = S0;
		}

		if (Keys[Keys.Num() - 1].SOffset > S1)
		{
			Keys[Keys.Num() - 1].SOffset = S1;
		}
	}

	EnsureFirstKeyAtZero();
}

void FMetaLandRoadLaneAttribute::SortKeys()
{
	Keys.Sort();
}

void FMetaLandRoadLaneAttribute::EnsureFirstKeyAtZero()
{
	if (TypeId == FMetaLandRoadAttributeRegistry::TypePropSetting)
	{
		return;
	}

	if (Keys.Num() == 0)
	{
		FMetaLandRoadLaneAttributeKey FirstKey;
		FirstKey.SOffset = 0.0;
		FirstKey.Value = FMetaLandRoadAttributeRegistry::MakeDefaultValue(TypeId);
		Keys.Add(FirstKey);
		return;
	}

	SortKeys();
	if (Keys[0].SOffset > KINDA_SMALL_NUMBER)
	{
		FMetaLandRoadLaneAttributeKey FirstKey = Keys[0];
		FirstKey.SOffset = 0.0;
		Keys.Insert(FirstKey, 0);
	}
	else
	{
		Keys[0].SOffset = 0.0;
	}

}

void FMetaLandRoadLaneAttribute::EnsureBeginAndEndKeys(double EndS)
{
	EnsureFirstKeyAtZero();

	const double ClampedEnd = FMath::Max(EndS, 0.0);
	if (ClampedEnd <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (Keys.Num() == 1)
	{
		FMetaLandRoadLaneAttributeKey EndKey = Keys[0];
		EndKey.SOffset = ClampedEnd;
		Keys.Add(EndKey);
		return;
	}

	Keys.Last().SOffset = ClampedEnd;
	for (int32 KeyIndex = 1; KeyIndex < Keys.Num() - 1; ++KeyIndex)
	{
		Keys[KeyIndex].SOffset = FMath::Clamp(Keys[KeyIndex].SOffset, 0.0, ClampedEnd);
	}
	SortKeys();
	Keys[0].SOffset = 0.0;
	Keys.Last().SOffset = ClampedEnd;
}

int32 FMetaLandRoadLaneAttribute::AddKey(double SOffset, double DuplicateTolerance)
{
	EnsureFirstKeyAtZero();

	const double ClampedS = FMath::Max(SOffset, 0.0);
	const int32 Existing = FindKeyIndex(ClampedS, DuplicateTolerance);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	FMetaLandRoadLaneAttributeKey NewKey;
	NewKey.SOffset = ClampedS;
	if (!EvalAt(ClampedS, NewKey.Value))
	{
		NewKey.Value = FMetaLandRoadAttributeRegistry::MakeDefaultValue(TypeId);
	}

	Keys.Add(NewKey);
	SortKeys();
	return FindKeyIndex(ClampedS, DuplicateTolerance);
}

bool FMetaLandRoadLaneAttribute::CanRemoveKey(int32 KeyIndex) const
{
	if (!Keys.IsValidIndex(KeyIndex) || Keys.Num() <= 1 || KeyIndex == 0)
	{
		return false;
	}

	if (TypeId == FMetaLandRoadAttributeRegistry::TypeLandscape && KeyIndex == Keys.Num() - 1)
	{
		return false;
	}

	return true;
}

bool FMetaLandRoadLaneAttribute::IsBeginKey(int32 KeyIndex) const
{
	return KeyIndex == 0 && Keys.IsValidIndex(KeyIndex);
}

bool FMetaLandRoadLaneAttribute::IsEndKey(int32 KeyIndex) const
{
	return Keys.Num() >= 2 && KeyIndex == Keys.Num() - 1;
}

bool FMetaLandRoadLaneAttribute::RemoveKey(int32 KeyIndex)
{
	if (!CanRemoveKey(KeyIndex))
	{
		return false;
	}

	Keys.RemoveAt(KeyIndex);
	return true;
}

bool FMetaLandRoadLaneAttribute::SetKeySOffset(int32 KeyIndex, double NewSOffset, double MaxSOffset)
{
	if (!Keys.IsValidIndex(KeyIndex))
	{
		return false;
	}

	if (KeyIndex == 0)
	{
		Keys[0].SOffset = 0.0;
		return false;
	}

	if (TypeId == FMetaLandRoadAttributeRegistry::TypeLandscape && IsEndKey(KeyIndex))
	{
		if (MaxSOffset >= 0.0)
		{
			Keys[KeyIndex].SOffset = MaxSOffset;
		}
		return false;
	}

	double ClampedS = FMath::Max(NewSOffset, 0.0);
	if (MaxSOffset >= 0.0)
	{
		ClampedS = FMath::Min(ClampedS, MaxSOffset);
	}

	Keys[KeyIndex].SOffset = ClampedS;
	SortKeys();
	if (Keys.Num() > 0)
	{
		Keys[0].SOffset = 0.0;
	}
	return true;
}

int32 FMetaLandRoadLaneAttribute::FindKeyIndex(double SOffset, double Tolerance) const
{
	const double SafeTolerance = FMath::Max(Tolerance, 0.0);
	int32 BestIndex = INDEX_NONE;
	double BestDelta = TNumericLimits<double>::Max();
	for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		const double Delta = FMath::Abs(Keys[KeyIndex].SOffset - SOffset);
		if (Delta <= SafeTolerance && Delta < BestDelta)
		{
			BestDelta = Delta;
			BestIndex = KeyIndex;
		}
	}
	return BestIndex;
}

void FMetaLandRoadLaneAttribute::ApplyFalloffToKey(int32 KeyIndex, const FRoadFalloffSettings& Falloff)
{
	if (!Keys.IsValidIndex(KeyIndex))
	{
		return;
	}

	FMetaLandRoadLandscapeAttributeValue Value;
	if (const FMetaLandRoadLandscapeAttributeValue* Existing =
		Keys[KeyIndex].Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>())
	{
		Value = *Existing;
	}
	Value.FalloffSettings = Falloff;
	Value.SyncLegacyFromFalloff();
	Keys[KeyIndex].Value.InitializeAs<FMetaLandRoadLandscapeAttributeValue>(Value);
}

void FMetaLandRoadLaneAttribute::ApplyFalloffToAllKeys(const FRoadFalloffSettings& Falloff)
{
	for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		ApplyFalloffToKey(KeyIndex, Falloff);
	}
}

bool FMetaLandRoadLaneAttribute::GetKeyFalloff(int32 KeyIndex, FRoadFalloffSettings& OutFalloff) const
{
	if (!Keys.IsValidIndex(KeyIndex))
	{
		return false;
	}

		if (const FMetaLandRoadLandscapeAttributeValue* LandscapeValue =
			Keys[KeyIndex].Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>())
		{
			FMetaLandRoadLandscapeAttributeValue Synced = *LandscapeValue;
			Synced.MigrateLegacyFields();
			Synced.SyncFalloffFromMetaRoadFields();
			OutFalloff = Synced.FalloffSettings;
			return true;
		}
	return false;
}

bool FMetaLandRoadLaneAttribute::EvalAt(double SOffset, TInstancedStruct<FMetaLandRoadLaneAttributeValue>& OutValue) const
{
	if (Keys.Num() == 0)
	{
		OutValue = FMetaLandRoadAttributeRegistry::MakeDefaultValue(TypeId);
		return OutValue.IsValid();
	}

	if (SOffset <= Keys[0].SOffset)
	{
		OutValue = Keys[0].Value;
		return OutValue.IsValid();
	}

	const int32 LastIndex = Keys.Num() - 1;
	if (SOffset >= Keys[LastIndex].SOffset)
	{
		OutValue = Keys[LastIndex].Value;
		return OutValue.IsValid();
	}

	int32 LeftIndex = 0;
	for (int32 KeyIndex = 0; KeyIndex < LastIndex; ++KeyIndex)
	{
		if (SOffset >= Keys[KeyIndex].SOffset && SOffset <= Keys[KeyIndex + 1].SOffset)
		{
			LeftIndex = KeyIndex;
			break;
		}
	}

	const FMetaLandRoadLaneAttributeKey& LeftKey = Keys[LeftIndex];
	const FMetaLandRoadLaneAttributeKey& RightKey = Keys[LeftIndex + 1];
	OutValue = LeftKey.Value;

	const FMetaLandRoadAttributeTypeDesc* Desc = FMetaLandRoadAttributeRegistry::FindType(TypeId);
	const bool bInterpolate = Desc && Desc->EvalMode == EMetaLandRoadAttributeEvalMode::Interpolated;
	if (!bInterpolate || !OutValue.IsValid() || !RightKey.Value.IsValid())
	{
		return OutValue.IsValid();
	}

	if (OutValue.GetScriptStruct() != RightKey.Value.GetScriptStruct())
	{
		return true;
	}

	const double Span = RightKey.SOffset - LeftKey.SOffset;
	const float Alpha = (Span > KINDA_SMALL_NUMBER)
		? static_cast<float>((SOffset - LeftKey.SOffset) / Span)
		: 0.0f;

	if (TypeId == FMetaLandRoadAttributeRegistry::TypeLandscape)
	{
		const FMetaLandRoadLandscapeAttributeValue* P1 = LeftKey.Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>();
		const FMetaLandRoadLandscapeAttributeValue* P2 = RightKey.Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>();
		if (P1 && P2)
		{
			if (FMetaLandRoadLandscapeAttributeValue* Dest = OutValue.GetMutablePtr<FMetaLandRoadLandscapeAttributeValue>())
			{
				// Perlin smootherstep: hold the key, S-curve in the span.
				// Hermite 3t²-2t³ is still a straight run through the middle.
				const float Ease = Alpha * Alpha * Alpha * (Alpha * (Alpha * 6.0f - 15.0f) + 10.0f);
				const FMetaLandRoadLandscapeAttributeValue* P0 = (LeftIndex > 0)
					? Keys[LeftIndex - 1].Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>()
					: P1;
				const FMetaLandRoadLandscapeAttributeValue* P3 = (LeftIndex + 1 < LastIndex)
					? Keys[LeftIndex + 2].Value.GetPtr<FMetaLandRoadLandscapeAttributeValue>()
					: P2;
				if (P0 && P3 && (LeftIndex > 0 || LeftIndex + 1 < LastIndex))
				{
					Dest->InterpolateCatmullRom(*P0, *P1, *P2, *P3, Ease);
					auto ClampSpan = [](float Value, float A, float B)
					{
						return FMath::Clamp(Value, FMath::Min(A, B), FMath::Max(A, B));
					};
					Dest->LayerFactor = ClampSpan(Dest->LayerFactor, P1->LayerFactor, P2->LayerFactor);
					Dest->SideFalloff = ClampSpan(Dest->SideFalloff, P1->SideFalloff, P2->SideFalloff);
					Dest->SideOffset = ClampSpan(Dest->SideOffset, P1->SideOffset, P2->SideOffset);
					Dest->HeightOffset = ClampSpan(Dest->HeightOffset, P1->HeightOffset, P2->HeightOffset);
					Dest->SyncFalloffFromMetaRoadFields();
				}
				else
				{
					*Dest = *P1;
					Dest->InterpolateWith(*P2, Ease);
				}
			}
			return true;
		}
	}

	if (FMetaLandRoadLaneAttributeValue* LeftValue = OutValue.GetMutablePtr())
	{
		if (const FMetaLandRoadLaneAttributeValue* RightValue = RightKey.Value.GetPtr())
		{
			LeftValue->InterpolateWith(*RightValue, Alpha);
		}
	}

	return true;
}

bool FMetaLandRoadLaneAttribute::EvalAsLandscape(double SOffset, FMetaLandRoadLandscapeAttributeValue& OutValue) const
{
	TInstancedStruct<FMetaLandRoadLaneAttributeValue> EvalValue;
	if (!EvalAt(SOffset, EvalValue))
	{
		return false;
	}

	if (const FMetaLandRoadLandscapeAttributeValue* LandscapeValue = EvalValue.GetPtr<FMetaLandRoadLandscapeAttributeValue>())
	{
		OutValue = *LandscapeValue;
		return true;
	}

	return false;
}

FMetaLandRoadLaneAttribute* FMetaLandRoadLaneAttribute::PinOnMap(
	TMap<FName, FMetaLandRoadLaneAttribute>& Map,
	FName InTypeId,
	EMetaLandRoadAttributeHost Host)
{
	if (!FMetaLandRoadAttributeRegistry::CanPinOn(InTypeId, Host))
	{
		return nullptr;
	}

	if (FMetaLandRoadLaneAttribute* Existing = Map.Find(InTypeId))
	{
		Existing->TypeId = InTypeId;
		Existing->EnsureFirstKeyAtZero();
		return Existing;
	}

	FMetaLandRoadLaneAttribute& NewTrack = Map.Add(InTypeId);
	NewTrack.TypeId = InTypeId;
	NewTrack.EnsureFirstKeyAtZero();
	return &NewTrack;
}

bool FMetaLandRoadLaneAttribute::UnpinFromMap(TMap<FName, FMetaLandRoadLaneAttribute>& Map, FName InTypeId)
{
	return Map.Remove(InTypeId) > 0;
}

FMetaLandRoadLaneAttribute* FMetaLandRoadLaneAttribute::FindOnMap(TMap<FName, FMetaLandRoadLaneAttribute>& Map, FName InTypeId)
{
	return Map.Find(InTypeId);
}

const FMetaLandRoadLaneAttribute* FMetaLandRoadLaneAttribute::FindOnMap(const TMap<FName, FMetaLandRoadLaneAttribute>& Map, FName InTypeId)
{
	return Map.Find(InTypeId);
}
