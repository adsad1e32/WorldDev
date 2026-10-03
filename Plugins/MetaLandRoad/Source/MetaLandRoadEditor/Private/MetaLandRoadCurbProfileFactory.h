#pragma once

#include "Factories/Factory.h"
#include "MetaLandRoadCurbProfileFactory.generated.h"

UCLASS()
class UMetaLandRoadCurbProfileFactory : public UFactory
{
	GENERATED_BODY()

public:
	UMetaLandRoadCurbProfileFactory();

	virtual UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;

	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
};
