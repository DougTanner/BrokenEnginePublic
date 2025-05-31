#pragma once

#include "InputToggle.h"

namespace engine
{

inline constexpr int64_t kiKeyboardKeyCount = 0xFF;

enum MouseButtons
{
	kMouseButtonLeft,
	kMouseButtonMiddle,
	kMouseButtonRight,
	kMouseButtonExtraOne,
	kMouseButtonExtraTwo,

	kMouseButtonCount
};

enum GamepadButtons
{
	kGamepadButtonA,
	kGamepadButtonB,
	kGamepadButtonX,
	kGamepadButtonY,

	kGamepadLeftShoulder,
	kGamepadRightShoulder,

	kGamepadStart,
	kGamepadMenu,

	kGamepadButtonCount
};

struct RawInput
{
	InputToggle pKeyboardKeys[kiKeyboardKeyCount] {};

	InputToggle pMouseButtons[kMouseButtonCount] {};
	XMFLOAT2 f2MousePosition {};
	int64_t iScrollWheel = 0;

	InputToggle pGamepadButtons[kGamepadButtonCount] {};
	XMFLOAT2 f2LeftThumbstick {};
	XMFLOAT2 f2RightThumbstick {};
	XMFLOAT2 f2Dpad {};
	XMFLOAT2 f2Triggers {};
};

class RawInputManager
{
public:

	RawInputManager();
	~RawInputManager();

	void HandleRawInput(LPARAM lparam);
	void UpdateFocus(bool bHasFocus, HWND hwnd);
	bool SetVibration(int64_t iPlayer, float fLeftMotor, float fRightMotor, float fLeftTrigger = 0.0f, float fRightTrigger = 0.0f);
	void TrapCursor(bool bTrap);

	const RawInput& Update();

	HWND mHwnd = nullptr;

	bool mpbKeyboardKeysDown[kiKeyboardKeyCount] {};
	Mouse mMouse;

private:

	std::unique_ptr<GamePad> mpGamePad;
	bool mbGamePadConnected = false;
	bool mbHasFocus = false;

	RawInput mRawInput {};
};

inline RawInputManager* gpRawInputManager = nullptr;

} // namespace engine
