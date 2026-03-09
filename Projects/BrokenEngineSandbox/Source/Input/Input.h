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

enum class StatusChangeType : uint8_t
{
	kSpawnPlayer,
	kRespawnPlayer,
	kTransferPlayer,
	kTransferSpaceship,
	kTransferBlaster,
	kTransferMissile,
	kDestroyPlayer,
};

inline bool IsTransferType(StatusChangeType eType)
{
	return eType >= StatusChangeType::kTransferPlayer && eType <= StatusChangeType::kTransferMissile;
}

struct TransferData
{
	bool operator==(const TransferData& rOther) const
	{
		return XMVector4Equal(vecPosition, rOther.vecPosition) &&
			XMVector4Equal(vecDirection, rOther.vecDirection) &&
			XMVector4Equal(vecVelocity, rOther.vecVelocity) &&
			alignment == rOther.alignment &&
			fHealth == rOther.fHealth &&
			fShield == rOther.fShield &&
			uiTypeIndex == rOther.uiTypeIndex &&
			fWindTrailIntensity == rOther.fWindTrailIntensity &&
			fWindTrailWidth == rOther.fWindTrailWidth &&
			fWindTrailLengthMultiplier == rOther.fWindTrailLengthMultiplier &&
			fAcceleration == rOther.fAcceleration &&
			fNextBlasterFireTime == rOther.fNextBlasterFireTime &&
			fNextSecondarySpawnTime == rOther.fNextSecondarySpawnTime &&
			fShieldCooldown == rOther.fShieldCooldown &&
			fShieldDownSoundCooldown == rOther.fShieldDownSoundCooldown &&
			fAnimationTime == rOther.fAnimationTime &&
			fShieldRotation == rOther.fShieldRotation &&
			fShieldShrink == rOther.fShieldShrink &&
			uiPlayerFlags == rOther.uiPlayerFlags &&
			fNextBlasterSpawnTime == rOther.fNextBlasterSpawnTime &&
			fDeltaRotationDelay == rOther.fDeltaRotationDelay &&
			fTime == rOther.fTime &&
			fExhaustDelay == rOther.fExhaustDelay &&
			fNextJitter == rOther.fNextJitter
#if defined(BT_CLIENT)
			&& smokeTrailId == rOther.smokeTrailId
#endif
			;
	}

	XMVECTOR vecPosition {};
	XMVECTOR vecDirection {};
	XMVECTOR vecVelocity {};
	engine::alignment_t alignment {};
	float fHealth = 0.0f;
	float fShield = 0.0f;
	uint8_t uiTypeIndex = 0;
	float fWindTrailIntensity = 0.0f;
	float fWindTrailWidth = 0.0f;
	float fWindTrailLengthMultiplier = 1.0f;
	float fAcceleration = 0.0f;

	// Player timers
	float fNextBlasterFireTime = 0.0f;
	float fNextSecondarySpawnTime = 0.0f;
	float fShieldCooldown = 0.0f;
	float fShieldDownSoundCooldown = 0.0f;

	// Player interpolate state
	float fAnimationTime = 0.0f;
	float fShieldRotation = 0.0f;
	float fShieldShrink = 1.0f;
	uint8_t uiPlayerFlags = 0;

	// Spaceship timers
	float fNextBlasterSpawnTime = 0.0f;

	// Missile timers
	float fDeltaRotationDelay = 0.0f;
	float fTime = 0.0f;
	float fExhaustDelay = 0.0f;
	float fNextJitter = 0.0f;

	// Smoke trail ID reuse (missiles only)
#if defined(BT_CLIENT)
	engine::smoke_trails_t smokeTrailId {};
#endif
};

struct StatusChange
{
	bool operator==(const StatusChange&) const = default;

	StatusChangeType eType {};
	TransferData data {};
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

	bool UpdateMenuInput(const engine::RawInput& rRawInput);

	const MenuInput& GetMenuInput() const { return mMenuInput; }
	bool GetGamepadMode() const { return mbGamepadMode; }

private:

	engine::RawInput mPreviousRawInputMenu {};

	MenuInput mMenuInput {};

	bool mbGamepadMode = false;

	// Was pressed helpers (use mPreviousRawInputMenu)
	bool KeyboardPressed(int64_t iKey, const engine::RawInput& rRawInput) { return rRawInput.pKeyboardKeys[iKey] && !mPreviousRawInputMenu.pKeyboardKeys[iKey]; }
	bool MousePressed(int iButton, const engine::RawInput& rRawInput) { return rRawInput.pMouseButtons[iButton] && !mPreviousRawInputMenu.pMouseButtons[iButton]; }
	bool GamepadPressed(int iButton, const engine::RawInput& rRawInput) { return rRawInput.pGamepadButtons[iButton] && !mPreviousRawInputMenu.pGamepadButtons[iButton]; }
};

inline Input* gpInput = nullptr;

} // namespace game
