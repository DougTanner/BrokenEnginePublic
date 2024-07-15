#include "Islands.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/OneShotCommandBuffer.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

XMVECTOR XM_CALLCONV TerrainCollision(FXMVECTOR vecStart, FXMVECTOR vecEnd, float fStepInterval)
{
	auto vecToEnd = XMVectorSubtract(vecEnd, vecStart);
	float fDistance = XMVectorGetX(XMVector3Length(vecToEnd));
	int64_t iSteps = static_cast<int64_t>(fDistance / fStepInterval);
	auto vecStep = vecToEnd / static_cast<float>(iSteps);
	float fElevation = XMVectorGetZ(vecStart);

	auto vecCurrent = vecStart;
	for (int64_t k = 0; k < iSteps; ++k, vecCurrent += vecStep)
	{
		float fTerrainElevation = gpIslands->GlobalElevation(vecCurrent);
		if (fTerrainElevation >= fElevation)
		{
			return vecCurrent;
		}
	}

	return vecEnd;
}

const shaders::AxisAlignedQuadLayout& XM_CALLCONV Islands::GetIsland(DirectX::FXMVECTOR vecPosition)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	for (const shaders::AxisAlignedQuadLayout& rLayout : mQuads)
	{
		if (f4Position.x >= rLayout.f4VertexRect.x && f4Position.x <= rLayout.f4VertexRect.x + rLayout.f4VertexRect.z && f4Position.y <= rLayout.f4VertexRect.y && f4Position.y >= rLayout.f4VertexRect.y + rLayout.f4VertexRect.w)
		{
			return rLayout;
		}
	}

	DEBUG_BREAK();
	return mQuads.at(0);
}

float XM_CALLCONV Islands::GlobalElevation(DirectX::FXMVECTOR vecPosition)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

#if 1
	int64_t iX = static_cast<int64_t>(kfGlobalHeightmapSize * (f4Position.x - mf4GlobalArea.x) / (mf4GlobalArea.z - mf4GlobalArea.x));
	int64_t iY = static_cast<int64_t>(kfGlobalHeightmapSize - kfGlobalHeightmapSize * (f4Position.y - mf4GlobalArea.w) / (mf4GlobalArea.y - mf4GlobalArea.w));

	if (iX < 0 || iX >= kiGlobalHeightmapSize || iY < 0 || iY >= kiGlobalHeightmapSize) [[unlikely]]
	{
		return mfSeaFloorElevation;
	}
	else
	{
		return mppfElevations[iY][iX];
	}
#else
	float fX = kfGlobalHeightmapSize * (f4Position.x - mf4GlobalArea.x) / (mf4GlobalArea.z - mf4GlobalArea.x);
	int64_t iX = static_cast<int64_t>(std::floor(fX));
	float fY = kfGlobalHeightmapSize - kfGlobalHeightmapSize * (f4Position.y - mf4GlobalArea.w) / (mf4GlobalArea.y - mf4GlobalArea.w);
	int64_t iY = static_cast<int64_t>(std::floor(fY));

	if (iX < 1 || iX >= kiGlobalHeightmapSize - 2 || iY < 1 || iY >= kiGlobalHeightmapSize - 2)
	{
		return mfSeaFloorElevation;
	}
	else
	{
		float fTopLeft = mppfElevations[iY][iX];
		float fTopRight = mppfElevations[iY][iX + 1];
		float fBottomLeft = mppfElevations[iY - 1][iX];
		float fBottomRight = mppfElevations[iY - 1][iX + 1];

		float fPercentX = fX - static_cast<float>(iX);
		float fPercentY = fY - static_cast<float>(iY);

		float fTop = (1.0f - fPercentX) * fTopLeft + fPercentX * fTopRight;
		float fBottom = (1.0f - fPercentX) * fBottomLeft + fPercentX * fBottomRight;
		return (1.0f - fPercentY) * fBottom + fPercentY * fTop;
	}
#endif
}

DirectX::XMVECTOR XM_CALLCONV Islands::GlobalNormal(DirectX::FXMVECTOR vecPosition)
{
	float fStepX = (mf4GlobalArea.z - mf4GlobalArea.x) / kfGlobalHeightmapSize;
	float fStepY = (mf4GlobalArea.y - mf4GlobalArea.w) / kfGlobalHeightmapSize;
	float fDistance = 2.0f * std::max(fStepX, fStepY);

	auto vecTopLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, fDistance, 0.0f, 0.0f));
	vecTopLeft = XMVectorSetZ(vecTopLeft, GlobalElevation(vecTopLeft));
	auto vecTopRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, fDistance, 0.0f, 0.0f));
	vecTopRight = XMVectorSetZ(vecTopRight, GlobalElevation(vecTopRight));
	auto vecBotLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, -fDistance, 0.0f, 0.0f));
	vecBotLeft = XMVectorSetZ(vecBotLeft, GlobalElevation(vecBotLeft));
	auto vecBotRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, -fDistance, 0.0f, 0.0f));
	vecBotRight = XMVectorSetZ(vecBotRight, GlobalElevation(vecBotRight));

	return XMVector3Normalize(XMVector3Cross(vecTopRight - vecBotLeft, vecTopLeft - vecBotRight));
}

Islands::Islands()
{
	gpIslands = this;

	miCount = game::Frame::kiIslandCount;
	mQuads.resize(miCount);

	FillQuads();

	int64_t iIndex = 0;
	auto& rChunkMap = gpFileManager->GetDataChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kIsland))
		{
			continue;
		}

		uint16_t uiBeachElevation = rChunk.pHeader->islandHeader.uiBeachElevation;
		mQuads[iIndex++].f4Misc.x = common::UnormToFloat(uiBeachElevation);
		mfBeachElevation = common::UnormToFloat(uiBeachElevation);
		mfSeaFloorElevation = gWaterDepth.Get() * -mfBeachElevation;
	}

	while (iIndex < miCount)
	{
		mQuads[iIndex].f4Misc.x = mQuads[iIndex - 1].f4Misc.x;
		++iIndex;
	}

	mIslandsStorageBuffer.Create(
	{
		.pcName = "Islands",
		.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
		.iCount = miCount,
		.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
		.dataVkDeviceSize = miCount * sizeof(shaders::AxisAlignedQuadLayout),
	});

	memcpy(mIslandsStorageBuffer.mpMappedMemory, mQuads.data(), miCount * sizeof(shaders::AxisAlignedQuadLayout));
}

Islands::~Islands()
{
	gpIslands = nullptr;
}

void Islands::SetIslandsFlip(IslandsFlip eIslandsFlip)
{
	if (meCurrentIslandsFlip == eIslandsFlip)
	{
		return;
	}

	meCurrentIslandsFlip = eIslandsFlip;

	mbFlipX = meCurrentIslandsFlip == kFlipX || meCurrentIslandsFlip == kFlipXY ? true : false;
	mbFlipY = meCurrentIslandsFlip == kFlipY || meCurrentIslandsFlip == kFlipXY ? true : false;

	FillQuads();
	memcpy(mIslandsStorageBuffer.mpMappedMemory, mQuads.data(), miCount * sizeof(shaders::AxisAlignedQuadLayout));

	mbBuildGlobalHeightmap = true;
	BuildGlobalHeightmap();
}

void Islands::BuildGlobalHeightmap()
{
	if (!mbBuildGlobalHeightmap)
	{
		return;
	}
	mbBuildGlobalHeightmap = false;

	LOG("BuildGlobalHeightmap()");

	Texture globalElevationTexture(
	{
		.textureFlags = {TextureFlags::kRenderPass},
		.pcName = "Global elevation gpu",
		.flags = 0,
		.format = VK_FORMAT_R32_SFLOAT,
		.extent = VkExtent3D {kiGlobalHeightmapSize, kiGlobalHeightmapSize, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.renderPassVkClearColorValue = {mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = TextureLayout::kColorAttachment,
	});

	Pipeline globalElevationPipeline(
	{
		.pcName = "Global elevation",
		.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = globalElevationTexture.mVkRenderPass,
		.vkExtent3D = globalElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kStorageBuffer, .pBuffers = &mIslandsStorageBuffer},
			{.flags = DescriptorFlags::kCombinedSamplers, .iCount = gpIslands->miCount, .ppTextures = gpTextureManager->mElevationTextures.data()},
		},
	 });

	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(0).mpMappedMemory[0]);
	mf4GlobalArea.x = std::numeric_limits<float>::max();
	mf4GlobalArea.y = std::numeric_limits<float>::lowest();
	mf4GlobalArea.z = std::numeric_limits<float>::lowest();
	mf4GlobalArea.w = std::numeric_limits<float>::max();
	for (int64_t i = 0; i < miCount; ++i)
	{
		mf4GlobalArea.x = std::min(mf4GlobalArea.x, mQuads[i].f4VertexRect.x);
		mf4GlobalArea.y = std::max(mf4GlobalArea.y, mQuads[i].f4VertexRect.y);
		mf4GlobalArea.z = std::max(mf4GlobalArea.z, mQuads[i].f4VertexRect.x + mQuads[i].f4VertexRect.z);
		mf4GlobalArea.w = std::min(mf4GlobalArea.w, mQuads[i].f4VertexRect.y + mQuads[i].f4VertexRect.w);
	}
	rGlobalLayout.f4VisibleArea = mf4GlobalArea;
	rGlobalLayout.f4Terrain.x = gIslandHeight.Get();
	rGlobalLayout.f4TerrainTwo.x = gWaterDepth.Get();

	VkBuffer cpuVkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory cpuVkDeviceMemory = VK_NULL_HANDLE;
	Buffer::CreateBuffer("Global elevation cpu", kiGlobalHeightmapSize * kiGlobalHeightmapSize * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cpuVkBuffer, cpuVkDeviceMemory);
	VkBufferImageCopy vkBufferImageCopy =
	{
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = VkImageSubresourceLayers
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageOffset = VkOffset3D {0, 0, 0},
		.imageExtent = globalElevationTexture.mInfo.extent,
	};

	OneShotCommandBuffer oneShotCommandBuffer;
	int32_t piPushConstants[4] {0, 0, 0, 0};
	gpBufferManager->mGlobalLayoutUniformBuffers.at(0).RecordCopy(oneShotCommandBuffer.mVkCommandBuffer);
	vkCmdPushConstants(oneShotCommandBuffer.mVkCommandBuffer, globalElevationPipeline.mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, piPushConstants);
	globalElevationTexture.RecordBeginRenderPass(oneShotCommandBuffer.mVkCommandBuffer);
	globalElevationPipeline.RecordDraw(0, oneShotCommandBuffer.mVkCommandBuffer, gpIslands->miCount, 0);
	globalElevationTexture.RecordEndRenderPass(oneShotCommandBuffer.mVkCommandBuffer);
	vkDeviceWaitIdle(gpDeviceManager->mVkDevice);
	vkCmdCopyImageToBuffer(oneShotCommandBuffer.mVkCommandBuffer, globalElevationTexture.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cpuVkBuffer, 1, &vkBufferImageCopy);
	oneShotCommandBuffer.Execute(true);

	float* pfMappedMemory = nullptr;
	CHECK_VK(vkMapMemory(gpDeviceManager->mVkDevice, cpuVkDeviceMemory, 0, kiGlobalHeightmapSize * kiGlobalHeightmapSize * sizeof(float), 0, reinterpret_cast<void**>(&pfMappedMemory)));
	memcpy(mppfElevations, pfMappedMemory, kiGlobalHeightmapSize * kiGlobalHeightmapSize * sizeof(float));
	vkUnmapMemory(gpDeviceManager->mVkDevice, cpuVkDeviceMemory);
	vkDestroyBuffer(gpDeviceManager->mVkDevice, cpuVkBuffer, nullptr);
	vkFreeMemory(gpDeviceManager->mVkDevice, cpuVkDeviceMemory, nullptr);
}

void Islands::FillQuads()
{
	for (int64_t i = 0; i < miCount; ++i)
	{
		mQuads[i].f4VertexRect.x = game::Frame::kpfIslandPositions[i][0];
		mQuads[i].f4VertexRect.y = game::Frame::kpfIslandPositions[i][1];
		mQuads[i].f4VertexRect.z = game::Frame::kpfIslandPositions[i][2];
		mQuads[i].f4VertexRect.w = game::Frame::kpfIslandPositions[i][3];

		mQuads[i].f4TextureRect.x = mbFlipX ? 1.0f : 0.0f;
		mQuads[i].f4TextureRect.z = mbFlipX ? 0.0f : 1.0f;

		mQuads[i].f4TextureRect.y = mbFlipY ? 1.0f : 0.0f;
		mQuads[i].f4TextureRect.w = mbFlipY ? 0.0f : 1.0f;
	}
}

} // namespace engine
