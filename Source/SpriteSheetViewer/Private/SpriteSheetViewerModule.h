#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FExtender;
class FUICommandList;
class SWindow;
class UTexture2D;

class FSpriteSheetViewerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Opens the sprite sheet player for a texture, or brings its window to the front. */
	static void OpenViewer(UTexture2D* Texture);
	static TSharedPtr<SWindow> FindViewerWindow(const UTexture2D* Texture);

	/** How many times the Texture Editor toolbar button has been built (used by the tests). */
	static int32 ToolbarButtonsBuilt;

private:
	TSharedRef<FExtender> ExtendTextureEditorToolbar(const TSharedRef<FUICommandList> Commands, const TArray<UObject*> Objects);

	FDelegateHandle ToolbarExtenderHandle;
};
