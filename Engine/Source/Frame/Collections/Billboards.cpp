#include "Billboards.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum BillboardFlags;

void BillboardsInterpolate::Register()
{
}

void BillboardsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void BillboardsInterpolate::AllocateAndCopy(BillboardsInterpolate& rCurrent, const BillboardsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void BillboardsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	BillboardsInterpolate& __restrict rCurrent = rFrameInterpolate.billboards;
	const BillboardsInterpolate& rPrevious = rPreviousFrame.interpolate.billboards;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Note: Owner is responsible for writing all other data (positions, flags, etc.) each frame via Sync()
	std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
}

void BillboardsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	BillboardsInterpolate& rBillboards = rFrameInterpolate.billboards;
	int64_t iIndex = rBillboards.IdToIndex(id);

	rBillboards.pVecPositions[iIndex] = rData.vecPosition;
	rBillboards.puiTypeIndices[iIndex] = rData.uiTypeIndex;
	rBillboards.puiFlags[iIndex] = rData.uiFlags;
	rBillboards.pfRotations[iIndex] = rData.fRotation;
	rBillboards.pfExtra[iIndex] = rData.fExtra;
}

void BillboardsPostRender::AllocateAndCopy(BillboardsPostRender& rCurrent, const BillboardsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void BillboardsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	BillboardsPostRender& __restrict rCurrent = rFrame.postRender.billboards;
	const BillboardsPostRender& __restrict rPrevious = rPreviousFrame.postRender.billboards;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		billboard_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

void BillboardsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void BillboardsPostRender::Add(game::Frame& __restrict rFrame, billboard_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
}

void BillboardsPostRender::Remove(game::Frame& __restrict rFrame, billboard_t& rId)
{
	ASSERT(rId.IsValid());

	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void BillboardsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void BillboardsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void BillboardsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void BillboardsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void BillboardsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const BillboardsInterpolate& rCurrent = rFrameInterpolate.billboards;
	gpProfileManager->SetCount(kCpuCounterBillboards, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto [pLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::BillboardLayout>(kCrc, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	int64_t iRendered = 0;
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiTypeIndex = rCurrent.puiTypeIndices[i];
		BillboardFlags_t flags;
		flags.meFlags = static_cast<BillboardFlags>(rCurrent.puiFlags[i]);
		float fRotation = rCurrent.pfRotations[i];
		float fExtra = rCurrent.pfExtra[i];
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];

		const BillboardsInterpolate::Type& rType = BillboardsInterpolate::sTypes.at(uiTypeIndex);

		// Project world position to clip space
		XMVECTOR vecProjection = XMVector4Transform(vecPosition, XMMatrixMultiply(game::gpCamera->mMatView, game::gpCamera->mMatPerspective));

		XMFLOAT4A f4Position {0.0f, 0.0f, 0.0f, 1.0f};
		XMStoreFloat4A(&f4Position, vecProjection);
		f4Position.x /= f4Position.w;
		f4Position.y /= f4Position.w;
		f4Position.z /= f4Position.w;
		f4Position.w /= f4Position.w;

		// Handle offscreen-only billboards (like offscreen indicators)
		if (flags & kOffscreenOnly && !(f4Position.x < -1.0f - fExtra || f4Position.x > 1.0f + fExtra || f4Position.y > 1.0f + fExtra || f4Position.y < -1.0f - fExtra))
		{
			continue;
		}

		float fSize = rType.fSize;

		if (flags & kOffscreenOnly)
		{
			f4Position.x = std::clamp(f4Position.x, -1.0f + fSize / gpSwapchainManager->mfAspectRatio, 1.0f - fSize / gpSwapchainManager->mfAspectRatio);
			f4Position.y = std::clamp(f4Position.y, -1.0f + fSize, 1.0f - fSize);
		}

		// Calculate rotation for offscreen rotate flag
		if (flags & kOffscreenRotate)
		{
			fRotation = XM_PI + XM_PIDIV2 + common::RotationFromPosition(XMVector3Normalize(XMLoadFloat4A(&f4Position)));
			if (f4Position.y > 0.0f)
			{
				fRotation = XM_PI - fRotation;
			}
		}

		// Populate GPU layout
		shaders::BillboardLayout& rBillboardLayout = pLayouts[iRendered];
		rBillboardLayout.f4Position = f4Position;
		rBillboardLayout.fSize = fSize;
		rBillboardLayout.fTextureIndex = CrcToIndex(rType.crc);
		rBillboardLayout.fRotation = fRotation;
		rBillboardLayout.fAlpha = rType.fAlpha;

		++iRendered;
	}

	gpProfileManager->SetCount(kCpuCounterBillboardsRendered, iRendered);

	if constexpr (kbEnableRecording)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
	}
	else
	{
		WritePipelineIndirectBuffers(iCommandBuffer, iRendered);
	}
}

bool BillboardsInterpolate::operator==(const BillboardsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(puiFlags[i], rOther.puiFlags[i]);
		bEqual &= common::BreakOnNotEqual(pfRotations[i], rOther.pfRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfExtra[i], rOther.pfExtra[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
	}

	return bEqual;
}

bool BillboardsPostRender::operator==(const BillboardsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

} // namespace engine
