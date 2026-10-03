#include "MetaLandRoadCurbProfileFactory.h"
#include "MetaLandRoadCurbProfile.h"
#include "AssetTypeCategories.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaLandRoadCurbProfileFactory)

#define LOCTEXT_NAMESPACE "MetaLandRoadCurbProfileFactory"

UMetaLandRoadCurbProfileFactory::UMetaLandRoadCurbProfileFactory()
{
	SupportedClass = UMetaLandRoadCurbProfile::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMetaLandRoadCurbProfileFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	FName InName,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	UMetaLandRoadCurbProfile* Profile = NewObject<UMetaLandRoadCurbProfile>(InParent, InClass, InName, Flags);
	if (Profile)
	{
		Profile->EnsureDefaultCurve();
		if (Profile->ProfileId.IsNone())
		{
			Profile->ProfileId = InName;
		}
	}
	return Profile;
}

uint32 UMetaLandRoadCurbProfileFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

FText UMetaLandRoadCurbProfileFactory::GetDisplayName() const
{
	return LOCTEXT("CurbProfileDisplayName", "MetaLandRoad Curb Profile");
}

#undef LOCTEXT_NAMESPACE
