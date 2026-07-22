#include "MoKuSplineActorCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "MoKuEditSplineActor.h"


TSharedRef<IDetailCustomization> FMoKuSplineConnectAssetMeshDetail::MakeInstance()
{

	return MakeShareable(new FMoKuSplineConnectAssetMeshDetail);
}

void FMoKuSplineConnectAssetMeshDetail::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<FName> CategoryNames;
	DetailBuilder.GetCategoryNames(CategoryNames);
	const TArray<FName> TransformCategoryNames = {
	FName("Transform"),        // 英文默认名
	FName(TEXT("变换")),       // 中文编辑器名
	FName("ActorTransform"),   // Actor 专属 Transform 别名
	FName("TransformCommon")   // UE 5.x 别名
	};

	for (const FName& TransCatName : TransformCategoryNames)
	{
			DetailBuilder.HideCategory(TransCatName);	
	}
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Selected Points", FText::GetEmpty(), ECategoryPriority::Important);
	DetailBuilder.HideCategory("Selected Points");

	for (FName CategoryName : CategoryNames)
	{
		Category = DetailBuilder.EditCategory(CategoryName);
		if (CategoryName != FName(TEXT("ExtraMesh"))&&CategoryName != FName( TEXT( "Spline"))&& CategoryName != FName(TEXT("BrushSetting"))&& CategoryName != FName(TEXT("Terrain")))
		{
			DetailBuilder.HideCategory(CategoryName);
		}
		else
		{
			Category.InitiallyCollapsed(true);
		}
			
	}

}