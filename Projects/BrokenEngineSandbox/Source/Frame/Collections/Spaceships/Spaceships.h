#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"
#include "Frame/Collections/Pushers/Pushers.h"
#include "Frame/Collections/Targets/Targets.h"
#ifdef BT_CLIENT
#include "Frame/Collections/WindTrails/WindTrails.h"
#endif

namespace game
{

// Shared constants (used across Spaceships*.cpp files)
inline constexpr float kfSpaceshipDestroyTime = 0.25f;
inline constexpr float kfSpaceshipDestroyExplosionInterval = 0.024f;
inline constexpr float kfSpaceshipPusherRadius = 3.0f;
inline constexpr float kfSpaceshipPusherIntensity = 150.0f;
inline constexpr float kfSpaceshipPusherPower = 1.0f;
inline constexpr float kfSpaceshipExplosionIntensity = 1.5f;
inline constexpr float kfSpaceshipExplosionParticleCount = 8.0f;
inline constexpr float kfSpaceshipExplosionSizeStart = 1.25f;
inline constexpr float kfSpaceshipExplosionSizeEnd = 0.5f;
inline constexpr float kfSpaceshipExplosionSmoke = 0.5f;
inline constexpr float kfSpaceshipExplosionPositionJitter = 0.75f;
inline constexpr float kfSpaceshipExplosionDirectionJitter = 0.5f;
inline constexpr uint32_t kuiSpaceshipExplosionTrailCount = 5;
inline constexpr float kfSpaceshipDeathPitchMin = 0.75f;
inline constexpr float kfSpaceshipDeathPitchRandom = 0.5f;
inline constexpr float kfSpaceshipDeathExplosionVolume = 0.4f;
inline constexpr float kfSpaceshipHitSoundVolume = 0.2f;

struct SpaceshipsInterpolate : public engine::Collection<SpaceshipsInterpolate>
{
	static constexpr const char* kName = "Spaceships";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Spaceships");

	// Register
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(SpaceshipsInterpolate& rCurrent, const SpaceshipsInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

#ifdef BT_CLIENT
	static void ClientInit(Frame& rFrame, int64_t iIndex);
	static void ClientInitAll(Frame& rFrame);
#endif

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
	engine::pusher_t* __restrict puiPushers = nullptr;
	target_t* __restrict puiTargets = nullptr;
#ifdef BT_CLIENT
	engine::wind_trail_t* __restrict puiWindTrails = nullptr;
#endif
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfFreezeTimes = nullptr;
#ifdef BT_CLIENT
	float* __restrict pfAnimationTimes = nullptr;
#endif
	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.pfDestroyedTimes, rSelf.puiPushers, rSelf.puiTargets,
			rSelf.pfDeltaRotations, rSelf.pfFreezeTimes);
	}
#ifdef BT_CLIENT
	auto ClientMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pfAnimationTimes, rSelf.puiWindTrails);
	}
#endif
	auto Members(this auto&& rSelf)
	{
#ifdef BT_CLIENT
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}

	// Utility
	bool operator==(const SpaceshipsInterpolate& rOther) const;
	bool ServerCompare(const SpaceshipsInterpolate& rOther) const;

	// Graphics resources
	static void GraphicsResources();

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};

enum class SpaceshipFlags : uint8_t
{
	kFleePlayer           = 0x01,
	kExploding            = 0x02,
	kReturnToIslandCenter = 0x04,
	kTransfer             = 0x08,
};
using SpaceshipFlags_t = common::Flags<SpaceshipFlags>;

struct SpaceshipsPostRender : public engine::Collection<SpaceshipsPostRender>
{
	static constexpr int64_t kiVersion = 5;

	// Allocate and copy
	static void AllocateAndCopy(SpaceshipsPostRender& rCurrent, const SpaceshipsPostRender& rPrevious);

	// Post render phases
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	SpaceshipFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecDamageDirections = nullptr;
	float* __restrict pfHealths = nullptr;
	float* __restrict pfDestroyedExplosionTimes = nullptr;
	float* __restrict pfNextBlasterSpawnTimes = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecDamageDirections, rSelf.pfHealths, rSelf.pfDestroyedExplosionTimes, rSelf.pfNextBlasterSpawnTimes, rSelf.pAlignments); }

	// Utility
	bool operator==(const SpaceshipsPostRender& rOther) const;
	bool ServerCompare(const SpaceshipsPostRender& rOther) const;
	static void AvoidTerrain(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, int64_t iStart, int64_t iEnd);

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecDirection;
		XMVECTOR vecVelocity {};
		engine::alignment_t alignment {};
		float fHealth = 0.0f;
		float fNextBlasterSpawnTime = 0.0f;
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::SpaceshipsInterpolate>;
extern template struct Collection<game::SpaceshipsPostRender>;
}
