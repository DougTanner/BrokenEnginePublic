#include "GltfPipelines.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/ShaderManager.h"
#include "Profile/ProfileManager.h"

using engine::gpBufferManager;
using engine::gpPipelineManager;
using engine::gpShaderManager;
using engine::gpTextureManager;

namespace game
{

GltfPipelines::GltfPipelines()
{
	gpGltfPipelines = this;
}

GltfPipelines::~GltfPipelines()
{
	gpGltfPipelines = nullptr;
}

void GltfPipelines::CreateGltfShadowPipelines()
{
	mpGltfPipelines[game::kGltfPipelinePlayerMissilesShadow].Create(data::kGltfaim9_missilescenegltfCrc,
	{
		.pcName = "PlayerMissilesShadow",
		.flags = {engine::PipelineFlags::kRenderTarget, engine::PipelineFlags::kIndirectHostVisible, engine::PipelineFlags::kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfShadowfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfaim9_missilescenegltfGLTF_MODELCrc),
		.vkRenderPass = gpTextureManager->mObjectShadowsTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mObjectShadowsTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPlayerMissilesStorageBuffers.data()},
		},
	},
	false);
}

void GltfPipelines::CreateGltfPipelines()
{
	mpGltfPipelines[game::kGltfPipelinePlayerMissiles].Create(data::kGltfaim9_missilescenegltfCrc,
	{
		.pcName = "PlayerMissiles",
		.flags = {engine::PipelineFlags::kIndirectHostVisible, engine::PipelineFlags::kDepthTest, engine::PipelineFlags::kDepthWrite, engine::PipelineFlags::kCullBack, engine::PipelineFlags::kSampleShading, engine::PipelineFlags::kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltffragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfaim9_missilescenegltfGLTF_MODELCrc),
		.pDescriptorInfos =
		{
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPlayerMissilesStorageBuffers.data()},
		},
	});

#if defined(ENABLE_GLTF_TEST)
	mpGltfPipelines[kGltfPipelineTest].Create(data::kGltfspaceship2scenegltfCrc,
	{
		.pcName = "GltfTest",
		.flags = {engine::PipelineFlags::kIndirectHostVisible, engine::PipelineFlags::kPushConstants, engine::PipelineFlags::kDepthTest, engine::PipelineFlags::kDepthWrite, engine::PipelineFlags::kCullBack, engine::PipelineFlags::kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltffragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfspaceship2scenegltfGLTF_MODELCrc),
		.pDescriptorInfos =
		{
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = engine::DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mGltfsStorageBuffers.data()},
		},
	});
#endif
}

void GltfPipelines::RecordGltfShadowPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
#if 0
	mpGltfPipelines[game::kGltfPipelinePlayerMissilesShadow].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
#endif

	// Registered shadow pipelines
	for (engine::GltfPipeline* pPipeline : gpPipelineManager->mRegisteredGltfShadowPipelines)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
	}
}

void GltfPipelines::RecordGltfPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
#if 0
	mpGltfPipelines[game::kGltfPipelinePlayerMissiles].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif
#if defined(ENABLE_GLTF_TEST)
	mpGltfPipelines[kGltfPipelineTest].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif

	// Registered pipelines
	for (engine::GltfPipeline* pPipeline : gpPipelineManager->mRegisteredGltfPipelines)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}
}

} // namespace game
