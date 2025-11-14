# UI System

Game-specific user interface implementation for BrokenEngineSandbox, providing all menus, HUD elements, and in-game overlays.

## Overview

The UI system uses the engine's declarative Widget framework to build hierarchical UI structures. All widgets are constructed using VStack/HStack layout containers with Button, Slider, Toggle, and Text primitives.

## Core UI Functions

**BuildUi()** - Root widget builder that assembles all UI screens into a single widget tree. Returns a VStack containing all menu and HUD components. Each screen has conditional visibility based on game state.

**MainMenu()** - Primary menu shown when `InMainMenu()` is true and UI state is pause. Provides Continue, Play, Graphics, Sound, and Quit buttons with language selection at bottom.

**InGameMenu()** - Pause menu shown during gameplay. Offers Resume, Restart, Graphics, Sound, Main Menu, and Quit options.

**GraphicsMenu()** - Settings screen for visual options including fullscreen, presentation mode, multisampling, anisotropy, sample shading, terrain detail, smoke simulation, and debug visualization controls.

**SoundMenu()** - Audio settings for master, music, and sound volume with defaults reset button.

**LanguageMenu()** - Horizontal language selector supporting English, Chinese, Spanish, Portuguese, French, and German. Highlights currently selected language.

**GameHud()** - In-game overlay displaying player status. Shows energy level (rotary indicator), shield/armor bars (horizontal colored bars), and secondary weapon capacity (rotary indicator). Only visible when UI state is none and not on death screen.

**InGame()** - Wave number display that appears during gameplay with animated fade-out.

**DeathMenu()** - Game over screen showing final wave reached with restart option.

**TweaksMenu()** - Debug menu for runtime parameter adjustment (various configurations available via preprocessor defines).

**InGameDebug()** - Debug text overlay for development builds.

## UI State Management

Visibility is controlled through lambda functions in widget Enabled fields that check:
- Game UI state (pause, graphics, sound, tweaks, none)
- Frame flags (main menu, death screen)
- Player state and capabilities

## Layout and Styling

Uses proportional sizing (0.0-1.0 range) relative to screen dimensions. Standard UI scale factor `kfUiScale = 1.3f` applied to size constants. Color values are RGBA hex constants. Text shadows provide depth and readability.

## Interaction

Buttons support OnClick callbacks for state changes, menu transitions, and game actions. Sliders and toggles bind to Wrapper objects that persist settings. Focus indicators show keyboard/gamepad navigation state.
