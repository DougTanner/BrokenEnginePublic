# /Engine/Source/Input/

The `/Engine/Source/Input/` directory contains the core input handling system that manages keyboard, mouse, and gamepad input through Windows Raw Input API and DirectXTK.

## File Overview

### Engine/Source/Input/InputToggle.h
Utility class for tracking input state transitions. Use this when you need to detect key/button press/release events.

- `InputToggle` class - Tracks down/pressed/released states using flags
- `UpdateToggle()` - Updates state based on current input
- `IsDown()`, `WasPressed()`, `WasReleased()` - Query current state
- `ToggleFlags` enum - Internal state flags

### Engine/Source/Input/RawInputManager.h / Engine/Source/Input/RawInputManager.cpp  
Core input manager that handles all input devices. This is the main interface for input in the engine.

**Header defines:**
- `kiKeyboardKeyCount` - Total keyboard keys supported (255)
- `MouseButtons` enum - Left, middle, right, extra1, extra2 mouse buttons
- `GamepadButtons` enum - A, B, X, Y, shoulders, start, menu buttons
- `RawInput` struct - Complete input state (keyboard array, mouse data, gamepad data)
- `RawInputManager` class - Main input manager singleton
- Global `gpRawInputManager` pointer

**Implementation provides:**
- Constructor/destructor - Initializes DirectXTK GamePad, sets global pointer
- `UpdateFocus()` - Registers/unregisters raw input devices when window gains/loses focus
- `SetVibration()` - Controls gamepad haptic feedback
- `TrapCursor()` - Constrains mouse cursor to window bounds
- `Update()` - Updates all input states and returns RawInput struct (call each frame)
- `HandleRawInput()` - Processes Windows WM_INPUT messages for keyboard

**Key data members:**
- `mRawInput` - Current frame's complete input state
- `mpbKeyboardKeysDown[]` - Raw keyboard state array
- `mMouse` - DirectXTK mouse object
- `mpGamePad` - DirectXTK gamepad object
- `mbHasFocus` - Window focus state

## Usage Notes
- Input manager is a singleton accessed via `gpRawInputManager`
- Call `Update()` each frame to refresh input state
- Uses DirectXTK for mouse and gamepad, Raw Input API for keyboard
- Only supports first gamepad (index 0)
- Scroll wheel has built-in 200ms delay between triggers