#pragma once

#include "Frame/Navmesh.h"
#include "Frame/Pools/Areas.h"
#include "Frame/Pools/Billboards.h"
#include "Frame/Pools/Explosions.h"
#include "Frame/Pools/HexShields.h"
#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/Pullers.h"
#include "Frame/Pools/Pushers.h"
#include "Frame/Pools/Smoke.h"
#include "Frame/Pools/Sounds.h"
#include "Frame/Pools/Splashes.h"
#include "Frame/Pools/Targets.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

enum class FrameType
{
	kNone,
	kGlobal,
	kInterpolate,
	kFull,
};

inline FrameType gCurrentFrameTypeProcessing = FrameType::kFull;

}

namespace game
{

void WriteFrameGlobal(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);

void WriteFrameInterpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);

void WriteFrameFull(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
void WriteFrameFullSpawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
void WriteFrameFullDestroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

}

namespace engine
{

inline int64_t giBackgroundThreadCount = 0;

struct alignas(64) FrameBaseGlobal
{
	int64_t iFrame = 0;
	FrameType eFrameType = FrameType::kFull;
	IslandsFlip eIslandsFlip = kFlipNone;
	common::RandomEngine randomEngine {};
	float fCurrentTime = 0.0f;
	float fSunAngle = 1.15f;
	XMFLOAT4 f4GlobalArea {};

	bool operator==(const FrameBaseGlobal& rOther) const = default;
};
static_assert(std::is_trivially_copyable_v<FrameBaseGlobal>);

struct alignas(64) FrameBaseInterpolate
{
	alignas(64) Areas enemyAreas {};
	alignas(64) Areas playerAreas {};
	alignas(64) AreaLights areaLights {};
	alignas(64) Billboards billboards {};
	alignas(64) Explosions explosions {};
	alignas(64) HexShields hexShields {};
	alignas(64) PointLights pointLights {};
		alignas(64) PointLightControllers<2, kuiMaxPointLightControllers2> pointLightControllers2 {};
		alignas(64) PointLightControllers<3, kuiMaxPointLightControllers3> pointLightControllers3 {};
	alignas(64) Puffs puffs{};
		alignas(64) PuffControllers<2, kuiMaxPuffControllers2> puffControllers2 {};
		alignas(64) PuffControllers<3, kuiMaxPuffControllers3> puffControllers3 {};
	alignas(64) Pullers pullers {};
	alignas(64) Pushers pushers {};
	alignas(64) Sounds sounds {};
	alignas(64) Splashes splashes {};
	alignas(64) Targets targets {};
	alignas(64) Trails trails {};

	bool operator==(const FrameBaseInterpolate& rOther) const = default;
};
static_assert(std::is_trivially_copyable_v<FrameBaseInterpolate>);

struct alignas(64) FrameBaseFull
{
	alignas(64) Navmesh navmesh {};

	bool operator==(const FrameBaseFull& rOther) const = default;
};
static_assert(std::is_trivially_copyable_v<FrameBaseFull>);

struct alignas(64) FrameBase
{
	static constexpr int64_t kiVersion = 5 + kiBillboardsVersion + kiExplosionsVersion + kiHexShieldsVersion + kiLightingVersion + kiNavmeshVersion + kiSoundsVersion + kiSmokeVersion + kiPullersVersion + kiPushersVersion + kiTargetsVersion + kiSplashesVersion;

	FrameBaseGlobal global {};
	FrameBaseInterpolate interpolate {};
	FrameBaseFull full {};

	FrameBase(IslandsFlip eInitialIslandsFlip);
	~FrameBase() = default;

	bool operator==(const FrameBase& rOther) const = default;

protected:

	// Should only be called by DifferenceStreamHeader
	FrameBase() = default;
};
static_assert(std::is_trivially_copyable_v<FrameBase>);
#define UPDATE_LIST_BASE &rFrame.interpolate.billboards, &rFrame.interpolate.hexShields

void WriteFrameGlobalBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
void WriteFrameInterpolateBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput);
void WriteFrameFullBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput);

template<int64_t BUCKET_SIZE>
void Multithread(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime, int64_t iCount, void (*pFunction)(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInput& __restrict, float, int64_t, int64_t), [[maybe_unused]] CpuTimers eCpuTimer)
{
	int64_t iBuckets = static_cast<int64_t>(std::round(static_cast<float>(iCount) / static_cast<float>(BUCKET_SIZE)));
	iBuckets = std::min(iBuckets, giBackgroundThreadCount + 1);
	int64_t iBucketSize = static_cast<int64_t>(static_cast<float>(iCount) / static_cast<float>(iBuckets));
	int64_t iLeft = iCount;

	SCOPED_CPU_PROFILE_MULTITHREADED(eCpuTimer, iBuckets);

	if (iBuckets > 1)
	{
		++giMultithreading;

		std::vector<std::future<void>> futures(iBuckets - 1);
		int64_t iPos = 0;
		for (int64_t i = 0; i < iBuckets - 1; ++i)
		{
			int64_t iBucketCount = std::min(iLeft, iBucketSize);
			futures[i] = std::async(std::launch::async, [fDeltaTime, &rFrame, &rPreviousFrame, &rFrameInput, iPos, iBucketCount, pFunction]()
			{
				pFunction(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, iPos, iPos + iBucketCount);
			});

			iPos += iBucketCount;
			iLeft -= iBucketCount;
		}

		pFunction(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, iPos, iPos + iLeft);
		common::WaitAll(futures);

		--giMultithreading;
	}
	else
	{
		pFunction(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, 0, iLeft);
	}
}

#define INTERPOLATE_LIST_FUNCTION(a, b) \
template <class T, class... Ts> \
void a(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime, [[maybe_unused]] T* pCurrentT, const Ts&... nextTs) \
{ \
	T::b(rFrame, rPreviousFrame, rFrameInputHeld, fDeltaTime); \
	if constexpr (sizeof...(nextTs) > 0) \
	{ \
		a(rFrame, rPreviousFrame, rFrameInputHeld, fDeltaTime, nextTs...); \
	} \
}

#define UPDATE_LIST_FUNCTION(a, b) \
template <class T, class... Ts> \
void a(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime, [[maybe_unused]] T* pCurrentT, const Ts&... nextTs) \
{ \
	T::b(rFrame, rPreviousFrame, rFrameInput, fDeltaTime); \
	if constexpr (sizeof...(nextTs) > 0) \
	{ \
		a(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, nextTs...); \
	} \
}

INTERPOLATE_LIST_FUNCTION(GlobalList, Global)
INTERPOLATE_LIST_FUNCTION(InterpolateList, Interpolate)
UPDATE_LIST_FUNCTION(PostRenderList, PostRender)
UPDATE_LIST_FUNCTION(SpawnList, Spawn)
UPDATE_LIST_FUNCTION(CollideList, Collide)
UPDATE_LIST_FUNCTION(DestroyList, Destroy)

}
