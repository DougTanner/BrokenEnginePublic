#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Pushers.h"
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

	// Particle config
	uint32_t uiBaseParticleCount = 0;
	int32_t iParticleCookie = 6;
	uint32_t uiParticleColor = 0xFF0000FF;

	// Particle physics
	float fParticlePositionJitter = 0.5f;
	float fParticleVelocityMin = 1.0f;
	float fParticleVelocityRandom = 10.0f;
	float fParticleVerticalVelocity = 20.0f;
	float fParticleVelocityDecay = 1.0f;
	float fParticleGravity = 30.0f;
	float fParticleWidth = 0.035f;
	float fParticleLength = 0.12f;
	float fParticleIntensityMin = 0.25f;
	float fParticleIntensityRandom = 3.0f;
	float fParticleIntensityDecay = 1.4f;
	float fParticleIntensityPower = 2.5f;
	float fParticleLightingSize = 10.0f;
	float fParticleLightingIntensity = 400.0f;

	// Timing
	float fPrimaryTime = 0.075f;
	float fPusherStartTime = 0.0f;
	float fPusherEndTime = 0.025f;

	// Pusher configuration
	float fPusherRadius = 6.0f;
	float fPusherIntensity = 20000.0f;
	float fPusherPower = 3.0f;

	// Trail configuration
	float fTrailDelayTime = 0.0f;
	float fTrailTimeMin = 0.2f;
	float fTrailTimeRandom = 0.2f;
	float fTrailIntensityMin = 0.025f;
	float fTrailIntensityRandom = 0.025f;
	float fTrailStart = 0.4f;
	float fTrailLengthMin = 0.5f;
	float fTrailLengthRandom = 3.0f;
	float fTrailGravity = 2.0f;

	// Secondary explosion configuration
	float fSecondaryPositionMin = 0.25f;
	float fSecondaryPositionJitter = 1.0f;

	bool operator==(const ExplosionType& rOther) const = default;
};

struct ExplosionsInterpolate : public Collection<ExplosionsInterpolate>
{
	// Types
	using Type = ExplosionType;
	static inline std::vector<Type> sTypes;

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rCurrentFrameInterpolate, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Member arrays (SOA)
	uint8_t* __restrict puiTypeIndices = nullptr;
	ExplosionFlags_t* __restrict pFlags = nullptr;
	float* __restrict pfStartTimes = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;

	// Per-instance scaling percentages
	float* __restrict pfLightPercents = nullptr;
	float* __restrict pfPusherPercents = nullptr;
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

	// Pusher handle
	pusher_t* __restrict pPushers = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(
		    rSelf.puiTypeIndices, rSelf.pFlags, rSelf.pfStartTimes,
		    rSelf.pVecPositions, rSelf.pVecDirections,
		    rSelf.pfLightPercents, rSelf.pfPusherPercents,
		    rSelf.pfSizePercents, rSelf.pfSmokePercents, rSelf.pfTimePercents,
		    rSelf.piTrailCounts, rSelf.pTrails, rSelf.pfTrailTimes,
		    rSelf.pfTrailIntensities, rSelf.pVecTrailStartPositions,
		    rSelf.pVecTrailEndPositions, rSelf.pPushers);
	}

	// Utility
	bool operator==(const ExplosionsInterpolate& rOther) const;
};

struct ExplosionsPostRender : public Collection<ExplosionsPostRender>
{
	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static uint8_t RegisterType(const ExplosionType& rType);
	static const ExplosionType& GetType(uint8_t uiIndex);

	// Register default explosion effect types (called from FrameBase::Register)
	static void Register();

	// Get registered controller type indices
	static uint8_t GetPrimaryLightControllerTypeIndex();
	static uint8_t GetSecondaryLightControllerTypeIndex();
	static uint8_t GetPrimaryPuffControllerTypeIndex();
	static uint8_t GetSecondaryPuffControllerTypeIndex();
	static uint8_t GetTrailTypeIndex();

	// Create an ExplosionType with default registered effect indices
	static ExplosionType CreateDefaultType();

	// Spawn new explosion
	static void XM_CALLCONV Spawn(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiTypeIndex,
	                               FXMVECTOR vecPosition, FXMVECTOR vecDirection, ExplosionFlags_t flags,
	                               uint32_t uiTrailCount = 0, float fTrailAngle = XM_2PI,
	                               uint32_t uiParticleCount = 0, float fParticleAngle = XM_2PI,
	                               float fLightPercent = 1.0f, float fPusherPercent = 1.0f,
	                               float fSizePercent = 1.0f, float fSmokePercent = 1.0f, float fTimePercent = 1.0f);

	// Destroy expired explosions
	static void Destroy(game::Frame& __restrict rFrame, float fCurrentTime);

	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool operator==(const ExplosionsPostRender& rOther) const;
};

} // namespace engine
