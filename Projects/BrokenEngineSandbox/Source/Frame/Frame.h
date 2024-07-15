#pragma once

#include "Frame/HealthDamage.h"

#include "Frame/FrameBase.h"

#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/Spaceships.h"
#include "Frame/Camera.h"
#include "Frame/Player.h"

namespace engine
{

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
struct DifferenceStreamHeader;

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamReader;

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamWriter;

}

namespace game
{

struct FrameInput;
struct FrameInputHeld;

enum class FrameFlags : uint64_t
{
	kMainMenu = 0x00000001,
	kGame     = 0x00000002,

	kFirstSpawn       = 0x00000004,
	kDeathScreen      = 0x00000010,

	kDashMouseCursor         = 0x00000400,
	kDashMouseAcceleration   = 0x00000800,
	kDashGamepadFiring       = 0x00001000,
	kDashGamepadAcceleration = 0x00002000,

	kPrimaryHold   = 0x00004000,
	kPrimaryToggle = 0x00008000,
};
using FrameFlags_t = common::Flags<FrameFlags>;

inline constexpr float kfAutoDestroyDistance = 80.0f;
inline constexpr float kfPickupSize = 0.0175f;

inline constexpr float kfEnemyFireAreaXAdjust = -2.0f;
inline constexpr float kfEnemyFireVisibleAreaYAdjustTop = -10.0f;
inline constexpr float kfEnemyFireVisibleAreaYAdjustBottom = -5.0f;

inline constexpr float kfToMissileCollisionRadius = 1.5f;

struct alignas(64) Frame : public engine::FrameBase
{
	static constexpr int64_t kiVersion = FrameBase::kiVersion + 19 + kiCameraVersion + kiBlastersVersion + kiMissilesVersion + kiPlayerVersion + kiSpaceshipsVersion;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}}; // NOTE: If this is ever changed, be careful with f4VertexRect and flip
	static constexpr DirectX::XMVECTOR kVecEnemySpawnPosition {10.0f, 30.0f, 0.0f, 1.0f};

	// Global
	FrameFlags_t flags {FrameFlags::kFirstSpawn};
	float fEndTime = 0.0f;

	static constexpr float kfWaveDisplayTime = 2.0f;
	float fWaveDisplayTimeLeft = 0.0f;

	bool bNextWave = false;
	int64_t iWave = 1;
	int64_t iLastSpawn = 0;
	int64_t iClumpsLeft = 0;
	int64_t iClumpSize = 0;
	int64_t iNextClumpSpawn = 0;
	float fNextClumpSpawnTime = 0;

	alignas(64) Camera camera {};
	alignas(64) Player player {};

	alignas(64) Blasters blasters {};
	alignas(64) Missiles missiles {};
	alignas(64) Spaceships spaceships {};
	
	static DirectX::FXMVECTOR XM_CALLCONV EnemySpawnPosition(Frame& __restrict rFrame);
	static std::optional<DirectX::FXMVECTOR> XM_CALLCONV ClosestEnemy(Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition);
	static [[nodiscard]] engine::target_t XM_CALLCONV GetMissileTarget(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecDirection, engine::TargetFlags_t targetFlags);
	static void XM_CALLCONV AreaDamage(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition, float fDamage, float fRadius);
	static void XM_CALLCONV BlasterImpact(Frame& __restrict rFrame, int64_t i, DirectX::FXMVECTOR vecImpactPosition);
	static void XM_CALLCONV SpawnPickup(Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition, float fChance = 1.0f, bool bForce = false);
	static void End(Frame& __restrict rFrame, bool bRemoveAutosave);

	Frame(FrameFlags_t initialFlags, engine::IslandsFlip eInitialIslandsFlip);
	~Frame() = default;

	bool operator==(const Frame& rOther) const = default;

private:

	// Should only be called by DifferenceStreamHeader
	Frame() = default;

	friend struct engine::DifferenceStreamHeader<Frame, FrameInput>;

	friend class engine::DifferenceStreamReader<Frame, FrameInput>;
	friend class engine::DifferenceStreamWriter<Frame, FrameInput>;
};
static_assert(std::is_trivially_copyable_v<Frame>);
// Camera depends on player, and others depend on camera
#define UPDATE_LIST UPDATE_LIST_BASE, &rFrame.player, &rFrame.camera, &rFrame.blasters, &rFrame.missiles, &rFrame.spaceships

template <typename COLLECTION, typename FLAG_TYPE, bool HEALTH = true>
void CollectionAreaDamage(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition, float fRadius, float fDamage, float fFreezeTime, COLLECTION& rCollection, FLAG_TYPE eFlag, bool bBurnParticles)
{
	for (int64_t i = 0; i < rCollection.iCount; ++i)
	{
		if (rCollection.pFlags[i] & eFlag) [[unlikely]]
		{
			continue;
		}

		if (engine::OutsideVisibleArea(rFrameInput, rCollection.pVecPositions[i]))
		{
			continue;
		}

		float fDistance = common::Distance(DirectX::XMVectorSetZ(rCollection.pVecPositions[i], engine::gBaseHeight.Get()), vecPosition);
		if (fDistance > fRadius)
		{
			continue;
		}

		float fPercent = std::clamp(fDistance / fRadius, 0.0f, 1.0f);

		if (bBurnParticles)
		{
			SpawnBurnParticles(rCollection.pVecPositions[i], XMVectorZero(), common::DirectionTo(vecPosition, rCollection.pVecPositions[i]), COLLECTION::kfBurnSize, fPercent);
		}

		rCollection.pfFreezeTimes[i] = std::max(fFreezeTime, rCollection.pfFreezeTimes[i]);

		float fVisibilityDamage = VisibilityToDamagePercent(rFrameInput, rCollection.pVecPositions[i]);
		if constexpr (HEALTH)
		{
			rCollection.pfHealths[i] -= fVisibilityDamage * fPercent * fDamage;
			if (rCollection.pfHealths[i] <= 0.0f) [[unlikely]]
			{
				rCollection.Explode(rFrame, i, common::DirectionTo(vecPosition, rCollection.pVecPositions[i]));
			}
		}
		else
		{
			rCollection.pfShields[i] -= fVisibilityDamage * fPercent * fDamage;
		}
	}
}

template <typename COLLECTION, typename FLAG_TYPE>
void CollectionSlow(DirectX::FXMVECTOR vecPosition, float fRadius, float fSlow, COLLECTION& rCollection, FLAG_TYPE eFlag)
{
	for (int64_t i = 0; i < rCollection.iCount; ++i)
	{
		if (rCollection.pFlags[i] & eFlag) [[unlikely]]
		{
			continue;
		}

		auto vecToObject = XMVectorSubtract(rCollection.pVecPositions[i], vecPosition);
		if (XMVectorGetX(XMVector3Length(vecToObject)) < fRadius)
		{
			rCollection.pVecVelocities[i] = XMVectorMultiply(XMVectorReplicate(1.0f - std::clamp(fSlow, 0.0f, 1.0f)), rCollection.pVecVelocities[i]);
		}
	}
}

void XM_CALLCONV SpawnDamageParticles(Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecDirection, float fPercent);

inline float Damage(Damages eDamage)
{
	return kppfDamages[eDamage][0];
}

inline float VisibilityToDamagePercent(const game::FrameInput& __restrict rFrameInput, DirectX::FXMVECTOR vecPosition)
{
	DirectX::XMFLOAT4 f4VisibleDistances = engine::VisibleDistances(rFrameInput, vecPosition);
	f4VisibleDistances.z *= 2.0f;
	f4VisibleDistances.w *= 2.0f;
	float fDistance = engine::VisibleDistance(f4VisibleDistances);
	return std::clamp(0.1f * fDistance, 0.0f, 1.0f);
}

inline DirectX::XMVECTOR XM_CALLCONV ApplyFlip(engine::IslandsFlip eIslandsFlip, DirectX::FXMVECTOR vecOriginal)
{
	switch (eIslandsFlip)
	{
	case engine::kFlipNone:
		return vecOriginal;

	case engine::kFlipX:
		return DirectX::XMVectorMultiply(DirectX::XMVectorSet(-1.0f, 1.0f, 1.0f, 1.0f), vecOriginal);

	case engine::kFlipY:
		return DirectX::XMVectorMultiply(DirectX::XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f), vecOriginal);

	case engine::kFlipXY:
		return DirectX::XMVectorMultiply(DirectX::XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f), vecOriginal);
	}

	DEBUG_BREAK();
	return vecOriginal;
}

inline float EnemyHealthMultiplier(Frame& __restrict rFrame)
{
	return 1.0f + static_cast<float>(rFrame.iWave) / 25.0f;
}

void XM_CALLCONV SpawnBurnParticles(DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecVelocity, DirectX::FXMVECTOR vecDirection, float fSize, float fIntensity);

} // namespace game
