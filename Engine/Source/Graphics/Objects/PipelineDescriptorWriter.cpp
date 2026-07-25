#if defined(BT_CLIENT)

#include "PipelineDescriptorWriter.h"

#include "Pipeline.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

namespace
{

// Check if a binding belongs to global Set 0 (must not register for per-pipeline descriptor updates)
bool BindingIsInSet0(const Pipeline& rPipeline, uint32_t uiBinding)
{
	if (rPipeline.mVkExternalDescriptorSetLayout == VK_NULL_HANDLE)
	{
		return false;
	}
	// Callers gate every BindingIsInSet0 with BindingExistsInShaderLayout (short-circuit), so the binding
	// is always declared by some shader here — ResolveBindingSetIndex's default-0 fallback never decides.
	return Pipeline::ResolveBindingSetIndex(rPipeline.mInfo, uiBinding) == 0;
}

// WriteModelDescriptor pushes this many single-texture image-infos (sampler, irradiance, prefiltered,
// lutbrdf); its bindless array reads gpTextureManager->mTextureDescriptors.mImageInfos.data() directly
// and does not touch the per-Write() image-info buffer. Used as the per-descriptor floor when sizing
// that buffer in Write() (see the iMaxImageInfos pre-scan).
constexpr int64_t kiModelDescriptorImageInfos = 4;

// Bundles the descriptor-write accumulator state threaded through Write()'s per-flag helpers and
// WriteModelDescriptor. All three array pointers are non-owning: the image-info buffer is workbuffer-
// backed and the write / buffer-info arrays are stack-local in Write(); the counts are mutated in place.
struct DescriptorWriteCursor
{
	VkWriteDescriptorSet* pWriteDescriptorSets;
	int64_t iDescriptorCount;
	VkDescriptorImageInfo* pImageInfos;
	int64_t iImageInfoCount;
	int64_t iMaxImageInfos;
	VkDescriptorBufferInfo* pBufferInfos;
	int64_t iBufferInfoCount;
};

// The framebuffer-0-only "this binding takes a deferred per-pipeline registration" gate, repeated at
// every register site. The extra kCombinedSamplers conjunct at the combined-sampler site stays at that
// call site (specific to that branch, not part of the general gate).
bool ShouldRegisterBinding(const Pipeline& rPipeline, int64_t iFramebuffer, uint32_t uiBinding)
{
	return iFramebuffer == 0 && PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, uiBinding) && !BindingIsInSet0(rPipeline, uiBinding);
}

// Emits one IBL combined-image-sampler write (irradiance / prefiltered / lutBRDF) — the three differ only
// in (registerCrc, source texture). Pushes the image-info + write into the cursor and registers the
// binding for deferred updates on framebuffer 0.
void PushCombinedImageSamplerWrite(Pipeline& rPipeline, int64_t iFramebuffer, VkWriteDescriptorSet& rVkWriteDescriptorSet, DescriptorWriteCursor& rCursor, common::crc_t registerCrc, Texture& rTexture)
{
	VkDescriptorImageInfo& rVkDescriptorImageInfo = rCursor.pImageInfos[rCursor.iImageInfoCount++];
	ASSERT(rCursor.iImageInfoCount <= rCursor.iMaxImageInfos);
	rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
	rVkDescriptorImageInfo.imageView = rTexture.mVkImageView;
	rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(rCursor.iDescriptorCount);
	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
	rVkWriteDescriptorSet.pBufferInfo = nullptr;

	rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

	if (ShouldRegisterBinding(rPipeline, iFramebuffer, static_cast<uint32_t>(rCursor.iDescriptorCount - 1)))
	{
		gpTextureManager->mTextureDescriptors.RegisterTextureBinding({.crc = registerCrc, .pPipeline = &rPipeline, .iBinding = rCursor.iDescriptorCount - 1, .samplerFlags = kSamplerRepeat, .pTexture = &rTexture});
	}
}

void WriteModelDescriptor(Pipeline& rPipeline, const DescriptorInfo& rDescriptorInfo, int64_t iFramebuffer, VkWriteDescriptorSet& rVkWriteDescriptorSet, DescriptorWriteCursor& rCursor)
{
	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(rDescriptorInfo.crc);

	// Sampler for bindless texture array
	{
		VkDescriptorImageInfo& rVkDescriptorImageInfo = rCursor.pImageInfos[rCursor.iImageInfoCount++];
		ASSERT(rCursor.iImageInfoCount <= rCursor.iMaxImageInfos);
		rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
		rVkDescriptorImageInfo.imageView = nullptr;
		rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(rCursor.iDescriptorCount);
		rVkWriteDescriptorSet.descriptorCount = 1;
		rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
		rVkWriteDescriptorSet.pBufferInfo = nullptr;

		rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
		ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

		if (ShouldRegisterBinding(rPipeline, iFramebuffer, static_cast<uint32_t>(rCursor.iDescriptorCount - 1)))
		{
			gpTextureManager->mTextureDescriptors.RegisterStandaloneSamplerBinding(&rPipeline, rCursor.iDescriptorCount - 1, kSamplerRepeat);
		}
	}

	// Bindless texture array
	{
		rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(rCursor.iDescriptorCount);
		rVkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mTextureDescriptors.mImageInfos.size());
		rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		rVkWriteDescriptorSet.pImageInfo = gpTextureManager->mTextureDescriptors.mImageInfos.data();
		rVkWriteDescriptorSet.pBufferInfo = nullptr;

		rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
		ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	}

	// Irradiance / PreFiltered / LutBrdf — three identical IBL combined-image-sampler writes
	PushCombinedImageSamplerWrite(rPipeline, iFramebuffer, rVkWriteDescriptorSet, rCursor, TextureManager::kIrradianceCrc, gpTextureManager->mTextureMap.at(TextureManager::kIrradianceCrc));
	PushCombinedImageSamplerWrite(rPipeline, iFramebuffer, rVkWriteDescriptorSet, rCursor, TextureManager::kPrefilteredCrc, gpTextureManager->mTextureMap.at(TextureManager::kPrefilteredCrc));
	PushCombinedImageSamplerWrite(rPipeline, iFramebuffer, rVkWriteDescriptorSet, rCursor, 0, gpTextureManager->mTextureCache.mPbrLutBrdfTexture);

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
			int64_t iArraysSize = common::SceneHeader::MaterialDataOffset(rSceneHeader.uiTextureCount, rSceneHeader.uiMaterialCount);
			const common::crc_t* pTextureCrcs = reinterpret_cast<const common::crc_t*>(rChunk.pData);
			const common::MaterialShaderData* pMaterialShaderData = reinterpret_cast<const common::MaterialShaderData*>(rChunk.pData + iArraysSize);
			shaders::PbrMaterialLayout* pCurrent = static_cast<shaders::PbrMaterialLayout*>(pData);
			const int64_t kiOldMaterialSize = offsetof(shaders::PbrMaterialLayout, fColorTextureIndex);
			for (int64_t j = 0; j < rSceneHeader.uiMaterialCount; ++j)
			{
				// Trust boundary: the five texture-index fields are on-disk uint8s that index the texture-CRC array
				// aliased over the scene chunk (pTextureCrcs[uiTextureCount]); an out-of-range index OOB-reads the
				// chunk. ModelPipeline::Create already bounded uiTextureCount/uiMaterialCount for this sceneCrc.
				const common::MaterialShaderData& rMaterial = pMaterialShaderData[j];
				if (rMaterial.uiColorTextureIndex >= rSceneHeader.uiTextureCount
					|| rMaterial.uiPhysicalDescriptorTextureIndex >= rSceneHeader.uiTextureCount
					|| rMaterial.uiNormalTextureIndex >= rSceneHeader.uiTextureCount
					|| rMaterial.uiOcclusionTextureIndex >= rSceneHeader.uiTextureCount
					|| rMaterial.uiEmissiveTextureIndex >= rSceneHeader.uiTextureCount)
				{
					throw common::CorruptStreamException("PipelineDescriptorWriter material");
				}
				std::memcpy(pCurrent, &pMaterialShaderData[j].f4BaseColorFactor, kiOldMaterialSize);
				pCurrent->fColorTextureIndex = static_cast<float>(gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiColorTextureIndex]));
				pCurrent->fPhysicalDescriptorTextureIndex = static_cast<float>(gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiPhysicalDescriptorTextureIndex]));
				pCurrent->fNormalTextureIndex = static_cast<float>(gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiNormalTextureIndex]));
				pCurrent->fOcclusionTextureIndex = static_cast<float>(gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiOcclusionTextureIndex]));
				pCurrent->fEmissiveTextureIndex = static_cast<float>(gpTextureManager->mTextureDescriptors.CrcToIndex(pTextureCrcs[pMaterialShaderData[j].uiEmissiveTextureIndex]));
				pCurrent++;
			}
		});
	}

	VkDescriptorBufferInfo& rVkDescriptorBufferInfo = rCursor.pBufferInfos[rCursor.iBufferInfoCount++];
	ASSERT(rCursor.iBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	rVkDescriptorBufferInfo.buffer = rPipeline.mModelMaterialsStorageBuffer.mDeviceLocalVkBuffer;
	rVkDescriptorBufferInfo.offset = 0;
	rVkDescriptorBufferInfo.range = VK_WHOLE_SIZE;

	rVkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(rCursor.iDescriptorCount);
	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	rVkWriteDescriptorSet.pImageInfo = nullptr;
	rVkWriteDescriptorSet.pBufferInfo = &rVkDescriptorBufferInfo;

	rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
}

void WriteBufferDescriptor(const DescriptorInfo& rDescriptorInfo, int64_t iFramebuffer, VkWriteDescriptorSet& rVkWriteDescriptorSet, DescriptorWriteCursor& rCursor)
{
	VkBuffer vkBuffer = VK_NULL_HANDLE;
	if (rDescriptorInfo.flags & kGlobalLayoutUniformBuffers)
	{
		vkBuffer = gpBufferManager->mGlobalLayoutUniformBuffers.at(static_cast<size_t>(iFramebuffer)).GetBuffer();
	}
	else if (rDescriptorInfo.flags & kMainLayoutUniformBuffers)
	{
		vkBuffer = gpBufferManager->mMainLayoutUniformBuffers.at(static_cast<size_t>(iFramebuffer)).GetBuffer();
	}
	else if (rDescriptorInfo.flags & kUniformBuffer || rDescriptorInfo.flags & kStorageBuffer)
	{
		vkBuffer = rDescriptorInfo.pBuffers != nullptr ? rDescriptorInfo.pBuffers->GetBuffer() : *rDescriptorInfo.pVkBuffers;
	}
	else if (rDescriptorInfo.flags & kPerCommandBufferUniformBuffers || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers)
	{
		vkBuffer = rDescriptorInfo.pBuffers[iFramebuffer].GetBuffer();
	}
	ASSERT(vkBuffer != VK_NULL_HANDLE);

	VkDescriptorBufferInfo& rVkDescriptorBufferInfo = rCursor.pBufferInfos[rCursor.iBufferInfoCount++];
	ASSERT(rCursor.iBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	rVkDescriptorBufferInfo.buffer = vkBuffer;
	rVkDescriptorBufferInfo.offset = 0;
	rVkDescriptorBufferInfo.range = VK_WHOLE_SIZE;

	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = rDescriptorInfo.flags & kStorageBuffer || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	rVkWriteDescriptorSet.pImageInfo = nullptr;
	rVkWriteDescriptorSet.pBufferInfo = &rVkDescriptorBufferInfo;

	rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
}

void WriteStandaloneSampler(Pipeline& rPipeline, const DescriptorInfo& rDescriptorInfo, int64_t iFramebuffer, uint32_t uiBinding, int64_t iRegisterBinding, VkWriteDescriptorSet& rVkWriteDescriptorSet, DescriptorWriteCursor& rCursor)
{
	VkDescriptorImageInfo& rVkDescriptorImageInfo = rCursor.pImageInfos[rCursor.iImageInfoCount++];
	ASSERT(rCursor.iImageInfoCount <= rCursor.iMaxImageInfos);
	rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(rDescriptorInfo.flags);
	rVkDescriptorImageInfo.imageView = nullptr;
	rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	rVkWriteDescriptorSet.descriptorCount = 1;
	rVkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	rVkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
	rVkWriteDescriptorSet.pBufferInfo = nullptr;

	if (ShouldRegisterBinding(rPipeline, iFramebuffer, uiBinding))
	{
		gpTextureManager->mTextureDescriptors.RegisterStandaloneSamplerBinding(&rPipeline, iRegisterBinding, rDescriptorInfo.flags);
	}

	rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
}

// Fills the per-element image infos for a combined-image-sampler / storage-image array and sets the
// write's type/count/pImageInfo. Registration of the bindings (combined samplers only) is a separate
// concern handled by RegisterCombinedSamplerBindings at the call site.
void WriteCombinedSamplers(const DescriptorInfo& rDescriptorInfo, VkWriteDescriptorSet& rVkWriteDescriptorSet, DescriptorWriteCursor& rCursor)
{
	VkDescriptorImageInfo* pStart = &rCursor.pImageInfos[rCursor.iImageInfoCount];

	for (int64_t k = 0; k < rDescriptorInfo.iCount; ++k)
	{
		VkDescriptorImageInfo& rVkDescriptorImageInfo = rCursor.pImageInfos[rCursor.iImageInfoCount++];
		ASSERT(rCursor.iImageInfoCount <= rCursor.iMaxImageInfos);
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
			// Array of texture pointers. A slot mid-reload after eviction points at an mTextureMap
			// entry whose view FreeGpuResources destroyed and AdoptTransferredImage has not yet
			// re-attached (post-eviction re-mint window) — a pipeline rebuild (settings/fullscreen
			// recreate) snapshotting that null view trips VUID-02997. Fall back to the array's
			// slot-0 placeholder; the adoption path patches the real view in afterward.
			VkImageView vkImageView = rDescriptorInfo.ppTextures[k]->mVkImageView;
			rVkDescriptorImageInfo.imageView = vkImageView != VK_NULL_HANDLE ? vkImageView : rDescriptorInfo.ppTextures[0]->mVkImageView;
		}

		rVkDescriptorImageInfo.imageLayout = rDescriptorInfo.flags & kCombinedSamplers ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
	}

	rVkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(rDescriptorInfo.iCount);
	rVkWriteDescriptorSet.descriptorType = rDescriptorInfo.flags & kCombinedSamplers ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	rVkWriteDescriptorSet.pImageInfo = pStart;
	rVkWriteDescriptorSet.pBufferInfo = nullptr;

	rCursor.pWriteDescriptorSets[rCursor.iDescriptorCount++] = rVkWriteDescriptorSet;
	ASSERT(rCursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
}

// Register combined image sampler bindings for deferred texture and sampler descriptor updates.
// These plant raw Pipeline* back-references into gpTextureManager->mTextureDescriptors that are
// symmetrically unregistered by Pipeline::Destroy. A whole-PipelineManager ClearTextureBindings()
// remains defensive; rebuilt bindless consumers replay registrations for live island slots.
void RegisterCombinedSamplerBindings(Pipeline& rPipeline, const DescriptorInfo& rDescriptorInfo, int64_t iRegisterBinding)
{
	if (rDescriptorInfo.textureCrc != 0)
	{
		Texture* pTexture = &gpTextureManager->mTextureMap.at(rDescriptorInfo.textureCrc);
		gpTextureManager->mTextureDescriptors.RegisterTextureBinding({.crc = rDescriptorInfo.textureCrc, .pPipeline = &rPipeline, .iBinding = iRegisterBinding, .samplerFlags = rDescriptorInfo.flags, .pTexture = pTexture});
		rPipeline.mTextureCrcs.push_back(rDescriptorInfo.textureCrc);
	}
	else if (rDescriptorInfo.iCount == 1 && rDescriptorInfo.pTexture != nullptr)
	{
		gpTextureManager->mTextureDescriptors.RegisterTextureBinding({.crc = 0, .pPipeline = &rPipeline, .iBinding = iRegisterBinding, .samplerFlags = rDescriptorInfo.flags, .pTexture = rDescriptorInfo.pTexture});
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
			gpTextureManager->mTextureDescriptors.RegisterBindlessArrayConsumer(rDescriptorInfo.ppTextures, &rPipeline, iRegisterBinding, rDescriptorInfo.flags, rDescriptorInfo.iCount);
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
					gpTextureManager->mTextureDescriptors.RegisterTextureBinding({.crc = arrayCrc, .pPipeline = &rPipeline, .iBinding = iRegisterBinding, .samplerFlags = rDescriptorInfo.flags, .ppTextures = rDescriptorInfo.ppTextures, .iCount = rDescriptorInfo.iCount});
					rPipeline.mTextureCrcs.push_back(arrayCrc);
				}
			}
			// Register under CRC 0 for sampler recreation coverage (texture array is copied into TextureBinding)
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding({.crc = 0, .pPipeline = &rPipeline, .iBinding = iRegisterBinding, .samplerFlags = rDescriptorInfo.flags, .ppTextures = rDescriptorInfo.ppTextures, .iCount = rDescriptorInfo.iCount});
		}
	}
}

void FilterWritesByShaderLayout(const Pipeline& rPipeline, VkWriteDescriptorSet* pVkWriteDescriptorSets, int64_t& riDescriptorCount)
{
	bool bCompute = rPipeline.mInfo.flags & kCompute;
	Shader* pFirstShader = rPipeline.mInfo.ppShaders[0];
	Shader* pSecondShader = bCompute ? nullptr : rPipeline.mInfo.ppShaders[1];

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

void RouteWritesBySet(const PipelineInfo& rPipelineInfo, VkWriteDescriptorSet* pVkWriteDescriptorSets, int64_t& riDescriptorCount, VkDescriptorSet vkDstSetSet2, bool bHasExternalSet1)
{
	int64_t iValidCount = 0;
	for (int64_t j = 0; j < riDescriptorCount; ++j)
	{
		uint32_t uiBinding = pVkWriteDescriptorSets[j].dstBinding;
		uint32_t uiSet = Pipeline::ResolveBindingSetIndex(rPipelineInfo, uiBinding);

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

void PipelineDescriptorWriter::Write(Pipeline& rPipeline)
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

	// Upper bound on the image-infos this pipeline's descriptors push into the per-Write() buffer below.
	// Safe over-estimate that reads only iCount: each Write branch pushes at most max(iCount, 4) per
	// descriptor (model = 4, combined/storage = iCount, standalone sampler = 1, buffer/texture = 0).
	// Scales with shaders::kiMaxIslands (4 bindless terrain arrays) without a hand-tuned constant.
	int64_t iMaxImageInfos = 0;
	for (int64_t i = 0; i < static_cast<int64_t>(rPipeline.mInfo.pDescriptorInfos.size()); ++i)
	{
		const DescriptorInfo& rDescriptorInfo = rPipeline.mInfo.pDescriptorInfos[i];
		if (rDescriptorInfo.flags & kEmpty)
		{
			break;
		}
		iMaxImageInfos += std::max(rDescriptorInfo.iCount, kiModelDescriptorImageInfos);
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

		VkWriteDescriptorSet pVkWriteDescriptorSets[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorBufferInfo pVkDescriptorBufferInfos[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		// Sized to the pipeline's actual need from the per-thread workbuffer (Write() runs only at
		// startup / device-loss / settings recreate). The 10 MB main-thread workbuffer never grows for
		// this; the pointer stays stable across nested workbuffer use in WriteModelDescriptor (Grow()
		// DEBUG_BREAKs + reallocates, so a stable pointer is the documented contract). Only the entries
		// Vulkan reads (bounded by descriptorCount) are referenced and all are fully written, so the
		// buffer needs no zero-init. Mirrors TextureDescriptors::WriteFullArrayDescriptors.
		auto pVkDescriptorImageInfos = common::gpThreadLocal->mWorkbuffer.PushBuffer<VkDescriptorImageInfo*>(iMaxImageInfos * static_cast<int64_t>(sizeof(VkDescriptorImageInfo)));

		DescriptorWriteCursor cursor
		{
			.pWriteDescriptorSets = pVkWriteDescriptorSets,
			.iDescriptorCount = 0,
			.pImageInfos = pVkDescriptorImageInfos,
			.iImageInfoCount = 0,
			.iMaxImageInfos = iMaxImageInfos,
			.pBufferInfos = pVkDescriptorBufferInfos,
			.iBufferInfoCount = 0,
		};
		for (int64_t i = 0; i < static_cast<int64_t>(rPipeline.mInfo.pDescriptorInfos.size()); ++i)
		{
			const DescriptorInfo& rDescriptorInfo = rPipeline.mInfo.pDescriptorInfos[i];
			if (rDescriptorInfo.flags & kEmpty)
			{
				break;
			}

			// kBindlessArrayConsumer routing lives inside RegisterCombinedSamplerBindings (the
			// kCombinedSamplers + non-Set-0 branch) — a flag without kCombinedSamplers would silently
			// skip registration and trip IslandTerrain::AcquireTextureSlot's ASSERT at first-mint
			// instead of failing here.
			ASSERT(!(rDescriptorInfo.flags & kBindlessArrayConsumer) || (rDescriptorInfo.flags & kCombinedSamplers));

			// Use explicit binding if specified, otherwise use sequential counter.
			// iRegisterBinding is the int64_t form passed to deferred-registration call sites below;
			// using the same named local everywhere prevents iDescriptorCount/uiBinding confusion
			// (VUID-00316).
			uint32_t uiBinding = rDescriptorInfo.iExplicitBinding >= 0 ? static_cast<uint32_t>(rDescriptorInfo.iExplicitBinding) : static_cast<uint32_t>(cursor.iDescriptorCount);
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

			bool bSampler = rDescriptorInfo.flags & kSamplerAny;
			if (rDescriptorInfo.flags & kModel)
			{
				WriteModelDescriptor(rPipeline, rDescriptorInfo, iFramebuffer, vkWriteDescriptorSet, cursor);
			}
			else if (rDescriptorInfo.flags & kUniformBuffer || rDescriptorInfo.flags & kStorageBuffer || rDescriptorInfo.flags & kPerCommandBufferUniformBuffers || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers || rDescriptorInfo.flags & kGlobalLayoutUniformBuffers || rDescriptorInfo.flags & kMainLayoutUniformBuffers)
			{
				WriteBufferDescriptor(rDescriptorInfo, iFramebuffer, vkWriteDescriptorSet, cursor);
			}
			else if (bSampler && !(rDescriptorInfo.flags & kCombinedSamplers))
			{
				WriteStandaloneSampler(rPipeline, rDescriptorInfo, iFramebuffer, uiBinding, iRegisterBinding, vkWriteDescriptorSet, cursor);
			}
			else if (rDescriptorInfo.flags & kTextures)
			{
				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mTextureDescriptors.mImageInfos.size());
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				vkWriteDescriptorSet.pImageInfo = gpTextureManager->mTextureDescriptors.mImageInfos.data();
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				cursor.pWriteDescriptorSets[cursor.iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(cursor.iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
			}
			else if (rDescriptorInfo.flags & kCombinedSamplers || rDescriptorInfo.flags & kStorageImages)
			{
				WriteCombinedSamplers(rDescriptorInfo, vkWriteDescriptorSet, cursor);

				if (rDescriptorInfo.flags & kCombinedSamplers && ShouldRegisterBinding(rPipeline, iFramebuffer, uiBinding))
				{
					RegisterCombinedSamplerBindings(rPipeline, rDescriptorInfo, iRegisterBinding);
				}
			}
			else
			{
				ASSERT(false);
			}

			// Each branch above pushes its own write(s) into the cursor (unified helper-completion
			// contract) — there is no unconditional outer push here.
		}

		// Filter writes to only include bindings that exist in the shader layout
		// This handles sparse bindings (e.g., GLTF shadow pipelines with bindings 0, 1, 2, 15)
		FilterWritesByShaderLayout(rPipeline, cursor.pWriteDescriptorSets, cursor.iDescriptorCount);

		// Route writes by set index: drop Set 0 (global), keep Set 1 and Set 2
		if (bHasExternalSet0)
		{
			RouteWritesBySet(rPipeline.mInfo, cursor.pWriteDescriptorSets, cursor.iDescriptorCount, vkDstSetSet2, bHasExternalSet1);
		}

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, static_cast<uint32_t>(cursor.iDescriptorCount), cursor.pWriteDescriptorSets, 0, nullptr);
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

namespace
{

// Shared body for the three single-image deferred descriptor updates below (combined-image-sampler / sampler /
// storage-image). They differ only in descriptorType, imageLayout, and which of sampler / imageView is
// populated (the other is a null handle). Rewrites the binding across every per-framebuffer descriptor set.
void UpdateImageDescriptor(Pipeline& rPipeline, int64_t iBinding, VkSampler vkSampler, VkImageView vkImageView, VkImageLayout vkImageLayout, VkDescriptorType vkDescriptorType)
{
	ASSERT(PipelineDescriptorWriter::BindingExistsInShaderLayout(rPipeline, static_cast<uint32_t>(iBinding)));

	for (VkDescriptorSet& rVkDescriptorSet : rPipeline.mVkDescriptorSets)
	{
		VkDescriptorImageInfo vkDescriptorImageInfo
		{
			.sampler = vkSampler,
			.imageView = vkImageView,
			.imageLayout = vkImageLayout,
		};

		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = vkDescriptorType,
			.pImageInfo = &vkDescriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

} // anonymous namespace

void PipelineDescriptorWriter::UpdateCombinedImageSampler(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView, VkSampler vkSampler)
{
	UpdateImageDescriptor(rPipeline, iBinding, vkSampler, vkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

void PipelineDescriptorWriter::UpdateSampler(Pipeline& rPipeline, int64_t iBinding, VkSampler vkSampler)
{
	UpdateImageDescriptor(rPipeline, iBinding, vkSampler, nullptr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_SAMPLER);
}

void PipelineDescriptorWriter::UpdateStorageImage(Pipeline& rPipeline, int64_t iBinding, VkImageView vkImageView)
{
	UpdateImageDescriptor(rPipeline, iBinding, VK_NULL_HANDLE, vkImageView, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}

} // namespace engine

#endif // defined(BT_CLIENT)
