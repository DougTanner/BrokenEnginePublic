#include "FrameBase.h"

#include "Frame/Player.h"

namespace engine
{

FrameInterpolateBase::FrameInterpolateBase()
{
}

FramePostRenderBase::FramePostRenderBase()
: vecArea(XMVectorSet(game::Frame::kfBaseAreaMinX, game::Frame::kfBaseAreaMaxY, game::Frame::kfBaseAreaMaxX, game::Frame::kfBaseAreaMinY))
{
}

bool FrameInterpolateBase::operator==(const FrameInterpolateBase& rOther) const
{
	bool bEqual = true;

	bEqual &= common::BreakOnNotEqual(frameFlags, rOther.frameFlags);
	bEqual &= common::BreakOnNotEqual(iFrame, rOther.iFrame);
	bEqual &= common::BreakOnNotEqual(fCurrentTime, rOther.fCurrentTime);
	bEqual &= common::BreakOnNotEqual(fDeltaTime, rOther.fDeltaTime);

	bEqual &= CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

	return bEqual;
}

common::crc_t FrameInterpolateBase::Crc() const
{
	common::crc_t checksum = 0;

	checksum ^= common::Crc(frameFlags);
	checksum ^= common::Crc(iFrame);
	checksum ^= common::Crc(fCurrentTime);
	checksum ^= common::Crc(fDeltaTime);

	std::apply([&](const auto&... cols)
	{
		((checksum ^= CollectionCrc(cols, cols.Members())), ...);
	}, Collections());

	return checksum;
}

common::crc_t FrameInterpolateBase::ServerCrc() const
{
	common::crc_t checksum = 0;

	checksum ^= common::Crc(frameFlags);
	checksum ^= common::Crc(iFrame);
	checksum ^= common::Crc(fCurrentTime);
	checksum ^= common::Crc(fDeltaTime);

	std::apply([&](const auto&... cols)
	{
		((checksum ^= ServerCollectionCrc(cols)), ...);
	}, ServerCollections());

	return checksum;
}

bool FrameInterpolateBase::ServerCompare(const FrameInterpolateBase& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual(frameFlags, rOther.frameFlags);
	bEqual &= common::BreakOnNotEqual(iFrame, rOther.iFrame);
	bEqual &= common::BreakOnNotEqual(fCurrentTime, rOther.fCurrentTime);
	bEqual &= common::BreakOnNotEqual(fDeltaTime, rOther.fDeltaTime);
	bEqual &= explosions.ServerCompare(rOther.explosions);
	bEqual &= pushers.ServerCompare(rOther.pushers);
	return bEqual;
}

void FrameInterpolateBase::Write(std::ostream& rStream) const
{
	common::Write(rStream, frameFlags);
	common::Write(rStream, iFrame);
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
	common::Read(rStream, iFrame);
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
	common::Read(rStream, iFrame);
	common::Read(rStream, fCurrentTime);
	common::Read(rStream, fDeltaTime);

	std::apply([&](auto&... cols)
	{
		(ServerCollectionRead(rStream, cols), ...);
	}, ServerCollections());
}

bool FramePostRenderBase::operator==(const FramePostRenderBase& rOther) const
{
	bool bEqual = true;

	bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
	bEqual &= common::BreakOnNotEqual(vecArea, rOther.vecArea);
	bEqual &= common::BreakOnNotEqual(uiNextUuid, rOther.uiNextUuid);
#ifdef BT_CLIENT
	bEqual &= common::BreakOnNotEqual(uiNextSoundUuid, rOther.uiNextSoundUuid);
	bEqual &= common::BreakOnNotEqual(uiNextVisualUuid, rOther.uiNextVisualUuid);
#endif
	bEqual &= common::BreakOnNotEqual(uiFrameId, rOther.uiFrameId);
	bEqual &= common::BreakOnNotEqual(eIslandsFlip, rOther.eIslandsFlip);
	bEqual &= common::BreakOnNotEqual(alignments, rOther.alignments);

	bEqual &= CompareCollections(Collections(), rOther.Collections(), std::make_index_sequence<std::tuple_size_v<decltype(Collections())>>{});

	return bEqual;
}

common::crc_t FramePostRenderBase::Crc() const
{
	common::crc_t checksum = 0;

	checksum ^= randomEngine.Crc();
	checksum ^= common::Crc(vecArea);
	checksum ^= common::Crc(uiNextUuid);
#ifdef BT_CLIENT
	checksum ^= common::Crc(uiNextSoundUuid);
	checksum ^= common::Crc(uiNextVisualUuid);
#endif
	checksum ^= common::Crc(uiFrameId);
	checksum ^= common::Crc(eIslandsFlip);
	checksum ^= alignments.Crc();

	std::apply([&](const auto&... cols)
	{
		((checksum ^= CollectionCrc(cols, cols.Members())), ...);
	}, Collections());

	return checksum;
}

common::crc_t FramePostRenderBase::ServerCrc() const
{
	common::crc_t checksum = 0;

	checksum ^= randomEngine.Crc();
	checksum ^= common::Crc(vecArea);
	checksum ^= common::Crc(uiNextUuid);
	checksum ^= common::Crc(uiFrameId);
	checksum ^= common::Crc(eIslandsFlip);
	checksum ^= alignments.Crc();

	std::apply([&](const auto&... cols)
	{
		((checksum ^= ServerCollectionCrc(cols)), ...);
	}, ServerCollections());

	return checksum;
}

bool FramePostRenderBase::ServerCompare(const FramePostRenderBase& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual(randomEngine, rOther.randomEngine);
	bEqual &= common::BreakOnNotEqual(vecArea, rOther.vecArea);
	bEqual &= common::BreakOnNotEqual(uiNextUuid, rOther.uiNextUuid);
	// Skip uiNextSoundUuid and uiNextVisualUuid (client-only)
	bEqual &= common::BreakOnNotEqual(uiFrameId, rOther.uiFrameId);
	bEqual &= common::BreakOnNotEqual(eIslandsFlip, rOther.eIslandsFlip);
	bEqual &= common::BreakOnNotEqual(alignments, rOther.alignments);
	bEqual &= explosions.ServerCompare(rOther.explosions);
	bEqual &= pushers.ServerCompare(rOther.pushers);
	return bEqual;
}

void FramePostRenderBase::Write(std::ostream& rStream) const
{
	common::Write(rStream, randomEngine);
	common::Write(rStream, vecArea);
	common::Write(rStream, uiNextUuid);
#ifdef BT_CLIENT
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
#ifdef BT_CLIENT
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

#ifdef BT_CLIENT
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

	// Load
	common::RandomEngine randomEngine = rPrevious.randomEngine;
	XMVECTOR vecArea = rPrevious.vecArea;
	uint64_t uiNextUuid = rPrevious.uiNextUuid;
#ifdef BT_CLIENT
	uint64_t uiNextSoundUuid = rPrevious.uiNextSoundUuid;
	uint64_t uiNextVisualUuid = rPrevious.uiNextVisualUuid;
#endif
	uint16_t uiFrameId = rPrevious.uiFrameId;
	IslandsFlip eIslandsFlip = rPrevious.eIslandsFlip;

	// Save
	rCurrent.randomEngine = randomEngine;
	rCurrent.vecArea = vecArea;
	rCurrent.uiNextUuid = uiNextUuid;
#ifdef BT_CLIENT
	rCurrent.uiNextSoundUuid = uiNextSoundUuid;
	rCurrent.uiNextVisualUuid = uiNextVisualUuid;
#endif
	rCurrent.uiFrameId = uiFrameId;
	rCurrent.eIslandsFlip = eIslandsFlip;
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

#ifdef BT_CLIENT
float DayPercent()
{
	float fSunAngle = game::gpCamera->SunAngle();
	if (fSunAngle >= 0.0f && fSunAngle <= XM_PIDIV2)
	{
		return fSunAngle / XM_PIDIV2;
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle <= XM_PI)
	{
		return 1.0f - (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

float NightPercent()
{
	float fSunAngle = game::gpCamera->SunAngle();
	if (fSunAngle >= XM_PI && fSunAngle < XM_PI + XM_PIDIV2)
	{
		return (fSunAngle - XM_PI) / XM_PIDIV2;
	}
	if (fSunAngle >= XM_PI + XM_PIDIV2)
	{
		return 1.0f - (fSunAngle - (XM_PI + XM_PIDIV2)) / XM_PIDIV2;
	}
	else
	{
		return 0.0f;
	}
}

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
