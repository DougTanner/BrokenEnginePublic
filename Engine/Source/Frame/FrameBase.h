#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Billboards.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/HexShields.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/Trails.h"
#include "Frame/Collections/WindRadials.h"
#include "Frame/Collections/WindTrails.h"
#include "Frame/FrameUtils.h"

namespace game
{

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
	WindRadialsInterpolate windRadials {};
	WindTrailsInterpolate windTrails {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.areaLights, rSelf.billboards, rSelf.explosions, rSelf.hexShields, rSelf.pointLights, rSelf.puffs, rSelf.pushers, rSelf.sounds, rSelf.trails, rSelf.windRadials, rSelf.windTrails);
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

		bEqual &= CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;

		checksum ^= common::Crc(eFrameType);
		checksum ^= common::Crc(iFrame);
		checksum ^= common::Crc(fCurrentTime);
		checksum ^= common::Crc(fDeltaTime);

		std::apply([&](const auto&... cols)
		{
			((checksum ^= CollectionCrc(cols, cols.Members())), ...);
		}, Collections());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, eFrameType);
		common::Write(rStream, iFrame);
		common::Write(rStream, fCurrentTime);
		common::Write(rStream, fDeltaTime);

		std::apply([&](const auto&... cols)
		{
			(CollectionWrite(rStream, cols, cols.Members()), ...);
		}, Collections());
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, eFrameType);
		common::Read(rStream, iFrame);
		common::Read(rStream, fCurrentTime);
		common::Read(rStream, fDeltaTime);

		std::apply([&](auto&... cols)
		{
			(CollectionRead(rStream, cols, cols.Members()), ...);
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

	Alignments alignments {};

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
	WindRadialsPostRender windRadials {};
	WindTrailsPostRender windTrails {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.areaLights, rSelf.billboards, rSelf.explosions, rSelf.hexShields, rSelf.pointLights, rSelf.puffs, rSelf.pushers, rSelf.sounds, rSelf.trails, rSelf.windRadials, rSelf.windTrails);
	}

	inline bool operator==(const FramePostRenderBase& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
		bEqual &= common::BreakOnNotEqual(vecArea, rOther.vecArea);
		bEqual &= common::BreakOnNotEqual(uiNextUuid, rOther.uiNextUuid);
		bEqual &= common::BreakOnNotEqual(uiFrameId, rOther.uiFrameId);
		bEqual &= common::BreakOnNotEqual(alignments, rOther.alignments);

		bEqual &= CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;

		checksum ^= randomEngine.Crc();
		checksum ^= common::Crc(vecArea);
		checksum ^= common::Crc(uiNextUuid);
		checksum ^= common::Crc(uiFrameId);
		checksum ^= alignments.Crc();

		std::apply([&](const auto&... cols)
		{
			((checksum ^= CollectionCrc(cols, cols.Members())), ...);
		}, Collections());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		common::Write(rStream, randomEngine);
		common::Write(rStream, vecArea);
		common::Write(rStream, uiNextUuid);
		common::Write(rStream, uiFrameId);
		alignments.Write(rStream);

		std::apply([&](const auto&... cols)
		{
			(CollectionWrite(rStream, cols, cols.Members()), ...);
		}, Collections());
	}

	inline void Read(std::istream& rStream)
	{
		common::Read(rStream, randomEngine);
		common::Read(rStream, vecArea);
		common::Read(rStream, uiNextUuid);
		common::Read(rStream, uiFrameId);
		alignments.Read(rStream);

		std::apply([&](auto&... cols)
		{
			(CollectionRead(rStream, cols, cols.Members()), ...);
		}, Collections());
	}
};

// Type aliases derived from Collections() - must be after class definitions are complete
using InterpolateTypes = TupleToTypeList_t<decltype(std::declval<FrameInterpolateBase>().Collections())>;
using PostRenderBaseTypes = TupleToTypeList_t<decltype(std::declval<FramePostRenderBase>().Collections())>;

// Inline definition - must be after FramePostRenderBase is complete
inline uuid_t uuid_t::Generate(FramePostRenderBase& rFramePostRender)
{
	return uuid_t {rFramePostRender.GenerateUuid()};
}

#if 0
// DT: TODO Setup spaceships to use this?
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

		// DT: TODO Workbuffer / disable allocation tracking
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
