#include "SpriteSheetGrid.h"
#include "SpriteSheetViewerModule.h"
#include "SSpriteSheetViewer.h"

#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpriteSheetNameParsingTest, "SpriteSheetViewer.NameParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpriteSheetNameParsingTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Name;
		bool bFound;
		int32 Columns;
		int32 Rows;
	};
	const FCase Cases[] =
	{
		{ TEXT("T_FX_FB_FireAR1_5x3_C"), true, 5, 3 },
		{ TEXT("Greeting_animation_j_16x2"), true, 16, 2 },
		{ TEXT("smoke-8X8"), true, 8, 8 },
		{ TEXT("8x4_bounce"), true, 8, 4 },
		{ TEXT("Sheet_4x4_v2_8x2"), true, 8, 2 },
		{ TEXT("T_Rock_2048x2048"), false, 0, 0 },
		{ TEXT("T_Rock_512x512"), false, 0, 0 },
		{ TEXT("T_FX_Lightning"), false, 0, 0 },
		{ TEXT("Icon_1x1"), false, 0, 0 },
		{ TEXT("Box_x3"), false, 0, 0 },
		{ TEXT("Fire_4x"), false, 0, 0 },
	};

	for (const FCase& Case : Cases)
	{
		int32 Columns = 0, Rows = 0;
		const bool bFound = SpriteSheetGrid::ParseName(Case.Name, Columns, Rows);
		TestEqual(FString::Printf(TEXT("%s found"), Case.Name), bFound, Case.bFound);
		if (Case.bFound && bFound)
		{
			TestEqual(FString::Printf(TEXT("%s columns"), Case.Name), Columns, Case.Columns);
			TestEqual(FString::Printf(TEXT("%s rows"), Case.Name), Rows, Case.Rows);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpriteSheetPlaybackTest, "SpriteSheetViewer.Playback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpriteSheetPlaybackTest::RunTest(const FString& Parameters)
{
	FSpriteSheetState State;
	State.Grid.Columns = 4;
	State.Grid.Rows = 2;
	State.Grid.Frames = 6;
	State.Grid.Fps = 10.f;

	State.Advance(0.25);
	TestEqual("Two frames after 0.25s at 10 fps", State.Frame, 2);
	State.Advance(0.4);
	TestEqual("Loops past the last used frame", State.Frame, 0);

	const FBox2f UV = State.FrameUV(5);
	TestEqual("Frame 5 column", UV.Min.X, 0.25f);
	TestEqual("Frame 5 row", UV.Min.Y, 0.5f);

	State.SetFrame(0);
	State.bPingPong = true;
	for (int32 i = 0; i < 6; ++i)
	{
		State.StepPlayback();
	}
	TestEqual("Ping-pong turns around at the end", State.Frame, 4);

	State.bPingPong = false;
	State.bLoop = false;
	State.bPlaying = true;
	State.SetFrame(4);
	State.Advance(1.0);
	TestEqual("Stops on the last frame without loop", State.Frame, 5);
	TestFalse("Stopped playing", State.bPlaying);
	return true;
}

namespace
{
	void SaveScreenshot(const TSharedRef<SWidget>& Widget, const FString& Name)
	{
		TArray<FColor> Pixels;
		FIntVector Size;
		if (FSlateApplication::Get().TakeScreenshot(Widget, Pixels, Size) && Size.X > 0 && Size.Y > 0)
		{
			for (FColor& Pixel : Pixels)
			{
				Pixel.A = 255;
			}
			const FString Path = FPaths::ProjectSavedDir() / TEXT("SpriteSheetViewer") / (Name + TEXT(".png"));
			FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), Size.X, Size.Y));
			UE_LOG(LogTemp, Display, TEXT("SpriteSheetViewer screenshot: %s"), *FPaths::ConvertRelativePathToFull(Path));
		}
	}
}

// Opens the Texture Editor and the player on a real sheet, and saves screenshots to Saved/SpriteSheetViewer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpriteSheetViewerUITest, "SpriteSheetViewer.UI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpriteSheetViewerUITest::RunTest(const FString& Parameters)
{
	const FString AssetPath = Parameters.IsEmpty() ? TEXT("/Game/VFX/Greeting_animation_c_5x6.Greeting_animation_c_5x6") : Parameters;
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Sample texture loads"), Texture))
	{
		return false;
	}

	const int32 ButtonsBefore = FSpriteSheetViewerModule::ToolbarButtonsBuilt;
	UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	AssetEditors->OpenEditorForAsset(Texture);

	TWeakObjectPtr<UTexture2D> WeakTexture = Texture;
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, WeakTexture, ButtonsBefore, AssetEditors]()
	{
		TestTrue(TEXT("Texture Editor toolbar got the Sprite Sheet button"), FSpriteSheetViewerModule::ToolbarButtonsBuilt > ButtonsBefore);

		if (IAssetEditorInstance* Editor = AssetEditors->FindEditorForAsset(WeakTexture.Get(), false))
		{
			if (TSharedPtr<FTabManager> Tabs = Editor->GetAssociatedTabManager())
			{
				if (TSharedPtr<SDockTab> Tab = Tabs->GetOwnerTab())
				{
					if (TSharedPtr<SWindow> Window = Tab->GetParentWindow())
					{
						SaveScreenshot(Window.ToSharedRef(), TEXT("TextureEditor"));
					}
				}
			}
		}

		FSpriteSheetViewerModule::OpenViewer(WeakTexture.Get());
		TestTrue(TEXT("Viewer window opened"), FSpriteSheetViewerModule::FindViewerWindow(WeakTexture.Get()).IsValid());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, WeakTexture]()
	{
		if (TSharedPtr<SWindow> Window = FSpriteSheetViewerModule::FindViewerWindow(WeakTexture.Get()))
		{
			TSharedRef<SSpriteSheetViewer> Viewer = StaticCastSharedRef<SSpriteSheetViewer>(Window->GetContent());
			TestTrue(TEXT("Playback advanced"), Viewer->GetState()->Frame > 0);
			SaveScreenshot(Window.ToSharedRef(), TEXT("ViewerAnimation"));
			Viewer->SetPlaying(false);
			Viewer->GetState()->SetFrame(7);
			Viewer->GetState()->bSheetView = true;
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([WeakTexture, AssetEditors]()
	{
		if (TSharedPtr<SWindow> Window = FSpriteSheetViewerModule::FindViewerWindow(WeakTexture.Get()))
		{
			SaveScreenshot(Window.ToSharedRef(), TEXT("ViewerSheet"));
			Window->RequestDestroyWindow();
		}
		AssetEditors->CloseAllEditorsForAsset(WeakTexture.Get());
		return true;
	}));
	return true;
}

#endif
