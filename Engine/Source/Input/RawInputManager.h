#pragma once

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
	bool pKeyboardKeys[kiKeyboardKeyCount] {};

	bool pMouseButtons[kMouseButtonCount] {};
	XMFLOAT2 f2MousePosition {};
	int iScrollWheelValue = 0;

	bool pGamepadButtons[kGamepadButtonCount] {};
	XMFLOAT2 f2LeftThumbstick {};
	XMFLOAT2 f2RightThumbstick {};
	XMFLOAT2 f2Dpad {};
	XMFLOAT2 f2Triggers {};
};

#if defined(BT_CLIENT)
class RawInputManager
{
public:

	RawInputManager();
	~RawInputManager();

	void HandleRawInput(LPARAM lparam);
	void UpdateFocus(bool bHasFocus, HWND hwnd);
	bool SetVibration(int64_t iPlayer, float fLeftMotor, float fRightMotor, float fLeftTrigger = 0.0f, float fRightTrigger = 0.0f);
	void TrapCursor(bool bTrap);

	void Update(bool bLostFocus);

	HWND mHwnd = nullptr;

	bool mpbKeyboardKeysDown[kiKeyboardKeyCount] {};
	Mouse mMouse;

	RawInput mRawInput {};

private:

	std::unique_ptr<GamePad> mpGamePad;
	bool mbGamePadConnected = false;
	bool mbHasFocus = false;
};

inline RawInputManager* gpRawInputManager = nullptr;
#endif // BT_CLIENT

} // namespace engine
