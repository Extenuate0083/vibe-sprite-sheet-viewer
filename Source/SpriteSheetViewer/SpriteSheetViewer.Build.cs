using UnrealBuildTool;

public class SpriteSheetViewer : ModuleRules
{
	public SpriteSheetViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AppFramework",
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"TextureEditor",
			"ImageCore",
		});
	}
}
