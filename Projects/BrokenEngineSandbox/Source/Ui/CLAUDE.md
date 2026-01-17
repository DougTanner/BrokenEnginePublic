# UI System

Game-specific user interface implementation for BrokenEngineSandbox, providing all menus, HUD elements, and in-game overlays.

## Overview

The UI system uses the engine's declarative Widget framework to build hierarchical UI structures. All widgets are constructed using VStack/HStack layout containers with Button, Slider, Toggle, RadioButtons, Rotary, and Text primitives. The entire UI is assembled via `BuildUi()` which returns a single widget tree containing all screens.

## Key Systems

- **Menu Screens** - Main menu, in-game pause menu, graphics settings, and sound settings with localized text support
- **Game HUD** - In-game overlay showing player energy (rotary indicator), shield/armor bars, and secondary weapon capacity
- **Death Screen** - Game over display with restart option
- **Debug Menus** - Conditional tweaks menus for runtime parameter adjustment (controlled via preprocessor defines)

## Architecture Notes

Widget visibility is controlled through lambda functions in Enabled fields that check game state (`gpGame->meUiState`), frame flags, and player capabilities. This allows the entire widget tree to exist while only rendering relevant screens.

Layout uses proportional sizing (0.0-1.0 range) relative to screen dimensions. Buttons support OnClick callbacks for state changes, while Sliders and Toggles bind to Wrapper objects that persist settings.

## Localization

The `Localization.h` file defines a Language enum and string table supporting English, Chinese, Spanish, Portuguese, French, and German. `TranslatedString()` provides fallback to English for missing translations.

## Files

- **Ui.cpp/h** - Widget builders for all UI screens and HUD elements
- **Localization.h** - String table and language selection
- **Wrapper.h** - Game-specific wrapper extensions (currently empty, uses engine base)
