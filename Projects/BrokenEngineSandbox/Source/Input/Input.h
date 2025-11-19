#pragma once

#include "Input/RawInputManager.h"

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
#if defined(ENABLE_DEBUG_INPUT)
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
	kMenuGraphics      = 0x00010000,
	kMenuTweaks        = 0x00020000,
#endif
#if defined(ENABLE_SCREENSHOTS)
	kToggleScreenshots = 0x00040000,
#endif
};
using MenuInputFlags_t = common::Flags<MenuInputFlags>;

struct MenuInput
{
	bool bGamepad = false;
	MenuInputFlags_t flags {};
	XMFLOAT2 f2Mouse {};
	XMFLOAT2 f2Gamepad {};
};

// Frame
enum class FrameInputHeldFlags : uint64_t
{
	kPrimary   = 0x0001,
	kSecondary = 0x0002,
#if defined(ENABLE_DEBUG_INPUT)
	kZoomOut   = 0x0004,
	kZoomIn    = 0x0008,
#endif
};
using FrameInputHeldFlags_t = common::Flags<FrameInputHeldFlags>;

enum class FrameInputPressedFlags : uint32_t
{
	kTogglePrimary = 0x0001,
	kToggleSecondary = 0x0002,
	kToggleSkill = 0x0004,
};
using FrameInputPressedFlags_t = common::Flags<FrameInputPressedFlags>;

struct FrameInput
{
	static constexpr int64_t kiVersion = 2;

	// Held section (persists across frames)
	bool bGamepad = false;
	float fRotateEye = 0.0f;
	FrameInputHeldFlags_t flags {};
	XMFLOAT2 f2MovePlayer {};
	XMVECTOR vecDirection {};

	// Pressed section (cleared after each update)
	FrameInputPressedFlags_t pressedFlags {};
	int32_t iScrollWheel = 0;

	void ClearPressed()
	{
		pressedFlags = {};
		iScrollWheel = 0;
	}

	inline common::crc_t Checksum() const
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(bGamepad);
		checksum ^= common::Crc(fRotateEye);
		checksum ^= common::Crc(flags);
		checksum ^= common::Crc(f2MovePlayer);
		checksum ^= common::Crc(vecDirection);
		checksum ^= common::Crc(pressedFlags);
		checksum ^= common::Crc(iScrollWheel);
		return checksum;
	}

	bool operator==(const FrameInput& rOther) const = default;
};
static_assert(std::is_trivially_copyable_v<FrameInput>);

// Input manager class
class Input
{
public:

	bool UpdateMenuInput(const engine::RawInput& rRawInput);
	void UpdateFrameInputPressed(const engine::RawInput& rRawInput, FrameInput& rFrameInput);

	const MenuInput& GetMenuInput() const { return mMenuInput; }
	bool GetGamepadMode() const { return mbGamepadMode; }

private:

	engine::RawInput mPreviousRawInputMenu {};
	engine::RawInput mPreviousRawInputFrame {};

	MenuInput mMenuInput {};

	bool mbGamepadMode = false;
	float mfPreviousTriggerX = 0.0f;

	common::Timer mScrollWheelDelay;
	int miLastScrollWheel = 0;

	// Was pressed helpers (use mPreviousRawInputMenu)
	bool KeyboardPressed(int64_t iKey, const engine::RawInput& rRawInput) { return rRawInput.pKeyboardKeys[iKey] && !mPreviousRawInputMenu.pKeyboardKeys[iKey]; }
	bool MousePressed(int iButton, const engine::RawInput& rRawInput) { return rRawInput.pMouseButtons[iButton] && !mPreviousRawInputMenu.pMouseButtons[iButton]; }
	bool GamepadPressed(int iButton, const engine::RawInput& rRawInput) { return rRawInput.pGamepadButtons[iButton] && !mPreviousRawInputMenu.pGamepadButtons[iButton]; }

	// Was pressed helpers (pass in mPreviousRawInputFrame)
	bool KeyboardPressed(int64_t iKey, const engine::RawInput& rRawInput, const engine::RawInput& rPreviousRawInput) { return rRawInput.pKeyboardKeys[iKey] && !rPreviousRawInput.pKeyboardKeys[iKey]; }
	bool MousePressed(int iButton, const engine::RawInput& rRawInput, const engine::RawInput& rPreviousRawInput) { return rRawInput.pMouseButtons[iButton] && !rPreviousRawInput.pMouseButtons[iButton]; }
	bool GamepadPressed(int iButton, const engine::RawInput& rRawInput, const engine::RawInput& rPreviousRawInput) { return rRawInput.pGamepadButtons[iButton] && !rPreviousRawInput.pGamepadButtons[iButton]; }
};

inline Input* gpInput = nullptr;

// Raw input to frame input conversion (held portion only, pressed portion updated separately)
FrameInput RawInputToFrameInput(const engine::RawInput& rRawInput);

} // namespace game
