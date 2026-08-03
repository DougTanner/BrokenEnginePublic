#pragma once

#include "Input/RawInputManager.h"
#include "Frame/StatusChange.h"

namespace game
{

// Menu
enum class MenuInputFlags : uint64_t
{
	kPauseMenu         = 0x00000001,
	kToggleFullscreen  = 0x00000002,
	kMouseIsDown       = 0x00000004,
	kMouseClick        = 0x00000008,
	kGamepadButton     = 0x00000010,
	kQuit              = 0x00000020,
	kToggleProfileText = 0x00000040,
	kTogglePauseFrame  = 0x00000080,
	kResetFrame        = 0x00000100,
	kQuicksave         = 0x00000200,
	kQuickload         = 0x00000400,
	kSaveReplay        = 0x00000800,
	kLoadReplay        = 0x00001000,
	kSlowTime          = 0x00002000,
	kSpeedUpTime       = 0x00004000,
	kSingleStep        = 0x00008000,
	kMenuTweaks        = 0x00020000,
	kToggleScreenshots = 0x00040000,
	kConnectLocal      = 0x00080000,
	kMenuDebugTexture  = 0x00200000,
	kDebugTextureNext  = 0x00400000,
	kDebugTexturePrev  = 0x00800000,
	kToggleDebugRender = 0x01000000,
	kCycleMenuIsland   = 0x02000000,
};
using MenuInputFlags_t = common::Flags<MenuInputFlags>;

struct MenuInput
{
	bool bGamepad = false;
	MenuInputFlags_t flags {};
	XMFLOAT2 f2Mouse {};
	XMFLOAT2 f2Gamepad {};
};

// Camera (display-rate, client-only, continuous)
struct CameraInput
{
	XMFLOAT2 f2Move {};
	int iScrollDelta = 0;
};

// Frame
struct FrameInput
{
	static constexpr int64_t kiVersion = 14;

	std::vector<StatusChange> statusChanges;

	common::crc_t Crc() const;

	friend std::ostream& operator<<(std::ostream& rStream, const FrameInput& rInput);
	friend std::istream& operator>>(std::istream& rStream, FrameInput& rInput);
};

// Input manager class
class Input
{
public:

	void UpdateMenuInput(bool bLostFocus, MenuInput& rMenuInput);
	void UpdateCameraInput();

	CameraInput mCameraInput {};

private:

	enum class InputStateFlags : uint8_t
	{
		kScrollWheelInitialized = 1 << 0,
		kGamepadMode            = 1 << 1,
	};

	engine::RawInput mPreviousRawInputMenu {};

	int miPreviousScrollWheelValue = 0;
	common::Flags<InputStateFlags> mStateFlags;

	// Was pressed helpers (use mPreviousRawInputMenu)
	bool KeyboardPressed(int64_t iKey, const engine::RawInput& rRawInput) { return rRawInput.pKeyboardKeys[iKey] && !mPreviousRawInputMenu.pKeyboardKeys[iKey]; }
	bool MousePressed(engine::MouseButtons eButton, const engine::RawInput& rRawInput) { return (rRawInput.mouseButtons & eButton) && !(mPreviousRawInputMenu.mouseButtons & eButton); }
	bool GamepadPressed(engine::GamepadButtons eButton, const engine::RawInput& rRawInput) { return (rRawInput.gamepadButtons & eButton) && !(mPreviousRawInputMenu.gamepadButtons & eButton); }
};

inline Input* gpInput = nullptr;

} // namespace game
