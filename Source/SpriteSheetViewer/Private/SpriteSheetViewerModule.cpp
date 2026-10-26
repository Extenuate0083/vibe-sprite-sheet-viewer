#include "SpriteSheetViewerModule.h"

#include "SSpriteSheetViewer.h"
#include "SpriteSheetGrid.h"
#include "SpriteSheetViewerStyle.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Interfaces/ITextureEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "UObject/ObjectKey.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SpriteSheetViewer"

int32 FSpriteSheetViewerModule::ToolbarButtonsBuilt = 0;

namespace
{
	TMap<FObjectKey, TWeakPtr<SWindow>> OpenWindows;
}

void FSpriteSheetViewerModule::StartupModule()
{
	ITextureEditorModule& TextureEditor = FModuleManager::LoadModuleChecked<ITextureEditorModule>("TextureEditor");
	FAssetEditorExtender Extender = FAssetEditorExtender::CreateRaw(this, &FSpriteSheetViewerModule::ExtendTextureEditorToolbar);
	ToolbarExtenderHandle = Extender.GetHandle();
	TextureEditor.GetToolBarExtensibilityManager()->GetExtenderDelegates().Add(MoveTemp(Extender));
}

void FSpriteSheetViewerModule::ShutdownModule()
{
	if (ITextureEditorModule* TextureEditor = FModuleManager::GetModulePtr<ITextureEditorModule>("TextureEditor"))
	{
		TextureEditor->GetToolBarExtensibilityManager()->GetExtenderDelegates().RemoveAll(
			[this](const FAssetEditorExtender& Delegate) { return Delegate.GetHandle() == ToolbarExtenderHandle; });
	}

	for (const TPair<FObjectKey, TWeakPtr<SWindow>>& Pair : OpenWindows)
	{
		if (TSharedPtr<SWindow> Window = Pair.Value.Pin())
		{
			Window->RequestDestroyWindow();
		}
	}
	OpenWindows.Empty();
}

TSharedRef<FExtender> FSpriteSheetViewerModule::ExtendTextureEditorToolbar(const TSharedRef<FUICommandList> Commands, const TArray<UObject*> Objects)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	UTexture2D* Texture = Objects.Num() > 0 ? Cast<UTexture2D>(Objects[0]) : nullptr;
	if (!Texture)
	{
		return Extender;
	}

	// "Settings" is the Texture Editor's last toolbar section, the one holding View Settings.
	TWeakObjectPtr<UTexture2D> WeakTexture = Texture;
	Extender->AddToolBarExtension("Settings", EExtensionHook::After, Commands, FToolBarExtensionDelegate::CreateLambda([WeakTexture](FToolBarBuilder& Builder)
	{
		bool bFromName = false;
		const FSpriteSheetGrid Grid = SpriteSheetGrid::Detect(WeakTexture.Get(), bFromName);
		const FText ToolTip = bFromName
			? FText::Format(LOCTEXT("ButtonTipGrid", "Play this texture as a sprite sheet animation ({0} x {1} grid from its name)"), Grid.Columns, Grid.Rows)
			: LOCTEXT("ButtonTip", "Play this texture as a sprite sheet animation");

		Builder.BeginSection("SpriteSheetViewer");
		Builder.AddWidget(
			SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 2.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(ToolTip)
				.ContentPadding(FMargin(6.f, 2.f))
				.OnClicked_Lambda([WeakTexture]()
				{
					if (UTexture2D* Tex = WeakTexture.Get())
					{
						FSpriteSheetViewerModule::OpenViewer(Tex);
					}
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Play"))
						.ColorAndOpacity(SpriteSheetViewerStyle::Accent)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ButtonLabel", "Sprite Sheet"))
					]
				]
			],
			"SpriteSheetViewer");
		Builder.EndSection();

		++ToolbarButtonsBuilt;
	}));

	return Extender;
}

void FSpriteSheetViewerModule::OpenViewer(UTexture2D* Texture)
{
	if (!Texture)
	{
		return;
	}

	if (TSharedPtr<SWindow> Existing = FindViewerWindow(Texture))
	{
		Existing->BringToFront(true);
		return;
	}

	TSharedRef<SSpriteSheetViewer> Viewer = SNew(SSpriteSheetViewer, Texture);
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("WindowTitle", "Sprite Sheet - {0}"), FText::FromString(Texture->GetName())))
		.ClientSize(FVector2D(1100.0, 780.0))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			Viewer
		];

	TSharedPtr<SWindow> Parent = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
	if (!Parent.IsValid())
	{
		Parent = FGlobalTabmanager::Get()->GetRootWindow();
	}
	if (Parent.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, Parent.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}

	OpenWindows.Add(FObjectKey(Texture), Window);
	FSlateApplication::Get().SetKeyboardFocus(Viewer, EFocusCause::SetDirectly);
}

TSharedPtr<SWindow> FSpriteSheetViewerModule::FindViewerWindow(const UTexture2D* Texture)
{
	if (const TWeakPtr<SWindow>* Found = OpenWindows.Find(FObjectKey(Texture)))
	{
		return Found->Pin();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpriteSheetViewerModule, SpriteSheetViewer)
