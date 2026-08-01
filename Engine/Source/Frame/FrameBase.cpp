#include "FrameBase.h"

#include "Frame/Frame.h"

namespace engine
{

common::crc_t FrameInterpolateBase::Crcs() const
{
	common::crc_t sharedCrc = 0;

	sharedCrc = (sharedCrc ^ common::Crc(iTick)) * common::kCrcMultiplier;
	sharedCrc = (sharedCrc ^ common::Crc(fCurrentTime)) * common::kCrcMultiplier;
	sharedCrc = (sharedCrc ^ common::Crc(fDeltaTime)) * common::kCrcMultiplier;

	std::apply([&](const auto&... collections)
	{
		((sharedCrc = (sharedCrc ^ SharedCollectionCrc(collections)) * common::kCrcMultiplier), ...);
	}, ServerCollections());

	return sharedCrc;
}

bool FrameInterpolateBase::LogDifferences(const FrameInterpolateBase& rOther) const
{
	common::ScopedLogDifferenceContext context("FrameInterpolate");
	bool bEqual = true;
	bEqual &= common::LogDifference<"iTick">(iTick, rOther.iTick);
	bEqual &= common::LogDifference<"fCurrentTime">(fCurrentTime, rOther.fCurrentTime);
	bEqual &= common::LogDifference<"fDeltaTime">(fDeltaTime, rOther.fDeltaTime);
	bEqual &= LogDifferencesCollections(ServerCollections(), rOther.ServerCollections(), std::make_index_sequence<std::tuple_size_v<decltype(ServerCollections())>>{});
	return bEqual;
}

void FrameInterpolateBase::Write(std::ostream& rStream) const
{
	common::Write(rStream, iTick);
	common::Write(rStream, fCurrentTime);
	common::Write(rStream, fDeltaTime);

	std::apply([&](const auto&... collections)
	{
		(CollectionWrite(rStream, collections, collections.Members()), ...);
	}, Collections());
}

void FrameInterpolateBase::Read(std::istream& rStream)
{
	common::Read(rStream, iTick);
	common::Read(rStream, fCurrentTime);
	common::Read(rStream, fDeltaTime);

	std::apply([&](auto&... collections)
	{
		(CollectionRead(rStream, collections, collections.Members()), ...);
	}, Collections());
}

void FrameInterpolateBase::ServerRead(std::istream& rStream)
{
	common::Read(rStream, iTick);
	common::Read(rStream, fCurrentTime);
	common::Read(rStream, fDeltaTime);

	std::apply([&](auto&... collections)
	{
		(SharedCollectionRead(rStream, collections), ...);
	}, ServerCollections());
}

common::crc_t FramePostRenderBase::Crcs() const
{
	common::crc_t crc = 0;

	crc = (crc ^ randomEngine.Crc()) * common::kCrcMultiplier;
	crc = (crc ^ common::Crc(uiNextUuid)) * common::kCrcMultiplier;
	crc = (crc ^ common::Crc(uiFrameId)) * common::kCrcMultiplier;
	crc = (crc ^ alignments.Crc()) * common::kCrcMultiplier;

	std::apply([&](const auto&... collections)
	{
		((crc = (crc ^ SharedCollectionCrc(collections)) * common::kCrcMultiplier), ...);
	}, ServerCollections());

	return crc;
}

bool FramePostRenderBase::LogDifferences(const FramePostRenderBase& rOther) const
{
	common::ScopedLogDifferenceContext context("FramePostRender");
	bool bEqual = true;
	bEqual &= common::LogDifference<"randomEngine">(randomEngine, rOther.randomEngine);
	bEqual &= common::LogDifference<"uiNextUuid">(uiNextUuid, rOther.uiNextUuid);
	// Skip uiNextSoundUuid and uiNextVisualUuid (client-only)
	bEqual &= common::LogDifference<"uiFrameId">(uiFrameId, rOther.uiFrameId);
	if (!(alignments == rOther.alignments))
	{
		bEqual = false;
		LOG(kNetwork, kError, "LogDifferences {} alignments differ", common::gpLogDifferenceContext);
	}
	bEqual &= LogDifferencesCollections(ServerCollections(), rOther.ServerCollections(), std::make_index_sequence<std::tuple_size_v<decltype(ServerCollections())>>{});
	return bEqual;
}

// Write/Read serialize the client-only UUID counters under BT_CLIENT, so the two builds'
// stream layouts differ structurally — a client-written stream is unreadable by a server
// Read (and vice versa). Safe today only because save/load is server-only
// (game::GameSaveLoad); the guard is convention, not structure. ServerRead handles the
// cross-build (network) direction and documents the skip.
void FramePostRenderBase::Write(std::ostream& rStream) const
{
	common::Write(rStream, randomEngine);
	common::Write(rStream, uiNextUuid);
#if defined(BT_CLIENT)
	common::Write(rStream, uiNextSoundUuid);
	common::Write(rStream, uiNextVisualUuid);
#endif
	common::Write(rStream, uiFrameId);
	alignments.Write(rStream);

	std::apply([&](const auto&... collections)
	{
		(CollectionWrite(rStream, collections, collections.Members()), ...);
	}, Collections());
}

void FramePostRenderBase::Read(std::istream& rStream)
{
	uint64_t uiRandomState = 0;
	common::Read(rStream, uiRandomState);
	randomEngine.SetSerializedState(uiRandomState);
	common::Read(rStream, uiNextUuid);
#if defined(BT_CLIENT)
	common::Read(rStream, uiNextSoundUuid);
	common::Read(rStream, uiNextVisualUuid);
#endif
	common::Read(rStream, uiFrameId);
	alignments.Read(rStream);

	std::apply([&](auto&... collections)
	{
		(CollectionRead(rStream, collections, collections.Members()), ...);
	}, Collections());
}

void FramePostRenderBase::ServerRead(std::istream& rStream)
{
	uint64_t uiRandomState = 0;
	common::Read(rStream, uiRandomState);
	randomEngine.SetSerializedState(uiRandomState);
	common::Read(rStream, uiNextUuid);
	// Server does not write uiNextSoundUuid or uiNextVisualUuid
	common::Read(rStream, uiFrameId);
	alignments.Read(rStream);

	std::apply([&](auto&... collections)
	{
		(SharedCollectionRead(rStream, collections), ...);
	}, ServerCollections());
}

void FrameInterpolateBase::Register()
{
	ForEachRegister(InterpolateTypes{});
}

#if defined(BT_CLIENT)
void FrameInterpolateBase::GraphicsResources()
{
	ForEachGraphicsResources(InterpolateTypes{});
}
#endif

void FrameInterpolateBase::AllocateAndCopy([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::FrameInterpolate& __restrict rPrevious)
{
	FrameInterpolateBase& rCurrentBase = rCurrent;
	const FrameInterpolateBase& rPreviousBase = rPrevious;
	AllocateAndCopyCollections(rCurrentBase.Collections(), rPreviousBase.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrentBase.Collections())>>{});
}

void FrameInterpolateBase::Update([[maybe_unused]] game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	// Store delta time in frame
	rCurrent.fDeltaTime = fDeltaTime;

#if defined(BT_CLIENT)
	// kRecalculated propagates through the rPrevious chain: Reconcile pre-stamps it on each
	// replay pNext (ReconcileReplay.cpp), and clears it on validated/catch-up snapshots so
	// normal-tick rPrevious never carries the bit. Reading rCurrent.frameFlags here would
	// surface stale ring-memory from a prior replay use of the slot — silencing audio.
	const FrameInterpolateBase& rPrevious = rPreviousFrame.interpolate;
	FrameFlags_t frameFlags = rPrevious.frameFlags;
	frameFlags.Clear({FrameFlags::kInterpolate, FrameFlags::kPostRender});
	frameFlags.Set(FrameFlags::kInterpolate);
	rCurrent.frameFlags = frameFlags;
#endif

	ForEachInterpolateUpdate(InterpolateTypes{}, rCurrent, rPreviousFrame);
}

void FramePostRenderBase::AllocateAndCopy([[maybe_unused]] game::FramePostRender& __restrict rCurrent, [[maybe_unused]] const game::FramePostRender& __restrict rPrevious)
{
	FramePostRenderBase& rCurrentBase = rCurrent;
	const FramePostRenderBase& rPreviousBase = rPrevious;
	AllocateAndCopyCollections(rCurrentBase.Collections(), rPreviousBase.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrentBase.Collections())>>{});
}

void FramePostRenderBase::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	game::FramePostRender& rCurrent = rFrame.postRender;
	const game::FramePostRender& rPrevious = rPreviousFrame.postRender;

#if defined(BT_CLIENT)
	rFrame.interpolate.frameFlags.Clear({FrameFlags::kInterpolate, FrameFlags::kPostRender});
	rFrame.interpolate.frameFlags.Set(FrameFlags::kPostRender);
#endif

	// Carry persistent state forward from the previous frame
	rCurrent.randomEngine = rPrevious.randomEngine;
	rCurrent.uiNextUuid = rPrevious.uiNextUuid;
#if defined(BT_CLIENT)
	rCurrent.uiNextSoundUuid = rPrevious.uiNextSoundUuid;
	rCurrent.uiNextVisualUuid = rPrevious.uiNextVisualUuid;
#endif
	rCurrent.uiFrameId = rPrevious.uiFrameId;
	rCurrent.alignments.CopyFrom(rPrevious.alignments);

	ForEachPostRenderUpdate(PostRenderBaseTypes{}, rFrame, rPreviousFrame, rStaticData);

	// Setup pusher zones for spatial acceleration
	PushersInterpolate::SetupZones(rFrame);
}

void FramePostRenderBase::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ForEachPostRenderPreCollision(PostRenderBaseTypes{}, rFrame, rPreviousFrame, rStaticData);
}

void FramePostRenderBase::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ForEachPostRenderPostCollision(PostRenderBaseTypes{}, rFrame, rPreviousFrame, rStaticData);
}

void FramePostRenderBase::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ForEachPostRenderAreaDamage(PostRenderBaseTypes{}, rFrame, rPreviousFrame, rStaticData);
}

void FramePostRenderBase::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ForEachPostRenderTransfer(PostRenderBaseTypes{}, rFrame, rStaticData);
}

void FramePostRenderBase::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ForEachPostRenderDestroy(PostRenderBaseTypes{}, rFrame, rStaticData);
}

void FramePostRenderBase::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	ForEachPostRenderSpawn(PostRenderBaseTypes{}, rFrame, rStaticData);
}

#if defined(BT_CLIENT)
void FrameInterpolateBase::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	ForEachBeginRender(InterpolateTypes{}, iCommandBuffer, rRenderInterpolates, rActiveCoords);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachInterpolateRender(InterpolateTypes{}, rCurrent, iCommandBuffer);
}

void FrameInterpolateBase::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachEndRender(InterpolateTypes{}, iCommandBuffer);
}
#endif // BT_CLIENT

} // namespace engine
