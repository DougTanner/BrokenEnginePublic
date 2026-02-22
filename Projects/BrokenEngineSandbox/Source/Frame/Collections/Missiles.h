#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/Targets.h"
#include "Frame/Collections/SmokeTrails.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

enum class MissileFlags : uint8_t
{
	kTransfer  = 0x01,
	kExploding = 0x02,
	kDirectional = 0x04,
	kTargetPlayer = 0x08,
	kTargetEnemy  = 0x10,
};
using MissileFlags_t = common::Flags<MissileFlags>;

struct MissilesInterpolate : public engine::Collection<MissilesInterpolate>
{
	static constexpr const char* kName = "Missiles";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Missiles");

	// Called on Game creation
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(MissilesInterpolate& rCurrent, const MissilesInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	engine::area_lights_t* __restrict puiAreaLights = nullptr;
	engine::pusher_t* __restrict puiPushers = nullptr;
	engine::smoke_trails_t* __restrict puiSmokeTrails = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.puiAreaLights, rSelf.puiPushers, rSelf.puiSmokeTrails, rSelf.pfDestroyedTimes); }

	// Utility
	bool operator==(const MissilesInterpolate& rOther) const;

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};

struct MissilesPostRender : public engine::Collection<MissilesPostRender>
{
	static constexpr int64_t kiVersion = 4;

	// Allocate and copy
	static void AllocateAndCopy(MissilesPostRender& rCurrent, const MissilesPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);
	static void Explode(Frame& __restrict rFrame, int64_t i, bool bDirectional);

	MissileFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecExplosionDirections = nullptr;
	XMVECTOR* __restrict pVecStoredDirections = nullptr;
	target_t* __restrict puiTargets = nullptr;
	float* __restrict pfExplosionRadii = nullptr;
	float* __restrict pfTimes = nullptr;
	float* __restrict pfDeltaRotationDelays = nullptr;
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfExaustDelays = nullptr;
	float* __restrict pfNextJitter = nullptr;
	float* __restrict pfDeltaRotationMax = nullptr;
	float* __restrict pfAccelerations = nullptr;
	float* __restrict pfPitches = nullptr;
	float* __restrict pfExhaustLengths = nullptr;
	engine::sound_t* __restrict puiSounds = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecExplosionDirections, rSelf.pVecStoredDirections, rSelf.puiTargets, rSelf.pfExplosionRadii, rSelf.pfTimes, rSelf.pfDeltaRotationDelays, rSelf.pfDeltaRotations, rSelf.pfExaustDelays, rSelf.pfNextJitter, rSelf.pfDeltaRotationMax, rSelf.pfAccelerations, rSelf.pfPitches, rSelf.pfExhaustLengths, rSelf.puiSounds, rSelf.pAlignments); }

	// Utility
	bool operator==(const MissilesPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecDirection;
		XMVECTOR vecVelocity;
		XMVECTOR vecStoredDirection;
		target_t uiTarget;
		float fAcceleration;
		MissileFlags_t flags;
		engine::alignment_t alignment {};
		float fDeltaRotationDelay = 0.0f;
		float fTime = 0.0f;
		float fExhaustDelay = 0.0f;
		float fNextJitter = 0.0f;
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game
