# RE5 Tweaks

ASI mod for Resident Evil 5 Gold (Steam, DX9) that fixes PC controls and display.

- Direct mouse turning, raw mouse input, free camera while moving
- Rebindable QTE keys (all QTE types)
- FOV slider, ultrawide fix, borderless window, uncapped resolution, FPS cap
- Laser sight, separate music / SFX / cutscene volume, skip intro logos
- In-game settings overlay (Insert)

## Install

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) as `dinput8.dll` in the game folder.
2. Copy `re5tweaks.asi` and `re5tweaks.ini` from the [latest release](../../releases/latest) into the game folder.
3. Edit `re5tweaks.ini` or press Insert in game.

## Build

32-bit MSVC with [Dear ImGui](https://github.com/ocornut/imgui) (submodule). Clone with `--recursive`, then from a VS x86 prompt:

```
rc /nologo re5tweaks.rc
cl /nologo /O2 /EHa /LD /MT /Iimgui re5tweaks.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp imgui\backends\imgui_impl_dx9.cpp imgui\backends\imgui_impl_win32.cpp re5tweaks.res /Fe:re5tweaks.asi user32.lib dxguid.lib
```
