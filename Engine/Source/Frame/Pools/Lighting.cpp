#include "Lighting.h"

#include "Frame/Render.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/TextureManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/Wrapper.h"

#include "Game.h"
#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

XMVECTOR XM_CALLCONV DirectionToDirectionMultipliers(FXMVECTOR vecDirection)
{
	XMFLOAT4A f4Direction {};
	XMStoreFloat4A(&f4Direction, vecDirection);

	return XMVectorSet(std::max(f4Direction.x, 0.0f), std::max(-f4Direction.x, 0.0f), std::max(f4Direction.y, 0.0f), std::max(-f4Direction.y, 0.0f));
}

void RenderLightingGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.f4LightingOne.x = gLightingDirectional.Get();
	rGlobalLayout.f4LightingOne.y = gLightingIndirect.Get();
	rGlobalLayout.f4LightingOne.z = gLightingObjectsAdd.Get();
	rGlobalLayout.f4LightingOne.w = gLightingCombinePower.Get();

	rGlobalLayout.f4LightingTwo.x = gLightingBlurDistance.Get();
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	rGlobalLayout.f4LightingTwo.y = static_cast<float>(iBlurTextureCount);
	rGlobalLayout.f4LightingTwo.z = gLightingTerrain.Get();
	rGlobalLayout.f4LightingTwo.w = gLightingObjects.Get();

	rGlobalLayout.f4LightingThree.x = gLightingBlurDirectionality.Get();
	rGlobalLayout.f4LightingThree.y = gLightingAddTerrain.Get();
	rGlobalLayout.f4LightingThree.z = gLightingBlurJitter.Get();
	rGlobalLayout.f4LightingThree.w = gLightingCombineDecay.Get();
}

void RenderLightingMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame)
{
	float fDayPercent = DayPercent(rFrame);

	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rMainLayout.fLightingSampledNormalsSize = gLightingSampledNormalsSize.Get();
	rMainLayout.fLightingSampledNormalsSizeMod = gLightingSampledNormalsSizeMod.Get();
	rMainLayout.fLightingSampledNormalsSpeed = gLightingSampledNormalsSpeed.Get();
	rMainLayout.fWaterHeightDarkenTop = gWaterHeightDarkenTop.Get();
	rMainLayout.fWaterHeightDarkenBottom = gWaterHeightDarkenBottom.Get();
	rMainLayout.fWaterHeightDarkenClamp = gWaterHeightDarkenClamp.Get();

	rMainLayout.fLightingTimeOfDayMultiplier = std::min(fDayPercent + (1.0f - fDayPercent) * gLightingTimeOfDayMultiplier.Get(), 0.85f);

	rMainLayout.fLightingWaterSkyboxSunBias = gLightingWaterSkyboxSunBias.Get();
	rMainLayout.fLightingWaterSkyboxNormalSoften = gLightingWaterSkyboxNormalSoften.Get();
	rMainLayout.fLightingWaterSkyboxNormalBlendWave = gLightingWaterSkyboxNormalBlendWave.Get();
	rMainLayout.fLightingWaterSkyboxIntensity = gLightingWaterSkyboxIntensity.Get();
	rMainLayout.fLightingWaterSkyboxAdd = gLightingWaterSkyboxAdd.Get();
	rMainLayout.fLightingWaterSkyboxOne = gLightingWaterSkyboxOne.Get() + (1.0f - fDayPercent) * 1.5f * gLightingWaterSkyboxOne.Get();
	rMainLayout.fLightingWaterSkyboxOnePower = gLightingWaterSkyboxOnePower.Get();
	rMainLayout.fLightingWaterSkyboxTwo = gLightingWaterSkyboxTwo.Get();
	rMainLayout.fLightingWaterSkyboxTwoPower = gLightingWaterSkyboxTwoPower.Get();
	rMainLayout.fLightingWaterSkyboxThree = gLightingWaterSkyboxThree.Get();
	rMainLayout.fLightingWaterSkyboxThreePower = gLightingWaterSkyboxThreePower.Get();

	rMainLayout.fLightingWaterSpecularDiffuse = gLightingWaterSpecularDiffuse.Get();
	rMainLayout.fLightingWaterSpecularDirect = gLightingWaterSpecularDirect.Get();
	rMainLayout.fLightingWaterSpecular = gLightingWaterSpecular.Get();

	rMainLayout.fLightingWaterSpecularNormalSoften = gLightingWaterSpecularNormalSoften.Get();
	rMainLayout.fLightingWaterSpecularNormalBlendWave = gLightingWaterSpecularNormalBlendWave.Get();
	rMainLayout.fLightingWaterSpecularIntensity = gLightingWaterSpecularIntensity.Get();
	rMainLayout.fLightingWaterSpecularAdd = gLightingWaterSpecularAdd.Get();
	rMainLayout.fLightingWaterSpecularOne = gLightingWaterSpecularOne.Get();
	rMainLayout.fLightingWaterSpecularOnePower = gLightingWaterSpecularOnePower.Get();
	rMainLayout.fLightingWaterSpecularTwo = gLightingWaterSpecularTwo.Get();
	rMainLayout.fLightingWaterSpecularTwoPower = gLightingWaterSpecularTwoPower.Get();
	rMainLayout.fLightingWaterSpecularThree = gLightingWaterSpecularThree.Get();
	rMainLayout.fLightingWaterSpecularThreePower = gLightingWaterSpecularThreePower.Get();

	// Gltf
	rMainLayout.fGltfExposuse = engine::gGltfExposuse.Get();
	rMainLayout.fGltfGamma = std::max(DayPercent(rFrame) * engine::gGltfGamma.Get(), 0.001f);
	rMainLayout.fGltfAmbient = engine::gGltfIblAmbient.Get();
	rMainLayout.fGltfDiffuse = gGltfDiffuse.Get();
	rMainLayout.fGltfSpecular = gGltfSpecular.Get();

	rMainLayout.fGltfMipCount = static_cast<float>(engine::gpTextureManager->miGltfCubeMipCount);
	rMainLayout.fGltfDebugViewInputs = 0.0f;
	rMainLayout.fGltfDebugViewEquation = 0.0f;
	rMainLayout.fGltfSmoke = gGltfSmoke.Get();

	rMainLayout.fGltfBrdf = gGltfBrdf.Get();
	rMainLayout.fGltfBrdfPower = gGltfBrdfPower.Get();
	rMainLayout.fGltfIbl = gGltfIbl.Get();
	rMainLayout.fGltfIblPower = gGltfIblPower.Get();
	rMainLayout.fGltfSun = gGltfSun.Get();
	rMainLayout.fGltfSunPower = gGltfSunPower.Get();
	rMainLayout.fGltfLighting = gGltfLighting.Get();
	rMainLayout.fGltfLightingPower = gGltfLightingPower.Get();

	int64_t iLightCount = 0;
	auto pVisibleLightsLayouts = reinterpret_cast<shaders::VisibleLightQuadLayout*>(gpBufferManager->mVisibleLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	int64_t iVisibleLightsRendered = 0;

	auto pAreaLightsLayouts = reinterpret_cast<shaders::QuadLayout*>(gpBufferManager->mAreaLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	int64_t iAreaLightsRendered = 0;
	for (decltype(rFrame.areaLights.uiMaxIndex) i = 0; i <= rFrame.areaLights.uiMaxIndex; ++i)
	{
		if (!rFrame.areaLights.pbUsed[i])
		{
			continue;
		}

		const AreaLightInfo& rAreaLightInfo = rFrame.areaLights.pObjectInfos[i];

		++iLightCount;

		shaders::VisibleLightQuadLayout& rVisibleLightQuadLayout = pVisibleLightsLayouts[iVisibleLightsRendered];
		shaders::QuadLayout& rAreaLightQuadLayout = pAreaLightsLayouts[iAreaLightsRendered];

		bool bInVisibleArea = rAreaLightInfo.bAlwaysVisible;
		for (int64_t j = 0; j < 4; ++j)
		{
			if (InVisibleArea(gf4RenderVisibleArea, rAreaLightInfo.pVecVisiblePositions[j]))
			{
				bInVisibleArea = true;
			}

			XMFLOAT4A f4Position {};
			XMStoreFloat4A(&f4Position, rAreaLightInfo.pVecVisiblePositions[j]);
			rVisibleLightQuadLayout.pf4Vertices[j] = f4Position;
			rVisibleLightQuadLayout.pf4Texcoords[j] = {rAreaLightInfo.pf2Texcoords[j].x, rAreaLightInfo.pf2Texcoords[j].y, 0.0f, 0.0f};

			float fElevation = gpIslands->GlobalElevation(rAreaLightInfo.pVecLightingPositions[j]);
			auto vecBaseAreaPosition = common::ToBaseHeight(rAreaLightInfo.pVecLightingPositions[j], rFrame.camera.vecEyePosition, std::max(fElevation, gBaseHeight.Get()));
			XMStoreFloat4A(&f4Position, vecBaseAreaPosition);
			rAreaLightQuadLayout.pf4VerticesTexcoords[j] = {f4Position.x, f4Position.y, rAreaLightInfo.pf2Texcoords[j].x, rAreaLightInfo.pf2Texcoords[j].y};

			rVisibleLightQuadLayout.puiColors[j] = rAreaLightInfo.puiColors[j];
		}

		if (!bInVisibleArea)
		{
			continue;
		}

		rVisibleLightQuadLayout.fIntensity = rAreaLightInfo.fVisibleIntensity;
		rVisibleLightQuadLayout.fRotation = 0.0f;
		rVisibleLightQuadLayout.uiTextureIndex = static_cast<uint32_t>(CrcToIndex(rAreaLightInfo.crc));

		XMFLOAT4A f4Misc {};
		f4Misc.x = CrcToIndex(rAreaLightInfo.crc);
		f4Misc.y = rAreaLightInfo.fLightingIntensity;
		rAreaLightQuadLayout.pf4Misc[0] = rAreaLightQuadLayout.pf4Misc[1] = rAreaLightQuadLayout.pf4Misc[2] = rAreaLightQuadLayout.pf4Misc[3] = f4Misc;
		XMStoreFloat4(&rAreaLightQuadLayout.f4Misc, rAreaLightInfo.vecDirectionMultipliers);
		rAreaLightQuadLayout.uiColor = rAreaLightInfo.puiColors[0];

		++iVisibleLightsRendered;
		++iAreaLightsRendered;
	}
	PROFILE_SET_COUNT(kCpuCounterAreaLightsRendered, iAreaLightsRendered);
	gpPipelineManager->mpPipelines[kPipelineAreaLights].WriteIndirectBuffer(iCommandBuffer, iAreaLightsRendered);

	auto pPointLightsLayouts = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mPointLightsStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	int64_t iPointLightsRendered = 0;
	for (decltype(rFrame.pointLights.uiMaxIndex) i = 0; i <= rFrame.pointLights.uiMaxIndex; ++i)
	{
		if (!rFrame.pointLights.pbUsed[i])
		{
			continue;
		}

		const PointLightInfo& rPointLightInfo = rFrame.pointLights.pObjectInfos[i];
		
		++iLightCount;

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rPointLightInfo.vecPosition);
		if (f4Position.x < gf4RenderVisibleArea.x || f4Position.x > gf4RenderVisibleArea.z || f4Position.y > gf4RenderVisibleArea.y || f4Position.y < gf4RenderVisibleArea.w)
		{
			continue;
		}
		
		XMFLOAT4 f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};

		// Visible
		XMStoreFloat4A(&f4Position, rPointLightInfo.vecPosition);

		XMFLOAT4 f4VertexRect = {f4Position.x - rPointLightInfo.fVisibleArea, f4Position.y + rPointLightInfo.fVisibleArea, 2.0f * rPointLightInfo.fVisibleArea, -2.0f * rPointLightInfo.fVisibleArea};

		shaders::VisibleLightQuadLayout& rVisibleLightQuadLayout = pVisibleLightsLayouts[iVisibleLightsRendered];
		rVisibleLightQuadLayout.pf4Vertices[0] = {f4VertexRect.x, f4VertexRect.y, f4Position.z, 1.0f};
		rVisibleLightQuadLayout.pf4Vertices[1] = {f4VertexRect.x + f4VertexRect.z, f4VertexRect.y, f4Position.z, 1.0f};
		rVisibleLightQuadLayout.pf4Vertices[2] = {f4VertexRect.x, f4VertexRect.y + f4VertexRect.w, f4Position.z, 1.0f};
		rVisibleLightQuadLayout.pf4Vertices[3] = {f4VertexRect.x + f4VertexRect.z, f4VertexRect.y + f4VertexRect.w, f4Position.z, 1.0f};
		rVisibleLightQuadLayout.pf4Texcoords[0] = {0.0f, 0.0f, 0.0f, 0.0f};
		rVisibleLightQuadLayout.pf4Texcoords[1] = {1.0f, 0.0f, 0.0f, 0.0f};
		rVisibleLightQuadLayout.pf4Texcoords[2] = {0.0f, 1.0f, 0.0f, 0.0f};
		rVisibleLightQuadLayout.pf4Texcoords[3] = {1.0f, 1.0f, 0.0f, 0.0f};
		rVisibleLightQuadLayout.fIntensity = rPointLightInfo.fVisibleIntensity;
		rVisibleLightQuadLayout.fRotation = rPointLightInfo.fRotation;
		rVisibleLightQuadLayout.puiColors[0] = rVisibleLightQuadLayout.puiColors[1] = rVisibleLightQuadLayout.puiColors[2] = rPointLightInfo.uiColor;
		rVisibleLightQuadLayout.uiTextureIndex = static_cast<uint32_t>(CrcToIndex(rPointLightInfo.crc));

		// Lighting
		float fElevation = gpIslands->GlobalElevation(rPointLightInfo.vecPosition);
		auto vecBaseAreaPosition = common::ToBaseHeight(rPointLightInfo.vecPosition, rFrame.camera.vecEyePosition, std::max(fElevation, gBaseHeight.Get()));
		XMStoreFloat4A(&f4Position, vecBaseAreaPosition);

		XMFLOAT4A f4Misc {};
		f4Misc.x = CrcToIndex(rPointLightInfo.crc);
		f4Misc.y = rPointLightInfo.fLightingIntensity;
		f4Misc.z = rPointLightInfo.fRotation;

		f4VertexRect = {f4Position.x - rPointLightInfo.fLightingArea, f4Position.y + rPointLightInfo.fLightingArea, 2.0f * rPointLightInfo.fLightingArea, -2.0f * rPointLightInfo.fLightingArea};

		shaders::AxisAlignedQuadLayout& rLightingQuadLayout = pPointLightsLayouts[iPointLightsRendered];
		rLightingQuadLayout.f4VertexRect = f4VertexRect;
		rLightingQuadLayout.f4TextureRect = f4TextureRect;
		rLightingQuadLayout.f4Misc = f4Misc;
		rLightingQuadLayout.uiColor = rPointLightInfo.uiColor;

		++iVisibleLightsRendered;
		++iPointLightsRendered;
	}
	PROFILE_SET_COUNT(kCpuCounterPointLightsRendered, iPointLightsRendered);
	gpPipelineManager->mpPipelines[kPipelinePointLights].WriteIndirectBuffer(iCommandBuffer, iPointLightsRendered);

	PROFILE_SET_COUNT(kCpuCounterLights, iLightCount);

	gpPipelineManager->mpPipelines[kPipelineVisibleLights].WriteIndirectBuffer(iCommandBuffer, iVisibleLightsRendered);
}

} // namespace engine
