#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"

// EmberGen-style palette shared with Vibe GTV: near-black panels, flat widgets, orange accent.
namespace SpriteSheetViewerStyle
{
	inline const FLinearColor Accent    = FLinearColor(FColor(0xFF, 0x84, 0x10));
	inline const FLinearColor AccentDim = FLinearColor(FColor(0xA8, 0x50, 0x08));
	inline const FLinearColor BgApp     = FLinearColor(FColor(0x12, 0x12, 0x13));
	inline const FLinearColor BgPanel   = FLinearColor(FColor(0x1B, 0x1B, 0x1D));
	inline const FLinearColor BgSection = FLinearColor(FColor(0x20, 0x20, 0x23));
	inline const FLinearColor BgHeader  = FLinearColor(FColor(0x24, 0x24, 0x27));
	inline const FLinearColor BgWidget  = FLinearColor(FColor(0x2A, 0x2A, 0x2E));
	inline const FLinearColor Border    = FLinearColor(FColor(0x2F, 0x2F, 0x33));
	inline const FLinearColor Text      = FLinearColor(FColor(0xD4, 0xD4, 0xD8));
	inline const FLinearColor TextDim   = FLinearColor(FColor(0x84, 0x84, 0x8C));

	inline FSlateFontInfo Font(int32 Size, const FName Weight = "Regular")
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}
}
