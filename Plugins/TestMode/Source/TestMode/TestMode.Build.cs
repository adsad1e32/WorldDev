// Copyright Epic Games, Inc. All Rights Reserved.

//using Google.Protobuf.Reflection;
using System.IO;
using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class TestMode : ModuleRules
{
	public TestMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;    
		PrivateIncludePaths.AddRange(
			new string[] {

				// ... add other private include paths required here ...
            }
			);

        PublicDependencyModuleNames.AddRange(
		new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "Projects",
                    "ModelingOperators",
                     "GeometryFramework",
                     "GeometryCore",
                     "GeometryAlgorithms",
					 "DynamicMesh",
                     "GeometryScriptingCore",
                     "StaticMeshDescription",
                     "RenderCore",

                });


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"ApplicationCore",
				"EditorStyle",
				"UnrealEd",
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",
				"PropertyEditor",
				"ModelingComponents",
				"GizmoEdMode",
				"LevelEditor",
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
				// ... add private dependencies that you statically link with here ...	

				// "DeveloperSettings",
			});
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
