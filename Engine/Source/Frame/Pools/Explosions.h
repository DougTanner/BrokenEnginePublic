#pragma once

#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/Pushers.h"
#include "Frame/Pools/Smoke.h"

namespace game
{

struct Frame;

}

namespace engine
{

enum class ExplosionFlags : uint8_t
{
	kDestroysSelf = 0x01,
	kYellow       = 0x02,
	kRed          = 0x04,
};
using ExplosionFlags_t = common::Flags<ExplosionFlags>;

inline constexpr int64_t kiMaxExplosionTrails = 8;

struct ExplosionInfo
{
	ExplosionFlags_t flags {};

	DirectX::XMVECTOR vecPosition {0.0f, 0.0f, 0.0f, 1.0f};
	DirectX::XMVECTOR vecDirection {1.0f, 0.0f, 0.0f, 0.0f};

	uint32_t uiParticleCount = 0;
	float fParticleAngle = DirectX::XM_2PI;

	uint32_t uiTrailCount = 0;
	float fTrailAngle = DirectX::XM_2PI;

	float fLightPercent = 1.0f;
	float fPusherPercent = 1.0f;
	float fSizePercent = 1.0f;
	float fSmokePercent = 1.0f;
	float fTimePercent = 1.0f;

	bool operator==(const ExplosionInfo& rOther) const = default;
};
struct Explosion
{
	float fStartTime = 0;

	int32_t iTrailCount = 0;
	trail_t pTrails[kiMaxExplosionTrails] {};
	float pfTrailTimes[kiMaxExplosionTrails] {};
	float pfTrailIntensities[kiMaxExplosionTrails] {};
	DirectX::XMVECTOR pVecTrailStartPositions[kiMaxExplosionTrails] {};
	DirectX::XMVECTOR pVecTrailEndPositions[kiMaxExplosionTrails] {};

	pusher_t pusher = 0;

	bool operator==(const Explosion& rOther) const = default;
};
struct Explosions : public ObjectPool<ExplosionInfo, Explosion, explosion_t, kuiMaxExplosions>
{
	void SetupExplosion(game::Frame& rFrame, const ExplosionInfo& rExplosionInfo, Explosion& rExplosion);

	void Add(explosion_t& ruiIndex, game::Frame& rFrame, const ExplosionInfo& rExplosionInfo)
	{
		ObjectPool::Add(ruiIndex, rExplosionInfo);

		if (ruiIndex != 0)
		{
			SetupExplosion(rFrame, rExplosionInfo, Get(ruiIndex));
		}
	}

	static void Interpolate(game::Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Explosions>);

inline constexpr int64_t kiExplosionsVersion = 1 + sizeof(Explosions);

}
