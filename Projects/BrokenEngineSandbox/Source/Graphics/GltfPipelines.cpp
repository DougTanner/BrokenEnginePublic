#include "GltfPipelines.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/ShaderManager.h"
#include "Profile/ProfileManager.h"

using enum engine::DescriptorFlags;
using enum engine::PipelineFlags;
using namespace engine;

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
		.flags = {kRenderTarget, kIndirectHostVisible, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfShadowfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfaim9_missilescenegltfGLTF_MODELCrc),
		.vkRenderPass = gpTextureManager->mObjectShadowsTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mObjectShadowsTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPlayerMissilesStorageBuffers.data()},
		},
	},
	false);

	mpGltfPipelines[game::kGltfPipelinePlayerShadow].Create(data::kGltfspaceship2scenegltfCrc,
	{
		.pcName = "PlayerShadow",
		.flags = {kRenderTarget, kIndirectHostVisible, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfShadowfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfspaceship2scenegltfGLTF_MODELCrc),
		.vkRenderPass = gpTextureManager->mObjectShadowsTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mObjectShadowsTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPlayerStorageBuffers.data()},
		},
	},
	false);
	mpGltfPipelines[game::kGltfPipelineSpaceshipsShadow].Create(data::kGltfSpaceshipscenegltfCrc,
	{
		.pcName = "SpaceshipsShadow",
		.flags = {kRenderTarget, kIndirectHostVisible, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfShadowfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfSpaceshipscenegltfGLTF_MODELCrc),
		.vkRenderPass = gpTextureManager->mObjectShadowsTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mObjectShadowsTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSpaceshipsStorageBuffers.data()},
		},
	},
	false);
}

void GltfPipelines::CreateGltfPipelines()
{
	mpGltfPipelines[game::kGltfPipelinePlayer].Create(data::kGltfspaceship2scenegltfCrc,
	{
		.pcName = "Player",
		.flags = {kIndirectHostVisible, kPushConstants, kDepthTest, kDepthWrite, kCullBack, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltffragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfspaceship2scenegltfGLTF_MODELCrc),
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPlayerStorageBuffers.data()},
		},
	});

	mpGltfPipelines[game::kGltfPipelinePlayerMissiles].Create(data::kGltfaim9_missilescenegltfCrc,
	{
		.pcName = "PlayerMissiles",
		.flags = {kIndirectHostVisible, kDepthTest, kDepthWrite, kCullBack, kSampleShading, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltffragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfaim9_missilescenegltfGLTF_MODELCrc),
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPlayerMissilesStorageBuffers.data()},
		},
	});

	mpGltfPipelines[game::kGltfPipelineSpaceships].Create(data::kGltfSpaceshipscenegltfCrc,
	{
		.pcName = "Spaceships",
		.flags = {kIndirectHostVisible, kPushConstants, kDepthTest, kDepthWrite, kCullBack, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltffragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfSpaceshipscenegltfGLTF_MODELCrc),
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSpaceshipsStorageBuffers.data()},
		},
	});

#if defined(ENABLE_GLTF_TEST)
	mpGltfPipelines[kGltfPipelineTest].Create(data::kGltfspaceship2scenegltfCrc,
	{
		.pcName = "GltfTest",
		.flags = {kIndirectHostVisible, kPushConstants, kDepthTest, kDepthWrite, kCullBack, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltffragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfspaceship2scenegltfGLTF_MODELCrc),
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mGltfsStorageBuffers.data()},
		},
	});
#endif
}

void GltfPipelines::RecordGltfShadowPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	mpGltfPipelines[game::kGltfPipelinePlayerMissilesShadow].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
	mpGltfPipelines[game::kGltfPipelinePlayerShadow].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
	mpGltfPipelines[game::kGltfPipelineSpaceshipsShadow].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
}

void GltfPipelines::RecordGltfPipelines(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	mpGltfPipelines[game::kGltfPipelinePlayerMissiles].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	mpGltfPipelines[game::kGltfPipelinePlayer].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	mpGltfPipelines[game::kGltfPipelineSpaceships].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#if defined(ENABLE_GLTF_TEST)
	mpGltfPipelines[kGltfPipelineTest].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif
}

} // namespace game
