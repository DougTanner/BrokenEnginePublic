#pragma once

#include "Frame/Collections/Collections.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInterpolate;
struct FramePostRender;
struct SpaceshipsInterpolate;

struct SpaceshipsInterpolate
{
	static constexpr int64_t kiVersion = 1;

	SpaceshipsInterpolate() = default;
	virtual ~SpaceshipsInterpolate() = default;

	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	void Render(int64_t iCommandBuffer) const;

	int64_t iCount = 0;
	int64_t iCapacity = 0;

	common::AlignedUniquePtr<std::byte> pData;
	// 1. IMPORTANT: Add to this macro when adding new members
	#define SPACESHIPS_INTERPOLATE_LIST(a) a.pVecPositions, a.pVecDirections, a.pfDestroyedTimes
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;

	inline bool operator==(const SpaceshipsInterpolate& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);

		for (int64_t i = 0; i < iCount; ++i)
		{
			// 2. IMPORTANT: Add a BreakOnNotEqual when adding members
			bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
			bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
			bEqual &= common::BreakOnNotEqual(pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
		}

		return bEqual;
	}

	static inline common::crc_t Checksum(const SpaceshipsInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(rCurrent.iCount);
		checksum ^= common::Crc(rCurrent.iCapacity);
		checksum ^= engine::MultiCrc(rCurrent.iCount, SPACESHIPS_INTERPOLATE_LIST(rCurrent));
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const SpaceshipsInterpolate& rCurrent)
{
	common::Write(rStream, rCurrent.iCount);
	common::Write(rStream, rCurrent.iCapacity);
	engine::MultiWrite(rStream, rCurrent.iCount, SPACESHIPS_INTERPOLATE_LIST(rCurrent));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, SpaceshipsInterpolate& rCurrent)
{
	common::Read(rStream, rCurrent.iCount);
	common::Read(rStream, rCurrent.iCapacity);
	engine::AllocateAndRead(rCurrent, rStream, SPACESHIPS_INTERPOLATE_LIST(rCurrent));
	return rStream;
}

enum class SpaceshipFlags : uint8_t
{
	kFleePlayer           = 0x01,
	kExploding            = 0x02,
	kReturnToIslandCenter = 0x04,
};
using SpaceshipFlags_t = common::Flags<SpaceshipFlags>;

struct SpaceshipsPostRender
{
	static constexpr int64_t kiVersion = 1;

	SpaceshipsPostRender() = default;
	virtual ~SpaceshipsPostRender() = default;

	static void Update(FramePostRender& __restrict rCurrentFramePostRender, const SpaceshipsInterpolate& __restrict rCurrentInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Collide(Frame& __restrict rFrame);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection);
	static void Destroy(Frame& __restrict rFrame);

	int64_t iCount = 0;
	int64_t iCapacity = 0;

	common::AlignedUniquePtr<std::byte> pData;
	// 1. IMPORTANT: Add to this macro when adding new members
	#define SPACESHIPS_POST_RENDER_LIST(a) a.pFlags, a.pVecVelocities, a.pfDeltaRotations, a.pfHealths, a.pfFreezeTimes, a.pfDestroyedExplosionTimes, a.pfNextBlasterSpawnTimes, a.piBlasterSpawns
	SpaceshipFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfHealths = nullptr;
	float* __restrict pfFreezeTimes = nullptr;
	float* __restrict pfDestroyedExplosionTimes = nullptr;
	float* __restrict pfNextBlasterSpawnTimes = nullptr;
	int32_t* __restrict piBlasterSpawns = nullptr;

	inline bool operator==(const SpaceshipsPostRender& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);

		for (int64_t i = 0; i < iCount; ++i)
		{
			// 2. IMPORTANT: Add a BreakOnNotEqual when adding members
			bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
			bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
			bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
			bEqual &= common::BreakOnNotEqual(pfHealths[i], rOther.pfHealths[i]);
			bEqual &= common::BreakOnNotEqual(pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
			bEqual &= common::BreakOnNotEqual(pfDestroyedExplosionTimes[i], rOther.pfDestroyedExplosionTimes[i]);
			bEqual &= common::BreakOnNotEqual(pfNextBlasterSpawnTimes[i], rOther.pfNextBlasterSpawnTimes[i]);
			bEqual &= common::BreakOnNotEqual(piBlasterSpawns[i], rOther.piBlasterSpawns[i]);
		}

		return bEqual;
	}

	static inline common::crc_t Checksum(const SpaceshipsPostRender& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(rCurrent.iCount);
		checksum ^= common::Crc(rCurrent.iCapacity);
		checksum ^= engine::MultiCrc(rCurrent.iCount, SPACESHIPS_POST_RENDER_LIST(rCurrent));
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const SpaceshipsPostRender& rCurrent)
{
	common::Write(rStream, rCurrent.iCount);
	common::Write(rStream, rCurrent.iCapacity);
	engine::MultiWrite(rStream, rCurrent.iCount, SPACESHIPS_POST_RENDER_LIST(rCurrent));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, SpaceshipsPostRender& rCurrent)
{
	common::Read(rStream, rCurrent.iCount);
	common::Read(rStream, rCurrent.iCapacity);
	engine::AllocateAndRead(rCurrent, rStream, SPACESHIPS_POST_RENDER_LIST(rCurrent));
	return rStream;
}

} // namespace game

#if 0

#include "Frame/Collections/Collections.h"
#include "Frame/Pools/Explosions.h"
#include "Frame/Pools/Pushers.h"
#include "Frame/Pools/Targets.h"

namespace game
{

enum class SpaceshipFlags : uint8_t
{
	kFleePlayer           = 0x01,
	kExploding            = 0x02,
	kReturnToIslandCenter = 0x04,
};
using SpaceshipFlags_t = common::Flags<SpaceshipFlags>;

struct alignas(64) Spaceships
{
	static constexpr int64_t kiMax = 1024;

	static constexpr float kfBlastersSpeed = 65.0f;
	static constexpr float kfFreezeTimeAreaDamage = 0.075f;
	static constexpr float kfBurnSize = 0.9f;

	// Interpolate
	int64_t iCount = 0;
	int64_t iKilled = 0;

	alignas(64) SpaceshipFlags_t pFlags[kiMax] {};
	alignas(64) XMVECTOR pVecPositions[kiMax] {};
	alignas(64) XMVECTOR pVecDirections[kiMax] {};
	alignas(64) engine::pusher_t puiPushers[kiMax] {};
	alignas(64) engine::target_t puiTargets[kiMax] {};
	alignas(64) engine::trail_t puiDamageTrails[kiMax] {};
	alignas(64) engine::billboard_t puiBillboards[kiMax] {};
	alignas(64) float pfDestroyedTimes[kiMax] {};

	// Post render
	alignas(64) XMVECTOR pVecVelocities[kiMax] {};
	alignas(64) float pfDeltaRotations[kiMax] {};
	alignas(64) float pfHealths[kiMax] {};
	alignas(64) float pfFreezeTimes[kiMax] {};
	alignas(64) float pfDestroyedExplosionTimes[kiMax] {};
	alignas(64) float pfNextBlasterSpawnTimes[kiMax] {};
	alignas(64) int32_t piBlasterSpawns[kiMax] {};

	// Utility
	bool operator==(const Spaceships& rOther) const;
	void Copy(int64_t iDestIndex, int64_t iSrcIndex);

	// Update
	static void Interpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRenderAvoidTerrain(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime, int64_t iStart, int64_t iEnd);
	static void PostRenderPushers(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime, int64_t iStart, int64_t iEnd);
	static void XM_CALLCONV Explode(Frame& __restrict rFrame, int64_t i, FXMVECTOR vecDirection = XMVectorZero());
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

	// Render
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Spaceships>);
inline constexpr int64_t kiSpaceshipsVersion = 5 + sizeof(Spaceships);

} // namespace game

#endif
