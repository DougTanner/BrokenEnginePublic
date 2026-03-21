#pragma once

#include "Frame/Alignments.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/AreaLights/AreaLights.h"
#include "Frame/Collections/Billboards/Billboards.h"
#endif
#include "Frame/Collections/Explosions/Explosions.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/HexShields/HexShields.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#endif
#include "Frame/Collections/Pushers/Pushers.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/Sounds/Sounds.h"
#include "Frame/Collections/SmokeTrails/SmokeTrails.h"
#include "Frame/Collections/WindRadials/WindRadials.h"
#include "Frame/Collections/WindTrails/WindTrails.h"
#endif
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

struct FrameInterpolateBase
{
	FrameInterpolateBase() = default;
	~FrameInterpolateBase() = default;
	FrameInterpolateBase(FrameInterpolateBase&&) noexcept = default;
	FrameInterpolateBase& operator=(FrameInterpolateBase&&) noexcept = default;

	// Called on Game creation
	static void Register();

#if defined(BT_CLIENT)
	// Called during Graphics creation
	static void GraphicsResources();
#endif

	// Interpolate phase
	static void AllocateAndCopy(game::FrameInterpolate& __restrict rCurrent, const game::FrameInterpolate& __restrict rPrevious);
	static void Update(game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

#if defined(BT_CLIENT)
	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
#endif

	FrameFlags_t frameFlags {FrameFlags::kPostRender};
	int64_t iTick = 0;
	float fCurrentTime = 0.0f;
	float fDeltaTime = 0.0f;

#if defined(BT_CLIENT)
	AreaLightsInterpolate areaLights {};
	BillboardsInterpolate billboards {};
#endif
	ExplosionsInterpolate explosions {};
#if defined(BT_CLIENT)
	HexShieldsInterpolate hexShields {};
	PointLightsInterpolate pointLights {};
	PuffsInterpolate puffs {};
#endif
	PushersInterpolate pushers {};
#if defined(BT_CLIENT)
	SoundsInterpolate sounds {};
	SmokeTrailsInterpolate smokeTrails {};
	WindRadialsInterpolate windRadials {};
	WindTrailsInterpolate windTrails {};
#endif

	auto Collections(this auto&& rSelf)
	{
		return std::tie(
#if defined(BT_CLIENT)
			rSelf.areaLights, rSelf.billboards,
#endif
			rSelf.explosions,
#if defined(BT_CLIENT)
			rSelf.hexShields, rSelf.pointLights, rSelf.puffs,
#endif
			rSelf.pushers
#if defined(BT_CLIENT)
			, rSelf.sounds, rSelf.smokeTrails, rSelf.windRadials, rSelf.windTrails
#endif
		);
	}

#if defined(BT_CLIENT)
	static constexpr size_t kCollectionCount = 11;
#else
	static constexpr size_t kCollectionCount = 2;
#endif

	auto ServerCollections(this auto&& rSelf)
	{
		return std::tie(rSelf.explosions, rSelf.pushers);
	}

	// Visibility bounds (X = East/West, Y = North/South)
	static constexpr float kfVisibleEastWest = 65.0f;
	static constexpr float kfVisibleNorthSouth = 45.0f;

	[[nodiscard]] static bool XM_CALLCONV IsVisible(FXMVECTOR vecSource, FXMVECTOR vecTarget)
	{
		float fDeltaX = std::abs(XMVectorGetX(vecTarget) - XMVectorGetX(vecSource));
		float fDeltaY = std::abs(XMVectorGetY(vecTarget) - XMVectorGetY(vecSource));
		return fDeltaX <= kfVisibleEastWest && fDeltaY <= kfVisibleNorthSouth;
	}

	std::pair<common::crc_t, common::crc_t> Crcs() const;
	bool LogDifferences(const FrameInterpolateBase& rOther) const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
	void ServerRead(std::istream& rStream);
};

static_assert(std::tuple_size_v<decltype(std::declval<FrameInterpolateBase>().Collections())> == FrameInterpolateBase::kCollectionCount, "FrameInterpolateBase: Collections() tuple size does not match kCollectionCount. Did you add a new collection member without updating Collections()?");

struct FramePostRenderBase
{
	FramePostRenderBase();
	~FramePostRenderBase() = default;
	FramePostRenderBase(FramePostRenderBase&&) noexcept = default;
	FramePostRenderBase& operator=(FramePostRenderBase&&) noexcept = default;

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
#if defined(BT_CLIENT)
	uint64_t uiNextSoundUuid = 1;
	uint64_t uiNextVisualUuid = 1;
#endif
	uint16_t uiFrameId = 0;
	IslandsFlip eIslandsFlip = kFlipNone;

	common::crc_t previousCrc = 0;      // Crc() of the previous frame (chain link)
	common::crc_t previousInputCrc = 0;  // ServerInputCrc() of input used to produce this frame
	common::crc_t crc = 0;              // Crcs().first — full CRC of this frame
	common::crc_t sharedCrc = 0;        // Crcs().second — shared CRC excluding client-only and server-only fields

	Alignments alignments {};

	int64_t MakeUuid(uint64_t& ruiCounter)
	{
		int64_t iCounter = ruiCounter++;
		return (static_cast<int64_t>(uiFrameId) << 48) | (iCounter & 0x0000FFFFFFFFFFFF);
	}

	int64_t GenerateUuid()          { return MakeUuid(uiNextUuid); }
#if defined(BT_CLIENT)
	int64_t GenerateSoundUuid()     { return MakeUuid(uiNextSoundUuid); }
	int64_t GenerateVisualUuid()    { return MakeUuid(uiNextVisualUuid); }
#endif // BT_CLIENT

#if defined(BT_CLIENT)
	AreaLightsPostRender areaLights {};
	BillboardsPostRender billboards {};
#endif
	ExplosionsPostRender explosions {};
#if defined(BT_CLIENT)
	HexShieldsPostRender hexShields {};
	PointLightsPostRender pointLights {};
	PuffsPostRender puffs {};
#endif
	PushersPostRender pushers {};
#if defined(BT_CLIENT)
	SoundsPostRender sounds {};
	SmokeTrailsPostRender smokeTrails {};
	WindRadialsPostRender windRadials {};
	WindTrailsPostRender windTrails {};
#endif

	auto Collections(this auto&& rSelf)
	{
		return std::tie(
#if defined(BT_CLIENT)
			rSelf.areaLights, rSelf.billboards,
#endif
			rSelf.explosions,
#if defined(BT_CLIENT)
			rSelf.hexShields, rSelf.pointLights, rSelf.puffs,
#endif
			rSelf.pushers
#if defined(BT_CLIENT)
			, rSelf.sounds, rSelf.smokeTrails, rSelf.windRadials, rSelf.windTrails
#endif
		);
	}

#if defined(BT_CLIENT)
	static constexpr size_t kCollectionCount = 11;
#else
	static constexpr size_t kCollectionCount = 2;
#endif

	auto ServerCollections(this auto&& rSelf)
	{
		return std::tie(rSelf.explosions, rSelf.pushers);
	}

	std::pair<common::crc_t, common::crc_t> Crcs() const;
	bool LogDifferences(const FramePostRenderBase& rOther) const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
	void ServerRead(std::istream& rStream);
};

static_assert(std::tuple_size_v<decltype(std::declval<FramePostRenderBase>().Collections())> == FramePostRenderBase::kCollectionCount, "FramePostRenderBase: Collections() tuple size does not match kCollectionCount. Did you add a new collection member without updating Collections()?");

static_assert(std::tuple_size_v<decltype(std::declval<FrameInterpolateBase>().Collections())> == std::tuple_size_v<decltype(std::declval<FramePostRenderBase>().Collections())>, "FrameInterpolateBase and FramePostRenderBase must have the same number of collections");

// Type aliases derived from Collections() - must be after class definitions are complete
using InterpolateTypes = TupleToTypeList_t<decltype(std::declval<FrameInterpolateBase>().Collections())>;
using PostRenderBaseTypes = TupleToTypeList_t<decltype(std::declval<FramePostRenderBase>().Collections())>;

#if defined(BT_CLIENT)
// SmokeTrails and WindTrails are excluded from ForEachInterpolateRender because their Render() takes uiFrameId.
// They are called separately in RenderFrameMain() with the per-frame ID.
using InterpolateRenderTypes = TypeList<AreaLightsInterpolate, BillboardsInterpolate, ExplosionsInterpolate,
	HexShieldsInterpolate, PointLightsInterpolate, PuffsInterpolate, PushersInterpolate, SoundsInterpolate, WindRadialsInterpolate>;
#endif

// Inline definition - must be after FramePostRenderBase is complete
inline uuid_t uuid_t::Generate(FramePostRenderBase& rFramePostRender)
{
	return uuid_t {rFramePostRender.GenerateUuid()};
}

#if defined(BT_CLIENT)
inline uuid_t uuid_t::GenerateVisual(FramePostRenderBase& rFramePostRender)
{
	return uuid_t {rFramePostRender.GenerateVisualUuid()};
}
#endif

} // namespace engine
