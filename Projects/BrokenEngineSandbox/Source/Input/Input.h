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

struct FrameInputHeld
{
	static constexpr int64_t kiVersion = 1;

	bool bGamepad = false; // DT: TODO Make Flag
	float fRotateEye = 0.0f;
	FrameInputHeldFlags_t flags {};
	XMFLOAT2 f2MovePlayer {};
	XMVECTOR vecDirection {};

	bool operator==(const FrameInputHeld& rOther) const = default;
};
	
enum class FrameInputPressedFlags : uint32_t
{
	kTogglePrimary = 0x0001,
	kToggleSecondary = 0x0002,
	kToggleSkill = 0x0004,
};
using FrameInputPressedFlags_t = common::Flags<FrameInputPressedFlags>;

struct FrameInputPressed
{
	static constexpr int64_t kiVersion = 1;

	FrameInputPressedFlags_t pressedFlags {};
	int32_t iScrollWheel = 0;

	bool operator==(const FrameInputPressed& rOther) const = default;
};

// Input manager class
class Input
{
public:

	bool UpdateMenuInput(const engine::RawInput& rRawInput);
	FrameInputPressed UpdateFrameInputPressed(const engine::RawInput& rRawInput);

	const MenuInput& GetMenuInput() const { return mMenuInput; }
	bool GetGamepadMode() const { return mbGamepadMode; }

private:

	engine::RawInput mPreviousRawInputMenu {};
	engine::RawInput mPreviousRawInputFrame {};

	MenuInput mMenuInput {};
	FrameInputPressed mFrameInputPressed {};

	bool mbGamepadMode = false;
	float mfPreviousTriggerX = 0.0f;

	common::Timer mScrollWheelDelay;
	int miLastScrollWheel = 0;

	static bool WasPressed(bool bCurrent, bool bPrevious) { return bCurrent && !bPrevious; }
	static bool WasReleased(bool bCurrent, bool bPrevious) { return !bCurrent && bPrevious; }
};

inline Input* gpInput = nullptr;

// Raw input (kept for FrameInputHeld which doesn't need toggle detection)
FrameInputHeld RawInputToFrameInputHeld(const engine::RawInput& rRawInput);

} // namespace game
