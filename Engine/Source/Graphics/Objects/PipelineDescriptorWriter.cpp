#include "PipelineDescriptorWriter.h"

#include "Pipeline.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

namespace
{

// Check if a binding belongs to global Set 0 (must not register for per-pipeline descriptor updates)
bool BindingIsInSet0(const Pipeline& rPipeline, const PipelineInfo& rPipelineInfo, uint32_t uiBinding)
{
	if (rPipeline.mVkExternalDescriptorSetLayout == VK_NULL_HANDLE)
	{
		return false;
	}
	int64_t iBind = static_cast<int64_t>(uiBinding);
	const Shader* pFirstShader = rPipelineInfo.ppShaders[0];
	if (iBind < pFirstShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pFirstShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
	{
		return pFirstShader->mInfo.pDescriptorSetIndices[uiBinding] == 0;
	}
	if (rPipeline.mInfo.flags & kCompute)
	{
		return false;
	}
	const Shader* pSecondShader = rPipelineInfo.ppShaders[1];
	if (iBind < pSecondShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pSecondShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
	{
		return pSecondShader->mInfo.pDescriptorSetIndices[uiBinding] == 0;
	}
	return false;
}

// kPipelineTerrain's image-info footprint: 4 bindless arrays × shaders::kiMaxIslands (= 64) per array
// = 256, plus ~13 single-texture material samplers (rock, sand normals 0/1/2, sand, rock normals 0/1/2,
// ambient combine, smoke, etc.) = ~269 entries. 384 leaves headroom for a fifth bindless array or more
// per-material samplers before another bump is needed. Stack-allocated per Write() call (~9 KB).
constexpr int64_t kiMaxImageInfos = 384;

void WriteModelDescriptor(Pipeline& rPipeline, const PipelineInfo& rPipelineInfo, const DescriptorInfo& rDescriptorInfo, int64_t iFramebuffer, VkWriteDescriptorSet& rVkWriteDescriptorSet, VkWriteDescriptorSet* pVkWriteDescriptorSets, int64_t& riDescriptorCount, VkDescriptorImageInfo* pVkDescriptorImageInfos, int64_t& riImageInfoCount, VkDescriptorBufferInfo* pVkDescriptorBufferInfos, int64_t& riBufferInfoCount)
{
	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(rDescriptorInfo.crc);

	// Sampler for bindless texture array
	{
		VkDescriptorImageInfo& rVkDescriptorImageInfo = pVkDescriptorImageInfos[riImageInfoCount++];
		ASSERT(riImageInfoCount < kiMaxImageInfos);
		rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
		rVkDescriptorImageInfo.imageView = nullptr;
		rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(riDescriptorCount);
		rVkWriteDescriptorSet.descriptorCount = 1;
		rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
		rVkWriteDescriptorSet.pBufferInfo = nullptr;

		pVkWriteDescriptorSets[riDescriptorCount++] = rVkWriteDescriptorSet;
		ASSERT(riDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

		if (iFramebuffer == 0 && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(riDescriptorCount - 1)) && !BindingIsInSet0(rPipeline, rPipelineInfo, static_cast<uint32_t>(riDescriptorCount - 1)))
		{
			gpTextureManager->mTextureDescriptors.RegisterStandaloneSamplerBinding(&rPipeline, riDescriptorCount - 1, kSamplerRepeat);
		}
	}

	// Bindless texture array
	{
		rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(riDescriptorCount);
		rVkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mTextureDescriptors.mImageInfos.size());
		rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		rVkWriteDescriptorSet.pImageInfo = gpTextureManager->mTextureDescriptors.mImageInfos.data();
		rVkWriteDescriptorSet.pBufferInfo = nullptr;

		pVkWriteDescriptorSets[riDescriptorCount++] = rVkWriteDescriptorSet;
		ASSERT(riDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	}

	// Irradiance
	Texture& rIrradianceTexture = gpTextureManager->mTextureMap.at(TextureManager::kIrradianceCrc);
	VkDescriptorImageInfo& rVkDescriptorImageInfoIrradiance = pVkDescriptorImageInfos[riImageInfoCount++];
	ASSERT(riImageInfoCount < kiMaxImageInfos);
	rVkDescriptorImageInfoIrradiance.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
	rVkDescriptorImageInfoIrradiance.imageView = rIrradianceTexture.mVkImageView;
	rVkDescriptorImageInfoIrradiance.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(riDescriptorCount);
	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoIrradiance;
	rVkWriteDescriptorSet.pBufferInfo = nullptr;

	pVkWriteDescriptorSets[riDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(riDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

	if (iFramebuffer == 0 && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(riDescriptorCount - 1)) && !BindingIsInSet0(rPipeline, rPipelineInfo, static_cast<uint32_t>(riDescriptorCount - 1)))
	{
		gpTextureManager->mTextureDescriptors.RegisterTextureBinding(TextureManager::kIrradianceCrc, &rPipeline, riDescriptorCount - 1, kSamplerRepeat, &rIrradianceTexture);
	}

	// PreFiltered
	Texture& rPreFilteredTexture = gpTextureManager->mTextureMap.at(TextureManager::kPrefilteredCrc);
	VkDescriptorImageInfo& rVkDescriptorImageInfoPreFiltered = pVkDescriptorImageInfos[riImageInfoCount++];
	ASSERT(riImageInfoCount < kiMaxImageInfos);
	rVkDescriptorImageInfoPreFiltered.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
	rVkDescriptorImageInfoPreFiltered.imageView = rPreFilteredTexture.mVkImageView;
	rVkDescriptorImageInfoPreFiltered.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(riDescriptorCount);
	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoPreFiltered;
	rVkWriteDescriptorSet.pBufferInfo = nullptr;

	pVkWriteDescriptorSets[riDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(riDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

	if (iFramebuffer == 0 && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(riDescriptorCount - 1)) && !BindingIsInSet0(rPipeline, rPipelineInfo, static_cast<uint32_t>(riDescriptorCount - 1)))
	{
		gpTextureManager->mTextureDescriptors.RegisterTextureBinding(TextureManager::kPrefilteredCrc, &rPipeline, riDescriptorCount - 1, kSamplerRepeat, &rPreFilteredTexture);
	}

	// LutBrdf
	VkDescriptorImageInfo& rVkDescriptorImageInfoLutBrdf = pVkDescriptorImageInfos[riImageInfoCount++];
	ASSERT(riImageInfoCount < kiMaxImageInfos);
	rVkDescriptorImageInfoLutBrdf.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
	rVkDescriptorImageInfoLutBrdf.imageView = gpTextureManager->mTextureCache.mPbrLutBrdfTexture.mVkImageView;
	rVkDescriptorImageInfoLutBrdf.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(riDescriptorCount);
	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoLutBrdf;
	rVkWriteDescriptorSet.pBufferInfo = nullptr;

	pVkWriteDescriptorSets[riDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(riDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

	if (iFramebuffer == 0 && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(riDescriptorCount - 1)) && !BindingIsInSet0(rPipeline, rPipelineInfo, static_cast<uint32_t>(riDescriptorCount - 1)))
	{
		gpTextureManager->mTextureDescriptors.RegisterTextureBinding(0, &rPipeline, riDescriptorCount - 1, kSamplerRepeat, &gpTextureManager->mTextureCache.mPbrLutBrdfTexture);
	}

	// Materials
	if (rPipeline.mModelMaterialsStorageBuffer.mDeviceLocalVkBuffer == VK_NULL_HANDLE)
	{
		rPipeline.mModelMaterialsStorageBuffer.Create(
		{
			.name = "Materials",
			.flags = {BufferFlags::kStorage, BufferFlags::kDeviceLocal},
			.dataVkDeviceSize = rChunk.pHeader->sceneHeader.uiMaterialCount * sizeof(shaders::PbrMaterialLayout),
		},
		[&](void* pData)
		{
			const common::SceneHeader& rSceneHeader = rChunk.pHeader->sceneHeader;
			int64_t iArraysSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiTextureCount * static_cast<int64_t>(sizeof(common::crc_t)))
			                    + common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
			const common::crc_t* pTextureCrcs = reinterpret_cast<const common::crc_t*>(rChunk.pData);
			const common::MaterialShaderData* pMaterialShaderData = reinterpret_cast<const common::MaterialShaderData*>(rChunk.pData + iArraysSize);
			shaders::PbrMaterialLayout* pCurrent = static_cast<shaders::PbrMaterialLayout*>(pData);
			const int64_t kiOldMaterialSize = offsetof(shaders::PbrMaterialLayout, fColorTextureIndex);
			for (int64_t j = 0; j < rSceneHeader.uiMaterialCount; ++j)
			{
				memcpy(pCurrent, &pMaterialShaderData[j].f4BaseColorFactor, kiOldMaterialSize);
				pCurrent->fColorTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiColorTextureIndex]);
				pCurrent->fPhysicalDescriptorTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiPhysicalDescriptorTextureIndex]);
				pCurrent->fNormalTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiNormalTextureIndex]);
				pCurrent->fOcclusionTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiOcclusionTextureIndex]);
				pCurrent->fEmissiveTextureIndex = gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiEmissiveTextureIndex]);
				pCurrent++;
			}
		});
	}

	VkDescriptorBufferInfo& rVkDescriptorBufferInfo = pVkDescriptorBufferInfos[riBufferInfoCount++];
	ASSERT(riBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	rVkDescriptorBufferInfo.buffer = rPipeline.mModelMaterialsStorageBuffer.mDeviceLocalVkBuffer;
	rVkDescriptorBufferInfo.offset = 0;
	rVkDescriptorBufferInfo.range = VK_WHOLE_SIZE;

	rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(riDescriptorCount);
	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	rVkWriteDescriptorSet.pImageInfo = nullptr;
	rVkWriteDescriptorSet.pBufferInfo = &rVkDescriptorBufferInfo;
}

void FilterWritesByShaderLayout(const Pipeline& rPipeline, const PipelineInfo& rPipelineInfo, VkWriteDescriptorSet* pVkWriteDescriptorSets, int64_t& riDescriptorCount)
{
	bool bCompute = rPipeline.mInfo.flags & kCompute;
	Shader* pFirstShader = rPipelineInfo.ppShaders[0];
	Shader* pSecondShader = bCompute ? nullptr : rPipelineInfo.ppShaders[1];

	int64_t iValidCount = 0;
	for (int64_t j = 0; j < riDescriptorCount; ++j)
	{
		uint32_t uiBinding = pVkWriteDescriptorSets[j].dstBinding;
		if (uiBinding < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings)
		{
			int64_t iBind = static_cast<int64_t>(uiBinding);
			const VkDescriptorSetLayoutBinding& rFirstBinding = iBind < pFirstShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pFirstShader->mInfo.pDescriptorBindings[uiBinding] : Pipeline::kEmptyBinding;
			const VkDescriptorSetLayoutBinding& rSecondBinding = pSecondShader != nullptr && iBind < pSecondShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pSecondShader->mInfo.pDescriptorBindings[uiBinding] : Pipeline::kEmptyBinding;
			if (rFirstBinding.descriptorCount > 0 || rSecondBinding.descriptorCount > 0)
			{
				pVkWriteDescriptorSets[iValidCount++] = pVkWriteDescriptorSets[j];
			}
		}
	}
	riDescriptorCount = iValidCount;
}

void RouteWritesBySet(const Pipeline& rPipeline, const PipelineInfo& rPipelineInfo, VkWriteDescriptorSet* pVkWriteDescriptorSets, int64_t& riDescriptorCount, VkDescriptorSet vkDstSetSet2, bool bHasExternalSet1)
{
	bool bCompute = rPipeline.mInfo.flags & kCompute;
	Shader* pFirstShader = rPipelineInfo.ppShaders[0];
	Shader* pSecondShader = bCompute ? nullptr : rPipelineInfo.ppShaders[1];

	int64_t iValidCount = 0;
	for (int64_t j = 0; j < riDescriptorCount; ++j)
	{
		uint32_t uiBinding = pVkWriteDescriptorSets[j].dstBinding;
		uint32_t uiSet = 0;
		int64_t iBind = static_cast<int64_t>(uiBinding);
		if (iBind < pFirstShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pFirstShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
		{
			uiSet = pFirstShader->mInfo.pDescriptorSetIndices[uiBinding];
		}
		else if (pSecondShader != nullptr && iBind < pSecondShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pSecondShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
		{
			uiSet = pSecondShader->mInfo.pDescriptorSetIndices[uiBinding];
		}

		if (uiSet == 2)
		{
			pVkWriteDescriptorSets[j].dstSet = vkDstSetSet2;
			pVkWriteDescriptorSets[iValidCount++] = pVkWriteDescriptorSets[j];
		}
		else if (uiSet == 1 && !bHasExternalSet1)
		{
			pVkWriteDescriptorSets[iValidCount++] = pVkWriteDescriptorSets[j];
		}
		// Set 0 writes are dropped (handled by global descriptor set)
	}
	riDescriptorCount = iValidCount;
}

} // anonymous namespace

bool PipelineDescriptorWriter::BindingExistsInShaderLayout(const Pipeline& rPipeline, uint32_t uiBinding)
{
	if (uiBinding >= common::ShaderHeader::kiMaxDescriptorSetLayoutBindings)
	{
		return false;
	}
	int64_t iBind = static_cast<int64_t>(uiBinding);
	const VkDescriptorSetLayoutBinding& rFirstBinding = iBind < rPipeline.mInfo.ppShaders[0]->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? rPipeline.mInfo.ppShaders[0]->mInfo.pDescriptorBindings[uiBinding] : Pipeline::kEmptyBinding;
	if (rPipeline.mInfo.flags & kCompute)
	{
		return rFirstBinding.descriptorCount > 0;
	}
	const VkDescriptorSetLayoutBinding& rSecondBinding = iBind < rPipeline.mInfo.ppShaders[1]->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? rPipeline.mInfo.ppShaders[1]->mInfo.pDescriptorBindings[uiBinding] : Pipeline::kEmptyBinding;
	return rFirstBinding.descriptorCount > 0 || rSecondBinding.descriptorCount > 0;
}

void PipelineDescriptorWriter::Write(Pipeline& rPipeline, const PipelineInfo& rPipelineInfo)
{
	bool bMultiSet = rPipeline.mInfo.flags & kMultiSet;
	bool bHasExternalSet0 = rPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE;
	bool bHasExternalSet1 = rPipeline.mVkExternalDescriptorSetLayoutSet1 != VK_NULL_HANDLE;

	int64_t iPerCommandBuffer = rPipeline.mbPerCommandBuffer ? gpSwapchainManager->mFramebuffers.size() : 1;
	if (!bHasExternalSet1)
	{
		rPipeline.mVkDescriptorSets.resize(iPerCommandBuffer);
	}
	if (bMultiSet)
	{
		rPipeline.mVkDescriptorSetsSet2.resize(iPerCommandBuffer);
	}

	for (int64_t iFramebuffer = 0; iFramebuffer < iPerCommandBuffer; ++iFramebuffer)
	{
		VkDescriptorPool vkDescriptorPool = gpDeviceManager->mVkDescriptorPool;

		// Allocate Set 1 descriptor set (or single set for compute pipelines)
		VkDescriptorSet vkDescriptorSet = VK_NULL_HANDLE;
		if (!bHasExternalSet1)
		{
			VkDescriptorSetAllocateInfo vkDescriptorSetAllocateInfo
			{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = vkDescriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &rPipeline.mVkDescriptorSetLayout,
			};
			CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &vkDescriptorSetAllocateInfo, &rPipeline.mVkDescriptorSets.at(iFramebuffer)));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET, rPipeline.mVkDescriptorSets.at(iFramebuffer), std::format("{}{}", rPipeline.mInfo.name.data(), iFramebuffer).c_str());
			vkDescriptorSet = rPipeline.mVkDescriptorSets.at(iFramebuffer);
		}

		// Allocate Set 2 descriptor set (multi-set models only)
		VkDescriptorSet vkDstSetSet2 = VK_NULL_HANDLE;
		if (bMultiSet)
		{
			VkDescriptorSetAllocateInfo vkDescriptorSetAllocateInfoSet2
			{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = vkDescriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &rPipeline.mVkDescriptorSetLayoutSet2,
			};
			CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &vkDescriptorSetAllocateInfoSet2, &rPipeline.mVkDescriptorSetsSet2.at(iFramebuffer)));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET, rPipeline.mVkDescriptorSetsSet2.at(iFramebuffer), std::format("{}Set2{}", rPipeline.mInfo.name.data(), iFramebuffer).c_str());
			vkDstSetSet2 = rPipeline.mVkDescriptorSetsSet2.at(iFramebuffer);
		}

		int64_t iDescriptorCount = 0;
		VkWriteDescriptorSet pVkWriteDescriptorSets[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		int64_t iImageInfoCount = 0;
		VkDescriptorImageInfo pVkDescriptorImageInfos[kiMaxImageInfos] {};
		int64_t iBufferInfoCount = 0;
		VkDescriptorBufferInfo pVkDescriptorBufferInfos[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++i)
		{
			const DescriptorInfo& rDescriptorInfo = rPipelineInfo.pDescriptorInfos[i];
			if (rDescriptorInfo.flags & kEmpty)
			{
				break;
			}

			// kBindlessArrayConsumer routing lives inside the kCombinedSamplers + non-Set-0 branch
			// at line ~435 — a flag without kCombinedSamplers would silently skip registration and
			// trip IslandTerrain::AcquireTextureSlot's ASSERT at first-mint instead of failing here.
			ASSERT(!(rDescriptorInfo.flags & kBindlessArrayConsumer) || (rDescriptorInfo.flags & kCombinedSamplers));

			// Use explicit binding if specified, otherwise use sequential counter.
			// iRegisterBinding is the int64_t form passed to deferred-registration call sites below;
			// using the same named local everywhere prevents the historical iDescriptorCount/uiBinding
			// confusion that caused VUID-00316 on WaterSkyboxOne.
			uint32_t uiBinding = rDescriptorInfo.iExplicitBinding >= 0 ? static_cast<uint32_t>(rDescriptorInfo.iExplicitBinding) : static_cast<uint32_t>(iDescriptorCount);
			const int64_t iRegisterBinding = static_cast<int64_t>(uiBinding);

			VkWriteDescriptorSet vkWriteDescriptorSet
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = vkDescriptorSet,
				.dstBinding = uiBinding,
				.dstArrayElement = 0,
				// .descriptorCount
				// .descriptorType
				// .pImageInfo
				// .pBufferInfo
				.pTexelBufferView = nullptr
			};

			bool bSampler = rDescriptorInfo.flags & kSamplerClamp || rDescriptorInfo.flags & kSamplerElevation || rDescriptorInfo.flags & kSamplerBorder || rDescriptorInfo.flags & kSamplerRepeat || rDescriptorInfo.flags & kSamplerMirroredRepeat || rDescriptorInfo.flags & kSamplerSmoke || rDescriptorInfo.flags & kSamplerWindClamp;
			if (rDescriptorInfo.flags & kModel)
			{
				WriteModelDescriptor(rPipeline, rPipelineInfo, rDescriptorInfo, iFramebuffer, vkWriteDescriptorSet, pVkWriteDescriptorSets, iDescriptorCount, pVkDescriptorImageInfos, iImageInfoCount, pVkDescriptorBufferInfos, iBufferInfoCount);
			}
			else if (rDescriptorInfo.flags & kUniformBuffer || rDescriptorInfo.flags & kStorageBuffer || rDescriptorInfo.flags & kPerCommandBufferUniformBuffers || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers)
			{
				VkBuffer vkBuffer = VK_NULL_HANDLE;
				if (rDescriptorInfo.flags & kUniformBuffer || rDescriptorInfo.flags & kStorageBuffer)
				{
					vkBuffer = rDescriptorInfo.pBuffers != nullptr ? rDescriptorInfo.pBuffers->GetBuffer() : *rDescriptorInfo.pVkBuffers;
				}
				else if (rDescriptorInfo.flags & kPerCommandBufferUniformBuffers || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers)
				{
					vkBuffer = rDescriptorInfo.pBuffers[iFramebuffer].GetBuffer();
				}
				ASSERT(vkBuffer != VK_NULL_HANDLE);

				VkDescriptorBufferInfo& rVkDescriptorBufferInfo = pVkDescriptorBufferInfos[iBufferInfoCount++];
				ASSERT(iBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
				rVkDescriptorBufferInfo.buffer = vkBuffer;
				rVkDescriptorBufferInfo.offset = 0;
				rVkDescriptorBufferInfo.range = VK_WHOLE_SIZE;

				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = rDescriptorInfo.flags & kStorageBuffer || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				vkWriteDescriptorSet.pImageInfo = nullptr;
				vkWriteDescriptorSet.pBufferInfo = &rVkDescriptorBufferInfo;
			}
			else if (bSampler && !(rDescriptorInfo.flags & kCombinedSamplers))
			{
				VkDescriptorImageInfo& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(rDescriptorInfo.flags);
				rVkDescriptorImageInfo.imageView = nullptr;
				rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				if (iFramebuffer == 0 && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, uiBinding) && !BindingIsInSet0(rPipeline, rPipelineInfo, uiBinding))
				{
					gpTextureManager->mTextureDescriptors.RegisterStandaloneSamplerBinding(&rPipeline, iRegisterBinding, rDescriptorInfo.flags);
				}
			}
			else if (rDescriptorInfo.flags & kTextures)
			{
				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mTextureDescriptors.mImageInfos.size());
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				vkWriteDescriptorSet.pImageInfo = gpTextureManager->mTextureDescriptors.mImageInfos.data();
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else if (rDescriptorInfo.flags & kCombinedSamplers || rDescriptorInfo.flags & kStorageImages)
			{
				VkDescriptorImageInfo* pStart = &pVkDescriptorImageInfos[iImageInfoCount];

				for (int64_t k = 0; k < rDescriptorInfo.iCount; ++k)
				{
					VkDescriptorImageInfo& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
					ASSERT(iImageInfoCount < kiMaxImageInfos);
					rVkDescriptorImageInfo.sampler = rDescriptorInfo.flags & kCombinedSamplers ? gpTextureManager->GetSampler(rDescriptorInfo.flags) : nullptr;

					if (rDescriptorInfo.textureCrc != 0)
					{
						rVkDescriptorImageInfo.imageView = gpTextureManager->mTextureMap.at(rDescriptorInfo.textureCrc).mVkImageView;
					}
					else if (rDescriptorInfo.iCount == 1 && rDescriptorInfo.pTexture != nullptr)
					{
						// Runtime-only texture (e.g. render targets, generated textures). Data-packed textures must use
						// the textureCrc path instead so they get deferred descriptor updates when lazy-loaded.
						ASSERT(rDescriptorInfo.pTexture->mInfo.crc == 0);
						rVkDescriptorImageInfo.imageView = rDescriptorInfo.pTexture->mVkImageView;
					}
					else
					{
						// Array of texture pointers
						rVkDescriptorImageInfo.imageView = rDescriptorInfo.ppTextures[k]->mVkImageView;
					}

					rVkDescriptorImageInfo.imageLayout = rDescriptorInfo.flags & kCombinedSamplers ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
				}

				// Register combined image sampler bindings for deferred texture and sampler descriptor updates
				if (iFramebuffer == 0 && rDescriptorInfo.flags & kCombinedSamplers && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, uiBinding) && !BindingIsInSet0(rPipeline, rPipelineInfo, uiBinding))
				{
					if (rDescriptorInfo.textureCrc != 0)
					{
						Texture* pTexture = &gpTextureManager->mTextureMap.at(rDescriptorInfo.textureCrc);
						gpTextureManager->mTextureDescriptors.RegisterTextureBinding(rDescriptorInfo.textureCrc, &rPipeline, iRegisterBinding, rDescriptorInfo.flags, pTexture);
						rPipeline.mTextureCrcs.push_back(rDescriptorInfo.textureCrc);
					}
					else if (rDescriptorInfo.iCount == 1 && rDescriptorInfo.pTexture != nullptr)
					{
						gpTextureManager->mTextureDescriptors.RegisterTextureBinding(0, &rPipeline, iRegisterBinding, rDescriptorInfo.flags, rDescriptorInfo.pTexture);
					}
					else if (rDescriptorInfo.ppTextures != nullptr)
					{
						if (rDescriptorInfo.flags & kBindlessArrayConsumer)
						{
							// Per-slot binding key is supplied lazily by the data subsystem (IslandTerrain
							// first-mint). Append this pipeline to the per-array consumer list; the per-CRC
							// loop is skipped because at create time every slot still points at the slot-0
							// placeholder and a per-slot registration under the placeholder CRC would be dead
							// weight (never patched post-boot). Sampler recreation reads through the live
							// array pointer (see TextureDescriptors::RewriteSamplerDescriptors), so no stale
							// CRC-0 snapshot is recorded here.
							ASSERT(BindingExistsInShaderLayout(rPipeline, uiBinding));
							gpTextureManager->mTextureDescriptors.mBindlessArrayConsumers.try_emplace(rDescriptorInfo.ppTextures).first->second.push_back(
								{&rPipeline, iRegisterBinding, rDescriptorInfo.flags, rDescriptorInfo.iCount});
						}
						else
						{
							// Register per-CRC entries for lazy texture loading and sampler updates.
							// NOTE: ppTextures is snapshotted into TextureBinding.textures here — any future
							// array consumer that mutates its backing storage in place after pipeline-create
							// MUST use kBindlessArrayConsumer instead; otherwise sampler recreation rewrites
							// the descriptor from the stale snapshot.
							for (int64_t k = 0; k < rDescriptorInfo.iCount; ++k)
							{
								common::crc_t arrayCrc = rDescriptorInfo.ppTextures[k]->mInfo.crc;
								if (arrayCrc != 0 && gpTextureManager->mTextureMap.contains(arrayCrc))
								{
									gpTextureManager->mTextureDescriptors.RegisterTextureBinding(arrayCrc, &rPipeline, iRegisterBinding, rDescriptorInfo.flags, nullptr, rDescriptorInfo.ppTextures, rDescriptorInfo.iCount);
									rPipeline.mTextureCrcs.push_back(arrayCrc);
								}
							}
							// Register under CRC 0 for sampler recreation coverage (texture array is copied into TextureBinding)
							gpTextureManager->mTextureDescriptors.RegisterTextureBinding(0, &rPipeline, iRegisterBinding, rDescriptorInfo.flags, nullptr, rDescriptorInfo.ppTextures, rDescriptorInfo.iCount);
						}
					}
				}

				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(rDescriptorInfo.iCount);
				vkWriteDescriptorSet.descriptorType = rDescriptorInfo.flags & kCombinedSamplers ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				vkWriteDescriptorSet.pImageInfo = pStart;
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else
			{
				ASSERT(false);
			}

			pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
			ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
		}

		// Filter writes to only include bindings that exist in the shader layout
		// This handles sparse bindings (e.g., GLTF shadow pipelines with bindings 0, 1, 2, 15)
		FilterWritesByShaderLayout(rPipeline, rPipelineInfo, pVkWriteDescriptorSets, iDescriptorCount);

		// Route writes by set index: drop Set 0 (global), keep Set 1 and Set 2
		if (bHasExternalSet0)
		{
			RouteWritesBySet(rPipeline, rPipelineInfo, pVkWriteDescriptorSets, iDescriptorCount, vkDstSetSet2, bHasExternalSet1);
		}

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, static_cast<uint32_t>(iDescriptorCount), pVkWriteDescriptorSets, 0, nullptr);
	}
}

void PipelineDescriptorWriter::UpdateStorageBuffer(Pipeline& rPipeline, int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer)
{
	// Guards against deferred-update callers (TextureDescriptors::Rewrite*) passing a binding
	// that the per-pipeline layout doesn't declare (e.g. confusing sequential descriptor index
	// with the explicit binding number). Pops the debugger before Vulkan validation fires.
	ASSERT(BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(iBinding)));

	VkDescriptorBufferInfo vkDescriptorBufferInfo
	{
		.buffer = pBuffer->GetBuffer(),
		.offset = 0,
		.range = VK_WHOLE_SIZE,
	};

	VkWriteDescriptorSet vkWriteDescriptorSet
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = rPipeline.mVkDescriptorSets.at(iFramebuffer),
		.dstBinding = static_cast<uint32_t>(iBinding),
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pImageInfo = nullptr,
		.pBufferInfo = &vkDescriptorBufferInfo,
		.pTexelBufferView = nullptr,
	};

	vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
}

void PipelineDescriptorWriter::UpdateCombinedImageSampler(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView, VkSampler vkSampler)
{
	ASSERT(BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(iBinding)));

	for (VkDescriptorSet& rVkDescriptorSet : rPipeline.mVkDescriptorSets)
	{
		VkDescriptorImageInfo vkDescriptorImageInfo
		{
			.sampler = vkSampler,
			.imageView = vkImageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &vkDescriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void PipelineDescriptorWriter::UpdateSampler(Pipeline& rPipeline, int64_t iBinding, VkSampler vkSampler)
{
	ASSERT(BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(iBinding)));

	for (VkDescriptorSet& rVkDescriptorSet : rPipeline.mVkDescriptorSets)
	{
		VkDescriptorImageInfo vkDescriptorImageInfo
		{
			.sampler = vkSampler,
			.imageView = nullptr,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.pImageInfo = &vkDescriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void PipelineDescriptorWriter::UpdateStorageImage(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView)
{
	ASSERT(BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(iBinding)));

	for (VkDescriptorSet& rVkDescriptorSet : rPipeline.mVkDescriptorSets)
	{
		VkDescriptorImageInfo vkDescriptorImageInfo
		{
			.sampler = VK_NULL_HANDLE,
			.imageView = vkImageView,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &vkDescriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

} // namespace engine
