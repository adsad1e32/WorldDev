#include "MoKuSplineActorCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "MoKuSplineActorCustomization"

TSharedRef<IDetailCustomization> FMoKuSplineConnectAssetMeshDetail::MakeInstance()
{
	return MakeShareable(new FMoKuSplineConnectAssetMeshDetail);
}

void FMoKuSplineConnectAssetMeshDetail::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<FName> CategoryNames;
	DetailBuilder.GetCategoryNames(CategoryNames);
	const TArray<FName> TransformCategoryNames = {
		FName("Transform"),
		FName(TEXT("变换")),
		FName("ActorTransform"),
		FName("TransformCommon")
	};

	for (const FName& TransCatName : TransformCategoryNames)
	{
		DetailBuilder.HideCategory(TransCatName);
	}

	DetailBuilder.HideCategory("Selected Points");

	for (FName CategoryName : CategoryNames)
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(CategoryName);
		const bool bVisibleCategory =
			CategoryName == FName(TEXT("Road"))
			|| CategoryName == FName(TEXT("RoadLayout"))
			|| CategoryName == FName(TEXT("LaneSection"))
			|| CategoryName == FName(TEXT("RoadLane"))
			|| CategoryName == FName(TEXT("ExtraMesh"))
			|| CategoryName == FName(TEXT("Spline"))
			|| CategoryName == FName(TEXT("BrushSetting"))
			|| CategoryName == FName(TEXT("Terrain"));

		if (!bVisibleCategory)
		{
			DetailBuilder.HideCategory(CategoryName);
		}
		else if (CategoryName == FName(TEXT("Road")) || CategoryName == FName(TEXT("RoadLayout")))
		{
			Category.InitiallyCollapsed(false);
		}
		else
		{
			Category.InitiallyCollapsed(true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
