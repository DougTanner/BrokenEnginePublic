#pragma once

#include "Frame/Collections/Collections.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;

struct BlastersInterpolate
{
	static constexpr int64_t kiVersion = 1;

	BlastersInterpolate() = default;
	virtual ~BlastersInterpolate() = default;

	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	int64_t iCount = 0;
	int64_t iCapacity = 0;

	common::AlignedUniquePtr<std::byte> pData;
	// 1. IMPORTANT: Add to this macro when adding new members
	#define BLASTERS_INTERPOLATE_LIST(a) a.pVecPositions
	XMVECTOR* __restrict pVecPositions = nullptr;
	engine::area_light_t* __restrict  puiAreaLights = nullptr;

	inline bool operator==(const BlastersInterpolate& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);

		for (int64_t i = 0; i < iCount; ++i)
		{
			// 2. IMPORTANT: Add a BreakOnNotEqual when adding members
			bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		}

		return bEqual;
	}

	static inline common::crc_t Checksum(const BlastersInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(rCurrent.iCount);
		checksum ^= common::Crc(rCurrent.iCapacity);
		checksum ^= engine::MultiCrc(rCurrent.iCount, BLASTERS_INTERPOLATE_LIST(rCurrent));
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const BlastersInterpolate& rCurrent)
{
	common::Write(rStream, rCurrent.iCount);
	common::Write(rStream, rCurrent.iCapacity);
	engine::MultiWrite(rStream, rCurrent.iCount, BLASTERS_INTERPOLATE_LIST(rCurrent));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, BlastersInterpolate& rCurrent)
{
	common::Read(rStream, rCurrent.iCount);
	common::Read(rStream, rCurrent.iCapacity);
	engine::AllocateAndRead(rCurrent, rStream, BLASTERS_INTERPOLATE_LIST(rCurrent));
	return rStream;
}

enum class BlasterFlags : uint8_t
{
	kDestroy = 0x01,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

struct BlastersPostRender
{
	static constexpr int64_t kiVersion = 1;

	BlastersPostRender() = default;
	virtual ~BlastersPostRender() = default;

	static void Update(FramePostRender& __restrict rCurrentFramePostRender, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Collide(Frame& __restrict rFrame);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecVelocity);
	static void Destroy(Frame& __restrict rFrame);

	int64_t iCount = 0;
	int64_t iCapacity = 0;

	common::AlignedUniquePtr<std::byte> pData;
	// 1. IMPORTANT: Add to this macro when adding new members
	#define BLASTERS_POST_RENDER_LIST(a) a.pFlags, a.pVecVelocities
	BlasterFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;

	inline bool operator==(const BlastersPostRender& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);

		for (int64_t i = 0; i < iCount; ++i)
		{
			// 2. IMPORTANT: Add a BreakOnNotEqual when adding members
			bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
			bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
		}

		return bEqual;
	}

	static inline common::crc_t Checksum(const BlastersPostRender& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(rCurrent.iCount);
		checksum ^= common::Crc(rCurrent.iCapacity);
		checksum ^= engine::MultiCrc(rCurrent.iCount, BLASTERS_POST_RENDER_LIST(rCurrent));
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const BlastersPostRender& rCurrent)
{
	common::Write(rStream, rCurrent.iCount);
	common::Write(rStream, rCurrent.iCapacity);
	engine::MultiWrite(rStream, rCurrent.iCount, BLASTERS_POST_RENDER_LIST(rCurrent));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, BlastersPostRender& rCurrent)
{
	common::Read(rStream, rCurrent.iCount);
	common::Read(rStream, rCurrent.iCapacity);
	engine::AllocateAndRead(rCurrent, rStream, BLASTERS_POST_RENDER_LIST(rCurrent));
	return rStream;
}

} // namespace game

#if 0

#include "Frame/Collections/Collections.h"
#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/Sounds.h"

namespace game
{

enum class BlasterFlags : uint8_t
{
	kDestroy       = 0x01,
	kSizeFromSpeed = 0x02,

	kCollideEnemies = 0x04,
	kCollidePlayer  = 0x08,

	kImpactObject  = 0x10,
	kImpactTerrain = 0x20,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

inline constexpr int64_t kiMaxSpawnBlasters = 512;

struct SpawnBlaster
{
	BlasterFlags_t flags;
	common::crc_t crc = 0;
	XMVECTOR vecPosition {};
	XMVECTOR vecVelocity {};
	XMFLOAT2 f2Size {};
	float fVisibleIntensity = 0.0f;
	float fLightArea = 0.0f;
	float fLightIntensity = 0.0f;
	float fDamage = 0.0f;
	XMFLOAT4 f4Decays{};

	inline bool operator==(const SpawnBlaster& rOther) const = default;
};

// Spawnable requires 1. operator== 2. Main() 3. Spawn()
struct alignas(64) Blasters : public engine::Spawnable<SpawnBlaster, kiMaxSpawnBlasters>
{
	static constexpr int64_t kiMax = 2048;

	// Interpolate
	int64_t iCount = 0;

	alignas(64) BlasterFlags_t pFlags[kiMax] {};
	alignas(64) float pfTimes[kiMax] {};
	alignas(64) XMVECTOR pVecPositions[kiMax] {};
	alignas(64) XMVECTOR pVecVelocities[kiMax] {};
	alignas(64) engine::area_light_t puiAreaLights[kiMax] {};
	alignas(64) common::crc_t pCrcs[kiMax] {};
	alignas(64) XMFLOAT2 pf2Sizes[kiMax] {};
	alignas(64) float pfFreezeTimes[kiMax] {};
	alignas(64) float pfVisibleIntensities[kiMax] {};
	alignas(64) float pfLightAreas[kiMax] {};
	alignas(64) float pfLightIntensities[kiMax] {};

	// Post render
	alignas(64) float pfSlowTimes[kiMax] {};
	alignas(64) float pfDamages[kiMax] {};
	alignas(64) float pfPitches[kiMax] {};
	alignas(64) engine::sound_t puiSounds[kiMax] {};
	alignas(64) XMFLOAT4 pf4Decays[kiMax] {}; // Velocity, Visible, Size, Damage

	// Utility
	inline bool operator==(const Blasters& rOther) const;
	void Copy(int64_t iDestIndex, int64_t iSrcIndex);

	// Update
	static void Interpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void XM_CALLCONV CollisionEffect(Frame& __restrict rFrame, int64_t i, bool bSmoke = true);
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, int64_t i);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

	// Render
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Blasters>);
inline constexpr int64_t kiBlastersVersion = 9 + sizeof(Blasters);

} // namespace game

#endif
