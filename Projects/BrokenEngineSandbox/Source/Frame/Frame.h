#pragma once

#include "Frame/FrameBase.h"
#include "Frame/Player.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Spaceships.h"

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
	static constexpr int64_t kiVersion = 1 + engine::FrameInterpolateBase::kiVersion + PlayerInterpolate::kiVersion + BlastersInterpolate::kiVersion + SpaceshipsInterpolate::kiVersion;

	FrameInterpolate() = default;
	virtual ~FrameInterpolate() = default;

	static void Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;

	PlayerInterpolate player {};

	BlastersInterpolate blasters {};

	SpaceshipsInterpolate spaceships {};

	// Wave spawning state
	static constexpr float kfWaveDisplayTime = 2.0f;
	float fWaveDisplayTimeLeft = 0.0f;

	bool bNextWave = false;
	int64_t iWave = 1;
	int64_t iLastSpawn = 0;
	int64_t iClumpsLeft = 0;
	int64_t iClumpSize = 0;
	int64_t iNextClumpSpawn = 0;
	float fNextClumpSpawnTime = 0.0f;

	inline bool operator==(const FrameInterpolate& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(static_cast<const engine::FrameInterpolateBase&>(*this), static_cast<const engine::FrameInterpolateBase&>(rOther));
		bEqual &= common::BreakOnNotEqual(player, rOther.player);
		bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
		bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);
		bEqual &= common::BreakOnNotEqual(fWaveDisplayTimeLeft, rOther.fWaveDisplayTimeLeft);
		bEqual &= common::BreakOnNotEqual(bNextWave, rOther.bNextWave);
		bEqual &= common::BreakOnNotEqual(iWave, rOther.iWave);
		bEqual &= common::BreakOnNotEqual(iLastSpawn, rOther.iLastSpawn);
		bEqual &= common::BreakOnNotEqual(iClumpsLeft, rOther.iClumpsLeft);
		bEqual &= common::BreakOnNotEqual(iClumpSize, rOther.iClumpSize);
		bEqual &= common::BreakOnNotEqual(iNextClumpSpawn, rOther.iNextClumpSpawn);
		bEqual &= common::BreakOnNotEqual(fNextClumpSpawnTime, rOther.fNextClumpSpawnTime);
		return bEqual;
	}

	static inline common::crc_t Checksum(const FrameInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).Checksum();
		checksum ^= PlayerInterpolate::Checksum(rCurrent.player);
		checksum ^= BlastersInterpolate::Checksum(rCurrent.blasters);
		checksum ^= SpaceshipsInterpolate::Checksum(rCurrent.spaceships);
		checksum ^= common::Crc(rCurrent.fWaveDisplayTimeLeft);
		checksum ^= common::Crc(rCurrent.bNextWave);
		checksum ^= common::Crc(rCurrent.iWave);
		checksum ^= common::Crc(rCurrent.iLastSpawn);
		checksum ^= common::Crc(rCurrent.iClumpsLeft);
		checksum ^= common::Crc(rCurrent.iClumpSize);
		checksum ^= common::Crc(rCurrent.iNextClumpSpawn);
		checksum ^= common::Crc(rCurrent.fNextClumpSpawnTime);
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const FrameInterpolate& rCurrent)
{
	rStream << static_cast<const engine::FrameInterpolateBase&>(rCurrent);
	rStream << rCurrent.player;
	rStream << rCurrent.blasters;
	rStream << rCurrent.spaceships;
	common::Write(rStream, rCurrent.fWaveDisplayTimeLeft);
	common::Write(rStream, rCurrent.bNextWave);
	common::Write(rStream, rCurrent.iWave);
	common::Write(rStream, rCurrent.iLastSpawn);
	common::Write(rStream, rCurrent.iClumpsLeft);
	common::Write(rStream, rCurrent.iClumpSize);
	common::Write(rStream, rCurrent.iNextClumpSpawn);
	common::Write(rStream, rCurrent.fNextClumpSpawnTime);
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, FrameInterpolate& rCurrent)
{
	rStream >> static_cast<engine::FrameInterpolateBase&>(rCurrent);
	rStream >> rCurrent.player;
	rStream >> rCurrent.blasters;
	rStream >> rCurrent.spaceships;
	common::Read(rStream, rCurrent.fWaveDisplayTimeLeft);
	common::Read(rStream, rCurrent.bNextWave);
	common::Read(rStream, rCurrent.iWave);
	common::Read(rStream, rCurrent.iLastSpawn);
	common::Read(rStream, rCurrent.iClumpsLeft);
	common::Read(rStream, rCurrent.iClumpSize);
	common::Read(rStream, rCurrent.iNextClumpSpawn);
	common::Read(rStream, rCurrent.fNextClumpSpawnTime);
	return rStream;
}

struct FramePostRender : public engine::FramePostRenderBase
{
	static constexpr int64_t kiVersion = 1 + engine::FramePostRenderBase::kiVersion + PlayerPostRender::kiVersion + PlayerPostRender::kiVersion + BlastersPostRender::kiVersion + SpaceshipsPostRender::kiVersion;

	FramePostRender() = default;
	virtual ~FramePostRender() = default;

	static void Update(FramePostRender& __restrict rCurrent, const FrameInterpolate& __restrict rCurrentInterpolate, const Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Collide(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);

	PlayerPostRender player {};

	BlastersPostRender blasters {};

	SpaceshipsPostRender spaceships {};

	inline bool operator==(const FramePostRender& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(static_cast<const engine::FramePostRenderBase&>(*this), static_cast<const engine::FramePostRenderBase&>(rOther));
		bEqual &= common::BreakOnNotEqual(player, rOther.player);
		bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
		bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);
		return bEqual;
	}

	static inline common::crc_t Checksum(const FramePostRender& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FramePostRenderBase&>(rCurrent).Checksum();
		checksum ^= PlayerPostRender::Checksum(rCurrent.player);
		checksum ^= BlastersPostRender::Checksum(rCurrent.blasters);
		checksum ^= SpaceshipsPostRender::Checksum(rCurrent.spaceships);
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const FramePostRender& rCurrent)
{
	rStream << static_cast<const engine::FramePostRenderBase&>(rCurrent);
	rStream << rCurrent.player;
	rStream << rCurrent.blasters;
	rStream << rCurrent.spaceships;
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, FramePostRender& rCurrent)
{
	rStream >> static_cast<engine::FramePostRenderBase&>(rCurrent);
	rStream >> rCurrent.player;
	rStream >> rCurrent.blasters;
	rStream >> rCurrent.spaceships;
	return rStream;
}

struct Frame : public engine::FrameBase
{
	static constexpr int64_t kiVersion = 1 + engine::FrameBase::kiVersion + FrameInterpolate::kiVersion + FramePostRender::kiVersion;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	static constexpr XMVECTOR kVecEnemySpawnPosition {10.0f, 30.0f, 0.0f, 1.0f};

	Frame();
	Frame(FrameFlags_t initialFlags);
	virtual ~Frame() = default;

	static void UpdateInterpolate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;
	static void UpdatePostRender(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime);

	static FXMVECTOR XM_CALLCONV EnemySpawnPosition();

	// Interpolate
	FrameFlags_t flags;
	FrameInterpolate interpolate;

	// Post render
	FramePostRender postRender;

	inline bool operator==(const Frame& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(static_cast<const engine::FrameBase&>(*this), static_cast<const engine::FrameBase&>(rOther));
		bEqual &= common::BreakOnNotEqual(flags, rOther.flags);
		bEqual &= common::BreakOnNotEqual(interpolate, rOther.interpolate);
		bEqual &= common::BreakOnNotEqual(postRender, rOther.postRender);
		return bEqual;
	}

	inline common::crc_t Checksum() const
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FrameBase&>(*this).Checksum();
		checksum ^= common::Crc(flags);
		checksum ^= FrameInterpolate::Checksum(interpolate);
		checksum ^= FramePostRender::Checksum(postRender);
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const Frame& rCurrent)
{
	rStream << static_cast<const engine::FrameBase&>(rCurrent);
	rStream << rCurrent.flags;
	rStream << rCurrent.interpolate;
	rStream << rCurrent.postRender;
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, Frame& rCurrent)
{
	rStream >> static_cast<engine::FrameBase&>(rCurrent);
	rStream >> rCurrent.flags;
	rStream >> rCurrent.interpolate;
	rStream >> rCurrent.postRender;
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
class DifferenceStreamReader;

template<typename SAVED_TYPE, typename DIFFERENCE_TYPE>
class DifferenceStreamWriter;

}

namespace game
{

struct FrameInput;

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

	inline bool operator==(const FrameInterpolate& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FrameInterpolate>);

struct alignas(64) FramePostRender : public engine::FrameBasePostRender
{
	inline bool operator==(const FramePostRender& rOther) const;
};
static_assert(std::is_trivially_copyable_v<FramePostRender>);

struct alignas(64) Frame
{
	static constexpr int64_t kiVersion = 5 + kiBlastersVersion + kiMissilesVersion + kiPlayerVersion + kiSpaceshipsVersion + engine::kiBillboardsVersion + engine::kiExplosionsVersion + engine::kiHexShieldsVersion + engine::kiLightingVersion + engine::kiNavmeshVersion + engine::kiSoundsVersion + engine::kiSmokeVersion + engine::kiPullersVersion + engine::kiPushersVersion + engine::kiTargetsVersion + engine::kiSplashesVersion;

	static constexpr XMVECTOR kVecEnemySpawnPosition {10.0f, 30.0f, 0.0f, 1.0f};

	Frame() = default;
	virtual ~Frame() = default;

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

	inline bool operator==(const Frame& rOther) const;

private:

	// Should only be called by DifferenceStream
	Frame() = default;

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
