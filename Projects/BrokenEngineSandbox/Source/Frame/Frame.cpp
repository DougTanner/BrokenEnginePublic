#include "Frame.h"

#include "Frame/FrameCollections.h"
#include "Frame/SmokeSpreadTest.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum GameFlags;

// FrameInterpolate
FrameInterpolate::FrameInterpolate()
	: pPlayers(std::make_unique<PlayersInterpolate>())
	, pBlasters(std::make_unique<BlastersInterpolate>())
	, pMissiles(std::make_unique<MissilesInterpolate>())
	, pSpaceships(std::make_unique<SpaceshipsInterpolate>())
	, pTargets(std::make_unique<TargetsInterpolate>())
{
}
FrameInterpolate::~FrameInterpolate() = default;
FrameInterpolate::FrameInterpolate(FrameInterpolate&&) noexcept = default;
FrameInterpolate& FrameInterpolate::operator=(FrameInterpolate&&) noexcept = default;

// FramePostRender
FramePostRender::FramePostRender()
	: pPlayers(std::make_unique<PlayersPostRender>())
	, pBlasters(std::make_unique<BlastersPostRender>())
	, pMissiles(std::make_unique<MissilesPostRender>())
	, pSpaceships(std::make_unique<SpaceshipsPostRender>())
	, pTargets(std::make_unique<TargetsPostRender>())
{
	transferRequests.reserve(kuiInitialTransferCapacity);
}
FramePostRender::~FramePostRender() = default;
FramePostRender::FramePostRender(FramePostRender&&) noexcept = default;
FramePostRender& FramePostRender::operator=(FramePostRender&&) noexcept = default;

// Frame
Frame::Frame() = default;
Frame::~Frame() = default;
Frame::Frame(Frame&&) noexcept = default;
Frame& Frame::operator=(Frame&&) noexcept = default;

void FrameInterpolate::Register()
{
	// Parent
	FrameInterpolateBase::Register();

	// Player
	PlayersInterpolate::Register();

	// Collections
	engine::ForEachRegister(GameInterpolateTypes{});
}

#if defined(BT_CLIENT)
void FrameInterpolate::GraphicsResources()
{
	// Parent
	FrameInterpolateBase::GraphicsResources();

	// Player
	PlayersInterpolate::GraphicsResources();

	// Collections
	engine::ForEachGraphicsResources(GameInterpolateTypes{});
}
#endif // BT_CLIENT

void FrameInterpolate::AllocateAndCopy(FrameInterpolate& __restrict rCurrent, const FrameInterpolate& __restrict rPrevious)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateAllocateAndCopy);

	// Parent
	FrameInterpolateBase::AllocateAndCopy(rCurrent, rPrevious);

	// Player
	PlayersInterpolate::AllocateAndCopy(*rCurrent.pPlayers, *rPrevious.pPlayers);

	// Collections
	engine::AllocateAndCopyCollections(GameInterpolateCollections(rCurrent), GameInterpolateCollections(rPrevious), std::make_index_sequence<std::tuple_size_v<decltype(GameInterpolateCollections(rCurrent))>>{});
}

void FrameInterpolate::Update(FrameInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateUpdate);

	const FrameInterpolate& rPrevious = rPreviousFrame.interpolate;

	// Parent
	FrameInterpolateBase::Update(rCurrent, rPreviousFrame, fDeltaTime);

	// Load
	GameFlags_t gameFlags = rPrevious.gameFlags;
	float fSpawnTimer = rPrevious.fSpawnTimer;

	// Update spawn timer
	fSpawnTimer += fDeltaTime;

	// Save
	rCurrent.gameFlags = gameFlags;
	rCurrent.fSpawnTimer = fSpawnTimer;

	// Player
	PlayersInterpolate::Update(rCurrent, rPreviousFrame);

	// Collections
	engine::ForEachInterpolateUpdate(GameInterpolateTypes{}, rCurrent, rPreviousFrame);
}

void FramePostRender::AllocateAndCopy(FramePostRender& __restrict rCurrent, const FramePostRender& __restrict rPrevious)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderAllocateAndCopy);

	// Parent
	engine::FramePostRenderBase::AllocateAndCopy(rCurrent, rPrevious);

	// Player
	PlayersPostRender::AllocateAndCopy(*rCurrent.pPlayers, *rPrevious.pPlayers);

	// Collections
	engine::AllocateAndCopyCollections(GamePostRenderCollections(rCurrent), GamePostRenderCollections(rPrevious), std::make_index_sequence<std::tuple_size_v<decltype(GamePostRenderCollections(rCurrent))>>{});
}

void FramePostRender::Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdate);

	rFrame.postRender.transferRequests.clear();

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, rFrameInput);

	// Propagate game-specific fields
	rFrame.postRender.enemyAlignment = rPreviousFrame.postRender.enemyAlignment;
	rFrame.postRender.playerAlignment = rPreviousFrame.postRender.playerAlignment;

	// Player
	PlayersPostRender::Update(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderUpdate(GamePostRenderTypes{}, rFrame, rPreviousFrame);
}

void FramePostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
	// Parent
	FramePostRenderBase::Transfer(rFrame);

	// Player
	PlayersPostRender::Transfer(rFrame);

	// Collections
	engine::ForEachPostRenderTransfer(GamePostRenderTypes{}, rFrame);
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderDestroy);

	// Parent
	FramePostRenderBase::Destroy(rFrame);

	// Player
	PlayersPostRender::Destroy(rFrame);

	// Collections
	engine::ForEachPostRenderDestroy(GamePostRenderTypes{}, rFrame);
}

static void SpawnSingleSpaceship(Frame& __restrict rFrame)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	constexpr float kfSpawnRadius = 100.0f;

	// Find any alive player to spawn near; skip if no alive players
	XMVECTOR vecPlayerPosition = XMVectorZero();
	bool bFound = false;
	for (int64_t i = 0; i < rInterpolate.pPlayers->iCount; ++i)
	{
		if (!(rFrame.postRender.pPlayers->pFlags[i] & PlayerFlags::kExploding))
		{
			vecPlayerPosition = rInterpolate.pPlayers->pVecPositions[i];
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		return;
	}

	// Random angle around player
	float fAngle = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
	auto vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));
	float fCurrentRadius = kfSpawnRadius;
	auto vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fCurrentRadius), vecDirection, vecPlayerPosition);

	// Avoid islands, retry with expanded radius
	float fTerrainElevation = engine::gpIslandTerrain->GlobalElevation(vecSpawnPosition);
	while (fTerrainElevation > 0.0f)
	{
		fCurrentRadius += 1.0f;
		vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(fCurrentRadius), vecDirection, vecPlayerPosition);
		fTerrainElevation = engine::gpIslandTerrain->GlobalElevation(vecSpawnPosition);
	}

	// If spawn position is outside bounds, spawn on opposite side of player (toward center)
	if (!common::InsideArea(vecSpawnPosition, rFrame.postRender.vecArea))
	{
		vecSpawnPosition = XMVectorMultiplyAdd(XMVectorReplicate(-kfSpawnRadius), vecDirection, vecPlayerPosition);
	}

	XMVECTOR vecDirectionToPlayer = XMVector3Normalize(XMVectorSubtract(vecPlayerPosition, vecSpawnPosition));
	SpaceshipsPostRender::Spawn(rFrame,
	{
		.vecPosition = vecSpawnPosition,
		.vecDirection = vecDirectionToPlayer,
		.alignment = rFrame.postRender.enemyAlignment,
	});

}

void FramePostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderSpawn);

	// Parent
	FramePostRenderBase::Spawn(rFrame);

	// Player
	PlayersPostRender::Spawn(rFrame, rFrameInput);

	// Collections
	engine::ForEachPostRenderSpawn(GamePostRenderTypes{}, rFrame);

	FrameInterpolate& rInterpolate = rFrame.interpolate;
	if (rFrame.interpolate.gameFlags & GameFlags::kMainMenu)
	{
		return;
	}

	if constexpr (kbEnableSmokeSpreadTest)
	{
		constexpr float kfTestSpawnInterval = 1.0f;
		while (rInterpolate.fSpawnTimer >= kfTestSpawnInterval)
		{
			rInterpolate.fSpawnTimer -= kfTestSpawnInterval;
			SmokeSpreadTestSpawnSpaceship(rFrame);
		}
	}
	else
	{
		// Spawn one spaceship every half second
		constexpr float kfSpawnInterval = 0.5f;
		while (rInterpolate.fSpawnTimer >= kfSpawnInterval)
		{
			rInterpolate.fSpawnTimer -= kfSpawnInterval;
			SpawnSingleSpaceship(rFrame);
		}
	}
}

void FramePostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderPreCollision);

	// Parent
	FramePostRenderBase::PreCollision(rFrame, rPreviousFrame);

	// Player
	PlayersPostRender::PreCollision(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderPreCollision(GamePostRenderTypes{}, rFrame, rPreviousFrame);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderPostCollision);

	// Parent
	FramePostRenderBase::PostCollision(rFrame, rPreviousFrame);

	// Player
	PlayersPostRender::PostCollision(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderPostCollision(GamePostRenderTypes{}, rFrame, rPreviousFrame);

	engine::Collision::Clear();
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderAreaDamage);

	// Parent
	FramePostRenderBase::AreaDamage(rFrame, rPreviousFrame);

	// Player
	PlayersPostRender::AreaDamage(rFrame, rPreviousFrame);

	// Collections
	engine::ForEachPostRenderAreaDamage(GamePostRenderTypes{}, rFrame, rPreviousFrame);

	engine::Collision::ClearAreaDamage();
}

[[nodiscard]] target_t Frame::GetMissileTarget(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecDirection, engine::alignment_t alignment)
{
	static constexpr float kfMaxTargetingRange = 45.0f;
	static constexpr float kfMaxTargetingRangeSquared = kfMaxTargetingRange * kfMaxTargetingRange;

	TargetsInterpolate& rTargetsInterpolate = *rFrame.interpolate.pTargets;
	TargetsPostRender& rTargetsPostRender = *rFrame.postRender.pTargets;

	target_t uiTarget {};
	float fSmallestAngle = std::numeric_limits<float>::max();
	uint8_t uiBestSubscribers = std::numeric_limits<uint8_t>::max();

	for (int64_t i = 0; i < rTargetsInterpolate.iCount; ++i)
	{
		TargetFlags_t flags = rTargetsPostRender.pFlags[i];

		// Must be a destination and a valid enemy target
		if (!(flags & TargetFlags::kDestination) || !rFrame.postRender.alignments.CanCollide(alignment, rTargetsPostRender.pAlignments[i]))
		{
			continue;
		}

		XMVECTOR vecTargetPosition = rTargetsInterpolate.pVecPositions[i];

		// Skip targets not visible to the player
		if (!FrameInterpolate::IsVisible(vecPosition, vecTargetPosition))
		{
			continue;
		}

		XMVECTOR vecToTarget = XMVectorSubtract(vecTargetPosition, vecPosition);
		float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(vecToTarget));

		// Skip targets beyond maximum targeting range
		if (fDistanceSquared > kfMaxTargetingRangeSquared)
		{
			continue;
		}

		XMVECTOR vecToTargetNormal = XMVector3Normalize(vecToTarget);
		float fAngle = std::abs(XMVectorGetX(XMVector3AngleBetweenNormals(vecDirection, vecToTargetNormal)));

		uint8_t uiSubscribers = rTargetsPostRender.puiSubscribers[i];
		if (uiSubscribers < uiBestSubscribers ||
		    (uiSubscribers == uiBestSubscribers && fAngle < fSmallestAngle))
		{
			uiBestSubscribers = uiSubscribers;
			fSmallestAngle = fAngle;
			uiTarget = rTargetsPostRender.puiIds[i];
		}
	}

	if (uiTarget.IsValid())
	{
		TargetsPostRender::AddSubscriber(rFrame, uiTarget);
	}

	return uiTarget;
}

#if defined(BT_CLIENT)
void FrameInterpolate::BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords)
{
	// Parent
	engine::FrameInterpolateBase::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);

	// Player
	PlayersInterpolate::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);

	// Collections
	engine::ForEachBeginRender(GameInterpolateTypes{}, iCommandBuffer, rRenderInterpolates, rActiveCoords);
}

void FrameInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::ScopedCpuProfile scopedCpuProfile(kCpuTimerRender);

	// Parent (excludes SmokeTrails/WindTrails which are called separately with uiFrameId)
	engine::FrameInterpolateBase::Render(rFrameInterpolate, iCommandBuffer);

	// Player
	PlayersInterpolate::Render(rFrameInterpolate, iCommandBuffer);

	// Collections
	engine::ForEachInterpolateRender(GameInterpolateTypes{}, rFrameInterpolate, iCommandBuffer);
}

void FrameInterpolate::EndRender(int64_t iCommandBuffer)
{
	// Parent
	engine::FrameInterpolateBase::EndRender(iCommandBuffer);

	// Player
	PlayersInterpolate::EndRender(iCommandBuffer);

	// Collections
	engine::ForEachEndRender(GameInterpolateTypes{}, iCommandBuffer);
}
#endif // BT_CLIENT

common::crc_t FrameInterpolate::Crc(const FrameInterpolate& rCurrent)
{
	common::crc_t checksum = 0;

	checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).Crc();

	checksum ^= common::Crc(rCurrent.gameFlags);
	checksum ^= common::Crc(rCurrent.fSpawnTimer);

	checksum ^= engine::CollectionCrc(*rCurrent.pPlayers, rCurrent.pPlayers->Members());

	std::apply([&](const auto&... cols)
	{
		((checksum ^= engine::CollectionCrc(cols, cols.Members())), ...);
	}, GameInterpolateCollections(rCurrent));

	return checksum;
}

common::crc_t FrameInterpolate::ServerCrc(const FrameInterpolate& rCurrent)
{
	common::crc_t checksum = 0;

	checksum ^= static_cast<const engine::FrameInterpolateBase&>(rCurrent).ServerCrc();

	checksum ^= common::Crc(rCurrent.gameFlags);
	checksum ^= common::Crc(rCurrent.fSpawnTimer);

	checksum ^= engine::CollectionCrc(*rCurrent.pPlayers, rCurrent.pPlayers->ServerCrcMembers());

	std::apply([&](const auto&... cols)
	{
		((checksum ^= engine::ServerCollectionCrc(cols)), ...);
	}, GameInterpolateCollections(rCurrent));

	return checksum;
}

bool FrameInterpolate::LogDifferences(const FrameInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("FrameInterpolate");
	bool bEqual = true;
	bEqual &= static_cast<const engine::FrameInterpolateBase&>(*this).LogDifferences(
		static_cast<const engine::FrameInterpolateBase&>(rOther));
	bEqual &= common::LogDifference<"fSpawnTimer">(fSpawnTimer, rOther.fSpawnTimer);
	bEqual &= common::LogDifference<"gameFlags">(gameFlags, rOther.gameFlags);
	bEqual &= pPlayers->LogDifferences(*rOther.pPlayers);
	bEqual &= pBlasters->LogDifferences(*rOther.pBlasters);
	bEqual &= pMissiles->LogDifferences(*rOther.pMissiles);
	bEqual &= pSpaceships->LogDifferences(*rOther.pSpaceships);
	bEqual &= pTargets->LogDifferences(*rOther.pTargets);
	return bEqual;
}

void FrameInterpolate::Write(std::ostream& rStream) const
{
	static_cast<const engine::FrameInterpolateBase&>(*this).Write(rStream);

	common::Write(rStream, fSpawnTimer);
	common::Write(rStream, gameFlags);

	engine::CollectionWrite(rStream, *pPlayers, pPlayers->Members());

	std::apply([&](const auto&... cols)
	{
		(engine::CollectionWrite(rStream, cols, cols.Members()), ...);
	}, GameInterpolateCollections(*this));
}

void FrameInterpolate::Read(std::istream& rStream)
{
	static_cast<engine::FrameInterpolateBase&>(*this).Read(rStream);

	common::Read(rStream, fSpawnTimer);
	common::Read(rStream, gameFlags);

	engine::CollectionRead(rStream, *pPlayers, pPlayers->Members());

	std::apply([&](auto&... cols)
	{
		(engine::CollectionRead(rStream, cols, cols.Members()), ...);
	}, GameInterpolateCollections(*this));
}

void FrameInterpolate::ServerRead(std::istream& rStream)
{
	static_cast<engine::FrameInterpolateBase&>(*this).ServerRead(rStream);

	common::Read(rStream, fSpawnTimer);
	common::Read(rStream, gameFlags);

	engine::ServerCollectionRead(rStream, *pPlayers);

	std::apply([&](auto&... cols)
	{
		(engine::ServerCollectionRead(rStream, cols), ...);
	}, GameInterpolateCollections(*this));
}

common::crc_t FramePostRender::Crc(const FramePostRender& rCurrent)
{
	common::crc_t checksum = 0;

	checksum ^= static_cast<const engine::FramePostRenderBase&>(rCurrent).Crc();

	checksum ^= common::Crc(rCurrent.enemyAlignment);
	checksum ^= common::Crc(rCurrent.playerAlignment);

	checksum ^= engine::CollectionCrc(*rCurrent.pPlayers, rCurrent.pPlayers->Members());

	std::apply([&](const auto&... cols)
	{
		((checksum ^= engine::CollectionCrc(cols, cols.Members())), ...);
	}, GamePostRenderCollections(rCurrent));

	return checksum;
}

common::crc_t FramePostRender::ServerCrc(const FramePostRender& rCurrent)
{
	common::crc_t checksum = 0;

	checksum ^= static_cast<const engine::FramePostRenderBase&>(rCurrent).ServerCrc();

	checksum ^= common::Crc(rCurrent.enemyAlignment);
	checksum ^= common::Crc(rCurrent.playerAlignment);

	checksum ^= engine::CollectionCrc(*rCurrent.pPlayers, rCurrent.pPlayers->Members());

	std::apply([&](const auto&... cols)
	{
		((checksum ^= engine::ServerCollectionCrc(cols)), ...);
	}, GamePostRenderCollections(rCurrent));

	return checksum;
}

bool FramePostRender::LogDifferences(const FramePostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("FramePostRender");
	bool bEqual = true;
	bEqual &= static_cast<const engine::FramePostRenderBase&>(*this).LogDifferences(
		static_cast<const engine::FramePostRenderBase&>(rOther));
	bEqual &= common::LogDifference<"enemyAlignment">(enemyAlignment, rOther.enemyAlignment);
	bEqual &= common::LogDifference<"playerAlignment">(playerAlignment, rOther.playerAlignment);
	bEqual &= pPlayers->LogDifferences(*rOther.pPlayers);
	bEqual &= pBlasters->LogDifferences(*rOther.pBlasters);
	bEqual &= pMissiles->LogDifferences(*rOther.pMissiles);
	bEqual &= pSpaceships->LogDifferences(*rOther.pSpaceships);
	bEqual &= pTargets->LogDifferences(*rOther.pTargets);
	return bEqual;
}

void FramePostRender::Write(std::ostream& rStream) const
{
	static_cast<const engine::FramePostRenderBase&>(*this).Write(rStream);

	enemyAlignment.Write(rStream);
	playerAlignment.Write(rStream);

	engine::CollectionWrite(rStream, *pPlayers, pPlayers->Members());

	std::apply([&](const auto&... cols)
	{
		(engine::CollectionWrite(rStream, cols, cols.Members()), ...);
	}, GamePostRenderCollections(*this));
}

void FramePostRender::Read(std::istream& rStream)
{
	static_cast<engine::FramePostRenderBase&>(*this).Read(rStream);

	enemyAlignment.Read(rStream);
	playerAlignment.Read(rStream);

	engine::CollectionRead(rStream, *pPlayers, pPlayers->Members());

	std::apply([&](auto&... cols)
	{
		(engine::CollectionRead(rStream, cols, cols.Members()), ...);
	}, GamePostRenderCollections(*this));
}

void FramePostRender::ServerRead(std::istream& rStream)
{
	static_cast<engine::FramePostRenderBase&>(*this).ServerRead(rStream);

	enemyAlignment.Read(rStream);
	playerAlignment.Read(rStream);

	engine::ServerCollectionRead(rStream, *pPlayers);

	std::apply([&](auto&... cols)
	{
		(engine::ServerCollectionRead(rStream, cols), ...);
	}, GamePostRenderCollections(*this));
}

common::crc_t Frame::Crc() const
{
	common::crc_t checksum = 0;
	checksum ^= FrameInterpolate::Crc(interpolate);
	checksum ^= FramePostRender::Crc(postRender);
	return checksum;
}

common::crc_t Frame::ServerCrc() const
{
	common::crc_t checksum = 0;
	checksum ^= FrameInterpolate::ServerCrc(interpolate);
	checksum ^= FramePostRender::ServerCrc(postRender);
	return checksum;
}

bool Frame::LogDifferences(const Frame& rOther) const
{
	bool bEqual = true;
	bEqual &= interpolate.LogDifferences(rOther.interpolate);
	bEqual &= postRender.LogDifferences(rOther.postRender);
	return bEqual;
}

void Frame::ServerRead(std::istream& rStream)
{
	interpolate.ServerRead(rStream);
	postRender.ServerRead(rStream);
}

std::ostream& operator<<(std::ostream& rStream, const Frame& rCurrent)
{
	rCurrent.interpolate.Write(rStream);
	rCurrent.postRender.Write(rStream);
	return rStream;
}

std::istream& operator>>(std::istream& rStream, Frame& rCurrent)
{
	rCurrent.interpolate.Read(rStream);
	rCurrent.postRender.Read(rStream);
	return rStream;
}

} // namespace game
