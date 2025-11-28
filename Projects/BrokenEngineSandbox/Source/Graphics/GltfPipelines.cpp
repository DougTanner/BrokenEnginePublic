#include "GltfPipelines.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
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

void GltfPipelines::CreateGltfPipelineShadows()
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

// DT: TEMP Remove this once everything is on dynamic pipelines
void GltfPipelines::RecordGltfPipelines([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] VkCommandBuffer vkCommandBuffer)
{
	// Registered pipelines now have their own secondary buffers and record via callbacks in CreateGltfPipeline()
#if 0
	mpGltfPipelines[game::kGltfPipelinePlayerMissiles].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif
#if defined(ENABLE_GLTF_TEST)
	mpGltfPipelines[kGltfPipelineTest].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif
}

} // namespace game
