#include "SpriteSheetGrid.h"

#include "Engine/Texture.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* ConfigSection = TEXT("SpriteSheetViewer.Grids");

	constexpr int32 MaxSide = 128;
	constexpr int32 MaxCells = 4096;
}

bool SpriteSheetGrid::ParseName(const FString& Name, int32& OutColumns, int32& OutRows)
{
	bool bFound = false;
	const int32 Len = Name.Len();
	for (int32 i = 1; i + 1 < Len; ++i)
	{
		if (Name[i] != TEXT('x') && Name[i] != TEXT('X'))
		{
			continue;
		}

		int32 Start = i;
		while (Start > 0 && FChar::IsDigit(Name[Start - 1]))
		{
			--Start;
		}
		int32 End = i + 1;
		while (End < Len && FChar::IsDigit(Name[End]))
		{
			++End;
		}

		// Up to three digits a side, so texture sizes like 2048x2048 are never mistaken for a grid.
		const int32 LeftDigits = i - Start;
		const int32 RightDigits = End - (i + 1);
		if (LeftDigits < 1 || RightDigits < 1 || LeftDigits > 3 || RightDigits > 3)
		{
			continue;
		}

		const int32 Columns = FCString::Atoi(*Name.Mid(Start, LeftDigits));
		const int32 Rows = FCString::Atoi(*Name.Mid(i + 1, RightDigits));
		if (Columns < 1 || Rows < 1 || Columns > MaxSide || Rows > MaxSide || Columns * Rows < 2 || Columns * Rows > MaxCells)
		{
			continue;
		}

		OutColumns = Columns;
		OutRows = Rows;
		bFound = true;
	}
	return bFound;
}

FSpriteSheetGrid SpriteSheetGrid::Detect(const UTexture* Texture, bool& bOutFromName)
{
	FSpriteSheetGrid Grid;
	bOutFromName = false;
	if (!Texture)
	{
		return Grid;
	}

	TArray<FString> Candidates;
	Candidates.Add(Texture->GetName());
#if WITH_EDITORONLY_DATA
	if (Texture->AssetImportData)
	{
		const FString Source = Texture->AssetImportData->GetFirstFilename();
		if (!Source.IsEmpty())
		{
			Candidates.Add(FPaths::GetBaseFilename(Source));
		}
	}
#endif

	for (const FString& Candidate : Candidates)
	{
		if (ParseName(Candidate, Grid.Columns, Grid.Rows))
		{
			bOutFromName = true;
			break;
		}
	}
	Grid.Frames = Grid.NumCells();
	return Grid;
}

bool SpriteSheetGrid::Load(const UTexture* Texture, FSpriteSheetGrid& OutGrid)
{
	if (!Texture || !GConfig)
	{
		return false;
	}

	FString Value;
	if (!GConfig->GetString(ConfigSection, *Texture->GetPathName(), Value, GEditorPerProjectIni))
	{
		return false;
	}

	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() != 4)
	{
		return false;
	}

	FSpriteSheetGrid Grid;
	Grid.Columns = FMath::Clamp(FCString::Atoi(*Parts[0]), 1, MaxSide);
	Grid.Rows = FMath::Clamp(FCString::Atoi(*Parts[1]), 1, MaxSide);
	Grid.Frames = FMath::Clamp(FCString::Atoi(*Parts[2]), 1, Grid.NumCells());
	Grid.Fps = FMath::Clamp(FCString::Atof(*Parts[3]), 1.f, 240.f);
	OutGrid = Grid;
	return true;
}

void SpriteSheetGrid::Save(const UTexture* Texture, const FSpriteSheetGrid& Grid)
{
	if (!Texture || !GConfig)
	{
		return;
	}

	const FString Value = FString::Printf(TEXT("%d,%d,%d,%g"), Grid.Columns, Grid.Rows, Grid.Frames, Grid.Fps);
	GConfig->SetString(ConfigSection, *Texture->GetPathName(), *Value, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SpriteSheetGrid::Forget(const UTexture* Texture)
{
	if (!Texture || !GConfig)
	{
		return;
	}

	GConfig->RemoveKey(ConfigSection, *Texture->GetPathName(), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}
