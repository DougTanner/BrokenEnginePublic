# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui-based UI screens for the game, rendered by engine's ImGuiManager. Most screen implementations are client-only (`#ifdef BT_CLIENT`); DeathMenuScreen compiles in both builds. ModalScreen handles error display for network connection issues.

## Overview

Game-specific screens providing HUD, main menu, modal dialogs, pause menu, settings, and death screen functionality. All screens use ImGui for rendering and support both keyboard/mouse and gamepad navigation.

## Screen Classes

- **HudScreen** - In-game overlay showing player shield and armor bars with icons. Lazy-loads textures via FileManager and renders to the ImGui background draw list. Has Initialize/Shutdown lifecycle for Vulkan descriptor management.
- **MainMenuScreen** - Entry point with Local Server, Remote Server, settings, and quit options plus a language selection bar. Switches ImGui font when Chinese is selected. Local Server triggers LAN discovery via `Game::StartServerDiscovery()` (covers both localhost and LAN), showing a "SCANNING..." disabled button while the scan is in progress, then a "CONNECTING..." disabled button while waiting for the server's connection response. Remote Server is a disabled placeholder for future Internet server support.
- **ModalScreen** - Centered modal error dialog displaying `gpGame->mModalMessage` with text wrapping and an OK button. Used for connection rejection messages and desync notifications. OK dismisses the modal by transitioning `meUiState` back to `kPause` and clearing the message. Client-only (`#ifdef BT_CLIENT`).
- **PauseMenuScreen** - In-game pause overlay with resume, settings, main menu, and quit options. Dynamically sizes buttons to the widest label.
- **GraphicsMenuScreen** - Rendering settings panel exposing engine Wrapper variables for display, multisampling, texture filtering, world detail, smoke, and wind. Time of day slider appears only in main menu.
- **SoundMenuScreen** - Volume sliders for master, music, and sound with a defaults reset option.
- **DeathMenuScreen** - Game over screen with a respawn button. Sends a respawn request via `NetworkClient` (`#ifdef BT_CLIENT`). Compiles in both client and server builds.

## MenuUtils.h/.cpp

Shared utilities used by all menu screens:
- **ScopedMenuScale** - RAII helper that scales ImGui padding and spacing for consistent menu sizing
- **AppendUtf8()** - Converts UTF-32 localized strings to null-terminated UTF-8 via workbuffer, avoiding heap allocations
- **ToUtf8()** - Heap-allocating UTF-32 to UTF-8 conversion for cases requiring `std::string`
- **WrapperToggle()/WrapperSlider()** - ImGui controls bound to engine Wrapper settings

## Architecture Notes

Screens check game UI state and game flags to determine visibility, early-returning when not active. All positioning uses proportional screen percentages for resolution independence. ImGuiManager (engine-side) owns instances of these screen classes and calls their `Render()` methods.
