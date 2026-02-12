#include "HexShields.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Graphics/Camera.h"
#include "Graphics/Managers/BufferManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void HexShieldsInterpolate::Register()
{
}

void HexShieldsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void HexShieldsInterpolate::AllocateAndCopy(HexShieldsInterpolate& rCurrent, const HexShieldsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pfLightingIntensities, rPrevious.pfLightingIntensities, rCurrent.iCount * sizeof(rCurrent.pfLightingIntensities[0]));
		std::memcpy(rCurrent.pfSizes, rPrevious.pfSizes, rCurrent.iCount * sizeof(rCurrent.pfSizes[0]));
		std::memcpy(rCurrent.pfColorMixes, rPrevious.pfColorMixes, rCurrent.iCount * sizeof(rCurrent.pfColorMixes[0]));
	}
}

void HexShieldsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	HexShieldsInterpolate& __restrict rCurrent = rFrameInterpolate.hexShields;
	const HexShieldsInterpolate& rPrevious = rPreviousFrame.interpolate.hexShields;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		rCurrent.pf4Transforms[0][i] = rPrevious.pf4Transforms[0][i];
		rCurrent.pf4Transforms[1][i] = rPrevious.pf4Transforms[1][i];
		rCurrent.pf4Transforms[2][i] = rPrevious.pf4Transforms[2][i];
		rCurrent.pf4TransformNormals[0][i] = rPrevious.pf4TransformNormals[0][i];
		rCurrent.pf4TransformNormals[1][i] = rPrevious.pf4TransformNormals[1][i];
		rCurrent.pf4TransformNormals[2][i] = rPrevious.pf4TransformNormals[2][i];
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			rCurrent.pf4Directions[j][i] = rPrevious.pf4Directions[j][i];
			rCurrent.pfVertIntensities[j][i] = rPrevious.pfVertIntensities[j][i];
			rCurrent.pfFragIntensities[j][i] = rPrevious.pfFragIntensities[j][i];
		}
	}
}

void HexShieldsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	HexShieldsInterpolate& rHexShields = rFrameInterpolate.hexShields;
	int64_t iIndex = rHexShields.IdToIndex(id);

	// Write all owner-provided fields
	rHexShields.pVecPositions[iIndex] = rData.vecPosition;
	rHexShields.pf4Transforms[0][iIndex] = rData.pf4Transforms[0];
	rHexShields.pf4Transforms[1][iIndex] = rData.pf4Transforms[1];
	rHexShields.pf4Transforms[2][iIndex] = rData.pf4Transforms[2];
	rHexShields.pf4TransformNormals[0][iIndex] = rData.pf4TransformNormals[0];
	rHexShields.pf4TransformNormals[1][iIndex] = rData.pf4TransformNormals[1];
	rHexShields.pf4TransformNormals[2][iIndex] = rData.pf4TransformNormals[2];
	for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
	{
		rHexShields.pf4Directions[j][iIndex] = rData.pf4Directions[j];
		rHexShields.pfVertIntensities[j][iIndex] = rData.pfVertIntensities[j];
		rHexShields.pfFragIntensities[j][iIndex] = rData.pfFragIntensities[j];
	}
	rHexShields.pfLightingIntensities[iIndex] = rData.fLightingIntensity;
	rHexShields.pfSizes[iIndex] = rData.fSize;
	rHexShields.pfColorMixes[iIndex] = rData.fColorMix;
}

void HexShieldsPostRender::AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void HexShieldsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::Add(game::Frame& __restrict rFrame, hex_shields_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	HexShieldsInterpolate& rInterpolate = rFrame.interpolate.hexShields;
	HexShieldsPostRender& rPostRender = rFrame.postRender.hexShields;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.pf4Transforms[0][uiSpawnIndex] = {};
	rInterpolate.pf4Transforms[1][uiSpawnIndex] = {};
	rInterpolate.pf4Transforms[2][uiSpawnIndex] = {};
	rInterpolate.pf4TransformNormals[0][uiSpawnIndex] = {};
	rInterpolate.pf4TransformNormals[1][uiSpawnIndex] = {};
	rInterpolate.pf4TransformNormals[2][uiSpawnIndex] = {};
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
	{
		rInterpolate.pf4Directions[j][uiSpawnIndex] = {};
		rInterpolate.pfVertIntensities[j][uiSpawnIndex] = 0.0f;
		rInterpolate.pfFragIntensities[j][uiSpawnIndex] = 0.0f;
	}
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfSizes[uiSpawnIndex] = 0.0f;
	rInterpolate.pfColorMixes[uiSpawnIndex] = 0.0f;
}

void HexShieldsPostRender::Remove(game::Frame& __restrict rFrame, hex_shields_t& rId)
{
	ASSERT(rId.IsValid());

	HexShieldsInterpolate& rInterpolate = rFrame.interpolate.hexShields;
	HexShieldsPostRender& rPostRender = rFrame.postRender.hexShields;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void HexShieldsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void HexShieldsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void HexShieldsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool HexShieldsInterpolate::operator==(const HexShieldsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		for (int64_t j = 0; j < 3; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pf4Transforms[j][i], rOther.pf4Transforms[j][i]);
			bEqual &= common::BreakOnNotEqual(pf4TransformNormals[j][i], rOther.pf4TransformNormals[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pf4Directions[j][i], rOther.pf4Directions[j][i]);
			bEqual &= common::BreakOnNotEqual(pfVertIntensities[j][i], rOther.pfVertIntensities[j][i]);
			bEqual &= common::BreakOnNotEqual(pfFragIntensities[j][i], rOther.pfFragIntensities[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(pfLightingIntensities[i], rOther.pfLightingIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfSizes[i], rOther.pfSizes[i]);
		bEqual &= common::BreakOnNotEqual(pfColorMixes[i], rOther.pfColorMixes[i]);
	}

	return bEqual;
}

bool HexShieldsPostRender::operator==(const HexShieldsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void HexShieldsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const HexShieldsInterpolate& rCurrent = rFrameInterpolate.hexShields;
	gpProfileManager->SetCount(kCpuCounterHexShields, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto [pLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::HexShieldLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	int64_t iRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load position and type
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const HexShieldsType& rType = GetType(rCurrent.puiTypeIndices[i]);

		// Visibility culling
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, vecPosition);
		static constexpr float kfAdjust = 20.0f;
		if (!game::gpCamera->InVisibleArea(game::gpCamera->f4RenderVisibleArea, f4Position, kfAdjust, kfAdjust, kfAdjust, kfAdjust))
		{
			continue;
		}

		// Build HexShieldLayout
		shaders::HexShieldLayout& rLayout = pLayouts[iRendered];
		rLayout.f4Position = f4Position;
		rLayout.f3x4Transform[0] = rCurrent.pf4Transforms[0][i];
		rLayout.f3x4Transform[1] = rCurrent.pf4Transforms[1][i];
		rLayout.f3x4Transform[2] = rCurrent.pf4Transforms[2][i];
		rLayout.f3x4TransformNormal[0] = rCurrent.pf4TransformNormals[0][i];
		rLayout.f3x4TransformNormal[1] = rCurrent.pf4TransformNormals[1][i];
		rLayout.f3x4TransformNormal[2] = rCurrent.pf4TransformNormals[2][i];

		// Convert packed color to vec4 (ABGR to RGBA float)
		uint32_t uiColor = rType.uiColor;
		rLayout.f4Color.x = static_cast<float>((uiColor >> 0) & 0xFF) / 255.0f;
		rLayout.f4Color.y = static_cast<float>((uiColor >> 8) & 0xFF) / 255.0f;
		rLayout.f4Color.z = static_cast<float>((uiColor >> 16) & 0xFF) / 255.0f;
		rLayout.f4Color.w = static_cast<float>((uiColor >> 24) & 0xFF) / 255.0f;

		uint32_t uiLightingColor = rType.uiLightingColor;
		rLayout.f4LightingColor.x = static_cast<float>((uiLightingColor >> 0) & 0xFF) / 255.0f;
		rLayout.f4LightingColor.y = static_cast<float>((uiLightingColor >> 8) & 0xFF) / 255.0f;
		rLayout.f4LightingColor.z = static_cast<float>((uiLightingColor >> 16) & 0xFF) / 255.0f;
		rLayout.f4LightingColor.w = static_cast<float>((uiLightingColor >> 24) & 0xFF) / 255.0f;

		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			rLayout.pf4Directions[j] = rCurrent.pf4Directions[j][i];
			rLayout.pfVertIntensities[j] = rCurrent.pfVertIntensities[j][i];
			rLayout.pfFragIntensities[j] = rCurrent.pfFragIntensities[j][i];
		}

		rLayout.fLightingIntensity = rCurrent.pfLightingIntensities[i];
		rLayout.fSize = rCurrent.pfSizes[i];
		rLayout.fColorMix = rCurrent.pfColorMixes[i];
		rLayout.fMinimumIntensity = rType.fMinimumIntensity;

		++iRendered;
	}

	gpProfileManager->SetCount(kCpuCounterHexShieldsRendered, iRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iRendered);
}

} // namespace engine
