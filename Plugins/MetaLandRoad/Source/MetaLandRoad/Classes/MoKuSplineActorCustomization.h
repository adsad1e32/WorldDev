#pragma once
#include "IDetailCustomization.h"


class FMoKuSplineConnectAssetMeshDetail  : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
