# RE5 Tweaks

ASI mod for Resident Evil 5 Gold (Steam, DX9) that fixes PC controls and display.

- Direct mouse turning, raw mouse input, free camera while moving
- Rebindable QTE keys (all QTE types)
- FOV slider, ultrawide fix, borderless window, uncapped resolution, FPS cap
- Laser sight, separate music / SFX / cutscene volume, skip intro logos
- In-game settings overlay (Insert)

## Install

1. Download the zip from the [latest release](../../releases/latest).
2. Extract everything into the game folder, next to `re5dx9.exe` (usually `Steam\steamapps\common\Resident Evil 5`).
3. Start the game. Press Insert for the settings menu, or edit `re5tweaks.ini`.

The zip already includes the ASI loader (`dinput8.dll`), so nothing else is needed. If you already have a `dinput8.dll` from another mod, keep yours; it loads RE5 Tweaks too.

To uninstall, delete `dinput8.dll`, `re5tweaks.asi` and `re5tweaks.ini`.

## Build

32-bit MSVC. Clone with `--recursive`, then from a VS x86 prompt:

```
rc /nologo re5tweaks.rc
cl /nologo /O2 /EHa /LD /MT /Iimgui re5tweaks.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp imgui\backends\imgui_impl_dx9.cpp imgui\backends\imgui_impl_win32.cpp re5tweaks.res /Fe:re5tweaks.asi user32.lib dxguid.lib
```
