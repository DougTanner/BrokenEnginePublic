#pragma once

namespace game
{

struct Frame;
struct FrameInputHeld;
struct FrameInputPressed;

} // namespace game

namespace engine
{

enum class FrameType
{
	kNone,
	kInterpolate,
	kPostRender,
};

inline int64_t giBackgroundThreadCount = 0;

// DT: TODO Should not be necessary once refactor done
inline FrameType gCurrentFrameTypeProcessing = FrameType::kPostRender;

struct FrameBase
{
	static constexpr int64_t kiVersion = 1;

	FrameBase();
	~FrameBase() = default;

	static void UpdateInterpolate(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;

	int64_t iFrame = 0;
	FrameType eFrameType = FrameType::kPostRender;

	XMFLOAT4 f4GlobalArea {};

	// DT: TODO
	bool operator==(const FrameBase& rOther) const = default;
};

struct FrameInterpolateBase
{
	static constexpr int64_t kiVersion = 1;

	static void Update(FrameInterpolateBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;

	float fSunAngle = 1.15f;

	// DT: TODO
	bool operator==(const FrameInterpolateBase& rOther) const = default;
};

struct FramePostRenderBase
{
	static constexpr int64_t kiVersion = 1;

	static void Update(FramePostRenderBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	common::RandomEngine randomEngine {};

	// DT: TODO
	bool operator==(const FramePostRenderBase& rOther) const = default;
};

#if 0

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

namespace game
{

struct FrameInputPressed;

void WriteFrameInterpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);

void WriteFramePostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);
void WriteFramePostRenderSpawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);
void WriteFramePostRenderDestroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, const FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

}

struct alignas(64) FrameBaseInterpolate
{
	// Remove
	float fCurrentTime = 0.0f;

	// Split
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

	bool operator==(const FrameBaseInterpolate& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FrameBaseInterpolate>);

struct alignas(64) FrameBasePostRender
{
	alignas(64) Navmesh navmesh {};

	bool operator==(const FrameBasePostRender& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FrameBasePostRender>);

void WriteFrameInterpolateBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
void WriteFramePostRenderBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed);

template<int64_t BUCKET_SIZE>
void Multithread(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime, int64_t iCount, void (*pFunction)(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, const game::FrameInputPressed& __restrict, float, int64_t, int64_t), [[maybe_unused]] CpuTimers eCpuTimer)
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
			futures[i] = std::async(std::launch::async, [fDeltaTime, &rFrame, &rPreviousFrame, &rFrameInputHeld, &rFrameInputPressed, iPos, iBucketCount, pFunction]()
			{
				pFunction(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, fDeltaTime, iPos, iPos + iBucketCount);
			});

			iPos += iBucketCount;
			iLeft -= iBucketCount;
		}

		pFunction(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, fDeltaTime, iPos, iPos + iLeft);
		common::WaitAll(futures);

		--giMultithreading;
	}
	else
	{
		pFunction(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, fDeltaTime, 0, iLeft);
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
void a(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime, [[maybe_unused]] T* pCurrentT, const Ts&... nextTs) \
{ \
	T::b(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, fDeltaTime); \
	if constexpr (sizeof...(nextTs) > 0) \
	{ \
		a(rFrame, rPreviousFrame, rFrameInputHeld, rFrameInputPressed, fDeltaTime, nextTs...); \
	} \
}

INTERPOLATE_LIST_FUNCTION(InterpolateList, Interpolate)
UPDATE_LIST_FUNCTION(PostRenderList, PostRender)
UPDATE_LIST_FUNCTION(SpawnList, Spawn)
UPDATE_LIST_FUNCTION(CollideList, Collide)
UPDATE_LIST_FUNCTION(DestroyList, Destroy)

#endif

}
