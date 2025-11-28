#pragma once

#include "Frame/FrameBase.h"
#include "Frame/HealthDamage.h"
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

	static void Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Sync(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Render(const Frame& __restrict rFrame, int64_t iCommandBuffer);

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

	static inline common::crc_t Crc(const FrameInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).Crc();
		checksum ^= PlayerInterpolate::Crc(rCurrent.player);
		checksum ^= engine::CollectionCrc(rCurrent.blasters, rCurrent.blasters.Members());
		checksum ^= engine::CollectionCrc(rCurrent.spaceships, rCurrent.spaceships.Members());
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

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FrameInterpolateBase&>(*this).Write(rStream);
		player.Write(rStream);
		engine::CollectionWrite(rStream, blasters, blasters.Members());
		engine::CollectionWrite(rStream, spaceships, spaceships.Members());
		common::Write(rStream, fWaveDisplayTimeLeft);
		common::Write(rStream, bNextWave);
		common::Write(rStream, iWave);
		common::Write(rStream, iLastSpawn);
		common::Write(rStream, iClumpsLeft);
		common::Write(rStream, iClumpSize);
		common::Write(rStream, iNextClumpSpawn);
		common::Write(rStream, fNextClumpSpawnTime);
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FrameInterpolateBase&>(*this).Read(rStream);
		player.Read(rStream);
		engine::CollectionRead(rStream, blasters, blasters.Members());
		engine::CollectionRead(rStream, spaceships, spaceships.Members());
		common::Read(rStream, fWaveDisplayTimeLeft);
		common::Read(rStream, bNextWave);
		common::Read(rStream, iWave);
		common::Read(rStream, iLastSpawn);
		common::Read(rStream, iClumpsLeft);
		common::Read(rStream, iClumpSize);
		common::Read(rStream, iNextClumpSpawn);
		common::Read(rStream, fNextClumpSpawnTime);
	}
};

struct FramePostRender : public engine::FramePostRenderBase
{
	static constexpr int64_t kiVersion = 1 + engine::FramePostRenderBase::kiVersion + PlayerPostRender::kiVersion + BlastersPostRender::kiVersion + SpaceshipsPostRender::kiVersion;

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
		bEqual &= common::BreakOnNotEqual<FramePostRenderBase>(*this, rOther);
		bEqual &= common::BreakOnNotEqual(player, rOther.player);
		bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
		bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);
		return bEqual;
	}

	static inline common::crc_t Crc(const FramePostRender& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FramePostRenderBase&>(rCurrent).Crc();
		checksum ^= PlayerPostRender::Crc(rCurrent.player);
		checksum ^= engine::CollectionCrc(rCurrent.blasters, rCurrent.blasters.Members());
		checksum ^= engine::CollectionCrc(rCurrent.spaceships, rCurrent.spaceships.Members());
		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FramePostRenderBase&>(*this).Write(rStream);
		player.Write(rStream);
		engine::CollectionWrite(rStream, blasters, blasters.Members());
		engine::CollectionWrite(rStream, spaceships, spaceships.Members());
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FramePostRenderBase&>(*this).Read(rStream);
		player.Read(rStream);
		engine::CollectionRead(rStream, blasters, blasters.Members());
		engine::CollectionRead(rStream, spaceships, spaceships.Members());
	}
};

struct Frame : public engine::FrameBase
{
	static constexpr int64_t kiVersion = 1 + engine::FrameBase::kiVersion + FrameInterpolate::kiVersion + FramePostRender::kiVersion;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	static constexpr XMVECTOR kVecEnemySpawnPosition {10.0f, 30.0f, 0.0f, 1.0f};

	Frame();
	Frame(FrameFlags_t initialFlags);

	// Called on Game creation
	static void RegisterTypes();

	// Called during Graphics creation
	static void AllocateGraphicsResources();

	// Interpolate phases
	static void InterpolateUpdate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void InterpolateSync(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render phases
	static void Render(const Frame& __restrict rFrame, int64_t iCommandBuffer);

	// Post render phases
	static void PostRenderUpdate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRenderCollide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRenderSpawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRenderDestroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const game::FrameInput& __restrict rFrameInput, float fDeltaTime);

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

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FrameBase&>(*this).Crc();
		checksum ^= common::Crc(flags);
		checksum ^= FrameInterpolate::Crc(interpolate);
		checksum ^= FramePostRender::Crc(postRender);
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const Frame& rCurrent)
{
	static_cast<const engine::FrameBase&>(rCurrent).Write(rStream);
	rCurrent.flags.Write(rStream);
	rCurrent.interpolate.Write(rStream);
	rCurrent.postRender.Write(rStream);
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, Frame& rCurrent)
{
	static_cast<engine::FrameBase&>(rCurrent).Read(rStream);
	rCurrent.flags.Read(rStream);
	rCurrent.interpolate.Read(rStream);
	rCurrent.postRender.Read(rStream);
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
