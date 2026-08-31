using UnrealBuildTool;

public class MetaLandRoad : ModuleRules
{
	public MetaLandRoad(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"StructUtils",
			"Projects",
			"ModelingOperators",
			"GeometryFramework",
			"GeometryCore",
			"GeometryAlgorithms",
			"DynamicMesh",
			"GeometryScriptingCore",
			"StaticMeshDescription",
			"RenderCore",
			"RHI",
			"Renderer",
			"Landscape",
			"Water",
			"Landmass",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",
			"AssetRegistry",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"EditorSubsystem",
				"LandscapeEditorUtilities",
				"LandscapeEditor",
				"MaterialEditor",
			});
		}
	}
}
