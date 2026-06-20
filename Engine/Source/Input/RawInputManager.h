#pragma once

namespace engine
{

inline constexpr int64_t kiKeyboardKeyCount = 0xFF;

enum MouseButtons : uint32_t
{
	kMouseButtonLeft     = 1u << 0,
	kMouseButtonMiddle   = 1u << 1,
	kMouseButtonRight    = 1u << 2,
	kMouseButtonExtraOne = 1u << 3,
	kMouseButtonExtraTwo = 1u << 4,
};

enum GamepadButtons : uint32_t
{
	kGamepadButtonA       = 1u << 0,
	kGamepadButtonB       = 1u << 1,
	kGamepadButtonX       = 1u << 2,
	kGamepadButtonY       = 1u << 3,

	kGamepadLeftShoulder  = 1u << 4,
	kGamepadRightShoulder = 1u << 5,

	kGamepadStart         = 1u << 6,
	kGamepadMenu          = 1u << 7,
};

struct RawInput
{
	bool pKeyboardKeys[kiKeyboardKeyCount] {};

	common::Flags<MouseButtons> mouseButtons {};
	XMFLOAT2 f2MousePosition {};
	int iScrollWheelValue = 0;

	common::Flags<GamepadButtons> gamepadButtons {};
	XMFLOAT2 f2LeftThumbstick {};
	XMFLOAT2 f2RightThumbstick {};
	XMFLOAT2 f2Dpad {};
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

	void Update(bool bLostFocus);

	RawInput mRawInput {};

private:

	enum class RawInputStateFlags : uint8_t
	{
		kGamePadConnected = 1 << 0,
		kHasFocus         = 1 << 1,
	};

	void TrapCursor(bool bTrap);

	HWND mHwnd = nullptr;

	bool mpbKeyboardKeysDown[kiKeyboardKeyCount] {};
	Mouse mMouse;

	std::unique_ptr<GamePad> mpGamePad;
	common::Flags<RawInputStateFlags> mStateFlags;
};

inline RawInputManager* gpRawInputManager = nullptr;
#endif // BT_CLIENT

} // namespace engine
