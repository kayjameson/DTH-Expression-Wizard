using UnrealBuildTool;

public class DTHExpressionWizard : ModuleRules
{
	public DTHExpressionWizard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"DeveloperSettings",
			"Json",
			"Projects",
			"PropertyEditor",
			"DesktopPlatform",
			"AssetTools",
			"AssetRegistry",
			"AdvancedPreviewScene",
			"ImageWrapper",
			"ApplicationCore",
			"RenderCore",
		});
	}
}
