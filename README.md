# Vibe Sprite Sheet Viewer

An Unreal Engine 5.8 editor plugin that plays sprite sheet textures as animations. It adds a **Sprite Sheet** button to the Texture Editor toolbar, right of View Settings. The button opens a player styled like [Vibe GTV](https://github.com/Extenuate0083/vibe-gtv).

## Features

- **Grid detection** from names such as `T_Fire_5x3` or `smoke-8X8` (columns × rows). Texture sizes like `2048x2048` are ignored.
- **Manual grid:** set Columns, Rows and Frames by hand, for sheets whose last row is partly empty. The values are saved per texture in the editor's per-project user settings.
- **Animation and Sheet views.** Sheet view shows grid lines and highlights the current cell; click a cell to jump to that frame.
- **Seek bar** with frame ticks and numbers, plus transport buttons (first, previous, play/pause, next, last), loop, and an editable frame counter.
- **Playback:** FPS setting, ping-pong mode, and the total duration.
- **View:** alpha on/off, checkerboard background, Fit, 1:1, scroll to zoom toward the cursor, and drag to pan.

| Key | Action |
|---|---|
| Space | Play / pause |
| ← / → | Previous / next frame |
| Home / End | First / last frame |
| Tab | Switch between Animation and Sheet view |
| F / 1 | Fit / actual size |

## Install

Download the zip from **Releases**, or clone this repo, then follow one of these:

- **One project:** copy the `SpriteSheetViewer` folder to `<YourProject>/Plugins/SpriteSheetViewer/`.
- **Every project on the PC:** copy the folder to `<UE_5.8>/Engine/Plugins/Marketplace/`, then enable it under Edit → Plugins.

The release zip includes a prebuilt Win64 DLL for UE 5.8. If you install from source, the editor offers to compile the plugin, which needs Visual Studio 2022 with "Game development with C++".

## Code layout

All code is editor-only and sits in `Source/SpriteSheetViewer/Private/`. There are no runtime modules, no content, and no third-party dependencies.

| File | Purpose |
|---|---|
| `SpriteSheetViewerModule.cpp` | Adds the button to the Texture Editor toolbar through `ITextureEditorModule`'s toolbar extensibility manager, and opens the viewer windows |
| `SSpriteSheetViewer.cpp` | The Slate player: canvas, seek bar, side panel, transport, and keyboard shortcuts |
| `SpriteSheetGrid.cpp` | Parses the grid from names, and loads and saves the per-texture grid in `EditorPerProjectUserSettings.ini` |
| `SpriteSheetViewerStyle.h` | Colour palette |
| `Tests/SpriteSheetViewerTests.cpp` | Automation tests |

## Tests

Run in the editor from Session Frontend → Automation, filter `SpriteSheetViewer`. From the command line:

```
UnrealEditor.exe <Project>.uproject -ExecCmds="Automation RunTests SpriteSheetViewer" -TestExit="Automation Test Queue Empty" -unattended
```

- `NameParsing`: grid detection from asset names.
- `Playback`: frame timing, looping, ping-pong, and stopping at the end.
- `UI`: opens the Texture Editor and the viewer on `/Game/VFX/Greeting_animation_c_5x6` and saves screenshots to `Saved/SpriteSheetViewer/`. It needs that sample asset; pass a different asset path as the test parameter to use another texture.
