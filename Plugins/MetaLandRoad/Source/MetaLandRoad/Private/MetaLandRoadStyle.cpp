#include "MetaLandRoadStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyleRegistry.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/ToolBarStyle.h"
#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyle.h"
//#include "Misc/Paths.h"
//#include "Brushes/SlateImageBrush.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FMetaLandRoadStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
#define RootToContentDir StyleSet->RootToContentDir

FString FMetaLandRoadStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir =IPluginManager::Get().FindPlugin(TEXT("MetaLandRoad"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

TSharedPtr< FSlateStyleSet > FMetaLandRoadStyle::StyleSet = nullptr;
TSharedPtr< ISlateStyle > FMetaLandRoadStyle::Get() { return StyleSet; }

FName FMetaLandRoadStyle::GetStyleSetName()
{
	static FName MetaLandRoadStyleName(TEXT("MetaLandRoadStyle"));
	return MetaLandRoadStyleName;
}



BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void FMetaLandRoadStyle::Initialize()
{
	// Const icon sizes
	const FVector2D Icon20x20(20.0f, 20.0f);

	// Only register once
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	StyleSet->SetParentStyleName("EditorStyle");
	StyleSet->SetContentRoot(FPaths::EnginePluginsDir() / TEXT("MetaLandRoad/Content"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));
	const FTextBlockStyle& NormalText = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	StyleSet->Set("MetaLandRoadEditorModeCommands.InteractiveTool", new IMAGE_PLUGIN_BRUSH("Icons/EditSplines_x40", Icon20x20));
	StyleSet->Set("MetaLandRoadEditorModeCommands.InteractiveTool.Small", new IMAGE_PLUGIN_BRUSH("Icons/EditSplines_x40", Icon20x20));
	StyleSet->Set("MetaLandRoadEditorModeCommands.ShapesTool", new IMAGE_PLUGIN_BRUSH("Icons/MeshSelect_40x", Icon20x20));
	StyleSet->Set("MetaLandRoadEditorModeCommands.ShapesTool.Small", new IMAGE_PLUGIN_BRUSH("Icons/MeshSelect_40x", Icon20x20));
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
};

const FSlateBrush* FMetaLandRoadStyle::GetBrush(FName PropertyName, const ANSICHAR* Specifier)
{
	return Get()->GetBrush(PropertyName, Specifier);
}


END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef IMAGE_PLUGIN_BRUSH
#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef DEFAULT_FONT


void FMetaLandRoadStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

