#pragma once

#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/ControlledPointLights.h"
#include "Frame/Collections/PointLights.h"
#include "Graphics/Graphics.h"

namespace game
{

struct Frame;
struct FrameInput;

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

	// Called on Game creation
	static void Register();

	// Called during Graphics creation
	static void AllocateGraphicsResources();

	// Interpolate phases
	static void InterpolateUpdate(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void InterpolateSync(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render an interpolated Frame
	static void Render(const game::Frame& __restrict rFrame, int64_t iCommandBuffer);

	// Post render phases
	static void PostRenderUpdate(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput);
	static void PostRenderPreCollision(FrameBase& __restrict rCurrent);
	static void PostRenderCollide();
	static void PostRenderPostCollision(FrameBase& __restrict rCurrent);
	static void PostRenderSpawn(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostRenderDestroy(FrameBase& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Interpolate
	int64_t iFrame = 0;
	FrameType eFrameType = FrameType::kPostRender;
	XMFLOAT4 f4GlobalArea {};

	// Post render

	inline bool operator==(const FrameBase& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(iFrame, rOther.iFrame);
		bEqual &= common::BreakOnNotEqual(eFrameType, rOther.eFrameType);
		bEqual &= common::BreakOnNotEqual(f4GlobalArea, rOther.f4GlobalArea);
		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(iFrame);
		checksum ^= common::Crc(eFrameType);
		checksum ^= common::Crc(f4GlobalArea);
		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, iFrame);
		common::Write(rStream, eFrameType);
		common::Write(rStream, f4GlobalArea);
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, iFrame);
		common::Read(rStream, eFrameType);
		common::Read(rStream, f4GlobalArea);
	}
};

struct FrameInterpolateBase
{
	static constexpr int64_t kiVersion = 5;

	static void Update(game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Sync(game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Render(const game::Frame& __restrict rFrame, int64_t iCommandBuffer);

	float fSunAngle = 1.15f;
	float fCurrentTime = 0.0f;

	AreaLightsInterpolate areaLights;
	ControlledPointLightsInterpolate controlledPointLights;
	PointLightsInterpolate pointLights;

	inline bool operator==(const FrameInterpolateBase& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(fSunAngle, rOther.fSunAngle);
		bEqual &= common::BreakOnNotEqual(fCurrentTime, rOther.fCurrentTime);
		bEqual &= common::BreakOnNotEqual(areaLights, rOther.areaLights);
		bEqual &= common::BreakOnNotEqual(controlledPointLights, rOther.controlledPointLights);
		bEqual &= common::BreakOnNotEqual(pointLights, rOther.pointLights);
		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(fSunAngle);
		checksum ^= common::Crc(fCurrentTime);
		checksum ^= engine::CollectionCrc(areaLights, areaLights.Members());
		checksum ^= engine::CollectionCrc(controlledPointLights, controlledPointLights.Members());
		checksum ^= engine::CollectionCrc(pointLights, pointLights.Members());
		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, fSunAngle);
		common::Write(rStream, fCurrentTime);
		CollectionWrite(rStream, areaLights, areaLights.Members());
		CollectionWrite(rStream, controlledPointLights, controlledPointLights.Members());
		CollectionWrite(rStream, pointLights, pointLights.Members());
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, fSunAngle);
		common::Read(rStream, fCurrentTime);
		CollectionRead(rStream, areaLights, areaLights.Members());
		CollectionRead(rStream, controlledPointLights, controlledPointLights.Members());
		CollectionRead(rStream, pointLights, pointLights.Members());
	}
};

struct FramePostRenderBase
{
	static constexpr int64_t kiVersion = 5;

	static void Update(game::FramePostRender& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput);
	static void PreCollision(game::Frame& __restrict rFrame);
	static void PostCollision(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);
	static void Destroy(game::Frame& __restrict rFrame);

	common::RandomEngine randomEngine {};
	int64_t iNextUuid = 1;

	AreaLightsPostRender areaLights;
	ControlledPointLightsPostRender controlledPointLights;
	PointLightsPostRender pointLights;

	inline bool operator==(const FramePostRenderBase& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
		bEqual &= common::BreakOnNotEqual(iNextUuid, rOther.iNextUuid);
		bEqual &= common::BreakOnNotEqual(areaLights, rOther.areaLights);
		bEqual &= common::BreakOnNotEqual(controlledPointLights, rOther.controlledPointLights);
		bEqual &= common::BreakOnNotEqual(pointLights, rOther.pointLights);
		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= randomEngine.Crc();
		checksum ^= common::Crc(iNextUuid);
		checksum ^= engine::CollectionCrc(areaLights, areaLights.Members());
		checksum ^= engine::CollectionCrc(controlledPointLights, controlledPointLights.Members());
		checksum ^= engine::CollectionCrc(pointLights, pointLights.Members());
		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, randomEngine);
		common::Write(rStream, iNextUuid);
		engine::CollectionWrite(rStream, areaLights, areaLights.Members());
		engine::CollectionWrite(rStream, controlledPointLights, controlledPointLights.Members());
		engine::CollectionWrite(rStream, pointLights, pointLights.Members());
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, randomEngine);
		common::Read(rStream, iNextUuid);
		engine::CollectionRead(rStream, areaLights, areaLights.Members());
		engine::CollectionRead(rStream, controlledPointLights, controlledPointLights.Members());
		engine::CollectionRead(rStream, pointLights, pointLights.Members());
	}
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

	inline bool operator==(const FrameBaseInterpolate& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FrameBaseInterpolate>);

struct alignas(64) FrameBasePostRender
{
	alignas(64) Navmesh navmesh {};

	inline bool operator==(const FrameBasePostRender& rOther) const;
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

} // namespace engine
