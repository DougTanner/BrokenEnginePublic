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
	kMenuGraphics      = 0x00010000,
	kMenuTweaks        = 0x00020000,
	kToggleScreenshots = 0x00040000,
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
	kZoomOut   = 0x0004,
	kZoomIn    = 0x0008,
};
using FrameInputHeldFlags_t = common::Flags<FrameInputHeldFlags>;

enum class FrameInputPressedFlags : uint32_t
{
	kTogglePrimary = 0x0001,
	kToggleSecondary = 0x0002,
	kToggleSkill = 0x0004,
};
using FrameInputPressedFlags_t = common::Flags<FrameInputPressedFlags>;

inline constexpr int64_t kiMaxSpawnedPlayers = 5;

struct PlayerInput
{
	bool operator==(const PlayerInput& rOther) const
	{
		return flags == rOther.flags &&
			f3Move.x == rOther.f3Move.x && f3Move.y == rOther.f3Move.y && f3Move.z == rOther.f3Move.z &&
			XMVector4Equal(vecDirection, rOther.vecDirection);
	}

	FrameInputHeldFlags_t flags {};
	XMFLOAT3 f3Move {};
	XMVECTOR vecDirection {1.0f, 0.0f, 0.0f, 0.0f};
};

struct FrameInput
{
	static constexpr int64_t kiVersion = 9;

	bool operator==(const FrameInput& rOther) const { return statusChanges == rOther.statusChanges; }

	std::vector<StatusChange> statusChanges;

	common::crc_t Crc() const;
	common::crc_t ServerInputCrc() const;

	friend std::ostream& operator<<(std::ostream& rStream, const FrameInput& rInput);
	friend std::istream& operator>>(std::istream& rStream, FrameInput& rInput);
};

// Input manager class
class Input
{
public:

	void UpdateMenuInput(bool bLostFocus, MenuInput& rMenuInput);

	bool GetGamepadMode() const { return mbGamepadMode; }

private:

	engine::RawInput mPreviousRawInputMenu {};

	bool mbGamepadMode = false;

	// Was pressed helpers (use mPreviousRawInputMenu)
	bool KeyboardPressed(int64_t iKey, const engine::RawInput& rRawInput) { return rRawInput.pKeyboardKeys[iKey] && !mPreviousRawInputMenu.pKeyboardKeys[iKey]; }
	bool MousePressed(int iButton, const engine::RawInput& rRawInput) { return rRawInput.pMouseButtons[iButton] && !mPreviousRawInputMenu.pMouseButtons[iButton]; }
	bool GamepadPressed(int iButton, const engine::RawInput& rRawInput) { return rRawInput.pGamepadButtons[iButton] && !mPreviousRawInputMenu.pGamepadButtons[iButton]; }
};

inline Input* gpInput = nullptr;

} // namespace game
