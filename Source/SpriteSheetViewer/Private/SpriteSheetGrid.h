#pragma once

#include "CoreMinimal.h"

class UTexture;

/** How a sprite sheet is sliced and played. */
struct FSpriteSheetGrid
{
	int32 Columns = 1;
	int32 Rows = 1;
	/** Frames actually used; the last row of a sheet is often partly empty. */
	int32 Frames = 1;
	float Fps = 24.f;

	int32 NumCells() const { return Columns * Rows; }
};

namespace SpriteSheetGrid
{
	/** Finds a "<columns>x<rows>" token in a name such as T_Fire_5x3 or smoke-8X8_C. The last valid token wins. */
	bool ParseName(const FString& Name, int32& OutColumns, int32& OutRows);

	/** Grid from the asset name, then the imported file name. Falls back to a single frame. */
	FSpriteSheetGrid Detect(const UTexture* Texture, bool& bOutFromName);

	/** Grid the user saved for this texture (editor per-project config), if any. */
	bool Load(const UTexture* Texture, FSpriteSheetGrid& OutGrid);
	void Save(const UTexture* Texture, const FSpriteSheetGrid& Grid);
	void Forget(const UTexture* Texture);
}
