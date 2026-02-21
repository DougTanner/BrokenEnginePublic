#pragma once

#include "Frame/FrameBase.h"
#include "Frame/GridCoord.h"
#include "Frame/HealthDamage.h"
#include "Frame/Player.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Missiles.h"
#include "Frame/Collections/Spaceships.h"
#include "Frame/Collections/Targets.h"

#include "Input/Input.h"

namespace game
{

enum class GameFlags : uint64_t
{
	kMainMenu    = 0x00000001,
	kGame        = 0x00000002,
	kContinue    = 0x00000004,
	kDeathScreen = 0x00000008,
};
using GameFlags_t = common::Flags<GameFlags>;

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

	GameFlags_t gameFlags;
	float fSpawnTimer = 0.0f;

	PlayersInterpolate players {};

	BlastersInterpolate blasters {};
	MissilesInterpolate missiles {};
	SpaceshipsInterpolate spaceships {};
	TargetsInterpolate targets {};

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.blasters, rSelf.missiles, rSelf.spaceships, rSelf.targets);
	}

	inline bool operator==(const FrameInterpolate& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual(static_cast<const engine::FrameInterpolateBase&>(*this), static_cast<const engine::FrameInterpolateBase&>(rOther));

		bEqual &= common::BreakOnNotEqual(fSpawnTimer, rOther.fSpawnTimer);
		bEqual &= common::BreakOnNotEqual(gameFlags, rOther.gameFlags);

		bEqual &= common::BreakOnNotEqual(players, rOther.players);
		bEqual &= engine::CompareCollections(std::tie(players), std::tie(rOther.players), std::make_index_sequence<1>{});

		bEqual &= engine::CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

		return bEqual;
	}

	static inline common::crc_t Crc(const FrameInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;

		checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).Crc();

		checksum ^= common::Crc(rCurrent.gameFlags);
		checksum ^= common::Crc(rCurrent.fSpawnTimer);

		checksum ^= engine::CollectionCrc(rCurrent.players, rCurrent.players.Members());

		std::apply([&](const auto&... cols)
		{
			((checksum ^= engine::CollectionCrc(cols, cols.Members())), ...);
		}, rCurrent.Collections());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FrameInterpolateBase&>(*this).Write(rStream);

		common::Write(rStream, fSpawnTimer);
		common::Write(rStream, gameFlags);

		engine::CollectionWrite(rStream, players, players.Members());

		std::apply([&](const auto&... cols)
		{
			(engine::CollectionWrite(rStream, cols, cols.Members()), ...);
		}, Collections());
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FrameInterpolateBase&>(*this).Read(rStream);

		common::Read(rStream, fSpawnTimer);
		common::Read(rStream, gameFlags);

		engine::CollectionRead(rStream, players, players.Members());

		std::apply([&](auto&... cols)
		{
			(engine::CollectionRead(rStream, cols, cols.Members()), ...);
		}, Collections());
	}
};

struct TransferRequest
{
	StatusChangeType eType {};
	TransferData data {};
	int64_t iEntityId = 0;
	int8_t iDeltaX = 0;
	int8_t iDeltaY = 0;
};

struct FrameBounds
{
	float fMinX {};
	float fMinY {};
	float fMaxX {};
	float fMaxY {};
};

inline FrameBounds XM_CALLCONV ComputeFrameBounds(FXMVECTOR vecArea)
{
	// vecArea: x=minX, y=maxY, z=maxX, w=minY
	return {
		.fMinX = XMVectorGetX(vecArea),
		.fMinY = XMVectorGetW(vecArea),
		.fMaxX = XMVectorGetZ(vecArea),
		.fMaxY = XMVectorGetY(vecArea),
	};
}

// Compute world-space frame bounds for a given grid coordinate
inline XMVECTOR XM_CALLCONV ComputeFrameArea(FXMVECTOR vecBaseArea, engine::GridCoord coord)
{
	float fWidth = XMVectorGetZ(vecBaseArea) - XMVectorGetX(vecBaseArea);
	float fHeight = XMVectorGetY(vecBaseArea) - XMVectorGetW(vecBaseArea);
	XMVECTOR vecOffset = XMVectorSet(static_cast<float>(coord.x) * fWidth, static_cast<float>(coord.y) * fHeight, static_cast<float>(coord.x) * fWidth, static_cast<float>(coord.y) * fHeight);
	return XMVectorAdd(vecBaseArea, vecOffset);
}

inline constexpr size_t kuiInitialTransferCapacity = 32;

inline bool XM_CALLCONV IsOutOfBounds(const FrameBounds& rBounds, FXMVECTOR vecPosition)
{
	float fPositionX = XMVectorGetX(vecPosition);
	float fPositionY = XMVectorGetY(vecPosition);

	return !(fPositionX > rBounds.fMinX && fPositionX < rBounds.fMaxX &&
	         fPositionY > rBounds.fMinY && fPositionY < rBounds.fMaxY);
}

inline void XM_CALLCONV ComputeTransferDelta(const FrameBounds& rBounds, FXMVECTOR vecPosition, int8_t& rDeltaX, int8_t& rDeltaY)
{
	float fPositionX = XMVectorGetX(vecPosition);
	float fPositionY = XMVectorGetY(vecPosition);
	rDeltaX = static_cast<int8_t>((fPositionX > rBounds.fMaxX) ? 1 : (fPositionX < rBounds.fMinX) ? -1 : 0);
	rDeltaY = static_cast<int8_t>((fPositionY > rBounds.fMaxY) ? 1 : (fPositionY < rBounds.fMinY) ? -1 : 0);
}

struct FramePostRender : public engine::FramePostRenderBase
{
	FramePostRender() { transferRequests.reserve(kuiInitialTransferCapacity); }

	// Post render phases
	static void AllocateAndCopy(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious);
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput);

	engine::alignment_t enemyAlignment {};
	engine::alignment_t playerAlignment {};

	PlayersPostRender players {};

	BlastersPostRender blasters {};
	MissilesPostRender missiles {};
	SpaceshipsPostRender spaceships {};
	TargetsPostRender targets {};

	// Transient transfer output buffer (not serialized, not in CRC/equality)
	std::vector<TransferRequest> transferRequests;

	auto Collections(this auto&& rSelf)
	{
		return std::tie(rSelf.blasters, rSelf.missiles, rSelf.spaceships, rSelf.targets);
	}

	inline bool operator==(const FramePostRender& rOther) const
	{
		bool bEqual = true;

		bEqual &= common::BreakOnNotEqual<FramePostRenderBase>(*this, rOther);

		bEqual &= common::BreakOnNotEqual(enemyAlignment, rOther.enemyAlignment);
		bEqual &= common::BreakOnNotEqual(playerAlignment, rOther.playerAlignment);

		bEqual &= common::BreakOnNotEqual(players, rOther.players);
		bEqual &= engine::CompareCollections(std::tie(players), std::tie(rOther.players), std::make_index_sequence<1>{});

		bEqual &= engine::CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

		return bEqual;
	}

	static inline common::crc_t Crc(const FramePostRender& rCurrent)
	{
		common::crc_t checksum = 0;

		checksum ^= static_cast<const engine::FramePostRenderBase&>(rCurrent).Crc();

		checksum ^= common::Crc(rCurrent.enemyAlignment);
		checksum ^= common::Crc(rCurrent.playerAlignment);

		checksum ^= engine::CollectionCrc(rCurrent.players, rCurrent.players.Members());

		std::apply([&](const auto&... cols)
		{
			((checksum ^= engine::CollectionCrc(cols, cols.Members())), ...);
		}, rCurrent.Collections());

		return checksum;
	}

	inline void Write(std::ostream& rStream) const
	{
		static_cast<const engine::FramePostRenderBase&>(*this).Write(rStream);

		enemyAlignment.Write(rStream);
		playerAlignment.Write(rStream);

		engine::CollectionWrite(rStream, players, players.Members());

		std::apply([&](const auto&... cols)
		{
			(engine::CollectionWrite(rStream, cols, cols.Members()), ...);
		}, Collections());
	}

	inline void Read(std::istream& rStream)
	{
		static_cast<engine::FramePostRenderBase&>(*this).Read(rStream);

		enemyAlignment.Read(rStream);
		playerAlignment.Read(rStream);

		engine::CollectionRead(rStream, players, players.Members());

		std::apply([&](auto&... cols)
		{
			(engine::CollectionRead(rStream, cols, cols.Members()), ...);
		}, Collections());
	}
};

struct Frame
{
	static constexpr int64_t kiVersion = 13;

	static constexpr int64_t kiIslandCount = 1;
	static constexpr float kpfIslandPositions[kiIslandCount][4] = {{-100.0f, 100.0f, 200.0f, -200.0f}};

	static constexpr float kfBaseAreaMinX = kpfIslandPositions[0][0];
	static constexpr float kfBaseAreaMaxY = kpfIslandPositions[0][1];
	static constexpr float kfBaseAreaMaxX = kpfIslandPositions[0][0] + kpfIslandPositions[0][2];
	static constexpr float kfBaseAreaMinY = kpfIslandPositions[0][1] + kpfIslandPositions[0][3];

	static [[nodiscard]] target_t XM_CALLCONV GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, engine::alignment_t alignment);

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

// Type aliases derived from Collections() - must be after class definitions are complete
using GameInterpolateTypes = engine::TupleToTypeList_t<decltype(std::declval<FrameInterpolate>().Collections())>;
using GamePostRenderTypes = engine::TupleToTypeList_t<decltype(std::declval<FramePostRender>().Collections())>;

} // namespace game
