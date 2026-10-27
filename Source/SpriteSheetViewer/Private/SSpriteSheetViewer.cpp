#include "SSpriteSheetViewer.h"

#include "SpriteSheetViewerStyle.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SpriteSheetViewer"

namespace Style = SpriteSheetViewerStyle;

namespace
{
	const FSlateBrush* SolidBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* RoundedBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 4.f);
		return &Brush;
	}

	FVector2f MeasureText(const FString& Text, const FSlateFontInfo& Font)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return FVector2f(Measure->Measure(Text, Font));
	}

	void DrawRectOutline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Min, const FVector2f& Max, const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2f> Points = { Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y), Min };
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true, Thickness);
	}

	constexpr float MinZoom = 0.05f;
	constexpr float MaxZoom = 64.f;

	const TCHAR* ViewConfigSection = TEXT("SpriteSheetViewer");
}

// ---------------------------------------------------------------------------------------------------------------------
// State

FVector2D FSpriteSheetState::TextureSize() const
{
	if (const UTexture2D* Tex = Texture.Get())
	{
		return FVector2D(FMath::Max(1.f, Tex->GetSurfaceWidth()), FMath::Max(1.f, Tex->GetSurfaceHeight()));
	}
	return FVector2D(1.0, 1.0);
}

FVector2D FSpriteSheetState::FrameSize() const
{
	const FVector2D Size = TextureSize();
	return FVector2D(Size.X / Grid.Columns, Size.Y / Grid.Rows);
}

FVector2D FSpriteSheetState::Stretch() const
{
	const float R = FMath::Clamp(Ratio, 0.f, 1.f);
	return FVector2D(FMath::Pow(2.f, FMath::Max((0.5f - R) * 2.f, 0.f)), FMath::Pow(2.f, FMath::Max((R - 0.5f) * 2.f, 0.f)));
}

FBox2f FSpriteSheetState::FrameUV(int32 InFrame) const
{
	const int32 F = FMath::Clamp(InFrame, 0, Grid.NumCells() - 1);
	const FVector2f Cell(1.f / Grid.Columns, 1.f / Grid.Rows);
	const FVector2f Min((F % Grid.Columns) * Cell.X, (F / Grid.Columns) * Cell.Y);
	return FBox2f(Min, Min + Cell);
}

void FSpriteSheetState::SetFrame(int32 InFrame)
{
	Frame = FMath::Clamp(InFrame, 0, NumFrames() - 1);
	Accumulator = 0.0;
}

void FSpriteSheetState::Step(int32 Delta)
{
	const int32 N = NumFrames();
	SetFrame(((Frame + Delta) % N + N) % N);
}

void FSpriteSheetState::Advance(double DeltaTime)
{
	const double FrameTime = 1.0 / FMath::Max(Grid.Fps, 1.f);
	Accumulator += DeltaTime;
	int32 Guard = 0;
	while (Accumulator >= FrameTime && bPlaying && Guard++ < 256)
	{
		Accumulator -= FrameTime;
		StepPlayback();
	}
	if (Guard >= 256)
	{
		Accumulator = 0.0;
	}
}

void FSpriteSheetState::StepPlayback()
{
	// Playback always loops.
	Frame = (Frame + 1) % NumFrames();
}

void FSpriteSheetState::ClampToGrid()
{
	Grid.Columns = FMath::Clamp(Grid.Columns, 1, 128);
	Grid.Rows = FMath::Clamp(Grid.Rows, 1, 128);
	Grid.Frames = FMath::Clamp(Grid.Frames, 1, Grid.NumCells());
	Grid.Fps = FMath::Clamp(Grid.Fps, 1.f, 240.f);
	Frame = FMath::Clamp(Frame, 0, NumFrames() - 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// Canvas: draws the current frame (or the whole sheet), with zoom, pan and frame picking.

class SSpriteSheetCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteSheetCanvas) {}
		SLATE_EVENT(FSimpleDelegate, OnFramePicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FSpriteSheetState>& InState)
	{
		State = InState;
		OnFramePicked = InArgs._OnFramePicked;
		SetClipping(EWidgetClipping::ClipToBounds);

		ImageBrush.DrawAs = ESlateBrushDrawType::Image;
		ImageBrush.Tiling = ESlateBrushTileType::NoTile;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(320.0, 240.0); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override
	{
		const FVector2f Avail = FVector2f(Geometry.GetLocalSize());
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(), SolidBrush(), ESlateDrawEffect::None, Style::BgApp);

		UTexture2D* Texture = State->Texture.Get();
		if (!Texture)
		{
			DrawCenteredMessage(Out, LayerId + 1, Geometry, LOCTEXT("TextureGone", "The texture is no longer available.").ToString());
			return LayerId + 1;
		}
		if (Texture->IsCurrentlyVirtualTextured())
		{
			DrawCenteredMessage(Out, LayerId + 1, Geometry, LOCTEXT("VirtualTexture", "Virtual textures can't be previewed here.").ToString());
			return LayerId + 1;
		}

		// Keep the full-resolution mips resident while the preview is open.
		Texture->SetForceMipLevelsToBeResident(2.f);

		const FLayout L = ComputeLayout(FVector2D(Avail));
		const FVector2f Origin = FVector2f(L.Origin);
		const FVector2f Size = FVector2f(L.ContentSize * L.Scale);

		// Background behind the image: checkerboard for alpha, or the chosen flat colour.
		if (!State->bSolidBg && State->bUseAlpha)
		{
			static FSlateBrush Checker = *FAppStyle::GetBrush("Checkerboard");
			Checker.Tiling = ESlateBrushTileType::Both;
			Checker.ImageSize = FVector2D(16.0, 16.0);
			FSlateDrawElement::MakeBox(Out, LayerId + 1, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Origin)), &Checker, ESlateDrawEffect::None, FLinearColor(0.22f, 0.22f, 0.24f));
		}
		else
		{
			FSlateDrawElement::MakeBox(Out, LayerId + 1, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Origin)), SolidBrush(), ESlateDrawEffect::None, State->BgColor.CopyWithNewOpacity(1.f));
		}

		ImageBrush.SetResourceObject(Texture);
		ImageBrush.ImageSize = State->TextureSize();
		ImageBrush.SetUVRegion(State->bSheetView ? FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, 1.f)) : State->FrameUV(State->Frame));
		const ESlateDrawEffect Effect = State->bUseAlpha ? ESlateDrawEffect::None : ESlateDrawEffect::IgnoreTextureAlpha;
		FSlateDrawElement::MakeBox(Out, LayerId + 2, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Origin)), &ImageBrush, Effect, FLinearColor::White);

		if (State->bSheetView)
		{
			PaintSheetOverlay(Out, LayerId + 3, Geometry, Origin, Size);
		}
		else
		{
			DrawRectOutline(Out, LayerId + 3, Geometry, Origin, Origin + Size, Style::Border, 1.f);
		}

		// Zoom readout, bottom right.
		const FSlateFontInfo Font = Style::Font(8);
		const FString Zoom = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(L.Scale * 100.f));
		const FVector2f TextSize = MeasureText(Zoom, Font);
		FSlateDrawElement::MakeText(Out, LayerId + 4, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Avail - TextSize - FVector2f(10.f, 8.f))), Zoom, Font, ESlateDrawEffect::None, Style::TextDim);

		return LayerId + 4;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		bDragged = false;
		DragDistance = 0.f;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (!HasMouseCapture())
		{
			return FReply::Unhandled();
		}

		const FVector2D Delta = Event.GetCursorDelta() / Geometry.Scale;
		DragDistance += Delta.Size();
		if (DragDistance > 3.f)
		{
			bDragged = true;
		}
		if (bDragged)
		{
			LeaveFit(Geometry);
			State->Pan += Delta;
		}
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (!HasMouseCapture())
		{
			return FReply::Unhandled();
		}

		if (!bDragged && Event.GetEffectingButton() == EKeys::LeftMouseButton && State->bSheetView)
		{
			PickFrame(Geometry, Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		State->bFit = true;
		State->Pan = FVector2D::ZeroVector;
		return FReply::Handled();
	}

	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		const FVector2D Cursor = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		const FVector2D Avail = Geometry.GetLocalSize();
		const FLayout L = ComputeLayout(Avail);
		const FVector2D ContentPoint = (Cursor - L.Origin) / L.Scale;

		const float NewScale = FMath::Clamp(L.Scale * FMath::Pow(1.25f, Event.GetWheelDelta()), MinZoom, MaxZoom);
		State->bFit = false;
		State->Zoom = NewScale;
		// Keep the texel under the cursor where it is.
		State->Pan = Cursor - ContentPoint * NewScale - (Avail - L.ContentSize * NewScale) * 0.5;
		return FReply::Handled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override
	{
		return (HasMouseCapture() && bDragged) ? FCursorReply::Cursor(EMouseCursor::GrabHandClosed) : FCursorReply::Unhandled();
	}

	/** Screen pixels per texel the view would use right now. */
	float CurrentScale(const FGeometry& Geometry) const { return ComputeLayout(Geometry.GetLocalSize()).Scale; }

private:
	struct FLayout
	{
		FVector2D Origin;
		FVector2D ContentSize;
		float Scale;
	};

	FLayout ComputeLayout(const FVector2D& Avail) const
	{
		FLayout L;
		// Content is measured in displayed texels, i.e. after the ratio stretch.
		L.ContentSize = (State->bSheetView ? State->TextureSize() : State->FrameSize()) * State->Stretch();
		if (State->bFit)
		{
			const double Margin = 24.0;
			L.Scale = (float)FMath::Min((Avail.X - 2.0 * Margin) / L.ContentSize.X, (Avail.Y - 2.0 * Margin) / L.ContentSize.Y);
			L.Scale = FMath::Clamp(L.Scale, MinZoom, MaxZoom);
			L.Origin = (Avail - L.ContentSize * L.Scale) * 0.5;
		}
		else
		{
			L.Scale = State->Zoom;
			L.Origin = (Avail - L.ContentSize * L.Scale) * 0.5 + State->Pan;
		}
		return L;
	}

	void LeaveFit(const FGeometry& Geometry)
	{
		if (State->bFit)
		{
			State->Zoom = CurrentScale(Geometry);
			State->Pan = FVector2D::ZeroVector;
			State->bFit = false;
		}
	}

	void PickFrame(const FGeometry& Geometry, const FVector2D& Local)
	{
		const FLayout L = ComputeLayout(Geometry.GetLocalSize());
		const FVector2D P = (Local - L.Origin) / L.Scale;
		const FVector2D Cell = State->FrameSize() * State->Stretch();
		const int32 Col = FMath::FloorToInt(P.X / Cell.X);
		const int32 Row = FMath::FloorToInt(P.Y / Cell.Y);
		if (Col < 0 || Row < 0 || Col >= State->Grid.Columns || Row >= State->Grid.Rows)
		{
			return;
		}
		const int32 Index = Row * State->Grid.Columns + Col;
		if (Index < State->NumFrames())
		{
			State->SetFrame(Index);
			OnFramePicked.ExecuteIfBound();
		}
	}

	void PaintSheetOverlay(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Origin, const FVector2f& Size) const
	{
		const FSpriteSheetGrid& Grid = State->Grid;
		const FVector2f Cell(Size.X / Grid.Columns, Size.Y / Grid.Rows);

		// Cells past the last frame are unused; dim them.
		for (int32 Index = State->NumFrames(); Index < Grid.NumCells(); ++Index)
		{
			const FVector2f Min = Origin + FVector2f((Index % Grid.Columns) * Cell.X, (Index / Grid.Columns) * Cell.Y);
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Cell, FSlateLayoutTransform(Min)), SolidBrush(), ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		}

		if (State->bShowGrid)
		{
			const FLinearColor LineColor = Style::Accent.CopyWithNewOpacity(0.45f);
			for (int32 C = 1; C < Grid.Columns; ++C)
			{
				const float X = Origin.X + C * Cell.X;
				TArray<FVector2f> Points = { FVector2f(X, Origin.Y), FVector2f(X, Origin.Y + Size.Y) };
				FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, LineColor, true, 1.f);
			}
			for (int32 R = 1; R < Grid.Rows; ++R)
			{
				const float Y = Origin.Y + R * Cell.Y;
				TArray<FVector2f> Points = { FVector2f(Origin.X, Y), FVector2f(Origin.X + Size.X, Y) };
				FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, LineColor, true, 1.f);
			}
		}
		DrawRectOutline(Out, Layer, Geometry, Origin, Origin + Size, Style::Border, 1.f);

		// Current frame.
		const int32 F = State->Frame;
		const FVector2f Min = Origin + FVector2f((F % Grid.Columns) * Cell.X, (F / Grid.Columns) * Cell.Y);
		FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(Cell, FSlateLayoutTransform(Min)), SolidBrush(), ESlateDrawEffect::None, Style::Accent.CopyWithNewOpacity(0.08f));
		DrawRectOutline(Out, Layer + 1, Geometry, Min + FVector2f(1.f, 1.f), Min + Cell - FVector2f(1.f, 1.f), Style::Accent, 2.f);
	}

	void DrawCenteredMessage(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Message) const
	{
		const FSlateFontInfo Font = Style::Font(10);
		const FVector2f TextSize = MeasureText(Message, Font);
		const FVector2f Pos = (FVector2f(Geometry.GetLocalSize()) - TextSize) * 0.5f;
		FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Pos)), Message, Font, ESlateDrawEffect::None, Style::TextDim);
	}

	TSharedPtr<FSpriteSheetState> State;
	FSimpleDelegate OnFramePicked;
	mutable FSlateBrush ImageBrush;
	bool bDragged = false;
	float DragDistance = 0.f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Seek bar: rail, frame ticks with numbers, and a draggable handle (same look as Vibe GTV).

class SSpriteSheetSeekBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteSheetSeekBar) {}
		SLATE_EVENT(FSimpleDelegate, OnScrub)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FSpriteSheetState>& InState)
	{
		State = InState;
		OnScrub = InArgs._OnScrub;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(200.0, 36.0); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override
	{
		const int32 Frames = State->NumFrames();
		const float Last = (float)FMath::Max(Frames - 1, 1);
		const float W = Geometry.GetLocalSize().X;
		const float X0 = Pad, X1 = FMath::Max(W - Pad, X0 + 1.f);
		auto XOf = [&](int32 F) { return X0 + (X1 - X0) * F / Last; };

		const float RailY = 9.f;
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(FVector2f(X1 - X0, 5.f), FSlateLayoutTransform(FVector2f(X0, RailY - 2.5f))), RoundedBrush(), ESlateDrawEffect::None, Style::BgWidget);
		const float FilledW = XOf(State->Frame) - X0;
		if (FilledW > 0.5f)
		{
			FSlateDrawElement::MakeBox(Out, LayerId + 1, Geometry.ToPaintGeometry(FVector2f(FilledW, 5.f), FSlateLayoutTransform(FVector2f(X0, RailY - 2.5f))), RoundedBrush(), ESlateDrawEffect::None, Style::AccentDim);
		}

		// Numbers on every frame when there's room; every 5 (then 10, 20...) on long animations.
		const FSlateFontInfo Font = Style::Font(7);
		const float PxPerFrame = (X1 - X0) / Last;
		int32 StepN = Frames > 20 ? 5 : 1;
		while (PxPerFrame * StepN < 22.f && StepN < Frames)
		{
			StepN = StepN == 1 ? 5 : StepN * 2;
		}
		const float TickTop = RailY + 6.f;
		for (int32 F = 0; F < Frames; ++F)
		{
			const int32 N = F + 1;
			const float X = XOf(F);
			const bool bMajor = N == 1 || N % StepN == 0;
			if (bMajor)
			{
				TArray<FVector2f> Tick = { FVector2f(X, TickTop), FVector2f(X, TickTop + 5.f) };
				FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(), MoveTemp(Tick), ESlateDrawEffect::None, Style::TextDim, false, 1.f);

				const FString Label = FString::FromInt(N);
				const FVector2f Size = MeasureText(Label, Font);
				const FLinearColor Color = F == State->Frame ? Style::Accent : Style::TextDim;
				FSlateDrawElement::MakeText(Out, LayerId + 1, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f(X - Size.X * 0.5f, TickTop + 6.f))), Label, Font, ESlateDrawEffect::None, Color);
			}
			else if (PxPerFrame >= 4.f)
			{
				TArray<FVector2f> Tick = { FVector2f(X, TickTop), FVector2f(X, TickTop + 3.f) };
				FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(), MoveTemp(Tick), ESlateDrawEffect::None, Style::TextDim.CopyWithNewOpacity(0.5f), false, 1.f);
			}
		}

		const FVector2f HandleSize(10.f, 16.f);
		const FVector2f HandlePos(XOf(State->Frame) - HandleSize.X * 0.5f, RailY - HandleSize.Y * 0.5f);
		if (IsHovered() || HasMouseCapture())
		{
			FSlateDrawElement::MakeBox(Out, LayerId + 2, Geometry.ToPaintGeometry(HandleSize + FVector2f(2.f, 2.f), FSlateLayoutTransform(HandlePos - FVector2f(1.f, 1.f))), RoundedBrush(), ESlateDrawEffect::None, FLinearColor::White);
		}
		FSlateDrawElement::MakeBox(Out, LayerId + 3, Geometry.ToPaintGeometry(HandleSize, FSlateLayoutTransform(HandlePos)), RoundedBrush(), ESlateDrawEffect::None, Style::Accent);
		return LayerId + 3;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		Scrub(Geometry, Event);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (HasMouseCapture())
		{
			Scrub(Geometry, Event);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}

private:
	void Scrub(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		const float X = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
		const float W = Geometry.GetLocalSize().X;
		const float T = FMath::Clamp((X - Pad) / FMath::Max(W - 2.f * Pad, 1.f), 0.f, 1.f);
		State->SetFrame(FMath::RoundToInt(T * (State->NumFrames() - 1)));
		OnScrub.ExecuteIfBound();
	}

	static constexpr float Pad = 10.f;
	TSharedPtr<FSpriteSheetState> State;
	FSimpleDelegate OnScrub;
};

// ---------------------------------------------------------------------------------------------------------------------
// Viewer

void SSpriteSheetViewer::Construct(const FArguments& InArgs, UTexture2D* InTexture)
{
	State->Texture = InTexture;
	State->Grid = SpriteSheetGrid::Detect(InTexture, State->bGridFromName);
	bGridSaved = SpriteSheetGrid::Load(InTexture, State->Grid);
	State->ClampToGrid();
	LoadBackground();

	const FSimpleDelegate Pause = FSimpleDelegate::CreateLambda([this]() { SetPlaying(false); });

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(SolidBrush())
		.BorderBackgroundColor(Style::BgApp)
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeHeader()
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SSpriteSheetCanvas, State)
					.OnFramePicked(Pause)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeSidePanel()
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(SolidBrush())
				.BorderBackgroundColor(Style::BgPanel)
				.Padding(FMargin(12.f, 10.f, 12.f, 0.f))
				[
					SNew(SSpriteSheetSeekBar, State)
					.OnScrub(Pause)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeTransport()
			]
		]
	];

	SetPlaying(true);
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeHeader()
{
	const FString Name = State->Texture.IsValid() ? State->Texture->GetName() : FString();

	return SNew(SBorder)
		.BorderImage(SolidBrush())
		.BorderBackgroundColor(Style::BgHeader)
		.Padding(FMargin(12.f, 6.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(4.f)
				.HeightOverride(16.f)
				[
					SNew(SBorder)
					.BorderImage(RoundedBrush())
					.BorderBackgroundColor(Style::Accent)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Name))
				.Font(Style::Font(11, "Bold"))
				.ColorAndOpacity(Style::Text)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SSpriteSheetViewer::GetInfoText)
				.Font(Style::Font(9))
				.ColorAndOpacity(Style::TextDim)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SSegmentedControl<bool>)
				.Value_Lambda([this]() { return State->bSheetView; })
				.OnValueChanged_Lambda([this](bool bSheet) { State->bSheetView = bSheet; })
				+ SSegmentedControl<bool>::Slot(false)
				.Text(LOCTEXT("AnimationView", "Animation"))
				.ToolTip(LOCTEXT("AnimationViewTip", "Play the frames (Tab)"))
				+ SSegmentedControl<bool>::Slot(true)
				.Text(LOCTEXT("SheetView", "Sheet"))
				.ToolTip(LOCTEXT("SheetViewTip", "Show the whole sheet; click a cell to jump to that frame (Tab)"))
			]
		];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeSection(const FText& Title, const TSharedRef<SWidget>& Body)
{
	return SNew(SBorder)
		.BorderImage(RoundedBrush())
		.BorderBackgroundColor(Style::BgSection)
		.Padding(FMargin(10.f, 8.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock)
				.Text(Title.ToUpper())
				.Font(Style::Font(8, "Bold"))
				.ColorAndOpacity(Style::Accent)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Body
			]
		];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeRow(const FText& Label, const TSharedRef<SWidget>& Value)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(0.45f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Style::Font(9))
			.ColorAndOpacity(Style::Text)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.55f)
		.VAlign(VAlign_Center)
		[
			Value
		];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeToggle(const FText& Label, const FText& ToolTip, bool FSpriteSheetState::* Flag)
{
	return SNew(SCheckBox)
		.ToolTipText(ToolTip)
		.IsFocusable(false)
		.IsChecked_Lambda([this, Flag]() { return (*State).*Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Flag](ECheckBoxState NewState) { (*State).*Flag = NewState == ECheckBoxState::Checked; })
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Style::Font(9))
			.ColorAndOpacity(Style::Text)
		];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeIconButton(FName Icon, const FText& ToolTip, TFunction<void()> OnClick)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.IsFocusable(false)
		.ToolTipText(ToolTip)
		.ContentPadding(FMargin(5.f, 3.f))
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(Icon))
			.ColorAndOpacity(Style::Text)
			.DesiredSizeOverride(FVector2D(18.0, 18.0))
		];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeResetLabel(const FText& Label, TFunction<void()> OnReset)
{
	TSharedPtr<SButton> Button;
	SAssignNew(Button, SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.IsFocusable(false)
		.ContentPadding(0.f)
		.Cursor(EMouseCursor::Hand)
		.ToolTipText(LOCTEXT("ResetTip", "Click to reset"))
		.OnClicked_Lambda([OnReset]() { OnReset(); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Style::Font(9))
			.ColorAndOpacity_Lambda([WeakButton = TWeakPtr<SButton>(Button)]()
			{
				const TSharedPtr<SButton> Pinned = WeakButton.Pin();
				return FSlateColor(Pinned && Pinned->IsHovered() ? Style::Accent : Style::Text);
			})
		];
	return SNew(SBox).HAlign(HAlign_Left)[Button.ToSharedRef()];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeSidePanel()
{
	auto IntBox = [this](int32 FSpriteSheetGrid::* Field, TFunction<int32()> Max) -> TSharedRef<SWidget>
	{
		return SNew(SNumericEntryBox<int32>)
			.AllowSpin(true)
			.MinValue(1)
			.MinSliderValue(1)
			.MaxValue_Lambda([Max]() { return TOptional<int32>(Max()); })
			.MaxSliderValue_Lambda([Max]() { return TOptional<int32>(Max()); })
			.Value_Lambda([this, Field]() { return TOptional<int32>(State->Grid.*Field); })
			.OnValueChanged_Lambda([this, Field](int32 Value)
			{
				const bool bFramesFollowCells = State->Grid.Frames == State->Grid.NumCells();
				State->Grid.*Field = Value;
				// Changing the grid keeps "use every cell" if that's what it was.
				if (Field != &FSpriteSheetGrid::Frames && bFramesFollowCells)
				{
					State->Grid.Frames = State->Grid.NumCells();
				}
				OnGridEdited();
			})
			.OnValueCommitted_Lambda([this](int32, ETextCommit::Type Commit) { if (Commit == ETextCommit::OnEnter) { ReturnFocus(); } });
	};

	TSharedRef<SWidget> GridBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeRow(LOCTEXT("Columns", "Columns"), IntBox(&FSpriteSheetGrid::Columns, []() { return 128; }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeRow(LOCTEXT("Rows", "Rows"), IntBox(&FSpriteSheetGrid::Rows, []() { return 128; }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeRow(LOCTEXT("Frames", "Frames"), IntBox(&FSpriteSheetGrid::Frames, [this]() { return State->Grid.NumCells(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Font(Style::Font(8))
			.Text_Lambda([this]()
			{
				if (bGridSaved)
				{
					return LOCTEXT("GridSaved", "Custom grid, saved for this texture.");
				}
				if (State->bGridFromName)
				{
					return FText::Format(LOCTEXT("GridFromName", "Detected {0} x {1} from the name."), State->Grid.Columns, State->Grid.Rows);
				}
				return LOCTEXT("GridNotFound", "No grid in the name (like _8x8). Set it here.");
			})
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(State->bGridFromName && !bGridSaved ? Style::Accent : Style::TextDim); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f).HAlign(HAlign_Left)
		[
			SNew(SButton)
			.IsFocusable(false)
			.Visibility_Lambda([this]() { return bGridSaved ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text(LOCTEXT("ResetGrid", "Reset to detected"))
			.ToolTipText(LOCTEXT("ResetGridTip", "Forget the saved grid and use the one from the texture name"))
			.OnClicked_Lambda([this]() { ResetGrid(); return FReply::Handled(); })
		];

	TSharedRef<SWidget> PlaybackBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeRow(LOCTEXT("Fps", "FPS"),
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(1.f).MaxValue(240.f)
				.MinSliderValue(1.f).MaxSliderValue(60.f)
				.Delta(1.f)
				.Value_Lambda([this]() { return TOptional<float>(State->Grid.Fps); })
				.OnValueChanged_Lambda([this](float Value) { State->Grid.Fps = Value; OnGridEdited(); })
				.OnValueCommitted_Lambda([this](float, ETextCommit::Type Commit) { if (Commit == ETextCommit::OnEnter) { ReturnFocus(); } }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Font(Style::Font(8))
			.ColorAndOpacity(Style::TextDim)
			.Text_Lambda([this]()
			{
				const float Seconds = State->NumFrames() / FMath::Max(State->Grid.Fps, 1.f);
				return FText::Format(LOCTEXT("Duration", "Duration {0}s"), FText::AsNumber(Seconds, &FNumberFormattingOptions().SetMaximumFractionalDigits(2).SetMinimumFractionalDigits(2)));
			})
		];

	auto ZoomButton = [this](const FText& Label, const FText& ToolTip, TFunction<void()> OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.IsFocusable(false)
			.ToolTipText(ToolTip)
			.ContentPadding(FMargin(6.f, 1.f))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Label).Font(Style::Font(8))
			];
	};

	TSharedRef<SWidget> ViewBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeToggle(LOCTEXT("GridLines", "Grid lines"), LOCTEXT("GridLinesTip", "Show cell borders in the Sheet view"), &FSpriteSheetState::bShowGrid)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeToggle(LOCTEXT("Alpha", "Alpha"), LOCTEXT("AlphaTip", "Blend with the texture's alpha channel"), &FSpriteSheetState::bUseAlpha)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				MakeResetLabel(LOCTEXT("Ratio", "Ratio"), [this]() { State->Ratio = 0.5f; })
			]
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(0.f).MaxValue(1.f)
				.MinSliderValue(0.f).MaxSliderValue(1.f)
				.Delta(0.01f)
				.MinFractionalDigits(2).MaxFractionalDigits(2)
				.ToolTipText(LOCTEXT("RatioTip", "0.5 = as stored  ·  0 = 2x wider  ·  1 = 2x taller"))
				.Value_Lambda([this]() { return TOptional<float>(State->Ratio); })
				.OnValueChanged_Lambda([this](float Value) { State->Ratio = FMath::Clamp(Value, 0.f, 1.f); })
				.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type Commit)
				{
					State->Ratio = FMath::Clamp(Value, 0.f, 1.f);
					if (Commit == ETextCommit::OnEnter)
					{
						ReturnFocus();
					}
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f).HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Font(Style::Font(8))
			.ColorAndOpacity(Style::TextDim)
			.Text_Lambda([this]()
			{
				const FVector2D S = State->Stretch();
				const FNumberFormattingOptions Digits = FNumberFormattingOptions().SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
				if (S.X > 1.001)
				{
					return FText::Format(LOCTEXT("StretchWide", "{0}x wide"), FText::AsNumber(S.X, &Digits));
				}
				if (S.Y > 1.001)
				{
					return FText::Format(LOCTEXT("StretchTall", "{0}x tall"), FText::AsNumber(S.Y, &Digits));
				}
				return LOCTEXT("StretchNone", "no stretch");
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
		[
			MakeResetLabel(LOCTEXT("Background", "Background"), [this]()
			{
				State->bSolidBg = false;
				State->BgColor = FSpriteSheetState().BgColor;
				SaveBackground();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSegmentedControl<bool>)
				.Value_Lambda([this]() { return State->bSolidBg; })
				.OnValueChanged_Lambda([this](bool bSolid) { State->bSolidBg = bSolid; SaveBackground(); })
				+ SSegmentedControl<bool>::Slot(false)
				.Text(LOCTEXT("BgChecker", "Checker"))
				.ToolTip(LOCTEXT("BgCheckerTip", "Checkerboard behind transparent pixels"))
				+ SSegmentedControl<bool>::Slot(true)
				.Text(LOCTEXT("BgSolid", "Solid"))
				.ToolTip(LOCTEXT("BgSolidTip", "Flat colour behind transparent pixels"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SColorBlock)
				.Color_Lambda([this]() { return State->BgColor.CopyWithNewOpacity(1.f); })
				.ShowBackgroundForAlpha(false)
				.CornerRadius(FVector4(3.f, 3.f, 3.f, 3.f))
				.Size(FVector2D(28.0, 18.0))
				.Cursor(EMouseCursor::Hand)
				.ToolTipText(LOCTEXT("BgColorTip", "Pick the background colour"))
				.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& Event)
				{
					if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
					{
						return FReply::Unhandled();
					}
					OpenBackgroundPicker();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				ZoomButton(LOCTEXT("Fit", "Fit"), LOCTEXT("FitTip", "Fit to the view (F, or double-click)"), [this]() { State->bFit = true; State->Pan = FVector2D::ZeroVector; })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZoomButton(LOCTEXT("ActualSize", "1:1"), LOCTEXT("ActualSizeTip", "Actual size (1)"), [this]() { State->bFit = false; State->Zoom = 1.f; State->Pan = FVector2D::ZeroVector; })
			]
		];

	return SNew(SBorder)
		.BorderImage(SolidBrush())
		.BorderBackgroundColor(Style::BgPanel)
		.Padding(8.f)
		[
			SNew(SBox)
			.WidthOverride(230.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 8.f)
				[
					MakeSection(LOCTEXT("GridSection", "Grid"), GridBody)
				]
				+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 8.f)
				[
					MakeSection(LOCTEXT("PlaybackSection", "Playback"), PlaybackBody)
				]
				+ SScrollBox::Slot()
				[
					MakeSection(LOCTEXT("ViewSection", "View"), ViewBody)
				]
			]
		];
}

TSharedRef<SWidget> SSpriteSheetViewer::MakeTransport()
{
	return SNew(SBorder)
		.BorderImage(SolidBrush())
		.BorderBackgroundColor(Style::BgPanel)
		.Padding(FMargin(12.f, 2.f, 12.f, 8.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SSpacer)
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeIconButton("Animation.Backward_End", LOCTEXT("FirstTip", "First frame (Home)"), [this]() { SetPlaying(false); State->SetFrame(0); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeIconButton("Animation.Backward_Step", LOCTEXT("PrevTip", "Previous frame (Left)"), [this]() { SetPlaying(false); State->Step(-1); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.IsFocusable(false)
				.ToolTipText(LOCTEXT("PlayTip", "Play / pause (Space)"))
				.ContentPadding(FMargin(6.f, 3.f))
				.OnClicked_Lambda([this]() { SetPlaying(!State->bPlaying); return FReply::Handled(); })
				[
					SNew(SImage)
					.Image_Lambda([this]() { return FAppStyle::GetBrush(State->bPlaying ? "Animation.Pause" : "Animation.Forward"); })
					.ColorAndOpacity(Style::Accent)
					.DesiredSizeOverride(FVector2D(24.0, 24.0))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeIconButton("Animation.Forward_Step", LOCTEXT("NextTip", "Next frame (Right)"), [this]() { SetPlaying(false); State->Step(1); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeIconButton("Animation.Forward_End", LOCTEXT("LastTip", "Last frame (End)"), [this]() { SetPlaying(false); State->SetFrame(State->NumFrames() - 1); })
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("FrameLabel", "Frame")).Font(Style::Font(9)).ColorAndOpacity(Style::TextDim)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(56.f)
					[
						SNew(SNumericEntryBox<int32>)
						.AllowSpin(false)
						.MinValue(1)
						.MaxValue_Lambda([this]() { return TOptional<int32>(State->NumFrames()); })
						.Value_Lambda([this]() { return TOptional<int32>(State->Frame + 1); })
						.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type Commit)
						{
							SetPlaying(false);
							State->SetFrame(Value - 1);
							if (Commit == ETextCommit::OnEnter)
							{
								ReturnFocus();
							}
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::Format(LOCTEXT("FrameTotal", "/ {0}"), State->NumFrames()); })
					.Font(Style::Font(9))
					.ColorAndOpacity(Style::TextDim)
				]
			]
		];
}

FText SSpriteSheetViewer::GetInfoText() const
{
	const FVector2D Tex = State->TextureSize();
	const FVector2D Cell = State->FrameSize();
	return FText::Format(LOCTEXT("Info", "{0} x {1}  ·  {2} frames  ·  {3} x {4} px per frame  ·  sheet {5} x {6}"),
		State->Grid.Columns, State->Grid.Rows, State->NumFrames(),
		FMath::RoundToInt(Cell.X), FMath::RoundToInt(Cell.Y),
		FMath::RoundToInt(Tex.X), FMath::RoundToInt(Tex.Y));
}

void SSpriteSheetViewer::OnGridEdited()
{
	State->ClampToGrid();
	SpriteSheetGrid::Save(State->Texture.Get(), State->Grid);
	bGridSaved = true;
}

void SSpriteSheetViewer::ResetGrid()
{
	const float Fps = State->Grid.Fps;
	SpriteSheetGrid::Forget(State->Texture.Get());
	State->Grid = SpriteSheetGrid::Detect(State->Texture.Get(), State->bGridFromName);
	State->Grid.Fps = Fps;
	State->ClampToGrid();
	bGridSaved = false;
}

void SSpriteSheetViewer::ReturnFocus()
{
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SSpriteSheetViewer::OpenBackgroundPicker()
{
	// Picking a colour switches to Solid; cancelling puts the previous mode back.
	const TSharedRef<SSpriteSheetViewer> Self = SharedThis(this);
	const bool bWasSolid = State->bSolidBg;
	FColorPickerArgs Args(State->BgColor, FOnLinearColorValueChanged::CreateSPLambda(Self, [this](FLinearColor Color)
	{
		State->BgColor = Color.CopyWithNewOpacity(1.f);
		State->bSolidBg = true;
	}));
	Args.bUseAlpha = false;
	Args.ParentWidget = Self;
	Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSPLambda(Self, [this, bWasSolid](FLinearColor Original)
	{
		State->BgColor = Original;
		State->bSolidBg = bWasSolid;
		SaveBackground();
	});
	Args.OnColorPickerWindowClosed = FOnWindowClosed::CreateSPLambda(Self, [this](const TSharedRef<SWindow>&) { SaveBackground(); });
	OpenColorPicker(Args);
}

void SSpriteSheetViewer::LoadBackground()
{
	if (!GConfig)
	{
		return;
	}
	GConfig->GetBool(ViewConfigSection, TEXT("SolidBackground"), State->bSolidBg, GEditorPerProjectIni);
	FString Color;
	if (GConfig->GetString(ViewConfigSection, TEXT("BackgroundColor"), Color, GEditorPerProjectIni))
	{
		State->BgColor.InitFromString(Color);
		State->BgColor.A = 1.f;
	}
}

void SSpriteSheetViewer::SaveBackground() const
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetBool(ViewConfigSection, TEXT("SolidBackground"), State->bSolidBg, GEditorPerProjectIni);
	GConfig->SetString(ViewConfigSection, TEXT("BackgroundColor"), *State->BgColor.ToString(), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SSpriteSheetViewer::SetPlaying(bool bInPlaying)
{
	State->bPlaying = bInPlaying;
	State->Accumulator = 0.0;

	if (bInPlaying && !PlaybackTimer.IsValid())
	{
		PlaybackTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SSpriteSheetViewer::TickPlayback));
	}
}

EActiveTimerReturnType SSpriteSheetViewer::TickPlayback(double InCurrentTime, float InDeltaTime)
{
	if (!State->bPlaying)
	{
		PlaybackTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	State->Advance(InDeltaTime);
	return EActiveTimerReturnType::Continue;
}

FReply SSpriteSheetViewer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::SpaceBar)
	{
		SetPlaying(!State->bPlaying);
	}
	else if (Key == EKeys::Left || Key == EKeys::Right)
	{
		SetPlaying(false);
		State->Step(Key == EKeys::Left ? -1 : 1);
	}
	else if (Key == EKeys::Home || Key == EKeys::End)
	{
		SetPlaying(false);
		State->SetFrame(Key == EKeys::Home ? 0 : State->NumFrames() - 1);
	}
	else if (Key == EKeys::Tab)
	{
		State->bSheetView = !State->bSheetView;
	}
	else if (Key == EKeys::F)
	{
		State->bFit = true;
		State->Pan = FVector2D::ZeroVector;
	}
	else if (Key == EKeys::One)
	{
		State->bFit = false;
		State->Zoom = 1.f;
		State->Pan = FVector2D::ZeroVector;
	}
	else
	{
		return FReply::Unhandled();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
