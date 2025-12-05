#include "HexShields.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void HexShieldsInterpolate::Update([[maybe_unused]] HexShieldsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	const HexShieldsInterpolate& rPrevious = rPreviousFrame.interpolate.hexShields;

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		rCurrent.pf4Transforms[0][i] = rPrevious.pf4Transforms[0][i];
		rCurrent.pf4Transforms[1][i] = rPrevious.pf4Transforms[1][i];
		rCurrent.pf4Transforms[2][i] = rPrevious.pf4Transforms[2][i];
		rCurrent.pf4TransformNormals[0][i] = rPrevious.pf4TransformNormals[0][i];
		rCurrent.pf4TransformNormals[1][i] = rPrevious.pf4TransformNormals[1][i];
		rCurrent.pf4TransformNormals[2][i] = rPrevious.pf4TransformNormals[2][i];
		uint32_t uiColor = rPrevious.puiColors[i];
		uint32_t uiLightingColor = rPrevious.puiLightingColors[i];
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			rCurrent.pf4Directions[j][i] = rPrevious.pf4Directions[j][i];
			rCurrent.pfVertIntensities[j][i] = rPrevious.pfVertIntensities[j][i];
			rCurrent.pfFragIntensities[j][i] = rPrevious.pfFragIntensities[j][i];
		}
		float fLightingIntensity = rPrevious.pfLightingIntensities[i];
		float fSize = rPrevious.pfSizes[i];
		float fColorMix = rPrevious.pfColorMixes[i];
		float fMinimumIntensity = rPrevious.pfMinimumIntensities[i];

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.puiColors[i] = uiColor;
		rCurrent.puiLightingColors[i] = uiLightingColor;
		rCurrent.pfLightingIntensities[i] = fLightingIntensity;
		rCurrent.pfSizes[i] = fSize;
		rCurrent.pfColorMixes[i] = fColorMix;
		rCurrent.pfMinimumIntensities[i] = fMinimumIntensity;
	}
}

void HexShieldsPostRender::Update([[maybe_unused]] HexShieldsPostRender& __restrict rCurrent, [[maybe_unused]] const HexShieldsPostRender& __restrict rPrevious)
{
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		hex_shields_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

void HexShieldsPostRender::Add(game::Frame& __restrict rFrame, hex_shields_t& rId)
{
	HexShieldsInterpolate& rInterpolate = rFrame.interpolate.hexShields;
	HexShieldsPostRender& rPostRender = rFrame.postRender.hexShields;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
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
	rInterpolate.puiColors[uiSpawnIndex] = 0xFFFFFFFF;
	rInterpolate.puiLightingColors[uiSpawnIndex] = 0xFFFFFFFF;
	for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
	{
		rInterpolate.pf4Directions[j][uiSpawnIndex] = {};
		rInterpolate.pfVertIntensities[j][uiSpawnIndex] = 0.0f;
		rInterpolate.pfFragIntensities[j][uiSpawnIndex] = 0.0f;
	}
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfSizes[uiSpawnIndex] = 1.0f;
	rInterpolate.pfColorMixes[uiSpawnIndex] = 0.0f;
	rInterpolate.pfMinimumIntensities[uiSpawnIndex] = 0.0f;
}

void HexShieldsPostRender::Remove(game::Frame& __restrict rFrame, hex_shields_t& rId)
{
	HexShieldsInterpolate& rInterpolate = rFrame.interpolate.hexShields;
	HexShieldsPostRender& rPostRender = rFrame.postRender.hexShields;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void HexShieldsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const HexShieldsInterpolate& rCurrent = rFrameInterpolate.hexShields;
	PROFILE_SET_COUNT(kCpuCounterHexShields, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto pLayouts = reinterpret_cast<shaders::HexShieldLayout*>(gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer).mpMappedMemory);

	int64_t iRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load position
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		float fSize = rCurrent.pfSizes[i];

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
		uint32_t uiColor = rCurrent.puiColors[i];
		rLayout.f4Color.x = static_cast<float>((uiColor >> 0) & 0xFF) / 255.0f;
		rLayout.f4Color.y = static_cast<float>((uiColor >> 8) & 0xFF) / 255.0f;
		rLayout.f4Color.z = static_cast<float>((uiColor >> 16) & 0xFF) / 255.0f;
		rLayout.f4Color.w = static_cast<float>((uiColor >> 24) & 0xFF) / 255.0f;

		uint32_t uiLightingColor = rCurrent.puiLightingColors[i];
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
		rLayout.fSize = fSize;
		rLayout.fColorMix = rCurrent.pfColorMixes[i];
		rLayout.fMinimumIntensity = rCurrent.pfMinimumIntensities[i];

		++iRendered;
	}

	PROFILE_SET_COUNT(kCpuCounterHexShieldsRendered, iRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iRendered);
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
		bEqual &= common::BreakOnNotEqual(puiColors[i], rOther.puiColors[i]);
		bEqual &= common::BreakOnNotEqual(puiLightingColors[i], rOther.puiLightingColors[i]);
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pf4Directions[j][i], rOther.pf4Directions[j][i]);
			bEqual &= common::BreakOnNotEqual(pfVertIntensities[j][i], rOther.pfVertIntensities[j][i]);
			bEqual &= common::BreakOnNotEqual(pfFragIntensities[j][i], rOther.pfFragIntensities[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(pfLightingIntensities[i], rOther.pfLightingIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfSizes[i], rOther.pfSizes[i]);
		bEqual &= common::BreakOnNotEqual(pfColorMixes[i], rOther.pfColorMixes[i]);
		bEqual &= common::BreakOnNotEqual(pfMinimumIntensities[i], rOther.pfMinimumIntensities[i]);
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

} // namespace engine
