#if defined(BT_CLIENT)

#include "TextureDescriptors.h"

#include "TextureManager.h"

#include "Graphics/Objects/PipelineDescriptorWriter.h"

namespace engine
{

TextureDescriptors::TextureDescriptors(TextureManager& rTextureManager)
: mrTextureManager(rTextureManager)
{
}

TextureDescriptors::ScopedBindlessWriteEpoch::ScopedBindlessWriteEpoch(TextureDescriptors& rTextureDescriptors)
: mrTextureDescriptors(rTextureDescriptors)
{
	ASSERT(!mrTextureDescriptors.mbBindlessWriteEpoch);
	mrTextureDescriptors.mbBindlessWriteEpoch = true;
}

TextureDescriptors::ScopedBindlessWriteEpoch::~ScopedBindlessWriteEpoch()
{
	ASSERT(mrTextureDescriptors.mbBindlessWriteEpoch);
	mrTextureDescriptors.mbBindlessWriteEpoch = false;
}

void TextureDescriptors::Create()
{
	// Global Set 0 layout (shared by graphics and compute pipelines):
	//   Binding 0:  globalUniform (UNIFORM_BUFFER, VERTEX | FRAGMENT | COMPUTE)
	//   Binding 1:  mainUniform   (UNIFORM_BUFFER, VERTEX | FRAGMENT | COMPUTE)
	//   Binding 13: samplerRepeatModelData (SAMPLER, FRAGMENT | COMPUTE)
	//   Binding 3:  samplerRepeat (SAMPLER, FRAGMENT | COMPUTE)
	//   Binding 4:  pTextures[]   (SAMPLED_IMAGE, FRAGMENT | COMPUTE, PARTIALLY_BOUND | UPDATE_AFTER_BIND)
	//   Binding 12: samplerClamp  (SAMPLER, FRAGMENT | COMPUTE)
	VkDescriptorSetLayoutBinding pBindings[]
	{
		{.binding = shaders::kiGlobalBindingGlobalUniform, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
		{.binding = shaders::kiGlobalBindingMainUniform, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
		{.binding = shaders::kiGlobalBindingSamplerRepeatModelData, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
		{.binding = shaders::kiGlobalBindingSamplerRepeat, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
		{.binding = shaders::kiGlobalBindingBindlessTextures, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = static_cast<uint32_t>(mImageInfos.size()), .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
		{.binding = shaders::kiGlobalBindingSamplerClamp, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
	};

	VkDescriptorBindingFlags pBindingFlags[]
	{
		0,
		0,
		0,
		0,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
		0,
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.pNext = nullptr,
		.bindingCount = static_cast<uint32_t>(std::size(pBindingFlags)),
		.pBindingFlags = pBindingFlags,
	};

	VkDescriptorSetLayoutCreateInfo layoutCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &bindingFlagsCreateInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = static_cast<uint32_t>(std::size(pBindings)),
		.pBindings = pBindings,
	};

	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &layoutCreateInfo, nullptr, &mGlobalDescriptorSetLayout));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mGlobalDescriptorSetLayout, "GlobalSet0");

	int64_t iFramebufferCount = static_cast<int64_t>(gpSwapchainManager->mFramebuffers.size());
	mGlobalDescriptorSets.resize(iFramebufferCount);
	for (int64_t i = 0; i < iFramebufferCount; ++i)
	{
		VkDescriptorSetAllocateInfo allocInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = gpDeviceManager->mVkDescriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &mGlobalDescriptorSetLayout,
		};
		CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &allocInfo, &mGlobalDescriptorSets.at(i)));
		VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET, mGlobalDescriptorSets.at(i), std::format("GlobalSet0{}", i).c_str());
	}

	WriteGlobalDescriptorSets();
}

void TextureDescriptors::Destroy()
{
	if (mGlobalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, gpDeviceManager->mVkDescriptorPool, static_cast<uint32_t>(mGlobalDescriptorSets.size()), mGlobalDescriptorSets.data());
		vkDestroyDescriptorSetLayout(gpDeviceManager->mVkDevice, mGlobalDescriptorSetLayout, nullptr);
		mGlobalDescriptorSetLayout = VK_NULL_HANDLE;
		mGlobalDescriptorSets.clear();
	}
}

void TextureDescriptors::WriteGlobalDescriptorSets()
{
	for (int64_t i = 0; i < static_cast<int64_t>(mGlobalDescriptorSets.size()); ++i)
	{
		VkDescriptorBufferInfo globalBufferInfo {.buffer = gpBufferManager->mGlobalLayoutUniformBuffers[i].GetBuffer(), .offset = 0, .range = VK_WHOLE_SIZE};
		VkDescriptorBufferInfo mainBufferInfo {.buffer = gpBufferManager->mMainLayoutUniformBuffers[i].GetBuffer(), .offset = 0, .range = VK_WHOLE_SIZE};
		VkDescriptorImageInfo samplerRepeatModelDataInfo {.sampler = mrTextureManager.mpSamplers[TextureManager::kSamplerSlotRepeatModelData]};
		VkDescriptorImageInfo samplerRepeatInfo {.sampler = mrTextureManager.mpSamplers[TextureManager::kSamplerSlotRepeat]};
		VkDescriptorImageInfo samplerClampInfo {.sampler = mrTextureManager.mpSamplers[TextureManager::kSamplerSlotClamp]};

		VkWriteDescriptorSet pWrites[]
		{
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = shaders::kiGlobalBindingGlobalUniform, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &globalBufferInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = shaders::kiGlobalBindingMainUniform, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &mainBufferInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = shaders::kiGlobalBindingSamplerRepeatModelData, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerRepeatModelDataInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = shaders::kiGlobalBindingSamplerRepeat, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerRepeatInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = shaders::kiGlobalBindingBindlessTextures, .descriptorCount = static_cast<uint32_t>(mImageInfos.size()), .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = mImageInfos.data()},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = shaders::kiGlobalBindingSamplerClamp, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerClampInfo},
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, static_cast<uint32_t>(std::size(pWrites)), pWrites, 0, nullptr);
	}
}

void TextureDescriptors::UpdateTextureArrayDescriptors()
{
	// Update global Set 0 binding 4 (bindless texture array)
	for (VkDescriptorSet& rVkDescriptorSet : mGlobalDescriptorSets)
	{
		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = shaders::kiGlobalBindingBindlessTextures,
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(mImageInfos.size()),
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.pImageInfo = mImageInfos.data(),
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};
		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void TextureDescriptors::InitializeIslandSlots()
{
	for (size_t i = 0; i < static_cast<size_t>(shaders::kiMaxIslands); ++i)
	{
		mrTextureManager.mRenderTargetTextures.mElevationTextures.at(i) = &mrTextureManager.mIslandPlaceholderElevation;
		mrTextureManager.mRenderTargetTextures.mColorTextures.at(i) = &mrTextureManager.mIslandPlaceholderColor;
		mrTextureManager.mRenderTargetTextures.mNormalsTextures.at(i) = &mrTextureManager.mIslandPlaceholderNormals;
		mrTextureManager.mRenderTargetTextures.mAmbientOcclusionTextures.at(i) = &mrTextureManager.mIslandPlaceholderAmbientOcclusion;
		mrTextureManager.mRenderTargetTextures.mMasksTextures.at(i) = &mrTextureManager.mIslandPlaceholderMasks;
	}
}

void TextureDescriptors::WriteArrayBindingDescriptors(TextureBinding& rBinding, VkSampler vkSampler)
{
	// Single-element write (per-island-slot bindings): touch only iArrayIndex so other slots'
	// descriptors are not clobbered by stale snapshot pointers.
	if (rBinding.iArrayIndex >= 0)
	{
		Texture* pTexture = rBinding.pTexture;
		// Null-view guard alongside the null-pointer guard: a slot mid-reload after eviction has a live
		// Texture whose view is destroyed until AdoptTransferredImage re-attaches it (see the mirrored
		// fallback in PipelineDescriptorWriter's WriteCombinedSamplers).
		VkDescriptorImageInfo imageInfo
		{
			.sampler = vkSampler,
			.imageView = pTexture != nullptr && pTexture->mVkImageView != VK_NULL_HANDLE ? pTexture->mVkImageView : mrTextureManager.mWhiteTexture.mVkImageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		for (VkDescriptorSet& rVkDescriptorSet : rBinding.pPipeline->mVkDescriptorSets)
		{
			VkWriteDescriptorSet vkWriteDescriptorSet
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = rVkDescriptorSet,
				.dstBinding = static_cast<uint32_t>(rBinding.iBinding),
				.dstArrayElement = static_cast<uint32_t>(rBinding.iArrayIndex),
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &imageInfo,
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr,
			};
			vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
		}
		rBinding.uiTextureGeneration = pTexture != nullptr ? pTexture->muiGeneration : 0;
		rBinding.bTextureUsesPlaceholder = pTexture == nullptr || pTexture->mVkImageView == VK_NULL_HANDLE;
		return;
	}

	int64_t iTextureCount = static_cast<int64_t>(rBinding.textures.size());
	WriteFullArrayDescriptors(*rBinding.pPipeline, rBinding.iBinding, rBinding.textures.data(), iTextureCount, vkSampler);

	rBinding.uiTextureGenerations.resize(iTextureCount);
	for (int64_t i = 0; i < iTextureCount; ++i)
	{
		rBinding.uiTextureGenerations.at(i) = rBinding.textures.at(i) != nullptr ? rBinding.textures.at(i)->muiGeneration : 0;
	}
	SynchronizeFullArrayBindingGenerations(rBinding);
}

void TextureDescriptors::SynchronizeFullArrayBindingGenerations(const TextureBinding& rBinding)
{
	// Full-array registrations are duplicated once per member CRC (and under CRC 0) so any lazy
	// member can trigger the one shared descriptor write. Keep every distinct duplicate's snapshot aligned
	// with that write; per-island records own one array element and are deliberately excluded.
	ASSERT(rBinding.iArrayIndex < 0);
	ASSERT(!rBinding.textures.empty());
	for (std::pair<const common::crc_t, std::vector<TextureBinding>>& rEntry : mTextureBindings)
	{
		for (TextureBinding& rOtherBinding : rEntry.second)
		{
			if (&rOtherBinding == &rBinding || rOtherBinding.pPipeline != rBinding.pPipeline || rOtherBinding.iBinding != rBinding.iBinding
				|| rOtherBinding.iArrayIndex >= 0 || rOtherBinding.textures != rBinding.textures)
			{
				continue;
			}
			ASSERT(rOtherBinding.uiTextureGenerations.size() == rBinding.uiTextureGenerations.size());
			std::copy(rBinding.uiTextureGenerations.begin(), rBinding.uiTextureGenerations.end(), rOtherBinding.uiTextureGenerations.begin());
		}
	}
}

void TextureDescriptors::WriteFullArrayDescriptors(Pipeline& rPipeline, int64_t iBinding, Texture* const* ppArray, int64_t iCount, VkSampler vkSampler)
{
	auto pImageInfos = common::gpThreadLocal->mWorkbuffer.PushBuffer<VkDescriptorImageInfo*>(iCount * static_cast<int64_t>(sizeof(VkDescriptorImageInfo)));
	for (int64_t i = 0; i < iCount; ++i)
	{
		pImageInfos[i].sampler = vkSampler;
		// Null-view guard: mid-reload slot (see WriteArrayBindingDescriptors)
		pImageInfos[i].imageView = ppArray[i] != nullptr && ppArray[i]->mVkImageView != VK_NULL_HANDLE ? ppArray[i]->mVkImageView : mrTextureManager.mWhiteTexture.mVkImageView;
		pImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	for (VkDescriptorSet& rVkDescriptorSet : rPipeline.mVkDescriptorSets)
	{
		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(iCount),
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = pImageInfos,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};
		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void TextureDescriptors::WriteArrayElementFromLive(Texture** ppArray, int64_t iIndex)
{
	// find() + ASSERT (not operator[]): the array must already be registered as a bindless consumer at
	// pipeline-create — same invariant guard as IslandTerrain::AcquireTextureSlot's Register lambda.
	auto it = mBindlessArrayConsumers.find(ppArray);
	ASSERT(it != mBindlessArrayConsumers.end());

	Texture* pTexture = ppArray[iIndex];
	// Null-view guard: mid-reload slot (see WriteArrayBindingDescriptors)
	VkImageView vkImageView = pTexture != nullptr && pTexture->mVkImageView != VK_NULL_HANDLE ? pTexture->mVkImageView : mrTextureManager.mWhiteTexture.mVkImageView;
	for (const BindlessArrayConsumer& rConsumer : it->second)
	{
		VkSampler vkSampler = mrTextureManager.GetSampler(rConsumer.samplerFlags);
		VkDescriptorImageInfo imageInfo
		{
			.sampler = vkSampler,
			.imageView = vkImageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		for (VkDescriptorSet& rVkDescriptorSet : rConsumer.pPipeline->mVkDescriptorSets)
		{
			VkWriteDescriptorSet vkWriteDescriptorSet
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = rVkDescriptorSet,
				.dstBinding = static_cast<uint32_t>(rConsumer.iBinding),
				.dstArrayElement = static_cast<uint32_t>(iIndex),
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &imageInfo,
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr,
			};
			vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
		}
	}
}

void TextureDescriptors::AssertBindlessWriteEpoch() const
{
	ASSERT(mbBindlessWriteEpoch);
}

void TextureDescriptors::RegisterBindlessArrayConsumer(Texture** ppTextures, Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags, int64_t iCount)
{
	ASSERT(PipelineDescriptorWriter::BindingExistsInShaderLayout(*pPipeline, static_cast<uint32_t>(iBinding)));
	mBindlessArrayConsumers.try_emplace(ppTextures).first->second.push_back({.pPipeline = pPipeline, .iBinding = iBinding, .samplerFlags = samplerFlags, .iCount = iCount});

	// PipelineManager rebuild clears raw Pipeline* registrations but leaves live island slots intact.
	// Re-register this new consumer's owned descriptor elements so an in-flight lazy adoption still
	// reaches the rebuilt Set 1 descriptor rather than waiting for a future mint.
	RenderTargetTextures& rTargets = mrTextureManager.mRenderTargetTextures;
	for (const auto& [iSlot, rSlot] : mIslandSlots)
	{
		common::crc_t bindingKey = 0;
		if (ppTextures == rTargets.mElevationTextures.data())
		{
			bindingKey = rSlot.islandCrc;
		}
		else if (ppTextures == rTargets.mColorTextures.data())
		{
			bindingKey = rSlot.textureCrcs[0];
		}
		else if (ppTextures == rTargets.mNormalsTextures.data())
		{
			bindingKey = rSlot.textureCrcs[1];
		}
		else if (ppTextures == rTargets.mAmbientOcclusionTextures.data())
		{
			bindingKey = rSlot.textureCrcs[2];
		}
		else if (ppTextures == rTargets.mMasksTextures.data())
		{
			bindingKey = rSlot.textureCrcs[3];
		}
		else
		{
			continue;
		}
		RegisterTextureBinding({.crc = bindingKey, .pPipeline = pPipeline, .iBinding = iBinding, .samplerFlags = samplerFlags, .ppTextures = ppTextures, .iCount = iCount, .iArrayIndex = iSlot});
	}
}

void TextureDescriptors::RegisterIslandSlotBindings(common::crc_t bindingKey, Texture** ppTextures, int64_t iSlot)
{
	auto it = mBindlessArrayConsumers.find(ppTextures);
	ASSERT(it != mBindlessArrayConsumers.end());
	for (const BindlessArrayConsumer& rConsumer : it->second)
	{
		RegisterTextureBinding({.crc = bindingKey, .pPipeline = rConsumer.pPipeline, .iBinding = rConsumer.iBinding, .samplerFlags = rConsumer.samplerFlags, .ppTextures = ppTextures, .iCount = rConsumer.iCount, .iArrayIndex = iSlot});
	}
}

void TextureDescriptors::MintIslandSlot(int64_t iSlot, common::crc_t islandCrc, Texture& rElevationTexture, const common::crc_t (&textureCrcs)[4])
{
	RenderTargetTextures& rTargets = mrTextureManager.mRenderTargetTextures;
	// Elevation remains at the slot-0 placeholder until all four chunk-backed channels are ready.
	// Keep the real target in IslandSlot so sampler/pipeline recreation cannot expose it early.
	rTargets.mElevationTextures.at(iSlot) = rTargets.mElevationTextures.at(0);
	rTargets.mColorTextures.at(iSlot) = &mrTextureManager.mTextureMap.at(textureCrcs[0]);
	rTargets.mNormalsTextures.at(iSlot) = &mrTextureManager.mTextureMap.at(textureCrcs[1]);
	rTargets.mAmbientOcclusionTextures.at(iSlot) = &mrTextureManager.mTextureMap.at(textureCrcs[2]);
	rTargets.mMasksTextures.at(iSlot) = &mrTextureManager.mTextureMap.at(textureCrcs[3]);

	// Heap: first-mint adds slot metadata and five binding-key vectors while RenderGlobal may be allocation tracked.
	ScopedSuppressAllocationTracking suppress;
	IslandSlot islandSlot;
	islandSlot.islandCrc = islandCrc;
	islandSlot.pElevationTexture = &rElevationTexture;
	for (int64_t i = 0; i < static_cast<int64_t>(std::size(textureCrcs)); ++i)
	{
		islandSlot.textureCrcs[i] = textureCrcs[i];
	}
	mIslandSlots.emplace(iSlot, islandSlot);
	RegisterIslandSlotBindings(islandCrc, rTargets.mElevationTextures.data(), iSlot);
	RegisterIslandSlotBindings(textureCrcs[0], rTargets.mColorTextures.data(), iSlot);
	RegisterIslandSlotBindings(textureCrcs[1], rTargets.mNormalsTextures.data(), iSlot);
	RegisterIslandSlotBindings(textureCrcs[2], rTargets.mAmbientOcclusionTextures.data(), iSlot);
	RegisterIslandSlotBindings(textureCrcs[3], rTargets.mMasksTextures.data(), iSlot);
}

void TextureDescriptors::EvictIslandSlot(int64_t iSlot, common::crc_t islandCrc, const common::crc_t (&textureCrcs)[4])
{
	AssertBindlessWriteEpoch();
	auto itSlot = mIslandSlots.find(iSlot);
	RenderTargetTextures& rTargets = mrTextureManager.mRenderTargetTextures;
	rTargets.mElevationTextures.at(iSlot) = rTargets.mElevationTextures.at(0);
	rTargets.mColorTextures.at(iSlot) = rTargets.mColorTextures.at(0);
	rTargets.mNormalsTextures.at(iSlot) = rTargets.mNormalsTextures.at(0);
	rTargets.mAmbientOcclusionTextures.at(iSlot) = rTargets.mAmbientOcclusionTextures.at(0);
	rTargets.mMasksTextures.at(iSlot) = rTargets.mMasksTextures.at(0);

	for (common::crc_t textureCrc : textureCrcs)
	{
		mImageInfos.at(mImageInfosMap.at(textureCrc)).imageView = mrTextureManager.mWhiteTexture.mVkImageView;
	}
	UpdateTextureArrayDescriptors();

	WriteArrayElementFromLive(rTargets.mElevationTextures.data(), iSlot);
	WriteArrayElementFromLive(rTargets.mColorTextures.data(), iSlot);
	WriteArrayElementFromLive(rTargets.mNormalsTextures.data(), iSlot);
	WriteArrayElementFromLive(rTargets.mAmbientOcclusionTextures.data(), iSlot);
	WriteArrayElementFromLive(rTargets.mMasksTextures.data(), iSlot);

	ScopedSuppressAllocationTracking suppress;
	UnregisterBindingsForKey(islandCrc);
	for (common::crc_t textureCrc : textureCrcs)
	{
		UnregisterBindingsForKey(textureCrc);
	}
	mIslandSlots.erase(itSlot);
}

void TextureDescriptors::RestoreIslandSlot(common::crc_t islandCrc)
{
	AssertBindlessWriteEpoch();
	for (auto& [iSlot, rSlot] : mIslandSlots)
	{
		if (rSlot.islandCrc != islandCrc)
		{
			continue;
		}

		auto it = mTextureBindings.find(islandCrc);
		if (it == mTextureBindings.end())
		{
			return;
		}
		mrTextureManager.mRenderTargetTextures.mElevationTextures.at(iSlot) = rSlot.pElevationTexture;
		for (TextureBinding& rBinding : it->second)
		{
			ASSERT(rBinding.iArrayIndex >= 0);
			rBinding.pTexture = rSlot.pElevationTexture;
			WriteArrayBindingDescriptors(rBinding, mrTextureManager.GetSampler(rBinding.samplerFlags));
		}
		return;
	}
	ASSERT(false);
}

void TextureDescriptors::RegisterTextureBinding(const TextureBindingInfo& rInfo)
{
	// Validates at pipeline-create that iBinding actually exists in pPipeline's shader layout.
	// Catches the iDescriptorCount/uiBinding confusion (VUID-00316 source) on frame 0 rather
	// than on the first sampler-recreate.
	ASSERT(PipelineDescriptorWriter::BindingExistsInShaderLayout(*rInfo.pPipeline, static_cast<uint32_t>(rInfo.iBinding)));

	std::vector<Texture*> textures;
	std::vector<uint64_t> uiTextureGenerations;
	Texture* pTexture = rInfo.pTexture;
	if (rInfo.iArrayIndex >= 0)
	{
		ASSERT(rInfo.ppTextures != nullptr);
		pTexture = rInfo.ppTextures[rInfo.iArrayIndex];
	}
	else if (rInfo.ppTextures != nullptr)
	{
		textures.assign(rInfo.ppTextures, rInfo.ppTextures + rInfo.iCount);
		uiTextureGenerations.resize(rInfo.iCount);
		for (int64_t i = 0; i < rInfo.iCount; ++i)
		{
			uiTextureGenerations.at(i) = rInfo.ppTextures[i] != nullptr ? rInfo.ppTextures[i]->muiGeneration : 0;
		}
	}
	uint64_t uiTextureGeneration = pTexture != nullptr ? pTexture->muiGeneration : 0;
	bool bTextureUsesPlaceholder = rInfo.iArrayIndex >= 0 && (pTexture == nullptr || pTexture->mVkImageView == VK_NULL_HANDLE);
	mTextureBindings.try_emplace(rInfo.crc).first->second.push_back({.pPipeline = rInfo.pPipeline, .iBinding = rInfo.iBinding, .samplerFlags = rInfo.samplerFlags, .pTexture = pTexture, .textures = std::move(textures), .uiTextureGeneration = uiTextureGeneration, .uiTextureGenerations = std::move(uiTextureGenerations), .bTextureUsesPlaceholder = bTextureUsesPlaceholder, .iArrayIndex = rInfo.iArrayIndex});
}

void TextureDescriptors::RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags)
{
	ASSERT(PipelineDescriptorWriter::BindingExistsInShaderLayout(*pPipeline, static_cast<uint32_t>(iBinding)));

	mStandaloneSamplerBindings.push_back({pPipeline, iBinding, samplerFlags});
}

void TextureDescriptors::UnregisterPipeline(Pipeline* pPipeline)
{
	for (auto it = mTextureBindings.begin(); it != mTextureBindings.end();)
	{
		std::erase_if(it->second, [pPipeline](const TextureBinding& rBinding)
		{
			return rBinding.pPipeline == pPipeline;
		});
		if (it->second.empty())
		{
			it = mTextureBindings.erase(it);
		}
		else
		{
			++it;
		}
	}

	for (auto it = mBindlessArrayConsumers.begin(); it != mBindlessArrayConsumers.end();)
	{
		std::erase_if(it->second, [pPipeline](const BindlessArrayConsumer& rConsumer)
		{
			return rConsumer.pPipeline == pPipeline;
		});
		if (it->second.empty())
		{
			it = mBindlessArrayConsumers.erase(it);
		}
		else
		{
			++it;
		}
	}

	std::erase_if(mStandaloneSamplerBindings, [pPipeline](const StandaloneSamplerBinding& rBinding)
	{
		return rBinding.pPipeline == pPipeline;
	});
}

void TextureDescriptors::UpdateDescriptorsForTexture(common::crc_t crc)
{
	Texture& rTexture = mrTextureManager.mTextureMap.at(crc);
	VkImageView vkImageView = rTexture.mVkImageView;

	// Update individual combined image sampler bindings
	auto it = mTextureBindings.find(crc);
	if (it != mTextureBindings.end())
	{
		for (TextureBinding& rBinding : it->second)
		{
			VkSampler vkSampler = mrTextureManager.GetSampler(rBinding.samplerFlags);
			if (rBinding.iArrayIndex >= 0)
			{
				AssertBindlessWriteEpoch();
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else if (!rBinding.textures.empty())
			{
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else
			{
				rBinding.pPipeline->UpdateCombinedImageSamplerDescriptor(rBinding.iBinding, vkImageView, vkSampler);
				rBinding.uiTextureGeneration = rTexture.muiGeneration;
			}
		}
	}

	// Ensure CRC has an assigned index and store updated imageView. Render-phase caller: CrcToIndex is
	//   lock-free, safe only because no worker Spawn runs concurrently (see CrcToIndex).
	ASSERT(common::gpThreadLocal == nullptr || !common::gpThreadLocal->mbInFrameTick);
	mImageInfos.at(CrcToIndex(crc)).imageView = vkImageView;
}

void TextureDescriptors::UnregisterBindingsForKey(common::crc_t bindingKey)
{
	// Heap: unordered_map::erase deallocates the bucket's vector<TextureBinding>; called from
	// IslandTerrain::EvictionSweep inside RenderGlobal.
	ScopedSuppressAllocationTracking suppress;
	mTextureBindings.erase(bindingKey);
}

void TextureDescriptors::WriteSingleTextureBinding(common::crc_t crc, TextureBinding& rBinding, VkSampler vkSampler)
{
	VkImageView vkImageView = VK_NULL_HANDLE;
	uint64_t uiGeneration = 0;
	if (rBinding.pTexture != nullptr)
	{
		vkImageView = rBinding.pTexture->mVkImageView;
		uiGeneration = rBinding.pTexture->muiGeneration;
	}
	else
	{
		auto it = mrTextureManager.mTextureMap.find(crc);
		if (it != mrTextureManager.mTextureMap.end())
		{
			vkImageView = it->second.mVkImageView;
			uiGeneration = it->second.muiGeneration;
		}
	}
	if (vkImageView != VK_NULL_HANDLE)
	{
		rBinding.pPipeline->UpdateCombinedImageSamplerDescriptor(rBinding.iBinding, vkImageView, vkSampler);
		rBinding.uiTextureGeneration = uiGeneration;
	}
}

void TextureDescriptors::RewriteSamplerDescriptors()
{
	// Update standalone sampler descriptors in per-pipeline sets
	for (const StandaloneSamplerBinding& rBinding : mStandaloneSamplerBindings)
	{
		VkSampler vkSampler = mrTextureManager.GetSampler(rBinding.samplerFlags);
		rBinding.pPipeline->UpdateSamplerDescriptor(rBinding.iBinding, vkSampler);
	}

	// Update combined image sampler descriptors in per-pipeline sets
	for (auto& [rCrc, rBindings] : mTextureBindings)
	{
		for (TextureBinding& rBinding : rBindings)
		{
			VkSampler vkSampler = mrTextureManager.GetSampler(rBinding.samplerFlags);

			if (rBinding.iArrayIndex >= 0)
			{
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else if (!rBinding.textures.empty())
			{
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else
			{
				WriteSingleTextureBinding(rCrc, rBinding, vkSampler);
			}
		}
	}

	// Bindless arrays mutate in place after pipeline-create (IslandTerrain::AcquireTextureSlot patches
	// per-slot Texture* pointers), so rewriting from a snapshot would clobber live slots with stale
	// placeholder pointers. Read through the live array pointer (consumer map key) instead. Per-slot
	// TextureBinding entries registered from AcquireTextureSlot also get refreshed by the loop above
	// — the redundant write here is harmless (both sources resolve to the same live Texture*).
	for (auto& [ppLiveArray, rConsumers] : mBindlessArrayConsumers)
	{
		for (const BindlessArrayConsumer& rConsumer : rConsumers)
		{
			VkSampler vkSampler = mrTextureManager.GetSampler(rConsumer.samplerFlags);
			WriteFullArrayDescriptors(*rConsumer.pPipeline, rConsumer.iBinding, ppLiveArray, rConsumer.iCount, vkSampler);
		}
	}
}

void TextureDescriptors::VerifyAllDescriptorGenerations() const
{
	for (const auto& [rCrc, rBindings] : mTextureBindings)
	{
		for (const TextureBinding& rBinding : rBindings)
		{
			if (rBinding.pTexture != nullptr && rBinding.pTexture->muiGeneration != 0
				&& ((rBinding.bTextureUsesPlaceholder && rBinding.pTexture->mVkImageView != VK_NULL_HANDLE)
					|| (!rBinding.bTextureUsesPlaceholder && (rBinding.pTexture->muiGeneration != rBinding.uiTextureGeneration || rBinding.pTexture->mVkImage == VK_NULL_HANDLE))))
			{
				LOG(kGraphics, kError, "Descriptor staleness: pipeline={} binding={} crc={} texture={} snapshotGen={} currentGen={} vkImage={}", rBinding.pPipeline->mInfo.name, rBinding.iBinding, rCrc, reinterpret_cast<uintptr_t>(rBinding.pTexture), rBinding.uiTextureGeneration, rBinding.pTexture->muiGeneration, reinterpret_cast<uintptr_t>(rBinding.pTexture->mVkImage));
				DEBUG_BREAK();
			}

			ASSERT(static_cast<int64_t>(rBinding.uiTextureGenerations.size()) == static_cast<int64_t>(rBinding.textures.size()));
			for (size_t i = 0; i < rBinding.textures.size(); ++i)
			{
				Texture* pTexture = rBinding.textures.at(i);
				if (pTexture != nullptr && pTexture->muiGeneration != 0
					&& (pTexture->muiGeneration != rBinding.uiTextureGenerations.at(i) || pTexture->mVkImage == VK_NULL_HANDLE))
				{
					LOG(kGraphics, kError, "Descriptor staleness (array): pipeline={} binding={} crc={} slot={} texture={} snapshotGen={} currentGen={} vkImage={}", rBinding.pPipeline->mInfo.name, rBinding.iBinding, rCrc, i, reinterpret_cast<uintptr_t>(pTexture), rBinding.uiTextureGenerations.at(i), pTexture->muiGeneration, reinterpret_cast<uintptr_t>(pTexture->mVkImage));
					DEBUG_BREAK();
				}
			}
		}
	}
}

void TextureDescriptors::ClearTextureBindings()
{
	// Pipeline recreation invalidates only raw Pipeline* registrations. Slot metadata survives so
	// RegisterBindlessArrayConsumer can rebuild per-element records for lazy channels still loading.
	mTextureBindings.clear();
	mBindlessArrayConsumers.clear();
	mStandaloneSamplerBindings.clear();
}

int64_t TextureDescriptors::CrcToIndex(common::crc_t crc)
{
	// Lock-free by phase exclusion, not by mutex. Two writer phases touch mImageInfosMap / miNextTextureIndex
	//   and never overlap: worker threads reach this only through ParticleManager::Spawn (serialized by
	//   mSpawnMutex) during RunFrameTick's gpMultithreading->Dispatch() fan-out, which fully joins before the
	//   main thread runs the render-path callers (UpdateDescriptorsForTexture / BlurLightingTexture). Those
	//   callers ASSERT(!mbInFrameTick) so the invariant fails loud if a future caller moves into frame-tick code.
	auto it = mImageInfosMap.find(crc);
	if (it != mImageInfosMap.end())
	{
		return it->second;
	}

	// Heap: unordered_map emplace may allocate. Entries map CRC->index permanently for the texture array,
	//   so a workbuffer (frame-scoped) can't own them, and we can't pre-populate without knowing all CRCs
	ScopedSuppressAllocationTracking suppress;

	int64_t iIndex = miNextTextureIndex++;
	ASSERT(iIndex < static_cast<int64_t>(mImageInfos.size()));
	mImageInfosMap.emplace(crc, iIndex);
	return iIndex;
}

float TextureDescriptors::CrcToBlurredIndex(common::crc_t crc)
{
	common::crc_t blurredCrc = crc ^ kBlurSalt;
	auto it = mImageInfosMap.find(blurredCrc);
	if (it != mImageInfosMap.end())
	{
		return static_cast<float>(it->second);
	}
	return static_cast<float>(CrcToIndex(crc));
}

} // namespace engine

#endif // BT_CLIENT
