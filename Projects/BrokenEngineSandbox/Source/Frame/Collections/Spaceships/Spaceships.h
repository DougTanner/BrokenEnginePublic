#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"

namespace engine { struct FrameStaticData; }
#include "Frame/GridCoord.h"
#include "Frame/Collections/Pushers/Pushers.h"
#include "Frame/Collections/Targets/Targets.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/WindTrails/WindTrails.h"
#endif

namespace game
{

// Shared constants (used across Spaceships*.cpp files)
inline constexpr float kfSpaceshipDestroyTime = 0.25f;
inline constexpr float kfSpaceshipDestroyExplosionInterval = 0.024f;

// Collision
inline constexpr float kfSpaceshipRadius = 1.5f;

// Movement — three independent tuning axes
inline constexpr float kfSpaceshipAcceleration = 10.0f;
inline constexpr float kfSpaceshipDrag = 0.25f;
inline constexpr float kfSpaceshipMaxSpeed = 40.0f;
inline constexpr float kfSpaceshipMaxTurnRate = 4.0f;

// Pusher
inline constexpr float kfSpaceshipPusherRadius = kfSpaceshipRadius * 1.5f;
inline constexpr float kfSpaceshipPusherIntensity = 36.0f;
inline constexpr float kfSpaceshipPusherPower = 3.0f;
inline constexpr float kfSpaceshipMaxPusherPushVelocity = kfSpaceshipMaxSpeed * 0.5f;

struct SpaceshipsInterpolate : public engine::Collection<SpaceshipsInterpolate>
{
	static constexpr int64_t kiVersion = 2;
	static constexpr const char* kName = "Spaceships";
	static constexpr common::crc_t kCrc = common::CrcConsteval("Spaceships");

	// Register
	static void Register();

	// Allocate and copy
	static void AllocateAndCopy(SpaceshipsInterpolate& rCurrent, const SpaceshipsInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

#if defined(BT_CLIENT)
	static void ClientInit(Frame& rFrame, int64_t iIndex);
	static void ClientInitAll(Frame& rFrame);
#endif

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
	engine::pusher_t* __restrict puiPushers = nullptr;
	target_t* __restrict puiTargets = nullptr;
#if defined(BT_CLIENT)
	engine::wind_trail_t* __restrict puiWindTrails = nullptr;
#endif
	float* __restrict pfDeltaRotations = nullptr;
#if defined(BT_CLIENT)
	float* __restrict pfAnimationTimes = nullptr;
#endif
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.pfDestroyedTimes, rSelf.puiPushers, rSelf.puiTargets, rSelf.pfDeltaRotations); }
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pfAnimationTimes, rSelf.puiWindTrails);
	}
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
		return std::tie(rSelf.puiPushers, rSelf.puiTargets, rSelf.puiWindTrails);
#else
		return std::tie(rSelf.puiPushers, rSelf.puiTargets);
#endif
	}

	// Utility
	bool LogDifferences(const SpaceshipsInterpolate& rOther) const;

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
	static constexpr int64_t kiVersion = 6;

	// Allocate and copy
	static void AllocateAndCopy(SpaceshipsPostRender& rCurrent, const SpaceshipsPostRender& rPrevious);

	// Post render phases
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void Transfer(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Destroy(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Spawn(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);

private:
	// Per-spaceship Update helpers (called from SpaceshipsPostRender::Update orchestrator)
	// Defined in SpaceshipsNavigation.cpp:
	static void XM_CALLCONV ComputeSteering(std::span<const XMFLOAT4> islandCandidates, FXMVECTOR vecPosition, FXMVECTOR vecDirection, bool bPlayerAlive, FXMVECTOR vecNearestPlayer, float fDeltaTime, SpaceshipFlags_t& rFlags, float& rfDeltaRotation);
	static void XM_CALLCONV ApplyMovement(Frame& __restrict rFrame, const SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, SpaceshipFlags_t flags, float fDeltaTime, XMVECTOR& rVecVelocity);
	static void ApplyTerrainBounce(const engine::FrameStaticData& rStaticData, SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, float fDeltaTime, float& rfDeltaRotation, XMVECTOR& rVecVelocity);

	// Defined in SpaceshipsCombat.cpp:
	static void XM_CALLCONV RegenerateHealth(FXMVECTOR vecPosition, bool bPlayerAlive, FXMVECTOR vecNearestPlayer, SpaceshipFlags_t flags, float fDeltaTime, float& rfHealth);
	static void XM_CALLCONV ApplyDeathKnockback(FXMVECTOR vecDamageDirection, XMVECTOR& rVecVelocity);

public:
	SpaceshipFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecDamageDirections = nullptr;
	float* __restrict pfHealths = nullptr;
	float* __restrict pfDestroyedExplosionTimes = nullptr;
	float* __restrict pfNextBlasterSpawnTimes = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	float* __restrict pfArrivalGracePeriods = nullptr;
	// No client-only fields today. Defined via SharedMembers() (== full set; Members() forwards) so CRC/server-read walk the explicit
	// shared subset — a future #if BT_CLIENT field then belongs in a ClientMembers() split (Collections hub rule) and stays out of the
	// CRC, instead of silently entering it via a Members()-only walk; the server-build kbServerMembersParity static_assert backstops Members()==SharedMembers().
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecDamageDirections, rSelf.pfHealths, rSelf.pfDestroyedExplosionTimes, rSelf.pfNextBlasterSpawnTimes, rSelf.pAlignments, rSelf.pfArrivalGracePeriods); }
	auto Members(this auto&& rSelf) { return rSelf.SharedMembers(); }
	auto PersistentMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecDamageDirections, rSelf.pAlignments);
	}

	// Utility
	bool LogDifferences(const SpaceshipsPostRender& rOther) const;
	static void AvoidTerrain(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData, int64_t iStart, int64_t iEnd);

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition = DirectX::XMVectorZero();
		XMVECTOR vecDirection = DirectX::XMVectorZero();
		XMVECTOR vecVelocity = DirectX::XMVectorZero();
		engine::alignment_t alignment {};
		float fHealth = 0.0f;
		float fNextBlasterSpawnTime = 0.0f;
		float fArrivalGracePeriod = 0.0f;
		float fDeltaRotation = 0.0f;
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::SpaceshipsInterpolate>;
extern template struct Collection<game::SpaceshipsPostRender>;
}
