#pragma once

#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Billboards.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/HexShields.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/Trails.h"
#include "Graphics/Graphics.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;

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

struct FrameInterpolateBase
{
	FrameInterpolateBase();
	~FrameInterpolateBase() = default;

	// Called on Game creation
	static void Register();

	// Called during Graphics creation
	static void GraphicsResources();

	// Interpolate phase
	static void AllocateAndCopy(game::FrameInterpolate& __restrict rCurrent, const game::FrameInterpolate& __restrict rPrevious);
	static void Update(game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	FrameType eFrameType = FrameType::kPostRender;
	int64_t iFrame = 0;
	float fCurrentTime = 0.0f;
	float fDeltaTime = 0.0f;

	AreaLightsInterpolate areaLights {};
	BillboardsInterpolate billboards {};
	ExplosionsInterpolate explosions {};
	HexShieldsInterpolate hexShields {};
	PointLightsInterpolate pointLights {};
	PuffsInterpolate puffs {};
	PushersInterpolate pushers {};
	SoundsInterpolate sounds {};
	TrailsInterpolate trails {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.areaLights, rSelf.billboards, rSelf.explosions, rSelf.hexShields, rSelf.pointLights, rSelf.puffs, rSelf.pushers, rSelf.sounds, rSelf.trails);
	}

	// Visibility bounds (X = East/West, Y = North/South)
	static inline constexpr float kfVisibleEastWest = 65.0f;
	static inline constexpr float kfVisibleNorthSouth = 45.0f;

	[[nodiscard]] static bool XM_CALLCONV IsVisible(FXMVECTOR vecSource, FXMVECTOR vecTarget)
	{
		float fDeltaX = std::abs(XMVectorGetX(vecTarget) - XMVectorGetX(vecSource));
		float fDeltaY = std::abs(XMVectorGetY(vecTarget) - XMVectorGetY(vecSource));
		return fDeltaX <= kfVisibleEastWest && fDeltaY <= kfVisibleNorthSouth;
	}

	inline bool operator==(const FrameInterpolateBase& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual(eFrameType, rOther.eFrameType);
		bEqual &= common::BreakOnNotEqual(iFrame, rOther.iFrame);
		bEqual &= common::BreakOnNotEqual(fCurrentTime, rOther.fCurrentTime);
		bEqual &= common::BreakOnNotEqual(fDeltaTime, rOther.fDeltaTime);

		bEqual &= common::BreakOnNotEqual(areaLights, rOther.areaLights);
		bEqual &= common::BreakOnNotEqual(billboards, rOther.billboards);
		bEqual &= common::BreakOnNotEqual(explosions, rOther.explosions);
		bEqual &= common::BreakOnNotEqual(hexShields, rOther.hexShields);
		bEqual &= common::BreakOnNotEqual(pointLights, rOther.pointLights);
		bEqual &= common::BreakOnNotEqual(puffs, rOther.puffs);
		bEqual &= common::BreakOnNotEqual(pushers, rOther.pushers);
		bEqual &= common::BreakOnNotEqual(sounds, rOther.sounds);
		bEqual &= common::BreakOnNotEqual(trails, rOther.trails);

		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;

		checksum ^= common::Crc(eFrameType);
		checksum ^= common::Crc(iFrame);
		checksum ^= common::Crc(fCurrentTime);
		checksum ^= common::Crc(fDeltaTime);

		std::apply([&checksum](const auto&... rCollections)
		{
			((checksum ^= CollectionCrc(rCollections, rCollections.Members())), ...);
		}, Collections());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, eFrameType);
		common::Write(rStream, iFrame);
		common::Write(rStream, fCurrentTime);
		common::Write(rStream, fDeltaTime);

		std::apply([&rStream](const auto&... rCollections)
		{
			(CollectionWrite(rStream, rCollections, rCollections.Members()), ...);
		}, Collections());
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, eFrameType);
		common::Read(rStream, iFrame);
		common::Read(rStream, fCurrentTime);
		common::Read(rStream, fDeltaTime);

		std::apply([&rStream](auto&... rCollections)
		{
			(CollectionRead(rStream, rCollections, rCollections.Members()), ...);
		}, Collections());
	}
};

struct FramePostRenderBase
{
	FramePostRenderBase();
	~FramePostRenderBase() = default;

	// Post render phases
	static void AllocateAndCopy(game::FramePostRender& __restrict rCurrent, const game::FramePostRender& __restrict rPrevious);
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	common::RandomEngine randomEngine {};
	XMVECTOR vecArea {};
	uint64_t uiNextUuid = 1;
	uint16_t uiFrameId = 0;

	int64_t GenerateUuid()
	{
		int64_t iCounter = uiNextUuid++;
		return (static_cast<int64_t>(uiFrameId) << 48) | (iCounter & 0x0000FFFFFFFFFFFF);
	}

	AreaLightsPostRender areaLights {};
	BillboardsPostRender billboards {};
	ExplosionsPostRender explosions {};
	HexShieldsPostRender hexShields {};
	PointLightsPostRender pointLights {};
	PuffsPostRender puffs {};
	PushersPostRender pushers {};
	SoundsPostRender sounds {};
	TrailsPostRender trails {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.areaLights, rSelf.billboards, rSelf.explosions, rSelf.hexShields, rSelf.pointLights, rSelf.puffs, rSelf.pushers, rSelf.sounds, rSelf.trails);
	}

	inline bool operator==(const FramePostRenderBase& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
		bEqual &= common::BreakOnNotEqual(vecArea, rOther.vecArea);
		bEqual &= common::BreakOnNotEqual(uiNextUuid, rOther.uiNextUuid);
		bEqual &= common::BreakOnNotEqual(uiFrameId, rOther.uiFrameId);

		bEqual &= common::BreakOnNotEqual(areaLights, rOther.areaLights);
		bEqual &= common::BreakOnNotEqual(billboards, rOther.billboards);
		bEqual &= common::BreakOnNotEqual(explosions, rOther.explosions);
		bEqual &= common::BreakOnNotEqual(hexShields, rOther.hexShields);
		bEqual &= common::BreakOnNotEqual(pointLights, rOther.pointLights);
		bEqual &= common::BreakOnNotEqual(puffs, rOther.puffs);
		bEqual &= common::BreakOnNotEqual(pushers, rOther.pushers);
		bEqual &= common::BreakOnNotEqual(sounds, rOther.sounds);
		bEqual &= common::BreakOnNotEqual(trails, rOther.trails);

		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;

		checksum ^= randomEngine.Crc();
		checksum ^= common::Crc(vecArea);
		checksum ^= common::Crc(uiNextUuid);
		checksum ^= common::Crc(uiFrameId);

		std::apply([&checksum](const auto&... rCollections)
		{
			((checksum ^= CollectionCrc(rCollections, rCollections.Members())), ...);
		}, Collections());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, randomEngine);
		common::Write(rStream, uiNextUuid);
		common::Write(rStream, uiFrameId);
		common::Write(rStream, vecArea);

		std::apply([&rStream](const auto&... rCollections)
		{
			(CollectionWrite(rStream, rCollections, rCollections.Members()), ...);
		}, Collections());
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, randomEngine);
		common::Read(rStream, uiNextUuid);
		common::Read(rStream, uiFrameId);
		common::Read(rStream, vecArea);

		std::apply([&rStream](auto&... rCollections)
		{
			(CollectionRead(rStream, rCollections, rCollections.Members()), ...);
		}, Collections());
	}
};

// Inline definition - must be after FramePostRenderBase is complete
inline uuid_t uuid_t::Generate(FramePostRenderBase& rFramePostRender)
{
	return uuid_t {rFramePostRender.GenerateUuid()};
}

#if 0
template<int64_t BUCKET_SIZE>
void Multithread(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime, int64_t iCount, void (*pFunction)(game::Frame& __restrict, const game::Frame& __restrict, const game::FrameInputHeld& __restrict, const game::FrameInputPressed& __restrict, float, int64_t, int64_t), [[maybe_unused]] CpuTimers eCpuTimer)
{
	int64_t iBuckets = static_cast<int64_t>(std::round(static_cast<float>(iCount) / static_cast<float>(BUCKET_SIZE)));
	iBuckets = std::min(iBuckets, giBackgroundThreadCount + 1);
	int64_t iBucketSize = static_cast<int64_t>(static_cast<float>(iCount) / static_cast<float>(iBuckets));
	int64_t iLeft = iCount;

	ScopedCpuProfile scopedCpuProfile(eCpuTimer, iBuckets);

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
#endif

} // namespace engine
