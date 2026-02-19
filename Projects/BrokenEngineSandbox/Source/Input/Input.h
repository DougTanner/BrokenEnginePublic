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

inline constexpr int64_t kiMaxPlayers = 5;

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

enum class StatusChangeType : uint8_t
{
	kSpawnPlayer,
	kRespawnPlayer,
};

struct StatusChange
{
	bool operator==(const StatusChange&) const = default;

	StatusChangeType eType {};
};

struct FrameInput
{
	static constexpr int64_t kiVersion = 5;

	bool operator==(const FrameInput& rOther) const
	{
		return bGamepad == rOther.bGamepad &&
			fRotateEye == rOther.fRotateEye &&
			pressedFlags == rOther.pressedFlags &&
			iScrollWheel == rOther.iScrollWheel &&
			playerInputs == rOther.playerInputs &&
			statusChanges == rOther.statusChanges;
	}

	// Global
	bool bGamepad = false;
	float fRotateEye = 0.0f;

	// Per-player
	std::vector<PlayerInput> playerInputs;

	// Pressed (player[0] only for now)
	FrameInputPressedFlags_t pressedFlags {};
	int32_t iScrollWheel = 0;

	// Status changes
	std::vector<StatusChange> statusChanges;

	void ClearPressed()
	{
		pressedFlags = {};
		iScrollWheel = 0;
		statusChanges.clear();
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(bGamepad);
		checksum ^= common::Crc(fRotateEye);
		for (size_t i = 0; i < playerInputs.size(); ++i)
		{
			checksum ^= common::Crc(playerInputs.at(i).flags);
			checksum ^= common::Crc(playerInputs.at(i).f3Move);
			checksum ^= common::Crc(playerInputs.at(i).vecDirection);
		}
		checksum ^= common::Crc(pressedFlags);
		checksum ^= common::Crc(iScrollWheel);
		for (const StatusChange& rStatusChange : statusChanges)
		{
			checksum ^= common::Crc(rStatusChange.eType);
		}
		return checksum;
	}

	friend std::ostream& operator<<(std::ostream& rStream, const FrameInput& rInput)
	{
		common::Write(rStream, rInput.bGamepad);
		common::Write(rStream, rInput.fRotateEye);
		common::Write(rStream, rInput.pressedFlags);
		common::Write(rStream, rInput.iScrollWheel);

		int64_t iPlayerCount = static_cast<int64_t>(rInput.playerInputs.size());
		common::Write(rStream, iPlayerCount);
		if (iPlayerCount > 0)
			common::Write(rStream, rInput.playerInputs.data(), static_cast<uint64_t>(iPlayerCount));

		int64_t iStatusCount = static_cast<int64_t>(rInput.statusChanges.size());
		common::Write(rStream, iStatusCount);
		if (iStatusCount > 0)
			common::Write(rStream, rInput.statusChanges.data(), static_cast<uint64_t>(iStatusCount));

		return rStream;
	}

	friend std::istream& operator>>(std::istream& rStream, FrameInput& rInput)
	{
		common::Read(rStream, rInput.bGamepad);
		common::Read(rStream, rInput.fRotateEye);
		common::Read(rStream, rInput.pressedFlags);
		common::Read(rStream, rInput.iScrollWheel);

		int64_t iPlayerCount = 0;
		common::Read(rStream, iPlayerCount);
		rInput.playerInputs.resize(iPlayerCount);
		if (iPlayerCount > 0)
			common::Read(rStream, rInput.playerInputs.data(), static_cast<uint64_t>(iPlayerCount));

		int64_t iStatusCount = 0;
		common::Read(rStream, iStatusCount);
		rInput.statusChanges.resize(iStatusCount);
		if (iStatusCount > 0)
			common::Read(rStream, rInput.statusChanges.data(), static_cast<uint64_t>(iStatusCount));

		return rStream;
	}
};

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
void RawInputToFrameInput(const engine::RawInput& rRawInput, FrameInput& rFrameInput, int64_t iHumanIndex);

} // namespace game
