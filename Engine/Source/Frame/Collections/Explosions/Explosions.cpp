#include "Explosions.h"

#if defined(BT_CLIENT)
#include "Data/Texture.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#include "Frame/Collections/SmokeTrails/SmokeTrails.h"
#include "Frame/Collections/WindRadials/WindRadials.h"
#endif // BT_CLIENT

namespace engine
{

template struct Collection<ExplosionsInterpolate>;
template struct Collection<ExplosionsPostRender>;

using enum ExplosionFlags;

#if defined(BT_CLIENT)

// Wind
constexpr float kfWindDepositDuration = 0.3f;

// Explosion timing
constexpr float kfPrimaryTime = 0.06f;

// Primary light
constexpr float kfPrimaryVisibleSize = 1.0f;
constexpr float kfPrimaryVisibleIntensity = 0.6f;
constexpr float kfPrimaryLightingSize = 2.0f;
constexpr float kfPrimaryLightingIntensity = 1250.0f;

// Secondary light
constexpr float kfSecondaryVisibleSize = 0.75f;
constexpr float kfSecondaryVisibleIntensity = kfPrimaryVisibleIntensity;
constexpr float kfSecondaryLightingSize = 0.75f * kfPrimaryLightingSize;
constexpr float kfSecondaryLightingIntensity = 0.75f * kfPrimaryLightingIntensity;

// Primary puff
constexpr float kfPrimaryPuffSize = 1.75f;
constexpr float kfPrimaryPuffStartTime = 0.0f;
constexpr float kfPrimaryPuffEndTime = 0.2f;
constexpr float kfPrimaryPuffIntensity = 0.5f / (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);

// Secondary puff
constexpr float kfSecondaryPuffTimes = 0.4f * (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);
constexpr float kfSecondaryPuffIntensity = 0.2f / kfSecondaryPuffTimes;

// Explosion trail
constexpr float kfExplosionTrailWidth = 1.0f;

// Static indices for registered explosion effect types
static uint8_t suiExplosionPointLightTypeIndex = kuiInvalidControllerType;
static uint8_t suiPrimaryLightControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiSecondaryLightControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiExplosionPuffTypeIndex = kuiInvalidControllerType;
static uint8_t suiPrimaryPuffControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiSecondaryPuffControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiExplosionTrailTypeIndex = kuiInvalidTrailType;
static uint8_t suiWindRadialControllerTypeIndex = kuiInvalidControllerType;

// Helper to sync an explosion trail
void XM_CALLCONV SyncExplosionTrail(game::FrameInterpolate& rFrameInterpolate, smoke_trails_t trailId, FXMVECTOR vecPosition, float fIntensity)
{
	if (!trailId.IsValid())
	{
		return;
	}

	SmokeTrailsInterpolate::Sync(rFrameInterpolate, trailId,
	{
		.vecPosition = vecPosition,
		.fIntensity = fIntensity,
	});
}

#endif // BT_CLIENT

void ExplosionsInterpolate::AllocateAndCopy(ExplosionsInterpolate& rCurrent, const ExplosionsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

#if defined(BT_CLIENT)
	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
		{
			std::memcpy(rCurrent.pTrails[j], rPrevious.pTrails[j], rCurrent.iCount * sizeof(rCurrent.pTrails[j][0]));
		}
	}
#endif // BT_CLIENT
}

void ExplosionsPostRender::AllocateAndCopy(ExplosionsPostRender& rCurrent, const ExplosionsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void ExplosionsInterpolate::Register()
{
#if defined(BT_CLIENT)
	// Guard against double registration
	if (suiExplosionPointLightTypeIndex != kuiInvalidControllerType)
	{
		return;
	}

	// Register PointLights::Type for explosions
	PointLightsInterpolate::RegisterType(suiExplosionPointLightTypeIndex,
	{
		.crc = data::kTexturesBC7ExplosionpngCrc,
		.uiColor = 0xFFFFFFFF,
		.fVisibleArea = kfPrimaryVisibleSize,
		.fVisibleIntensity = kfPrimaryVisibleIntensity,
		.fLightingArea = kfPrimaryLightingSize,
		.fLightingIntensity = kfPrimaryLightingIntensity,
	});

	// Register primary light controller type (3-keyframe: start -> peak -> fade)
	PointLightsInterpolate::RegisterControllerType(suiPrimaryLightControllerTypeIndex,
	{
		.uiBaseTypeIndex = suiExplosionPointLightTypeIndex,
		.uiKeyframeCount = 3,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, 0.4f * kfPrimaryTime, 3.0f * kfPrimaryTime, 0.0f},
		.keyframes =
		{
			{.fVisibleArea = 0.3f * kfPrimaryVisibleSize, .fVisibleIntensity = 0.25f * kfPrimaryVisibleIntensity, .fLightingArea = 0.6f * kfPrimaryLightingSize, .fLightingIntensity = 0.25f * kfPrimaryLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.6f * kfPrimaryVisibleSize, .fVisibleIntensity = kfPrimaryVisibleIntensity, .fLightingArea = 1.5f * kfPrimaryLightingSize, .fLightingIntensity = kfPrimaryLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.6f * kfPrimaryVisibleSize, .fVisibleIntensity = 0.0f, .fLightingArea = 1.2f * kfPrimaryLightingSize, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
			{},
		},
	});

	// Register secondary light controller type (3-keyframe: delayed start -> peak -> fade)
	PointLightsInterpolate::RegisterControllerType(suiSecondaryLightControllerTypeIndex,
	{
		.uiBaseTypeIndex = suiExplosionPointLightTypeIndex,
		.uiKeyframeCount = 3,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, 1.0f * kfPrimaryTime, 3.0f * kfPrimaryTime, 0.0f},
		.keyframes =
		{
			{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
			{.fVisibleArea = kfSecondaryVisibleSize, .fVisibleIntensity = kfSecondaryVisibleIntensity, .fLightingArea = 2.0f * kfSecondaryLightingSize, .fLightingIntensity = kfSecondaryLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
			{},
		},
	});

	// Register Puffs::Type for explosions
	PuffsInterpolate::RegisterType(suiExplosionPuffTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
	});

	// Register primary puff controller type (2-keyframe: start -> expand)
	PuffsInterpolate::RegisterControllerType(suiPrimaryPuffControllerTypeIndex,
	{
		.uiBaseTypeIndex = suiExplosionPuffTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {kfPrimaryPuffStartTime, kfPrimaryPuffEndTime, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = 0.1f * kfPrimaryPuffSize, .fIntensity = kfPrimaryPuffIntensity, .fRotation = 0.0f},
			{.fArea = 0.6f * kfPrimaryPuffSize, .fIntensity = kfPrimaryPuffIntensity, .fRotation = 0.0f},
			{},
			{},
		},
	});

	// Register secondary puff controller type (2-keyframe: smaller, shorter)
	PuffsInterpolate::RegisterControllerType(suiSecondaryPuffControllerTypeIndex,
	{
		.uiBaseTypeIndex = suiExplosionPuffTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfSecondaryPuffTimes, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = 0.1f * kfPrimaryPuffSize, .fIntensity = kfSecondaryPuffIntensity, .fRotation = 0.0f},
			{.fArea = 0.4f * kfPrimaryPuffSize, .fIntensity = kfSecondaryPuffIntensity, .fRotation = 0.0f},
			{},
			{},
		},
	});

	// Register SmokeTrails::Type for explosion trails
	SmokeTrailsInterpolate::RegisterType(suiExplosionTrailTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
		.fWidth = kfExplosionTrailWidth,
	});

	// Register wind radial controller type (2-keyframe: full intensity -> zero)
	WindRadialsInterpolate::RegisterControllerType(suiWindRadialControllerTypeIndex,
	{
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfWindDepositDuration, 0.0f, 0.0f},
		.keyframes = {{.fIntensity = 1.0f, .fSize = 1.0f}, {.fIntensity = 0.0f, .fSize = 1.0f}, {}, {}},
	});
#endif // BT_CLIENT
}

#if defined(BT_CLIENT)

uint8_t ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex()
{
	return suiPrimaryLightControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex()
{
	return suiSecondaryLightControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex()
{
	return suiPrimaryPuffControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex()
{
	return suiSecondaryPuffControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetTrailTypeIndex()
{
	return suiExplosionTrailTypeIndex;
}

uint8_t ExplosionsInterpolate::GetWindRadialControllerTypeIndex()
{
	return suiWindRadialControllerTypeIndex;
}

#endif // BT_CLIENT

void ExplosionsPostRender::Destroy(game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ExplosionsInterpolate& rInterpolate = rFrame.interpolate.explosions;
	ExplosionsPostRender& rPostRender = rFrame.postRender.explosions;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiTypeIndex = rInterpolate.puiTypeIndices[i];
		const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(uiTypeIndex);

		float fStartTime = rInterpolate.pfStartTimes[i];
		float fExplosionTime = fCurrentTime - fStartTime;
		float fTimePercent = rInterpolate.pfTimePercents[i];

		int32_t iTrailCount = rInterpolate.piTrailCounts[i];

#if defined(BT_CLIENT)
		// Remove expired trails (cleanup happens every frame, not just at explosion expiration)
		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			smoke_trails_t& trailId = rInterpolate.pTrails[j][i];
			if (!trailId.IsValid())
			{
				continue;
			}

			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rInterpolate.pfTrailTimes[j][i];
			if (fExplosionTime >= fTrailEndTime)
			{
				SmokeTrailsPostRender::Remove(rFrame, trailId);
			}
		}
#endif // BT_CLIENT

		ExplosionFlags_t flags = rInterpolate.pFlags[i];

		// Skip non-self-destroying explosions for full removal
		if (!(flags & kDestroysSelf))
		{
			continue;
		}

		// Calculate end time (longest of trail durations)
		float fEndTime = 0.0f;
		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rInterpolate.pfTrailTimes[j][i];
			fEndTime = std::max(fEndTime, fTrailEndTime);
		}

		// Check if explosion has expired
		if (fExplosionTime < fEndTime)
		{
			continue;
		}

		// Remove the explosion using swap-and-pop
		DestroyElement(rInterpolate, rPostRender, i, rInterpolate.Members(), rPostRender.Members());
	}
}

void ExplosionsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void ExplosionsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

bool ExplosionsInterpolate::LogDifferences(const ExplosionsInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("ExplosionsInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference<"puiTypeIndices">(i, puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference<"pfStartTimes">(i, pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference_Vec("pVecDirections", i, pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::LogDifference<"pfSizePercents">(i, pfSizePercents[i], rOther.pfSizePercents[i]);
		bEqual &= common::LogDifference<"pfTimePercents">(i, pfTimePercents[i], rOther.pfTimePercents[i]);
		bEqual &= common::LogDifference<"piTrailCounts">(i, piTrailCounts[i], rOther.piTrailCounts[i]);

		for (int64_t j = 0; j < piTrailCounts[i]; ++j)
		{
			bEqual &= common::LogDifference<"pfTrailTimes">(i, pfTrailTimes[j][i], rOther.pfTrailTimes[j][i]);
		}
	}

	return bEqual;
}

bool ExplosionsPostRender::LogDifferences(const ExplosionsPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("ExplosionsPostRender");
	return Collection::LogDifferences(rOther);
}

#if defined(BT_CLIENT)

static int64_t siTotalCount = 0;

void ExplosionsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	siTotalCount = 0;
}

void ExplosionsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	siTotalCount += rFrameInterpolate.explosions.iCount;
}

void ExplosionsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterExplosions, siTotalCount);
}

#endif // BT_CLIENT

} // namespace engine
