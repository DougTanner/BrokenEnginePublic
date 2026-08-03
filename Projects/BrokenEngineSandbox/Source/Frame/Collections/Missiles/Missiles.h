#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"

namespace engine { struct FrameStaticData; }
#include "Frame/GridCoord.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/AreaLights/AreaLights.h"
#endif
#if defined(BT_CLIENT)
#include "Frame/Collections/SmokeTrails/SmokeTrails.h"
#include "Frame/Collections/Sounds/Sounds.h"
#endif
#include "Frame/Collections/Targets/Targets.h"

namespace game
{

// Shared constants (used across Missiles*.cpp files)
inline constexpr float kfMissileDestroyTime = 0.35f;
inline constexpr float kfMissileDeltaRotationDelay = 0.5f;
// Exhaust length MUST stay constexpr: re-randomized every PostRender tick from the shared frame random
// engine into CRC'd pfExhaustLengths, so the draw must be deterministic across client/server.
inline constexpr float kfMissileExhaustLength = 1.25f;
inline constexpr float kfMissileExhaustLengthRandom = 1.0f;
// Spawn-time pitch range MUST stay constexpr: pfPitches[i] feeds the shared looping-voice fPitch, so the random
// draw must be deterministic across client/server. Runtime-tweakable launch-cue pitch lives in SoundWrappers.h.
inline constexpr float kfMissilePitchMin = 0.75f;
inline constexpr float kfMissilePitchRandom = 0.5f;
struct Frame;
struct FrameInterpolate;

enum class MissileFlags : uint8_t
{
	kTransfer  = 0x01,
	kExploding = 0x02,
	kDirectional = 0x04,
	kTargetPlayer = 0x08,
	kTargetEnemy  = 0x10,
	kFalling       = 0x20,
	kSilentDespawn = 0x40,
};
using MissileFlags_t = common::Flags<MissileFlags>;

struct MissilesInterpolate : public engine::Collection<MissilesInterpolate>
{
	static constexpr int64_t kiVersion = 2;
	static constexpr const char* kName = "Missiles";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Missiles");

	// Called on Game creation
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(MissilesInterpolate& rCurrent, const MissilesInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

#if defined(BT_CLIENT)
	static void ClientInit(Frame& rFrame, int64_t iIndex, engine::smoke_trails_t smokeTrailReuseId = {});
	static void ClientInitAll(Frame& rFrame);
#endif

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
#if defined(BT_CLIENT)
	engine::area_lights_t* __restrict puiAreaLights = nullptr;
	engine::smoke_trails_t* __restrict puiSmokeTrails = nullptr;
#endif
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.pfDestroyedTimes); }
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.puiAreaLights, rSelf.puiSmokeTrails); }
#endif
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
		return std::tie(rSelf.puiAreaLights, rSelf.puiSmokeTrails);
#else
		return std::tie();
#endif
	}

	// Utility
	bool LogDifferences(const MissilesInterpolate& rOther) const;

#if defined(BT_CLIENT)
	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
#endif // BT_CLIENT
};

struct MissilesPostRender : public engine::Collection<MissilesPostRender>
{
	static constexpr int64_t kiVersion = 9;

	// Allocate and copy
	static void AllocateAndCopy(MissilesPostRender& rCurrent, const MissilesPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void Transfer(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Destroy(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Explode(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData, int64_t i, bool bDirectional);
	static void Fall(Frame& __restrict rFrame, int64_t i, float fDeltaTime);

	MissileFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecExplosionDirections = nullptr;
	XMVECTOR* __restrict pVecStoredDirections = nullptr;
	target_t* __restrict puiTargets = nullptr;
	float* __restrict pfTimes = nullptr;
	float* __restrict pfDeltaRotationDelays = nullptr;
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfNextJitter = nullptr;
	float* __restrict pfDeltaRotationMax = nullptr;
	float* __restrict pfAccelerations = nullptr;
	float* __restrict pfPitches = nullptr;
	float* __restrict pfExhaustLengths = nullptr;
#if defined(BT_CLIENT)
	engine::sound_t* __restrict puiSounds = nullptr;
#endif
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecExplosionDirections, rSelf.pVecStoredDirections, rSelf.puiTargets, rSelf.pfTimes, rSelf.pfDeltaRotationDelays, rSelf.pfDeltaRotations, rSelf.pfNextJitter, rSelf.pfDeltaRotationMax, rSelf.pfAccelerations, rSelf.pfPitches, rSelf.pfExhaustLengths, rSelf.pAlignments); }
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.puiSounds); }
#endif
	auto Members(this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}
	auto PersistentMembers(this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tie(rSelf.pFlags, rSelf.pVecExplosionDirections, rSelf.pfDeltaRotationMax, rSelf.pfAccelerations, rSelf.pfPitches, rSelf.puiSounds, rSelf.pAlignments);
#else
		return std::tie(rSelf.pFlags, rSelf.pVecExplosionDirections, rSelf.pfDeltaRotationMax, rSelf.pfAccelerations, rSelf.pfPitches, rSelf.pAlignments);
#endif
	}

	// Note: MissilesPostRender doesn't own visual IDs directly - only sounds (already gated) and alignment

	// Utility
	bool LogDifferences(const MissilesPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition = DirectX::XMVectorZero();
		XMVECTOR vecDirection = DirectX::XMVectorZero();
		XMVECTOR vecVelocity = DirectX::XMVectorZero();
		XMVECTOR vecStoredDirection = DirectX::XMVectorZero();
		target_t uiTarget;
		float fAcceleration;
		MissileFlags_t flags;
		engine::alignment_t alignment {};
		float fDeltaRotationDelay = 0.0f;
		float fDeltaRotation = 0.0f;
		float fDeltaRotationMax = 0.0f;
		float fPitch = 0.0f;
		float fTime = 0.0f;
		float fNextJitter = 0.0f;
		// Arrival from a neighbouring cell: restore every carried value verbatim instead of defaulting.
		bool bTransfer = false;
#if defined(BT_CLIENT)
		engine::smoke_trails_t smokeTrailId {};
#endif
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::MissilesInterpolate>;
extern template struct Collection<game::MissilesPostRender>;
}
