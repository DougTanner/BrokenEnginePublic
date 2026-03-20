#include "FrameBase.h"

namespace engine
{

FramePostRenderBase::FramePostRenderBase()
: vecArea(XMVectorSet(game::Frame::kfBaseAreaMinX, game::Frame::kfBaseAreaMaxY, game::Frame::kfBaseAreaMaxX, game::Frame::kfBaseAreaMinY))
{
}

std::pair<common::crc_t, common::crc_t> FrameInterpolateBase::Crcs() const
{
	common::crc_t crc = 0;
	common::crc_t serverCrc = 0;

	common::Crc(frameFlags, crc, serverCrc);
	common::Crc(iTick, crc, serverCrc);
	common::Crc(fCurrentTime, crc, serverCrc);
	common::Crc(fDeltaTime, crc, serverCrc);

	std::apply([&](const auto&... cols)
	{
		((crc ^= CollectionCrc(cols, cols.Members())), ...);
	}, Collections());

	std::apply([&](const auto&... cols)
	{
		((serverCrc ^= ServerCollectionCrc(cols)), ...);
	}, ServerCollections());

	return {crc, serverCrc};
}

bool FrameInterpolateBase::LogDifferences(const FrameInterpolateBase& rOther) const
{
	common::ScopedLogDifferenceContext context("FrameInterpolate");
	bool bEqual = true;
	bEqual &= common::LogDifference<"frameFlags">(frameFlags, rOther.frameFlags);
	bEqual &= common::LogDifference<"iTick">(iTick, rOther.iTick);
	bEqual &= common::LogDifference<"fCurrentTime">(fCurrentTime, rOther.fCurrentTime);
	bEqual &= common::LogDifference<"fDeltaTime">(fDeltaTime, rOther.fDeltaTime);
	bEqual &= explosions.LogDifferences(rOther.explosions);
	bEqual &= pushers.LogDifferences(rOther.pushers);
	return bEqual;
}

void FrameInterpolateBase::Write(std::ostream& rStream) const
{
	common::Write(rStream, frameFlags);
	common::Write(rStream, iTick);
	common::Write(rStream, fCurrentTime);
	common::Write(rStream, fDeltaTime);

	std::apply([&](const auto&... cols)
	{
		(CollectionWrite(rStream, cols, cols.Members()), ...);
	}, Collections());
}

void FrameInterpolateBase::Read(std::istream& rStream)
{
	common::Read(rStream, frameFlags);
	common::Read(rStream, iTick);
	common::Read(rStream, fCurrentTime);
	common::Read(rStream, fDeltaTime);

	std::apply([&](auto&... cols)
	{
		(CollectionRead(rStream, cols, cols.Members()), ...);
	}, Collections());
}

void FrameInterpolateBase::ServerRead(std::istream& rStream)
{
	common::Read(rStream, frameFlags);
	common::Read(rStream, iTick);
	common::Read(rStream, fCurrentTime);
	common::Read(rStream, fDeltaTime);

	std::apply([&](auto&... cols)
	{
		(ServerCollectionRead(rStream, cols), ...);
	}, ServerCollections());
}

std::pair<common::crc_t, common::crc_t> FramePostRenderBase::Crcs() const
{
	common::crc_t outCrc = 0;
	common::crc_t outServerCrc = 0;

	common::crc_t c = randomEngine.Crc();
	outCrc ^= c;
	outServerCrc ^= c;

	common::Crc(vecArea, outCrc, outServerCrc);
	common::Crc(uiNextUuid, outCrc, outServerCrc);
#if defined(BT_CLIENT)
	outCrc ^= common::Crc(uiNextSoundUuid);
	outCrc ^= common::Crc(uiNextVisualUuid);
#endif
	common::Crc(uiFrameId, outCrc, outServerCrc);
	common::Crc(eIslandsFlip, outCrc, outServerCrc);

	c = alignments.Crc();
	outCrc ^= c;
	outServerCrc ^= c;

	std::apply([&](const auto&... cols)
	{
		((outCrc ^= CollectionCrc(cols, cols.Members())), ...);
	}, Collections());

	std::apply([&](const auto&... cols)
	{
		((outServerCrc ^= ServerCollectionCrc(cols)), ...);
	}, ServerCollections());

	return {outCrc, outServerCrc};
}

bool FramePostRenderBase::LogDifferences(const FramePostRenderBase& rOther) const
{
	common::ScopedLogDifferenceContext context("FramePostRender");
	bool bEqual = true;
	bEqual &= common::LogDifference<"randomEngine">(randomEngine, rOther.randomEngine);
	bEqual &= common::LogDifference_Vec("vecArea", vecArea, rOther.vecArea);
	bEqual &= common::LogDifference<"uiNextUuid">(uiNextUuid, rOther.uiNextUuid);
	// Skip uiNextSoundUuid and uiNextVisualUuid (client-only)
	bEqual &= common::LogDifference<"uiFrameId">(uiFrameId, rOther.uiFrameId);
	bEqual &= common::LogDifference<"eIslandsFlip">(static_cast<int>(eIslandsFlip), static_cast<int>(rOther.eIslandsFlip));
	if (!(alignments == rOther.alignments))
	{
		bEqual = false;
		Log(kLogNetwork, "LogDifferences {} alignments differ", common::gpLogDifferenceContext);
	}
	bEqual &= explosions.LogDifferences(rOther.explosions);
	bEqual &= pushers.LogDifferences(rOther.pushers);
	return bEqual;
}

void FramePostRenderBase::Write(std::ostream& rStream) const
{
	common::Write(rStream, randomEngine);
	common::Write(rStream, vecArea);
	common::Write(rStream, uiNextUuid);
#if defined(BT_CLIENT)
	common::Write(rStream, uiNextSoundUuid);
	common::Write(rStream, uiNextVisualUuid);
#endif
	common::Write(rStream, uiFrameId);
	common::Write(rStream, eIslandsFlip);
	alignments.Write(rStream);

	std::apply([&](const auto&... cols)
	{
		(CollectionWrite(rStream, cols, cols.Members()), ...);
	}, Collections());
}

void FramePostRenderBase::Read(std::istream& rStream)
{
	common::Read(rStream, randomEngine);
	common::Read(rStream, vecArea);
	common::Read(rStream, uiNextUuid);
#if defined(BT_CLIENT)
	common::Read(rStream, uiNextSoundUuid);
	common::Read(rStream, uiNextVisualUuid);
#endif
	common::Read(rStream, uiFrameId);
	common::Read(rStream, eIslandsFlip);
	alignments.Read(rStream);

	std::apply([&](auto&... cols)
	{
		(CollectionRead(rStream, cols, cols.Members()), ...);
	}, Collections());
}

void FramePostRenderBase::ServerRead(std::istream& rStream)
{
	common::Read(rStream, randomEngine);
	common::Read(rStream, vecArea);
	common::Read(rStream, uiNextUuid);
	// Server does not write uiNextSoundUuid or uiNextVisualUuid
	common::Read(rStream, uiFrameId);
	common::Read(rStream, eIslandsFlip);
	alignments.Read(rStream);

	std::apply([&](auto&... cols)
	{
		(ServerCollectionRead(rStream, cols), ...);
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
	const FrameInterpolateBase& rPrevious = rPreviousFrame.interpolate;

	// Store delta time in frame
	rCurrent.fDeltaTime = fDeltaTime;

	// Load
	FrameFlags_t frameFlags = rPrevious.frameFlags;

	// Update
	frameFlags.Clear({FrameFlags::kInterpolate, FrameFlags::kPostRender});
	frameFlags.Set(FrameFlags::kInterpolate);

	// Save
	rCurrent.frameFlags = frameFlags;

	ForEachInterpolateUpdate(InterpolateTypes{}, rCurrent, rPreviousFrame);
}

void FramePostRenderBase::AllocateAndCopy([[maybe_unused]] game::FramePostRender& __restrict rCurrent, [[maybe_unused]] const game::FramePostRender& __restrict rPrevious)
{
	FramePostRenderBase& rCurrentBase = rCurrent;
	const FramePostRenderBase& rPreviousBase = rPrevious;
	AllocateAndCopyCollections(rCurrentBase.Collections(), rPreviousBase.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(rCurrentBase.Collections())>>{});
}

void FramePostRenderBase::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const game::FrameInput& __restrict rFrameInput)
{
	game::FramePostRender& rCurrent = rFrame.postRender;
	const game::FramePostRender& rPrevious = rPreviousFrame.postRender;

	rFrame.interpolate.frameFlags.Clear({FrameFlags::kInterpolate, FrameFlags::kPostRender});
	rFrame.interpolate.frameFlags.Set(FrameFlags::kPostRender);

	// Carry persistent state forward from the previous frame
	rCurrent.randomEngine = rPrevious.randomEngine;
	rCurrent.vecArea = rPrevious.vecArea;
	rCurrent.uiNextUuid = rPrevious.uiNextUuid;
#if defined(BT_CLIENT)
	rCurrent.uiNextSoundUuid = rPrevious.uiNextSoundUuid;
	rCurrent.uiNextVisualUuid = rPrevious.uiNextVisualUuid;
#endif
	rCurrent.uiFrameId = rPrevious.uiFrameId;
	rCurrent.eIslandsFlip = rPrevious.eIslandsFlip;
	rCurrent.alignments.CopyFrom(rPrevious.alignments);

	ForEachPostRenderUpdate(PostRenderBaseTypes{}, rFrame, rPreviousFrame);

	// Setup pusher zones for spatial acceleration
	PushersInterpolate::SetupZones(rFrame);
}

void FramePostRenderBase::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ForEachPostRenderPreCollision(PostRenderBaseTypes{}, rFrame, rPreviousFrame);
}

void FramePostRenderBase::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ForEachPostRenderPostCollision(PostRenderBaseTypes{}, rFrame, rPreviousFrame);
}

void FramePostRenderBase::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ForEachPostRenderAreaDamage(PostRenderBaseTypes{}, rFrame, rPreviousFrame);
}

void FramePostRenderBase::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderTransfer(PostRenderBaseTypes{}, rFrame);
}

void FramePostRenderBase::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderDestroy(PostRenderBaseTypes{}, rFrame);
}

void FramePostRenderBase::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
	ForEachPostRenderSpawn(PostRenderBaseTypes{}, rFrame);
}

#if defined(BT_CLIENT)
void FrameInterpolateBase::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	ForEachBeginRender(InterpolateTypes{}, iCommandBuffer, rRenderInterpolates, rActiveCoords);
}

void FrameInterpolateBase::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rCurrent, [[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachInterpolateRender(InterpolateRenderTypes{}, rCurrent, iCommandBuffer);
}

void FrameInterpolateBase::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	ForEachEndRender(InterpolateTypes{}, iCommandBuffer);
}
#endif // BT_CLIENT

} // namespace engine
