#pragma once

#include "Frame/Pools/ObjectPool.h"

#include "Frame/Pools/PoolConfig.h"

namespace game
{

struct Frame;

}

namespace engine
{

enum class SplashFlags : uint8_t
{
};
using SplashFlags_t = common::Flags<SplashFlags>;

struct SplashInfo
{
	SplashFlags_t flags {};

	DirectX::XMVECTOR vecPosition {0.0f, 0.0f, 0.0f, 1.0f};

	bool operator==(const SplashInfo& rOther) const = default;
};
struct Splash
{
	float fTime = 0.0f;
	float fNextParticleTime = 0.0f;

	bool operator==(const Splash& rOther) const = default;
};
struct Splashes : public ObjectPool<SplashInfo, Splash, splash_t, kuiMaxSplashes>
{
	void SetupSplash(game::Frame& rFrame, const SplashInfo& rSplashInfo, Splash& rSplash);

	static void PostRender(game::Frame& __restrict rFrame, float fDeltaTime);
};
static_assert(std::is_trivially_copyable_v<Splashes>);

inline constexpr int64_t kiSplashesVersion = 1 + sizeof(Splashes);

}
