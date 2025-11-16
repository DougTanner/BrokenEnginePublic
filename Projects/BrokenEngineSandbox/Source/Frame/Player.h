#pragma once

namespace game
{

struct Frame;
struct FrameInputHeld;
struct FrameInputPressed;

struct PlayerInterpolate
{
	static constexpr int64_t kiVersion = 1;

	static void Update(PlayerInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	void Render(int64_t iCommandBuffer) const;

	XMVECTOR vecPosition = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR vecDirection {1.0f, 0.0f, 0.0f, 0.0f};

	// DT: TODO
	bool operator==(const PlayerInterpolate& rOther) const = default;
};

inline std::ostream& operator<<(std::ostream& rStream, const PlayerInterpolate& rPlayer)
{
	rStream.write(reinterpret_cast<const char*>(&rPlayer.vecPosition), sizeof(rPlayer.vecPosition));
	rStream.write(reinterpret_cast<const char*>(&rPlayer.vecDirection), sizeof(rPlayer.vecDirection));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, PlayerInterpolate& rPlayer)
{
	rStream.read(reinterpret_cast<char*>(&rPlayer.vecPosition), sizeof(rPlayer.vecPosition));
	rStream.read(reinterpret_cast<char*>(&rPlayer.vecDirection), sizeof(rPlayer.vecDirection));
	return rStream;
}

enum class PlayerFlags : uint8_t
{
	kExploding = 0x01,
};
using PlayerFlags_t = common::Flags<PlayerFlags>;

struct PlayerPostRender
{
	static constexpr int64_t kiVersion = 1;

	static void Update(PlayerPostRender& __restrict rCurrent, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	PlayerFlags_t flags;
	XMVECTOR vecVelocity {0.0f, 0.0f, 0.0f, 0.0f};
	XMVECTOR vecWantedDirection {1.0f, 0.0f, 0.0f, 0.0f};

	// DT: TODO
	bool operator==(const PlayerPostRender& rOther) const = default;
};

inline std::ostream& operator<<(std::ostream& rStream, const PlayerPostRender& rPlayer)
{
	rStream << rPlayer.flags;
	rStream.write(reinterpret_cast<const char*>(&rPlayer.vecVelocity), sizeof(rPlayer.vecVelocity));
	rStream.write(reinterpret_cast<const char*>(&rPlayer.vecWantedDirection), sizeof(rPlayer.vecWantedDirection));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, PlayerPostRender& rPlayer)
{
	rStream >> rPlayer.flags;
	rStream.read(reinterpret_cast<char*>(&rPlayer.vecVelocity), sizeof(rPlayer.vecVelocity));
	rStream.read(reinterpret_cast<char*>(&rPlayer.vecWantedDirection), sizeof(rPlayer.vecWantedDirection));
	return rStream;
}

} // namespace game

#if 0

#include "Frame/HealthDamage.h"
#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/Smoke.h"

namespace game
{

constexpr float kfMissileDamagePlayerRadius = 1.5f;

struct alignas(64) Player
{
	static float MaxArmor(const Frame& __restrict rFrame);
	static float MaxShield(const Frame& __restrict rFrame);
	static float MaxEnergy(const Frame& __restrict rFrame);
	static float MissileCapacity(const Frame& __restrict rFrame);
	
	static std::tuple<int64_t, int64_t> SecondaryCapacity(const Frame& __restrict rFrame);

	// Interpolate
	PlayerFlags_t flags {};
	float fShieldRotation = 0.0f;
	float fShieldShrink = 1.0f;
	float fShieldCooldown = 0.0f;
	shaders::HexShieldLayout hexShieldLayout {};
	engine::hex_shield_t uiHexShield = 0;
	engine::target_t uiTarget = 0;
	float fSkillTime = 0.0f;
	XMVECTOR vecDashDirection {};
	engine::area_light_t uiDashAreaLight = 0;
	engine::area_light_t uiSpotlightAreaLight = 0;
	XMVECTOR vecSpotlightDirection {1.0f, 0.0f, 0.0f, 0.0f};

	// Post render
	float fNextSecondarySpawnTime = 0.0f;

	bool bBlasterToggledOn = false;
	float fNextPrimarySpawnTime = 0.0f;
	bool bBlasterSpawnLeft = true;

	float fArmor = kfPlayerArmor;
	float fShield = kfPlayerShield;
	float fEnergy = kfPlayerEnergy;
	float fMissiles = kfPlayerMissileCapacity;

	float fShellTimeLeft = 0.0f;

	float fShieldDownSoundCooldown = 0.0f;

	float fDestroyedTime = 0.0f;
	float fDestroyedExplosionTime = 0.0f;

	// Utility
	bool operator==(const Player& rOther) const;

	// Update
	static void InterpolateDash(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);

	static void PostRenderBlasters(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime, std::optional<XMVECTOR>& rOptionalClosestEnemy, bool bClosestIsVisible);
	static void PostRenderMissiles(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime, std::optional<XMVECTOR>& rOptionalClosestEnemy, bool bClosestIsVisible);
	static void PostRenderDash(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	static void XM_CALLCONV Damage(Frame& __restrict rFrame, float fDamage, FXMVECTOR vecPosition, float fHexShield, bool bSound = true);
	static float XM_CALLCONV AreaDamage(Frame& __restrict rFrame, float fDamage, const common::AreaVertices& rAreaVertices);
	static std::tuple<float, XMVECTOR> XM_CALLCONV CollideBlasters(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, FXMVECTOR vecPosition, float& rfShield);
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	// Render
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Player>);

} // namespace game

#endif