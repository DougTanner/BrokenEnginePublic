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
	DirectX::XMFLOAT2 f2Mouse {};
	DirectX::XMFLOAT2 f2Gamepad {};
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

enum class FrameInputPressedFlags : uint64_t
{
	kTogglePrimary   = 0x0001,
	kToggleSecondary = 0x0002,
	kToggleSkill     = 0x0004,
};
using FrameInputPressedFlags_t = common::Flags<FrameInputPressedFlags>;

struct FrameInputHeld
{
	bool bGamepad = false;
	float fRotateEye = 0.0f;
	FrameInputHeldFlags_t flags {};
	DirectX::XMFLOAT2 f2MovePlayer {};
	DirectX::XMVECTOR vecDirection {};

	bool operator==(const FrameInputHeld& rOther) const = default;
};
	
struct FrameInput
{
	static constexpr int64_t kiVersion = 3;

	FrameInputHeld held {};

	FrameInputPressedFlags_t pressedFlags {};

	// Used to decide if enemies are visible to the player (during play)
	// Putting it here in the FrameInput will save it into replays, for deterministic playback
	// Doesn't affect visibility, just enemies decisions on when to start attacking
	// Also affects damage done to enemies (can't damage offscreen)
	DirectX::XMFLOAT4 f4VisibleTopLeft {};
	DirectX::XMFLOAT4 f4VisibleTopRight {};
	DirectX::XMFLOAT4 f4VisibleBottomLeft {};
	DirectX::XMFLOAT4 f4VisibleBottomRight {};
	DirectX::XMFLOAT4 f4LargeVisibleArea {};

	bool operator==(const FrameInput& rOther) const = default;
};

// Raw input
std::tuple<MenuInput, FrameInput> ProcessRawInput(const engine::RawInput& rRawInput);

} // namespace game
