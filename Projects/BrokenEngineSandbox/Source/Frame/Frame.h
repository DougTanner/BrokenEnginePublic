#pragma once

#include "Frame/FrameBase.h"
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
	// Called on Game creation
	static void Register();

	// Called during Graphics creation
	static void GraphicsResources();

	// Interpolate phases
	static void AllocateAndCopy(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious);
	static void Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	FrameFlags_t flags;
	float fSpawnTimer = 0.0f;

	PlayerInterpolate player {};

	BlastersInterpolate blasters {};
	MissilesInterpolate missiles {};
	SpaceshipsInterpolate spaceships {};
	TargetsInterpolate targets {};

	inline bool operator==(const FrameInterpolate& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual(static_cast<const engine::FrameInterpolateBase&>(*this), static_cast<const engine::FrameInterpolateBase&>(rOther));

		bEqual &= common::BreakOnNotEqual(fSpawnTimer, rOther.fSpawnTimer);
		bEqual &= common::BreakOnNotEqual(flags, rOther.flags);

		bEqual &= common::BreakOnNotEqual(player, rOther.player);

		bEqual &= common::BreakOnNotEqual(blasters, rOther.blasters);
		bEqual &= common::BreakOnNotEqual(missiles, rOther.missiles);
		bEqual &= common::BreakOnNotEqual(spaceships, rOther.spaceships);
		bEqual &= common::BreakOnNotEqual(targets, rOther.targets);

		return bEqual;
	}

	static inline common::crc_t Crc(const FrameInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;

		checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).Crc();

		checksum ^= common::Crc(rCurrent.flags);
		checksum ^= common::Crc(rCurrent.fSpawnTimer);

		checksum ^= PlayerInterpolate::Crc(rCurrent.player);

		checksum ^= engine::CollectionCrc(rCurrent.blasters, rCurrent.blasters.Members());
		checksum ^= engine::CollectionCrc(rCurrent.missiles, rCurrent.missiles.Members());
		checksum ^= engine::CollectionCrc(rCurrent.spaceships, rCurrent.spaceships.Members());
		checksum ^= engine::CollectionCrc(rCurrent.targets, rCurrent.targets.Members());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FrameInterpolateBase&>(*this).Write(rStream);

		common::Write(rStream, fSpawnTimer);
		common::Write(rStream, flags);

		player.Write(rStream);

		engine::CollectionWrite(rStream, blasters, blasters.Members());
		engine::CollectionWrite(rStream, missiles, missiles.Members());
		engine::CollectionWrite(rStream, spaceships, spaceships.Members());
		engine::CollectionWrite(rStream, targets, targets.Members());
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FrameInterpolateBase&>(*this).Read(rStream);

		common::Read(rStream, fSpawnTimer);
		common::Read(rStream, flags);

		player.Read(rStream);

		engine::CollectionRead(rStream, blasters, blasters.Members());
		engine::CollectionRead(rStream, missiles, missiles.Members());
		engine::CollectionRead(rStream, spaceships, spaceships.Members());
		engine::CollectionRead(rStream, targets, targets.Members());
	}
};

struct FramePostRender : public engine::FramePostRenderBase
{
	// Post render phases
	static void AllocateAndCopy(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious);
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

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

struct Frame
{
	static constexpr int64_t kiVersion = 2;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	static [[nodiscard]] target_t XM_CALLCONV GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, TargetFlags_t targetFlags);

	FrameInterpolate interpolate;
	FramePostRender postRender;

	inline bool operator==(const Frame& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(interpolate, rOther.interpolate);
		bEqual &= common::BreakOnNotEqual(postRender, rOther.postRender);
		return bEqual;
	}

	inline common::crc_t Crc() const
	{
		common::crc_t checksum = 0;
		checksum ^= FrameInterpolate::Crc(interpolate);
		checksum ^= FramePostRender::Crc(postRender);
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const Frame& rCurrent)
{
	rCurrent.interpolate.Write(rStream);
	rCurrent.postRender.Write(rStream);
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, Frame& rCurrent)
{
	rCurrent.interpolate.Read(rStream);
	rCurrent.postRender.Read(rStream);
	return rStream;
}

} // namespace game
