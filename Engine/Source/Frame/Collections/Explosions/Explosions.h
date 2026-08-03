#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct FrameStaticData;
class Wrapper;

#if defined(BT_CLIENT)
struct SmokeTrailsInterpolate;
using smoke_trails_t = id_t<SmokeTrailsInterpolate>;
#endif

inline constexpr int64_t kiMaxExplosionTrails = 8;
inline constexpr uint8_t kuiInvalidTrailType = 255;

enum class ExplosionFlags : uint8_t
{
	kDestroysSelf = 0x01,
	kYellow       = 0x02,
	kRed          = 0x04,
};
using ExplosionFlags_t = common::Flags<ExplosionFlags>;

struct ExplosionType
{
	// Controller type indices for fire-and-forget effects
	uint8_t uiPrimaryLightControllerTypeIndex = kuiInvalidControllerType;
	uint8_t uiSecondaryLightControllerTypeIndex = kuiInvalidControllerType;
	uint8_t uiPrimaryPuffControllerTypeIndex = kuiInvalidControllerType;
	uint8_t uiSecondaryPuffControllerTypeIndex = kuiInvalidControllerType;
	uint8_t uiTrailTypeIndex = kuiInvalidTrailType;
	uint8_t uiWindRadialControllerTypeIndex = kuiInvalidControllerType;

	// Particle config
	uint32_t uiBaseParticleCount = 0;
	common::crc_t particleCrc = common::CrcConsteval("Textures\\Particles\\[BC4]Long\\5.png");
	uint32_t uiParticleColor = 0xFF0000FF;

	// Particle physics
	float fParticlePositionJitter = 0.5f;
	float fParticleVelocityMin = 1.0f;
	float fParticleVelocityRandom = 10.0f;
	float fParticleVerticalVelocityMin = 0.0f;
	float fParticleVerticalVelocityRandom = 20.0f;
	float fParticleVelocityDecay = 1.0f;
	float fParticleGravity = 30.0f;
	float fParticleWidth = 0.035f;
	float fParticleLength = 0.1f;
	float fParticleIntensityMin = 0.25f;
	float fParticleIntensityRandom = 2.0f;
	float fParticleIntensityDecay = 2.4f;
	float fParticleIntensityPower = 2.5f;

	// Timing
	float fPrimaryTime = 0.075f;

	// Trail configuration
	float fTrailDelayTime = 0.0f;
	float fTrailTimeMin = 0.2f;
	float fTrailTimeRandom = 0.2f;
	float fTrailIntensityMin = 0.025f;
	float fTrailIntensityRandom = 0.025f;
	float fTrailStart = 0.6f;
	float fTrailLengthMin = 0.75f;
	float fTrailLengthRandom = 4.5f;
	float fTrailGravity = 2.0f;

	// Secondary explosion count (lights and puffs)
	uint32_t uiSecondaryExplosionCount = 4;

	// Secondary explosion configuration
	float fSecondaryPositionMin = 0.25f;
	float fSecondaryPositionJitter = 1.0f;

	// Per-type runtime tweak multipliers (Particles tab). Null on server, optional on client.
	Wrapper* pParticleWidthScale = nullptr;
	Wrapper* pParticleLengthScale = nullptr;
	Wrapper* pParticleLengthSpreadScale = nullptr;
	Wrapper* pParticlePositionJitterScale = nullptr;
	Wrapper* pParticleVelocityBaseScale = nullptr;
	Wrapper* pParticleVelocitySpreadScale = nullptr;
	Wrapper* pParticleVerticalVelocityBaseScale = nullptr;
	Wrapper* pParticleVerticalVelocitySpreadScale = nullptr;
	Wrapper* pParticleVelocityDecayScale = nullptr;
	Wrapper* pParticleGravityScale = nullptr;
	Wrapper* pParticleVisibleIntensityScale = nullptr;
	Wrapper* pParticleIntensitySpreadScale = nullptr;
	Wrapper* pParticleIntensityDecayScale = nullptr;
	Wrapper* pParticleIntensityPowerScale = nullptr;

	bool operator==(const ExplosionType& rOther) const = default;
};

struct ExplosionsInterpolate : public Collection<ExplosionsInterpolate>,
                               public TypeRegistry<ExplosionType>
{
	static constexpr int64_t kiVersion = 1;

	// Register default explosion effect types (called from FrameInterpolateBase::Register)
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(ExplosionsInterpolate& rCurrent, const ExplosionsInterpolate& rPrevious);

	// Normalize fixed trail-slot counts after a complete deserialization.
	static void PostRead(ExplosionsInterpolate& rCurrent)
	{
		for (int64_t i = 0; i < rCurrent.iCount; ++i)
		{
			const int32_t iOriginalCount = rCurrent.piTrailCounts[i];
			const int32_t iClampedCount = std::clamp(iOriginalCount, static_cast<int32_t>(0), static_cast<int32_t>(kiMaxExplosionTrails));
			if (iOriginalCount != iClampedCount)
			{
				rCurrent.piTrailCounts[i] = iClampedCount;
				LOG(kDefault, kWarning, "ExplosionsInterpolate piTrailCounts row {} clamped from {} to {}", i, iOriginalCount, iClampedCount);
			}
		}
	}

#if defined(BT_CLIENT)
	// Get registered controller type indices
	static uint8_t GetPrimaryLightControllerTypeIndex();
	static uint8_t GetSecondaryLightControllerTypeIndex();
	static uint8_t GetPrimaryPuffControllerTypeIndex();
	static uint8_t GetSecondaryPuffControllerTypeIndex();
	static uint8_t GetTrailTypeIndex();
	static uint8_t GetWindRadialControllerTypeIndex();
#endif // BT_CLIENT

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rCurrentFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

#if defined(BT_CLIENT)
	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords);
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
#endif

	// Member arrays (SOA)
	uint8_t* __restrict puiTypeIndices = nullptr;
	ExplosionFlags_t* __restrict pFlags = nullptr;
	float* __restrict pfStartTimes = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;

	// Per-instance time-scaling percentage
	float* __restrict pfTimePercents = nullptr;

	// Trail state (8 separate arrays - pTrails[j] is array of all explosions' j-th trail)
	int32_t* __restrict piTrailCounts = nullptr;
#if defined(BT_CLIENT)
	smoke_trails_t* pTrails[kiMaxExplosionTrails] = {};
#endif
	float* pfTrailTimes[kiMaxExplosionTrails] = {};
#if defined(BT_CLIENT)
	float* pfTrailIntensities[kiMaxExplosionTrails] = {};
	XMVECTOR* pVecTrailStartPositions[kiMaxExplosionTrails] = {};
	XMVECTOR* pVecTrailEndPositions[kiMaxExplosionTrails] = {};
#endif

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(
		    rSelf.puiTypeIndices, rSelf.pFlags, rSelf.pfStartTimes,
		    rSelf.pVecPositions, rSelf.pVecDirections,
		    rSelf.pfTimePercents,
		    rSelf.piTrailCounts, rSelf.pfTrailTimes);
	}
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf)
	{
		return std::tie(
		    rSelf.pTrails,
		    rSelf.pfTrailIntensities, rSelf.pVecTrailStartPositions,
		    rSelf.pVecTrailEndPositions);
	}
#endif // BT_CLIENT
	auto Members(this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}
	auto PersistentMembers([[maybe_unused]] this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tie(rSelf.pTrails);
#else
		return std::tie();
#endif
	}

	// Utility
	bool LogDifferences(const ExplosionsInterpolate& rOther) const;
};

struct ExplosionsPostRender : public Collection<ExplosionsPostRender>
{
	// Bump on any SOA layout change — feeds the Frame::kiVersion save/replay gate
	static constexpr int64_t kiVersion = 1;

	// Allocate and copy
	static void AllocateAndCopy(ExplosionsPostRender& rCurrent, const ExplosionsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Destroy expired explosions
	static void Destroy(game::Frame& __restrict rFrame, const FrameStaticData& rStaticData);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool LogDifferences(const ExplosionsPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		uint8_t uiTypeIndex;
		XMVECTOR vecPosition;
		XMVECTOR vecDirection;
		ExplosionFlags_t flags {};
		uint32_t uiTrailCount = 0;
		float fTrailAngle = XM_2PI;
		uint32_t uiParticleCount = 0;
		float fParticleAngle = XM_2PI;
		float fSizePercent = 1.0f;
		float fSmokePercent = 1.0f;
		float fTimePercent = 1.0f;
	};

	static void Spawn(game::Frame& __restrict rFrame, float fCurrentTime, const SpawnInfo& rInfo);
};

extern template struct Collection<ExplosionsInterpolate>;
extern template struct Collection<ExplosionsPostRender>;

} // namespace engine
