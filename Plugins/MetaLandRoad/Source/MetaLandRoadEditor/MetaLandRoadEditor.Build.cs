using UnrealBuildTool;

public class MetaLandRoadEditor : ModuleRules
{
	public MetaLandRoadEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MetaLandRoad",
			"InteractiveToolsFramework",
			"EditorInteractiveToolsFramework",
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			System.IO.Path.Combine(ModuleDirectory, "Private"),
			System.IO.Path.Combine(ModuleDirectory, "Private/Tools"),
			System.IO.Path.Combine(ModuleDirectory, "Private/Gizmos"),
			System.IO.Path.Combine(ModuleDirectory, "Private/Slate"),
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"EditorFramework",
			"ApplicationCore",
			"EditorStyle",
			"UnrealEd",
			"PropertyEditor",
			"ModelingComponents",
			"GizmoEdMode",
			"LevelEditor",
			"ToolMenus",
			"ContentBrowser",
			"ContentBrowserData",
			"Landscape",
			"Water",
			"Landmass",
			"EditorSubsystem",
			"WaterEditor",
			"DeveloperSettings",
			"LandscapeEditorUtilities",
			"LandscapeEditor",
			"PCGEditor",
			"ComponentVisualizers",
			"MaterialEditor",
			"AssetRegistry",
			"TypedElementFramework",
			"TypedElementRuntime",
			"Projects",
			"GeometryFramework",
			"GeometryCore",
			"GeometryAlgorithms",
			"DynamicMesh",
			"GeometryScriptingCore",
			"ModelingOperators",
			"RenderCore",
			"RHI",
		});
	}
}
