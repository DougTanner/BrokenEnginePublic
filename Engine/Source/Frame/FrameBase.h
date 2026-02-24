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
#include "Frame/Collections/SmokeTrails.h"
#include "Frame/Collections/WindRadials.h"
#include "Frame/Collections/WindTrails.h"
#include "Frame/FrameUtils.h"
#include "Graphics/IslandsFlip.h"

namespace game
{

struct FrameInput;

} // namespace game

namespace engine
{

enum class FrameFlags : uint64_t
{
	kInterpolate  = 0x00000001,
	kPostRender   = 0x00000002,
	kRecalculated = 0x00000004,
};
using FrameFlags_t = common::Flags<FrameFlags>;

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
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);

	FrameFlags_t frameFlags {FrameFlags::kPostRender};
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
	SmokeTrailsInterpolate smokeTrails {};
	WindRadialsInterpolate windRadials {};
	WindTrailsInterpolate windTrails {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.areaLights, rSelf.billboards, rSelf.explosions, rSelf.hexShields, rSelf.pointLights, rSelf.puffs, rSelf.pushers, rSelf.sounds, rSelf.smokeTrails, rSelf.windRadials, rSelf.windTrails);
	}

	static constexpr size_t kCollectionCount = 11;

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

		bEqual &= common::BreakOnNotEqual(frameFlags, rOther.frameFlags);
		bEqual &= common::BreakOnNotEqual(iFrame, rOther.iFrame);
		bEqual &= common::BreakOnNotEqual(fCurrentTime, rOther.fCurrentTime);
		bEqual &= common::BreakOnNotEqual(fDeltaTime, rOther.fDeltaTime);

		bEqual &= CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;

		checksum ^= common::Crc(frameFlags);
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
		common::Write(rStream, frameFlags);
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
		common::Read(rStream, frameFlags);
		common::Read(rStream, iFrame);
		common::Read(rStream, fCurrentTime);
		common::Read(rStream, fDeltaTime);

		std::apply([&](auto&... cols)
		{
			(CollectionRead(rStream, cols, cols.Members()), ...);
		}, Collections());
	}
};

static_assert(std::tuple_size_v<decltype(std::declval<FrameInterpolateBase>().Collections())> == FrameInterpolateBase::kCollectionCount,
	"FrameInterpolateBase: Collections() tuple size does not match kCollectionCount. "
	"Did you add a new collection member without updating Collections()?");

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
	static void Transfer(game::Frame& __restrict rFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	common::RandomEngine randomEngine {};
	XMVECTOR vecArea {};
	uint64_t uiNextUuid = 1;
	uint16_t uiFrameId = 0;
	IslandsFlip eIslandsFlip = kFlipNone;

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
	SmokeTrailsPostRender smokeTrails {};
	WindRadialsPostRender windRadials {};
	WindTrailsPostRender windTrails {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.areaLights, rSelf.billboards, rSelf.explosions, rSelf.hexShields, rSelf.pointLights, rSelf.puffs, rSelf.pushers, rSelf.sounds, rSelf.smokeTrails, rSelf.windRadials, rSelf.windTrails);
	}

	static constexpr size_t kCollectionCount = 11;

	inline bool operator==(const FramePostRenderBase& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
		bEqual &= common::BreakOnNotEqual(vecArea, rOther.vecArea);
		bEqual &= common::BreakOnNotEqual(uiNextUuid, rOther.uiNextUuid);
		bEqual &= common::BreakOnNotEqual(uiFrameId, rOther.uiFrameId);
		bEqual &= common::BreakOnNotEqual(eIslandsFlip, rOther.eIslandsFlip);
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
		checksum ^= common::Crc(eIslandsFlip);
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
		common::Write(rStream, eIslandsFlip);
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
		common::Read(rStream, eIslandsFlip);
		alignments.Read(rStream);

		std::apply([&](auto&... cols)
		{
			(CollectionRead(rStream, cols, cols.Members()), ...);
		}, Collections());
	}
};

static_assert(std::tuple_size_v<decltype(std::declval<FramePostRenderBase>().Collections())> == FramePostRenderBase::kCollectionCount,
	"FramePostRenderBase: Collections() tuple size does not match kCollectionCount. "
	"Did you add a new collection member without updating Collections()?");

static_assert(std::tuple_size_v<decltype(std::declval<FrameInterpolateBase>().Collections())> ==
              std::tuple_size_v<decltype(std::declval<FramePostRenderBase>().Collections())>,
	"FrameInterpolateBase and FramePostRenderBase must have the same number of collections");

// Type aliases derived from Collections() - must be after class definitions are complete
using InterpolateTypes = TupleToTypeList_t<decltype(std::declval<FrameInterpolateBase>().Collections())>;
using PostRenderBaseTypes = TupleToTypeList_t<decltype(std::declval<FramePostRenderBase>().Collections())>;

// SmokeTrails and WindTrails are excluded from ForEachInterpolateRender because their Render() takes uiFrameId.
// They are called separately in RenderFrameMain() with the per-frame ID.
using InterpolateRenderTypes = TypeList<AreaLightsInterpolate, BillboardsInterpolate, ExplosionsInterpolate,
	HexShieldsInterpolate, PointLightsInterpolate, PuffsInterpolate, PushersInterpolate, SoundsInterpolate, WindRadialsInterpolate>;

// Inline definition - must be after FramePostRenderBase is complete
inline uuid_t uuid_t::Generate(FramePostRenderBase& rFramePostRender)
{
	return uuid_t {rFramePostRender.GenerateUuid()};
}

} // namespace engine
