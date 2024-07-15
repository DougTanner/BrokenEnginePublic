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
	DirectX::XMFLOAT2 f2MousePosition {};
	int64_t iScrollWheel = 0;

	InputToggle pGamepadButtons[kGamepadButtonCount] {};
	DirectX::XMFLOAT2 f2LeftThumbstick {};
	DirectX::XMFLOAT2 f2RightThumbstick {};
	DirectX::XMFLOAT2 f2Dpad {};
	DirectX::XMFLOAT2 f2Triggers {};
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
	DirectX::Mouse mMouse;

private:

	std::unique_ptr<DirectX::GamePad> mpGamePad;
	bool mbGamePadConnected = false;
	bool mbHasFocus = false;

	RawInput mRawInput {};
};

inline RawInputManager* gpRawInputManager = nullptr;

} // namespace engine
