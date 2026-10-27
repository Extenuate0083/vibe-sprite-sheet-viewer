#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SpriteSheetGrid.h"

class UTexture2D;

/** Everything the viewer's widgets share: the texture, its grid, playback and view settings. */
struct FSpriteSheetState
{
	TWeakObjectPtr<UTexture2D> Texture;
	FSpriteSheetGrid Grid;
	bool bGridFromName = false;

	int32 Frame = 0;
	bool bPlaying = true;
	double Accumulator = 0.0;

	bool bSheetView = false;
	bool bShowGrid = true;
	bool bUseAlpha = true;
	/** Flat BgColor behind the image instead of the checkerboard. */
	bool bSolidBg = false;
	FLinearColor BgColor = FLinearColor(FColor(0x2A, 0x2A, 0x2E));

	/** Display aspect correction, as in Vibe GTV: 0.5 = as stored, 0 = 2x wider, 1 = 2x taller. */
	float Ratio = 0.5f;

	/** Screen pixels per texture pixel. Ignored while bFit is set. */
	float Zoom = 1.f;
	bool bFit = false;
	FVector2D Pan = FVector2D::ZeroVector;

	int32 NumFrames() const { return FMath::Clamp(Grid.Frames, 1, Grid.NumCells()); }
	FVector2D TextureSize() const;
	FVector2D FrameSize() const;
	/** Horizontal and vertical display stretch from Ratio. */
	FVector2D Stretch() const;
	/** Frame's top-left corner and size in UV space. */
	FBox2f FrameUV(int32 InFrame) const;

	void SetFrame(int32 InFrame);
	void Step(int32 Delta);
	/** Advances playback by DeltaTime seconds. */
	void Advance(double DeltaTime);
	void StepPlayback();
	void ClampToGrid();
};

/** Plays a sprite sheet texture as an animation, with a Vibe GTV-style seek bar and transport. */
class SSpriteSheetViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteSheetViewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UTexture2D* InTexture);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	TSharedRef<FSpriteSheetState> GetState() const { return State; }
	void SetPlaying(bool bInPlaying);

private:
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeSidePanel();
	TSharedRef<SWidget> MakeTransport();
	TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Body);
	TSharedRef<SWidget> MakeRow(const FText& Label, const TSharedRef<SWidget>& Value);
	TSharedRef<SWidget> MakeToggle(const FText& Label, const FText& ToolTip, bool FSpriteSheetState::* Flag);
	TSharedRef<SWidget> MakeIconButton(FName Icon, const FText& ToolTip, TFunction<void()> OnClick);
	/** Row label that resets its value when clicked, like Vibe GTV's. */
	TSharedRef<SWidget> MakeResetLabel(const FText& Label, TFunction<void()> OnReset);

	void OnGridEdited();
	void OpenBackgroundPicker();
	void LoadBackground();
	void SaveBackground() const;
	void ResetGrid();
	FText GetInfoText() const;
	void ReturnFocus();

	EActiveTimerReturnType TickPlayback(double InCurrentTime, float InDeltaTime);

	TSharedRef<FSpriteSheetState> State = MakeShared<FSpriteSheetState>();
	TSharedPtr<FActiveTimerHandle> PlaybackTimer;
	/** The grid came from the user's saved settings rather than the name. */
	bool bGridSaved = false;
};
