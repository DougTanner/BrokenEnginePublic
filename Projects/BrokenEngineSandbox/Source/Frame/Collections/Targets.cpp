#include "Targets.h"

#include "Frame/Frame.h"

namespace game
{

using enum TargetFlags;

void TargetsInterpolate::Register()
{
}

void TargetsInterpolate::AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiBillboards, rPrevious.puiBillboards, static_cast<size_t>(rCurrent.iCount) * sizeof(engine::billboard_t));
	}
}

void TargetsInterpolate::Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	TargetsInterpolate& rTargets = rFrameInterpolate.targets;
	int64_t iIndex = rTargets.IdToIndex(id);

	// Write own fields
	rTargets.pVecPositions[iIndex] = rData.vecPosition;
	rTargets.puiTypeIndices[iIndex] = rData.uiTypeIndex;

	// Sync owned billboard if it exists (only visible when subscribed)
	engine::billboard_t uiBillboard = rTargets.puiBillboards[iIndex];
	if (uiBillboard.IsValid())
	{
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
}

void TargetsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	TargetsInterpolate& rCurrent = rCurrentFrameInterpolate.targets;

	// Owner (Spaceships) writes position, type index, and syncs billboard via IdToIndex pattern.
	// Billboard IDs are copied in AllocateAndCopy (needed for Remove() to access billboard).

	PROFILE_SET_COUNT(engine::kCpuCounterTargets, rCurrent.iCount);
}

void TargetsPostRender::AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, static_cast<size_t>(rCurrent.iCount) * sizeof(target_t));
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, static_cast<size_t>(rCurrent.iCount) * sizeof(TargetFlags_t));
		std::memcpy(rCurrent.puiSubscribers, rPrevious.puiSubscribers, static_cast<size_t>(rCurrent.iCount) * sizeof(uint8_t));
	}
}

void TargetsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void TargetsInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

void TargetsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void TargetsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void TargetsPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void TargetsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
{
}

void TargetsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] float fDeltaTime)
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

	// Zero-init (billboard created when first subscriber added)
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTargetTypeIndex;
	rInterpolate.puiBillboards[uiSpawnIndex] = {};
	rPostRender.pFlags[uiSpawnIndex] = {};
	rPostRender.puiSubscribers[uiSpawnIndex] = 0;
}

void TargetsPostRender::Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	int64_t iIndex = rInterpolate.IdToIndex(rId);

	if (flags & kDestination)
	{
		// Spaceship died - kill target immediately regardless of subscribers
		// Remove billboard if it exists
		if (rInterpolate.puiBillboards[iIndex].IsValid())
		{
			engine::BillboardsPostRender::Remove(rFrame, rInterpolate.puiBillboards[iIndex]);
		}

		// Remove target from collection immediately
		engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
		rId = {};
		return;
	}

	// Subscriber removal path
	ASSERT(rPostRender.puiSubscribers[iIndex] > 0);

	// Remove billboard when last subscriber leaves (makes target invisible)
	if (rPostRender.puiSubscribers[iIndex] == 1)
	{
		engine::BillboardsPostRender::Remove(rFrame, rInterpolate.puiBillboards[iIndex]);
	}

	--rPostRender.puiSubscribers[iIndex];

	// Only actually remove when both conditions are met:
	// - No destination flag set (owner removed their reference)
	// - No subscribers remaining (no missiles tracking this target)
	if (!(rPostRender.pFlags[iIndex] & kDestination) && rPostRender.puiSubscribers[iIndex] == 0)
	{
		engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
		rId = {};
	}
}

void TargetsPostRender::AddSubscriber(Frame& __restrict rFrame, target_t id)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	int64_t iIndex = rInterpolate.IdToIndex(id);

	// Create billboard when first subscriber added (makes target visible)
	if (rPostRender.puiSubscribers[iIndex] == 0)
	{
		uint8_t uiTypeIndex = rInterpolate.puiTypeIndices[iIndex];
		uint8_t uiBillboardTypeIndex = TargetsInterpolate::GetType(uiTypeIndex).uiBillboardTypeIndex;
		engine::BillboardsPostRender::Add(rFrame, rInterpolate.puiBillboards[iIndex], uiBillboardTypeIndex);
	}

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
