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
	kGlobal,
	kMain,
	kFull,
};


#if defined(BT_DEBUG)
inline FrameType gCurrentFrameTypeProcessing = FrameType::kFull;
#endif

}

namespace game
{

void FrameGlobal(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
void FrameInterpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
void FramePostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
void FrameSpawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
void FrameDestroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

}

namespace engine
{

inline constexpr std::chrono::nanoseconds kUpdateStepNs = 1'000'000'000ns / 250;
inline constexpr float kfDeltaTime = common::NanosecondsToFloatSeconds<float>(kUpdateStepNs);

inline int64_t giBackgroundThreadCount = 0;

// Can be used by frame update to decide what is visible to the player
constexpr float kfVisibleXAdjust = 0.0f;
constexpr float kfVisibleYAdjustTop = 0.0f;
constexpr float kfVisibleYAdjustBottom = 0.0f;

inline bool XM_CALLCONV InVisibleArea(DirectX::XMFLOAT4 f4VisibleArea, DirectX::XMFLOAT4 f4Position, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
{
	return !(f4Position.x < f4VisibleArea.x - fAdjustLeft || f4Position.x > f4VisibleArea.z + fAdjustRight || f4Position.y > f4VisibleArea.y + fAdjustTop || f4Position.y < f4VisibleArea.w - fAdjustBottom);
}

inline bool XM_CALLCONV InVisibleArea(DirectX::XMFLOAT4 f4VisibleArea, DirectX::XMFLOAT4A f4Position, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
{
	return !(f4Position.x < f4VisibleArea.x - fAdjustLeft || f4Position.x > f4VisibleArea.z + fAdjustRight || f4Position.y > f4VisibleArea.y + fAdjustTop || f4Position.y < f4VisibleArea.w - fAdjustBottom);
}

inline bool XM_CALLCONV InVisibleArea(DirectX::XMFLOAT4 f4VisibleArea, DirectX::FXMVECTOR vecPosition, float fAdjustLeft = 0.0f, float fAdjustRight = 0.0f, float fAdjustTop = 0.0f, float fAdjustBottom = 0.0f)
{
	DirectX::XMFLOAT4A f4Position;
	DirectX::XMStoreFloat4A(&f4Position, vecPosition);
	return InVisibleArea(f4VisibleArea, f4Position, fAdjustLeft, fAdjustRight, fAdjustTop, fAdjustBottom);
}

DirectX::XMFLOAT4 XM_CALLCONV VisibleDistances(const game::FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition);

inline float VisibleDistance(DirectX::XMFLOAT4 f4Distances)
{
	return std::min(std::min(std::min(f4Distances.x, f4Distances.y), f4Distances.z), f4Distances.w);
}

bool XM_CALLCONV InsideVisibleArea(const game::FrameInput& rFrameInput, DirectX::FXMVECTOR vecPosition, float fAdjustLeft = kfVisibleXAdjust, float fAdjustRight = kfVisibleXAdjust, float fAdjustTop = kfVisibleYAdjustTop, float fAdjustBottom = kfVisibleYAdjustBottom);
bool XM_CALLCONV OutsideVisibleArea(const game::FrameInput& rFrameInput, DirectX::FXMVECTOR vecPosition, float fAdjustLeft = kfVisibleXAdjust, float fAdjustRight = kfVisibleXAdjust, float fAdjustTop = kfVisibleYAdjustTop, float fAdjustBottom = kfVisibleYAdjustBottom);

struct alignas(64) FrameBase
{
	static constexpr int64_t kiVersion = 5 + kiBillboardsVersion + kiExplosionsVersion + kiHexShieldsVersion + kiLightingVersion + kiNavmeshVersion + kiSoundsVersion + kiSmokeVersion + kiPullersVersion + kiPushersVersion + kiTargetsVersion + kiSplashesVersion;

	// Global
	int64_t iFrame = 0;
	FrameType eFrameType = FrameType::kFull;
	IslandsFlip eIslandsFlip = kFlipNone;
	common::RandomEngine randomEngine {};
	float fCurrentTime = 0.0f;
	float fSunAngle = 1.15f;
	DirectX::XMFLOAT4 f4GlobalArea {};

	alignas(64) Navmesh navmesh {};

	// Objects in a pool don't destroy themselves and the responsibility is on the owner to Remove() them (Controllers can optionally destroy themselves)
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

	FrameBase(IslandsFlip eInitialIslandsFlip);
	~FrameBase() = default;
	
	bool operator==(const FrameBase& rOther) const = default;

protected:

	// Should only be called by DifferenceStreamHeader
	FrameBase() = default;
};
static_assert(std::is_trivially_copyable_v<FrameBase>);
#define UPDATE_LIST_BASE &rFrame.billboards, &rFrame.hexShields

void UpdateFrameBase(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime, FrameType eFrameType);

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
