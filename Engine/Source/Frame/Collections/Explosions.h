#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Trails.h"

namespace game
{

struct Frame;

}

namespace engine
{

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
	float fParticleLightingSize = 10.0f;
	float fParticleLightingIntensity = 800.0f;

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

	bool operator==(const ExplosionType& rOther) const = default;
};

struct ExplosionsInterpolate : public Collection<ExplosionsInterpolate>,
                               public TypeRegistry<ExplosionType>
{
	// Register default explosion effect types (called from FrameInterpolateBase::Register)
	static void Register();

	// Graphics resources
	static void GraphicsResources() {}

	// Allocate and copy
	static void AllocateAndCopy(ExplosionsInterpolate& rCurrent, const ExplosionsInterpolate& rPrevious);

	// Get registered controller type indices
	static uint8_t GetPrimaryLightControllerTypeIndex();
	static uint8_t GetSecondaryLightControllerTypeIndex();
	static uint8_t GetPrimaryPuffControllerTypeIndex();
	static uint8_t GetSecondaryPuffControllerTypeIndex();
	static uint8_t GetTrailTypeIndex();
	static uint8_t GetWindRadialControllerTypeIndex();

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rCurrentFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Member arrays (SOA)
	uint8_t* __restrict puiTypeIndices = nullptr;
	ExplosionFlags_t* __restrict pFlags = nullptr;
	float* __restrict pfStartTimes = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;

	// Per-instance scaling percentages
	float* __restrict pfLightPercents = nullptr;
	float* __restrict pfSizePercents = nullptr;
	float* __restrict pfSmokePercents = nullptr;
	float* __restrict pfTimePercents = nullptr;

	// Trail state (8 separate arrays - pTrails[j] is array of all explosions' j-th trail)
	int32_t* __restrict piTrailCounts = nullptr;
	trails_t* __restrict pTrails[kiMaxExplosionTrails] = {};
	float* __restrict pfTrailTimes[kiMaxExplosionTrails] = {};
	float* __restrict pfTrailIntensities[kiMaxExplosionTrails] = {};
	XMVECTOR* __restrict pVecTrailStartPositions[kiMaxExplosionTrails] = {};
	XMVECTOR* __restrict pVecTrailEndPositions[kiMaxExplosionTrails] = {};

	auto Members(this auto&& rSelf)
	{
		return std::tie(
		    rSelf.puiTypeIndices, rSelf.pFlags, rSelf.pfStartTimes,
		    rSelf.pVecPositions, rSelf.pVecDirections,
		    rSelf.pfLightPercents,
		    rSelf.pfSizePercents, rSelf.pfSmokePercents, rSelf.pfTimePercents,
		    rSelf.piTrailCounts, rSelf.pTrails, rSelf.pfTrailTimes,
		    rSelf.pfTrailIntensities, rSelf.pVecTrailStartPositions,
		    rSelf.pVecTrailEndPositions);
	}

	// Utility
	bool operator==(const ExplosionsInterpolate& rOther) const;
};

struct ExplosionsPostRender : public Collection<ExplosionsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(ExplosionsPostRender& rCurrent, const ExplosionsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);

	// Destroy expired explosions
	static void Destroy(game::Frame& __restrict rFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool operator==(const ExplosionsPostRender& rOther) const;

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
		float fLightPercent = 1.0f;
		float fSizePercent = 1.0f;
		float fSmokePercent = 1.0f;
		float fTimePercent = 1.0f;
	};

	static void Spawn(game::Frame& __restrict rFrame, float fCurrentTime, const SpawnInfo& rInfo);
};

} // namespace engine
