#pragma once

#include "Frame/Alignments.h"
#ifdef BT_CLIENT
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Billboards.h"
#endif
#include "Frame/Collections/Explosions.h"
#ifdef BT_CLIENT
#include "Frame/Collections/HexShields.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#endif
#include "Frame/Collections/Pushers.h"
#ifdef BT_CLIENT
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/SmokeTrails.h"
#include "Frame/Collections/WindRadials.h"
#include "Frame/Collections/WindTrails.h"
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

inline int64_t giBackgroundThreadCount = 0;

struct FrameInterpolateBase
{
	FrameInterpolateBase();
	~FrameInterpolateBase() = default;
	FrameInterpolateBase(FrameInterpolateBase&&) noexcept = default;
	FrameInterpolateBase& operator=(FrameInterpolateBase&&) noexcept = default;

	// Called on Game creation
	static void Register();

#ifdef BT_CLIENT
	// Called during Graphics creation
	static void GraphicsResources();
#endif

	// Interpolate phase
	static void AllocateAndCopy(game::FrameInterpolate& __restrict rCurrent, const game::FrameInterpolate& __restrict rPrevious);
	static void Update(game::FrameInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

#ifdef BT_CLIENT
	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
#endif

	FrameFlags_t frameFlags {FrameFlags::kPostRender};
	int64_t iFrame = 0;
	float fCurrentTime = 0.0f;
	float fDeltaTime = 0.0f;

#ifdef BT_CLIENT
	AreaLightsInterpolate areaLights {};
	BillboardsInterpolate billboards {};
#endif
	ExplosionsInterpolate explosions {};
#ifdef BT_CLIENT
	HexShieldsInterpolate hexShields {};
	PointLightsInterpolate pointLights {};
	PuffsInterpolate puffs {};
#endif
	PushersInterpolate pushers {};
#ifdef BT_CLIENT
	SoundsInterpolate sounds {};
	SmokeTrailsInterpolate smokeTrails {};
	WindRadialsInterpolate windRadials {};
	WindTrailsInterpolate windTrails {};
#endif

	auto Collections(this auto&& rSelf)
	{
		return std::tie(
#ifdef BT_CLIENT
			rSelf.areaLights, rSelf.billboards,
#endif
			rSelf.explosions,
#ifdef BT_CLIENT
			rSelf.hexShields, rSelf.pointLights, rSelf.puffs,
#endif
			rSelf.pushers
#ifdef BT_CLIENT
			, rSelf.sounds, rSelf.smokeTrails, rSelf.windRadials, rSelf.windTrails
#endif
		);
	}

#ifdef BT_CLIENT
	static constexpr size_t kCollectionCount = 11;
#else
	static constexpr size_t kCollectionCount = 2;
#endif

	auto ServerCollections(this auto&& rSelf)
	{
		return std::tie(rSelf.explosions, rSelf.pushers);
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

	bool operator==(const FrameInterpolateBase& rOther) const;
	common::crc_t Crc() const;
	common::crc_t ServerCrc() const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
	void ServerRead(std::istream& rStream);
};

static_assert(std::tuple_size_v<decltype(std::declval<FrameInterpolateBase>().Collections())> == FrameInterpolateBase::kCollectionCount,
	"FrameInterpolateBase: Collections() tuple size does not match kCollectionCount. "
	"Did you add a new collection member without updating Collections()?");

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
#ifdef BT_CLIENT
	uint64_t uiNextSoundUuid = 1;
	uint64_t uiNextVisualUuid = 1;
#endif
	uint16_t uiFrameId = 0;
	IslandsFlip eIslandsFlip = kFlipNone;

	Alignments alignments {};

	int64_t GenerateUuid()
	{
		int64_t iCounter = uiNextUuid++;
		return (static_cast<int64_t>(uiFrameId) << 48) | (iCounter & 0x0000FFFFFFFFFFFF);
	}

#ifdef BT_CLIENT
	int64_t GenerateSoundUuid()
	{
		int64_t iCounter = uiNextSoundUuid++;
		return (static_cast<int64_t>(uiFrameId) << 48) | (iCounter & 0x0000FFFFFFFFFFFF);
	}

	int64_t GenerateVisualUuid()
	{
		int64_t iCounter = uiNextVisualUuid++;
		return (static_cast<int64_t>(uiFrameId) << 48) | (iCounter & 0x0000FFFFFFFFFFFF);
	}
#endif

#ifdef BT_CLIENT
	AreaLightsPostRender areaLights {};
	BillboardsPostRender billboards {};
#endif
	ExplosionsPostRender explosions {};
#ifdef BT_CLIENT
	HexShieldsPostRender hexShields {};
	PointLightsPostRender pointLights {};
	PuffsPostRender puffs {};
#endif
	PushersPostRender pushers {};
#ifdef BT_CLIENT
	SoundsPostRender sounds {};
	SmokeTrailsPostRender smokeTrails {};
	WindRadialsPostRender windRadials {};
	WindTrailsPostRender windTrails {};
#endif

	auto Collections(this auto&& rSelf)
	{
		return std::tie(
#ifdef BT_CLIENT
			rSelf.areaLights, rSelf.billboards,
#endif
			rSelf.explosions,
#ifdef BT_CLIENT
			rSelf.hexShields, rSelf.pointLights, rSelf.puffs,
#endif
			rSelf.pushers
#ifdef BT_CLIENT
			, rSelf.sounds, rSelf.smokeTrails, rSelf.windRadials, rSelf.windTrails
#endif
		);
	}

#ifdef BT_CLIENT
	static constexpr size_t kCollectionCount = 11;
#else
	static constexpr size_t kCollectionCount = 2;
#endif

	auto ServerCollections(this auto&& rSelf)
	{
		return std::tie(rSelf.explosions, rSelf.pushers);
	}

	bool operator==(const FramePostRenderBase& rOther) const;
	common::crc_t Crc() const;
	common::crc_t ServerCrc() const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
	void ServerRead(std::istream& rStream);
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

#ifdef BT_CLIENT
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

#ifdef BT_CLIENT
inline uuid_t uuid_t::GenerateVisual(FramePostRenderBase& rFramePostRender)
{
	return uuid_t {rFramePostRender.GenerateVisualUuid()};
}
#endif

} // namespace engine
