# /Engine/Source/Input/

The `/Engine/Source/Input/` directory contains the core input handling system that manages keyboard, mouse, and gamepad input through Windows Raw Input API and DirectXTK.

## Overview
This directory provides a unified input system that:
- Handles keyboard input via Win32 Raw Input API
- Manages mouse input via DirectXTK Mouse class
- Supports gamepad input via DirectXTK GamePad class
- Provides state tracking utilities for button press/release detection

## Core Files

### InputToggle.h
**Purpose**: Template utility class for tracking input state transitions (down/pressed/released).

**Key Components**:
- `InputToggle` class - Tracks button state changes using bit flags
- `UpdateToggle()` - Updates state based on current input (detects press/release)
- `IsDown()` - Returns true if button is currently held
- `WasPressed()` - Returns true on the frame the button was pressed
- `WasReleased()` - Returns true on the frame the button was released
- `ToggleFlags` enum - Internal bit flags for state tracking

**Usage**: Wrap any boolean input value to detect state changes between frames.

### RawInputManager.h
**Purpose**: Main input manager interface and data structures.

**Key Components**:
- `kiKeyboardKeyCount` (255) - Number of keyboard keys tracked
- `MouseButtons` enum - Defines mouse button indices (Left, Middle, Right, Extra1, Extra2)
- `GamepadButtons` enum - Maps gamepad buttons (A, B, X, Y, LShoulder, RShoulder, Start, Menu)
- `RawInput` struct - Complete input state snapshot containing:
  - Keyboard key states array
  - Mouse position, buttons, and scroll state
  - Gamepad buttons, triggers, and analog sticks
- `RawInputManager` class - Singleton manager interface
- `gpRawInputManager` - Global pointer to the input manager instance

### RawInputManager.cpp
**Purpose**: Implementation of the input manager with platform-specific handling.

**Key Methods**:
- Constructor - Initializes DirectXTK GamePad and Mouse objects, sets global pointer
- `UpdateFocus()` - Registers/unregisters raw input devices based on window focus
- `SetVibration()` - Controls gamepad rumble motors (left/right intensity)
- `TrapCursor()` - Constrains mouse cursor to window client area
- `Update()` - Main update method that:
  - Polls DirectXTK Mouse and GamePad states
  - Processes keyboard raw input buffer
  - Applies 200ms scroll wheel trigger delay
  - Returns complete RawInput state struct
- `HandleRawInput()` - Processes WM_INPUT messages for keyboard events

**Internal State**:
- `mRawInput` - Current frame's input state
- `mpbKeyboardKeysDown[256]` - Raw keyboard key states
- `mMouse` - DirectXTK Mouse instance
- `mpGamePad` - DirectXTK GamePad instance
- `mbHasFocus` - Window focus tracking

## Dependencies
- **Windows API**: Raw Input API for keyboard handling
- **DirectXTK**: Mouse and GamePad classes for mouse/gamepad input
- **Common**: Uses logging system from `/Common/Log.h`

## Usage Notes
- The input manager is a global singleton accessed via `gpRawInputManager`
- Call `Update()` once per frame to get the latest input state
- Window focus affects input registration - no input when unfocused
- Currently limited to first gamepad (player index 0)
- Scroll wheel input is throttled to prevent rapid triggering
- All input is polled, not event-driven (except raw keyboard input)
