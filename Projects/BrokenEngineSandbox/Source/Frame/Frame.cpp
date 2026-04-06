#include "Frame.h"

#include "Frame/FrameCollections.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum GameFlags;

const int64_t Frame::kiVersion = 26 + engine::kiNavDataVersion
	+ BlastersInterpolate::kiVersion
	+ BlastersPostRender::kiVersion
	+ MissilesInterpolate::kiVersion
	+ MissilesPostRender::kiVersion
	+ PlayersInterpolate::kiVersion
	+ PlayersPostRender::kiVersion
	+ SpaceshipsInterpolate::kiVersion
	+ SpaceshipsPostRender::kiVersion
	+ TargetsInterpolate::kiVersion
	+ TargetsPostRender::kiVersion;

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

void FramePostRender::Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, const engine::FrameStaticData& rStaticData)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdate);

	rFrame.postRender.transferRequests.clear();

	// Parent
	FramePostRenderBase::Update(rFrame, rPreviousFrame, rFrameInput, rStaticData);

	// Propagate game-specific fields
	rFrame.postRender.enemyAlignment = rPreviousFrame.postRender.enemyAlignment;
	rFrame.postRender.playerAlignment = rPreviousFrame.postRender.playerAlignment;

	// Player
	PlayersPostRender::Update(rFrame, rPreviousFrame, rStaticData);

	// Collections
	engine::ForEachPostRenderUpdate(GamePostRenderTypes{}, rFrame, rPreviousFrame, rStaticData);
}

void FramePostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	// Parent
	FramePostRenderBase::Transfer(rFrame, rStaticData);

	// Player
	PlayersPostRender::Transfer(rFrame, rStaticData);

	// Collections
	engine::ForEachPostRenderTransfer(GamePostRenderTypes{}, rFrame, rStaticData);
}

void FramePostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderDestroy);

	// Parent
	FramePostRenderBase::Destroy(rFrame, rStaticData);

	// Player
	PlayersPostRender::Destroy(rFrame, rStaticData);

	// Collections
	engine::ForEachPostRenderDestroy(GamePostRenderTypes{}, rFrame, rStaticData);
}

static void SpawnSingleSpaceship(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData, int64_t iPlayerIndex)
{
	FrameInterpolate& rInterpolate = rFrame.interpolate;

	constexpr float kfSpawnRadius = 100.0f;

	// Skip if this player is exploding
	if (rFrame.postRender.pPlayers->pFlags[iPlayerIndex] & PlayerFlags::kExploding)
	{
		return;
	}
	XMVECTOR vecPlayerPosition = rInterpolate.pPlayers->pVecPositions[iPlayerIndex];

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
	if (!common::InsideArea(vecSpawnPosition, rStaticData.vecArea))
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

void FramePostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderSpawn);

	// Parent
	FramePostRenderBase::Spawn(rFrame, rStaticData);

	// Player
	PlayersPostRender::Spawn(rFrame, rFrameInput, rStaticData);

	// Collections
	engine::ForEachPostRenderSpawn(GamePostRenderTypes{}, rFrame, rStaticData);

	FrameInterpolate& rInterpolate = rFrame.interpolate;
	if (rFrame.interpolate.gameFlags & GameFlags::kMainMenu)
	{
		return;
	}

	// Spawn one spaceship per player every half second
	constexpr float kfSpawnInterval = 0.5f;
	while (rInterpolate.fSpawnTimer >= kfSpawnInterval)
	{
		rInterpolate.fSpawnTimer -= kfSpawnInterval;
		for (int64_t i = 0; i < rInterpolate.pPlayers->iCount; ++i)
		{
			SpawnSingleSpaceship(rFrame, rStaticData, i);
		}
	}
}

void FramePostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderPreCollision);

	// Parent
	FramePostRenderBase::PreCollision(rFrame, rPreviousFrame, rStaticData);

	// Player
	PlayersPostRender::PreCollision(rFrame, rPreviousFrame, rStaticData);

	// Collections
	engine::ForEachPostRenderPreCollision(GamePostRenderTypes{}, rFrame, rPreviousFrame, rStaticData);
}

void FramePostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderPostCollision);

	// Parent
	FramePostRenderBase::PostCollision(rFrame, rPreviousFrame, rStaticData);

	// Player
	PlayersPostRender::PostCollision(rFrame, rPreviousFrame, rStaticData);

	// Collections
	engine::ForEachPostRenderPostCollision(GamePostRenderTypes{}, rFrame, rPreviousFrame, rStaticData);

	engine::Collision::Clear();
}

void FramePostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderAreaDamage);

	// Parent
	FramePostRenderBase::AreaDamage(rFrame, rPreviousFrame, rStaticData);

	// Player
	PlayersPostRender::AreaDamage(rFrame, rPreviousFrame, rStaticData);

	// Collections
	engine::ForEachPostRenderAreaDamage(GamePostRenderTypes{}, rFrame, rPreviousFrame, rStaticData);

	engine::AreaDamage::Clear();
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

common::crc_t FrameInterpolate::Crcs(const FrameInterpolate& rCurrent)
{
	common::crc_t sharedCrc = static_cast<const engine::FrameInterpolateBase&>(rCurrent).Crcs();

	sharedCrc ^= common::Crc(rCurrent.gameFlags);
	sharedCrc ^= common::Crc(rCurrent.fSpawnTimer);

	sharedCrc ^= engine::CollectionCrc(*rCurrent.pPlayers, rCurrent.pPlayers->SharedCrcMembers());

	std::apply([&](const auto&... cols)
	{
		((sharedCrc ^= engine::SharedCollectionCrc(cols)), ...);
	}, GameInterpolateCollections(rCurrent));

	return sharedCrc;
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

	engine::SharedCollectionRead(rStream, *pPlayers);

	std::apply([&](auto&... cols)
	{
		(engine::SharedCollectionRead(rStream, cols), ...);
	}, GameInterpolateCollections(*this));
}

common::crc_t FramePostRender::Crcs(const FramePostRender& rCurrent)
{
	common::crc_t sharedCrc = static_cast<const engine::FramePostRenderBase&>(rCurrent).Crcs();

	sharedCrc ^= common::Crc(rCurrent.enemyAlignment);
	sharedCrc ^= common::Crc(rCurrent.playerAlignment);

	sharedCrc ^= engine::CollectionCrc(*rCurrent.pPlayers, rCurrent.pPlayers->SharedCrcMembers());

	std::apply([&](const auto&... cols)
	{
		((sharedCrc ^= engine::SharedCollectionCrc(cols)), ...);
	}, GamePostRenderCollections(rCurrent));

	return sharedCrc;
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

	engine::SharedCollectionRead(rStream, *pPlayers);

	std::apply([&](auto&... cols)
	{
		(engine::SharedCollectionRead(rStream, cols), ...);
	}, GamePostRenderCollections(*this));
}

common::crc_t Frame::Crcs() const
{
	return FrameInterpolate::Crcs(interpolate) ^ FramePostRender::Crcs(postRender);
}

common::crc_t Frame::Crc() const
{
	return Crcs();
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
