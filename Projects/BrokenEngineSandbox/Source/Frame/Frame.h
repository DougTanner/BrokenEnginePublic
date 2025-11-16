#pragma once

#include "Frame/FrameBase.h"
#include "Frame/Player.h"

namespace game
{

enum class FrameFlags : uint64_t
{
	kMainMenu    = 0x00000001,
	kGame        = 0x00000002,
	kFirstSpawn  = 0x00000004,
	kDeathScreen = 0x00000008,
};
using FrameFlags_t = common::Flags<FrameFlags>;

// Set simulation timestep to 30 fps
inline constexpr std::chrono::nanoseconds kUpdateStepNs = 1'000'000'000ns / 30;
inline constexpr float kfDeltaTime = common::NanosecondsToFloatSeconds<float>(kUpdateStepNs);

struct FrameInterpolate : public engine::FrameInterpolateBase
{
	static constexpr int64_t kiVersion = 1 + engine::FrameInterpolateBase::kiVersion + PlayerInterpolate::kiVersion;

	static void Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;

	PlayerInterpolate player {};

	// DT: TODO
	bool operator==(const FrameInterpolate& rOther) const = default;
};

inline std::ostream& operator<<(std::ostream& rStream, const FrameInterpolate& rFrame)
{
	rStream << static_cast<const engine::FrameInterpolateBase&>(rFrame);
	rStream << rFrame.player;
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, FrameInterpolate& rFrame)
{
	rStream >> static_cast<engine::FrameInterpolateBase&>(rFrame);
	rStream >> rFrame.player;
	return rStream;
}

struct FramePostRender : public engine::FramePostRenderBase
{
	static constexpr int64_t kiVersion = 1 + engine::FramePostRenderBase::kiVersion + PlayerPostRender::kiVersion;

	static void Update(FramePostRender& __restrict rCurrent, const Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	PlayerPostRender player {};

	// DT: TODO
	bool operator==(const FramePostRender& rOther) const = default;
};

inline std::ostream& operator<<(std::ostream& rStream, const FramePostRender& rFrame)
{
	rStream << static_cast<const engine::FramePostRenderBase&>(rFrame);
	rStream << rFrame.player;
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, FramePostRender& rFrame)
{
	rStream >> static_cast<engine::FramePostRenderBase&>(rFrame);
	rStream >> rFrame.player;
	return rStream;
}

struct Frame : public engine::FrameBase
{
	static constexpr int64_t kiVersion = 1 + engine::FrameBase::kiVersion + FrameInterpolate::kiVersion + FramePostRender::kiVersion;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	Frame();
	Frame(FrameFlags_t initialFlags);
	~Frame() = default;

	FrameFlags_t flags;

	static void UpdateInterpolate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;
	static void UpdatePostRender(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, const game::FrameInputHeld& __restrict rFrameInputHeld, const game::FrameInputPressed& __restrict rFrameInputPressed, float fDeltaTime);

	FrameInterpolate interpolate;

	FramePostRender postRender;

	// DT: TODO
	bool operator==(const Frame& rOther) const = default;
};

inline std::ostream& operator<<(std::ostream& rStream, const Frame& rFrame)
{
	rStream << static_cast<const engine::FrameBase&>(rFrame);
	rStream << rFrame.flags;
	rStream << rFrame.interpolate;
	rStream << rFrame.postRender;
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, Frame& rFrame)
{
	rStream >> static_cast<engine::FrameBase&>(rFrame);
	rStream >> rFrame.flags;
	rStream >> rFrame.interpolate;
	rStream >> rFrame.postRender;
	return rStream;
}

} // namespace game

#if 0

#include "Frame/HealthDamage.h"

#include "Frame/FrameBase.h"

#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/Spaceships.h"
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

struct FrameInputHeld;
struct FrameInputPressed;


inline constexpr float kfAutoDestroyDistance = 80.0f;
inline constexpr float kfPickupSize = 0.0175f;

inline constexpr float kfToMissileCollisionRadius = 1.5f;

struct alignas(64) FrameInterpolate : public engine::FrameBaseInterpolate
{
	float fEndTime = 0.0f;
	float fDeltaTime = 0.0f;

	static constexpr float kfWaveDisplayTime = 2.0f;
	float fWaveDisplayTimeLeft = 0.0f;

	bool bNextWave = false;
	int64_t iWave = 1;
	int64_t iLastSpawn = 0;
	int64_t iClumpsLeft = 0;
	int64_t iClumpSize = 0;
	int64_t iNextClumpSpawn = 0;
	float fNextClumpSpawnTime = 0;

	alignas(64) Blasters blasters {};
	alignas(64) Missiles missiles {};
	alignas(64) Spaceships spaceships {};

	// Camera state (deterministic, input-dependent)

	bool operator==(const FrameInterpolate& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FrameInterpolate>);

struct alignas(64) FramePostRender : public engine::FrameBasePostRender
{
	bool operator==(const FramePostRender& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FramePostRender>);

struct alignas(64) Frame
{
	static constexpr int64_t kiVersion = 5 + kiBlastersVersion + kiMissilesVersion + kiPlayerVersion + kiSpaceshipsVersion + engine::kiBillboardsVersion + engine::kiExplosionsVersion + engine::kiHexShieldsVersion + engine::kiLightingVersion + engine::kiNavmeshVersion + engine::kiSoundsVersion + engine::kiSmokeVersion + engine::kiPullersVersion + engine::kiPushersVersion + engine::kiTargetsVersion + engine::kiSplashesVersion;

	static constexpr XMVECTOR kVecEnemySpawnPosition {10.0f, 30.0f, 0.0f, 1.0f};

	FrameInterpolate interpolate {};
	FramePostRender postRender {};

	static FXMVECTOR XM_CALLCONV EnemySpawnPosition();
	static std::optional<FXMVECTOR> XM_CALLCONV ClosestEnemy(Frame& __restrict rFrame, FXMVECTOR vecPosition);
	static [[nodiscard]] engine::target_t XM_CALLCONV GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, engine::TargetFlags_t targetFlags);
	static void XM_CALLCONV AreaDamage(Frame& __restrict rFrame, FXMVECTOR vecPosition, float fDamage, float fRadius);
	static void XM_CALLCONV BlasterImpact(Frame& __restrict rFrame, int64_t i, FXMVECTOR vecImpactPosition);
	static void XM_CALLCONV SpawnPickup(Frame& __restrict rFrame, FXMVECTOR vecPosition, float fChance = 1.0f, bool bForce = false);
	static void End(Frame& __restrict rFrame, bool bRemoveAutosave);

	Frame(FrameFlags_t initialFlags);
	~Frame() = default;

	bool operator==(const Frame& rOther) const;

private:

	// Should only be called by DifferenceStreamHeader
	Frame() = default;

	friend struct engine::DifferenceStreamHeader<Frame, FrameInputHeld>;
	friend struct engine::DifferenceStreamHeader<Frame, FrameInputPressed>;

	friend class engine::DifferenceStreamReader<Frame, FrameInputHeld>;
	friend class engine::DifferenceStreamReader<Frame, FrameInputPressed>;

	friend class engine::DifferenceStreamWriter<Frame, FrameInputHeld>;
	friend class engine::DifferenceStreamWriter<Frame, FrameInputPressed>;
};
static_assert(std::is_trivially_copyable_v<Frame>);

template <typename COLLECTION, typename FLAG_TYPE, bool HEALTH = true>
void CollectionAreaDamage(Frame& __restrict rFrame, FXMVECTOR vecPosition, float fRadius, float fDamage, float fFreezeTime, COLLECTION& rCollection, FLAG_TYPE eFlag, bool bBurnParticles)
{
	for (int64_t i = 0; i < rCollection.iCount; ++i)
	{
		if (rCollection.pFlags[i] & eFlag) [[unlikely]]
		{
			continue;
		}

		float fDistance = common::Distance(XMVectorSetZ(rCollection.pVecPositions[i], engine::gBaseHeight.Get()), vecPosition);
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

		if constexpr (HEALTH)
		{
			rCollection.pfHealths[i] -= fPercent * fDamage;
			if (rCollection.pfHealths[i] <= 0.0f) [[unlikely]]
			{
				rCollection.Explode(rFrame, i, common::DirectionTo(vecPosition, rCollection.pVecPositions[i]));
			}
		}
		else
		{
			rCollection.pfShields[i] -= fPercent * fDamage;
		}
	}
}

template <typename COLLECTION, typename FLAG_TYPE>
void CollectionSlow(FXMVECTOR vecPosition, float fRadius, float fSlow, COLLECTION& rCollection, FLAG_TYPE eFlag)
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

void XM_CALLCONV SpawnDamageParticles(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fPercent);

inline float Damage(Damages eDamage)
{
	return kppfDamages[eDamage][0];
}

inline XMVECTOR XM_CALLCONV ApplyFlip(engine::IslandsFlip eIslandsFlip, FXMVECTOR vecOriginal)
{
	switch (eIslandsFlip)
	{
	case engine::kFlipNone:
		return vecOriginal;

	case engine::kFlipX:
		return XMVectorMultiply(XMVectorSet(-1.0f, 1.0f, 1.0f, 1.0f), vecOriginal);

	case engine::kFlipY:
		return XMVectorMultiply(XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f), vecOriginal);

	case engine::kFlipXY:
		return XMVectorMultiply(XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f), vecOriginal);
	}

	DEBUG_BREAK();
	return vecOriginal;
}

inline float EnemyHealthMultiplier(Frame& __restrict rFrame)
{
	return 1.0f + static_cast<float>(rFrame.interpolate.iWave) / 25.0f;
}

void XM_CALLCONV SpawnBurnParticles(FXMVECTOR vecPosition, FXMVECTOR vecVelocity, FXMVECTOR vecDirection, float fSize, float fIntensity);

} // namespace game

#endif
