#include "Targets.h"

#include "Frame/Frame.h"

namespace game
{

using enum TargetFlags;

void TargetsInterpolate::Register()
{
}

void TargetsInterpolate::Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	TargetsInterpolate& rTargets = rFrameInterpolate.targets;
	int64_t iIndex = rTargets.IdToIndex(id);

	// Write own fields
	rTargets.pVecPositions[iIndex] = rData.vecPosition;
	rTargets.puiTypeIndices[iIndex] = rData.uiTypeIndex;

	// Sync owned billboard (grandchild handling)
	engine::billboard_t uiBillboard = rTargets.puiBillboards[iIndex];
	const TargetsType& rType = TargetsInterpolate::GetType(rData.uiTypeIndex);

	engine::BillboardsInterpolate::Sync(
		rFrameInterpolate,
		uiBillboard,
		{
			.vecPosition = rData.vecPosition,
			.uiTypeIndex = rType.uiBillboardTypeIndex,
			.uiFlags = 0,
			.fRotation = 0.0f,
			.fExtra = 0.0f,
		}
	);
}

void TargetsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	TargetsInterpolate& rCurrent = rCurrentFrameInterpolate.targets;
	const TargetsInterpolate& rPrevious = rPreviousFrame.interpolate.targets;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Owner (Spaceships) writes position, type index, and syncs billboard via IdToIndex pattern.
	// Only copy billboard IDs forward (needed for Remove() to access billboard).
	std::memcpy(rCurrent.puiBillboards, rPrevious.puiBillboards, static_cast<size_t>(rCurrent.iCount) * sizeof(engine::billboard_t));

	PROFILE_SET_COUNT(engine::kCpuCounterTargets, rCurrent.iCount);
}

void TargetsPostRender::Update([[maybe_unused]] TargetsPostRender& __restrict rCurrent, [[maybe_unused]] const TargetsPostRender& __restrict rPrevious, [[maybe_unused]] float fDeltaTime)
{
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		target_t id = rPrevious.puiIds[i];
		TargetFlags_t flags = rPrevious.pFlags[i];
		uint8_t uiSubscribers = rPrevious.puiSubscribers[i];

		// Save
		rCurrent.puiIds[i] = id;
		rCurrent.pFlags[i] = flags;
		rCurrent.puiSubscribers[i] = uiSubscribers;
	}
}

void TargetsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void TargetsPostRender::Add(Frame& __restrict rFrame, target_t& rId, uint8_t uiTargetTypeIndex)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Create billboard for visual indicator
	engine::billboard_t uiBillboard;
	uint8_t uiBillboardTypeIndex = TargetsInterpolate::GetType(uiTargetTypeIndex).uiBillboardTypeIndex;
	engine::BillboardsPostRender::Add(rFrame, uiBillboard, uiBillboardTypeIndex);

	// Zero-init
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTargetTypeIndex;
	rInterpolate.puiBillboards[uiSpawnIndex] = uiBillboard;
	rPostRender.pFlags[uiSpawnIndex] = {};
	rPostRender.puiSubscribers[uiSpawnIndex] = 0;
}

void TargetsPostRender::Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	int64_t iIndex = rInterpolate.IdToIndex(rId);

	// Handle subscriber pattern: decrement or clear based on flag type
	if (flags & kDestination)
	{
		rPostRender.pFlags[iIndex].Clear(kDestination);
	}
	else
	{
		ASSERT(rPostRender.puiSubscribers[iIndex] > 0);
		--rPostRender.puiSubscribers[iIndex];
	}

	// Only actually remove when both conditions are met:
	// - No destination flag set (owner removed their reference)
	// - No subscribers remaining (no missiles tracking this target)
	if (!(rPostRender.pFlags[iIndex] & kDestination) && rPostRender.puiSubscribers[iIndex] == 0)
	{
		engine::BillboardsPostRender::Remove(rFrame, rInterpolate.puiBillboards[iIndex]);
		engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

		rId = {};
	}
}

void TargetsPostRender::AddSubscriber(Frame& __restrict rFrame, target_t id)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	int64_t iIndex = rInterpolate.IdToIndex(id);
	++rPostRender.puiSubscribers[iIndex];
}

void TargetsPostRender::RegisterType(uint8_t& ruiIndex, const TargetsType& rType)
{
	ASSERT(ruiIndex == 0xFF);
	ruiIndex = static_cast<uint8_t>(TargetsInterpolate::sTypes.size());

	// Register corresponding billboard type
	uint8_t uiBillboardTypeIndex = 0xFF;
	engine::BillboardsInterpolate::RegisterType(uiBillboardTypeIndex,
	{
		.crc = rType.crc,
		.fSize = rType.fSize,
		.fAlpha = rType.fAlpha,
	});

	// Store type with billboard type index
	TargetsType typeWithBillboard = rType;
	typeWithBillboard.uiBillboardTypeIndex = uiBillboardTypeIndex;
	TargetsInterpolate::sTypes.push_back(typeWithBillboard);
}

bool TargetsInterpolate::operator==(const TargetsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(puiBillboards[i], rOther.puiBillboards[i]);
	}

	return bEqual;
}

bool TargetsPostRender::operator==(const TargetsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(puiSubscribers[i], rOther.puiSubscribers[i]);
	}

	return bEqual;
}

} // namespace game
