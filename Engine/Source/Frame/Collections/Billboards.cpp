#include "Billboards.h"

#include "Frame/Frame.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum BillboardFlags;

void BillboardsInterpolate::Update([[maybe_unused]] BillboardsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void BillboardsPostRender::Update([[maybe_unused]] BillboardsPostRender& __restrict rCurrent, [[maybe_unused]] const BillboardsPostRender& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		billboard_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

billboard_t BillboardsPostRender::Add(game::Frame& __restrict rFrame, uint8_t uiTypeIndex, BillboardFlags_t flags, float fRotation, float fExtra, XMVECTOR vecPosition)
{
	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);

	// Defaults
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.puiFlags[uiSpawnIndex] = static_cast<uint8_t>(std::to_underlying(flags.meFlags));
	rInterpolate.pfRotations[uiSpawnIndex] = fRotation;
	rInterpolate.pfExtra[uiSpawnIndex] = fExtra;
	rInterpolate.pVecPositions[uiSpawnIndex] = vecPosition;

	rPostRender.puiIds[uiSpawnIndex] = newId;

	return newId;
}

void BillboardsPostRender::Remove(game::Frame& __restrict rFrame, billboard_t id)
{
	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
}

uint8_t BillboardsPostRender::RegisterType(const BillboardsInterpolate::Type& type)
{
	uint8_t uiIndex = static_cast<uint8_t>(BillboardsInterpolate::sTypes.size());
	BillboardsInterpolate::sTypes.push_back(type);
	return uiIndex;
}

const BillboardsInterpolate::Type& BillboardsPostRender::GetType(uint8_t uiTypeIndex)
{
	return BillboardsInterpolate::sTypes.at(uiTypeIndex);
}

void BillboardsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const BillboardsInterpolate& rCurrent = rFrameInterpolate.billboards;
	PROFILE_SET_COUNT(kCpuCounterBillboards, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pLayouts = reinterpret_cast<shaders::BillboardLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);

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
		rBillboardLayout.f4Misc = {fSize, CrcToIndex(rType.crc), fRotation, rType.fAlpha};

		++iRendered;
	}

	PROFILE_SET_COUNT(kCpuCounterBillboardsRendered, iRendered);

#if defined(ENABLE_RECORDING)
	WritePipelineIndirectBuffers(iCommandBuffer, 0);
#else
	WritePipelineIndirectBuffers(iCommandBuffer, iRendered);
#endif
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
