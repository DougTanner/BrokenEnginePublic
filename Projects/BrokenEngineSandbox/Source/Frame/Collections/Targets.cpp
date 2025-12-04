#include "Targets.h"

#include "Frame/Frame.h"

namespace game
{

using enum TargetFlags;

void TargetsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	TargetsInterpolate& rCurrent = rCurrentFrameInterpolate.targets;
	const TargetsInterpolate& rPrevious = rPreviousFrame.interpolate.targets;
	engine::BillboardsInterpolate& rBillboards = rCurrentFrameInterpolate.billboards;

	if (rCurrent.pData == nullptr) { return; }

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		uint8_t uiTypeIndex = rPrevious.puiTypeIndices[i];
		engine::billboard_t uiBillboard = rPrevious.puiBillboards[i];

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.puiBillboards[i] = uiBillboard;

		// Sync billboard position from target position
		int64_t iBillboardIndex = rBillboards.IdToIndex(uiBillboard);
		rBillboards.pVecPositions[iBillboardIndex] = vecPosition;
	}
}

void TargetsPostRender::Update([[maybe_unused]] TargetsPostRender& __restrict rCurrent, [[maybe_unused]] const TargetsPostRender& __restrict rPrevious)
{
	if (rCurrent.pData == nullptr) { return; }

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

void TargetsPostRender::Add(Frame& __restrict rFrame, target_t& rId)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Create billboard for visual indicator (zero-init, owner will set values)
	engine::billboard_t uiBillboard;
	engine::BillboardsPostRender::Add(rFrame, uiBillboard);

	// Zero-init
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = 0;
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

uint8_t TargetsPostRender::RegisterType(const TargetsInterpolate::Type& type)
{
	uint8_t uiIndex = static_cast<uint8_t>(TargetsInterpolate::sTypes.size());

	// Register corresponding billboard type
	uint8_t uiBillboardTypeIndex = engine::BillboardsPostRender::RegisterType({
		.crc = type.crc,
		.fSize = type.fSize,
		.fAlpha = type.fAlpha,
	});

	// Store type with billboard type index
	TargetsInterpolate::Type typeWithBillboard = type;
	typeWithBillboard.uiBillboardTypeIndex = uiBillboardTypeIndex;
	TargetsInterpolate::sTypes.push_back(typeWithBillboard);

	return uiIndex;
}

const TargetsInterpolate::Type& TargetsPostRender::GetType(uint8_t uiTypeIndex)
{
	return TargetsInterpolate::sTypes.at(uiTypeIndex);
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
