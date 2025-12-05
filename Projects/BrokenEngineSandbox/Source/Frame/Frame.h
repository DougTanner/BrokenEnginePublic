#pragma once

#include "Frame/FrameBase.h"
#include "Frame/HealthDamage.h"
#include "Frame/Player.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/Spaceships.h"
#include "Frame/Collections/Targets.h"

namespace game
{

enum class FrameFlags : uint64_t
{
	kMainMenu    = 0x00000001,
	kGame        = 0x00000002,
	kDeathScreen = 0x00000008,
};
using FrameFlags_t = common::Flags<FrameFlags>;

// Set simulation timestep to 32/64/128 fps (kfDeltaTime: 0.03125f/0.015625f/0.0078125f)
inline constexpr std::chrono::nanoseconds kUpdateStepNs = 1'000'000'000ns / 64;
inline constexpr float kfDeltaTime = common::NanosecondsToFloatSeconds<float>(kUpdateStepNs);

struct FrameInterpolate : public engine::FrameInterpolateBase
{
	static void Allocate(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious);
	static void Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	PlayerInterpolate player {};

	BlastersInterpolate blasters {};

	MissilesInterpolate missiles {};

	SpaceshipsInterpolate spaceships {};

	TargetsInterpolate targets {};

	// Timed spawning state
	float fSpawnTimer = 0.0f;

	inline bool operator==(const FrameInterpolate& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(static_cast<const engine::FrameInterpolateBase&>(*this), static_cast<const engine::FrameInterpolateBase&>(rOther));
		bEqual &= common::BreakOnNotEqual(player, rOther.player);
		bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
		bEqual &= common::BreakOnNotEqual(missiles, rOther.missiles);
		bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);
		bEqual &= common::BreakOnNotEqual(targets, rOther.targets);
		bEqual &= common::BreakOnNotEqual(fSpawnTimer, rOther.fSpawnTimer);
		return bEqual;
	}

	static inline common::crc_t Crc(const FrameInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).Crc();
		checksum ^= PlayerInterpolate::Crc(rCurrent.player);
		checksum ^= engine::CollectionCrc(rCurrent.blasters, rCurrent.blasters.Members());
		checksum ^= engine::CollectionCrc(rCurrent.missiles, rCurrent.missiles.Members());
		checksum ^= engine::CollectionCrc(rCurrent.spaceships, rCurrent.spaceships.Members());
		checksum ^= engine::CollectionCrc(rCurrent.targets, rCurrent.targets.Members());
		checksum ^= common::Crc(rCurrent.fSpawnTimer);
		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FrameInterpolateBase&>(*this).Write(rStream);
		player.Write(rStream);
		engine::CollectionWrite(rStream, blasters, blasters.Members());
		engine::CollectionWrite(rStream, missiles, missiles.Members());
		engine::CollectionWrite(rStream, spaceships, spaceships.Members());
		engine::CollectionWrite(rStream, targets, targets.Members());
		common::Write(rStream, fSpawnTimer);
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FrameInterpolateBase&>(*this).Read(rStream);
		player.Read(rStream);
		engine::CollectionRead(rStream, blasters, blasters.Members());
		engine::CollectionRead(rStream, missiles, missiles.Members());
		engine::CollectionRead(rStream, spaceships, spaceships.Members());
		engine::CollectionRead(rStream, targets, targets.Members());
		common::Read(rStream, fSpawnTimer);
	}
};

struct FramePostRender : public engine::FramePostRenderBase
{
	static void Allocate(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious);
	static void Update(game::Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput);
	static void PreCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void AreaDamage(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	PlayerPostRender player {};

	BlastersPostRender blasters {};

	MissilesPostRender missiles {};

	SpaceshipsPostRender spaceships {};

	TargetsPostRender targets {};

	inline bool operator==(const FramePostRender& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual<FramePostRenderBase>(*this, rOther);
		bEqual &= common::BreakOnNotEqual(player, rOther.player);
		bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
		bEqual &= common::BreakOnNotEqual(missiles, rOther.missiles);
		bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);
		bEqual &= common::BreakOnNotEqual(targets, rOther.targets);
		return bEqual;
	}

	static inline common::crc_t Crc(const FramePostRender& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= static_cast<const engine::FramePostRenderBase&>(rCurrent).Crc();
		checksum ^= PlayerPostRender::Crc(rCurrent.player);
		checksum ^= engine::CollectionCrc(rCurrent.blasters, rCurrent.blasters.Members());
		checksum ^= engine::CollectionCrc(rCurrent.missiles, rCurrent.missiles.Members());
		checksum ^= engine::CollectionCrc(rCurrent.spaceships, rCurrent.spaceships.Members());
		checksum ^= engine::CollectionCrc(rCurrent.targets, rCurrent.targets.Members());
		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FramePostRenderBase&>(*this).Write(rStream);
		player.Write(rStream);
		engine::CollectionWrite(rStream, blasters, blasters.Members());
		engine::CollectionWrite(rStream, missiles, missiles.Members());
		engine::CollectionWrite(rStream, spaceships, spaceships.Members());
		engine::CollectionWrite(rStream, targets, targets.Members());
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FramePostRenderBase&>(*this).Read(rStream);
		player.Read(rStream);
		engine::CollectionRead(rStream, blasters, blasters.Members());
		engine::CollectionRead(rStream, missiles, missiles.Members());
		engine::CollectionRead(rStream, spaceships, spaceships.Members());
		engine::CollectionRead(rStream, targets, targets.Members());
	}
};

struct Frame : public engine::FrameBase
{
	static constexpr int64_t kiVersion = 1;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	static constexpr XMVECTOR kVecEnemySpawnPosition {10.0f, 30.0f, 0.0f, 1.0f};

	Frame();
	Frame(FrameFlags_t initialFlags);

	// Called on Game creation
	static void Register();

	// Called during Graphics creation
	static void AllocateGraphicsResources();

	// Interpolate phases
	static void InterpolateAllocate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void InterpolateUpdate(Frame& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render phases
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Post render phases
	static void PostRenderAllocate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostRenderUpdate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, const game::FrameInput& __restrict rFrameInput);
	static void PostRenderPreCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostRenderCollide();
	static void PostRenderPostCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostRenderAreaDamage(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostRenderSpawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostRenderDestroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	static FXMVECTOR XM_CALLCONV EnemySpawnPosition();
	static [[nodiscard]] target_t XM_CALLCONV GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, TargetFlags_t targetFlags);

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
