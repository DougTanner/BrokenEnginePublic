#if defined(BT_CLIENT)

#include "TextureDescriptors.h"

#include "TextureManager.h"

namespace engine
{

TextureDescriptors::TextureDescriptors(TextureManager& rTextureManager)
: mrTextureManager(rTextureManager)
{
}

void TextureDescriptors::Create()
{
	// Global Set 0 layout:
	//   Binding 0:  globalUniform (UNIFORM_BUFFER, ALL_GRAPHICS)
	//   Binding 1:  mainUniform   (UNIFORM_BUFFER, ALL_GRAPHICS)
	//   Binding 3:  samplerRepeat (SAMPLER, FRAGMENT)
	//   Binding 4:  pTextures[]   (SAMPLED_IMAGE, FRAGMENT, PARTIALLY_BOUND | UPDATE_AFTER_BIND)
	//   Binding 12: samplerClamp  (SAMPLER, FRAGMENT)
	VkDescriptorSetLayoutBinding pBindings[]
	{
		{.binding = 0,  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 1,  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 3,  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 4,  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = static_cast<uint32_t>(mImageInfos.size()), .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 12, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
	};

	VkDescriptorBindingFlags pBindingFlags[]
	{
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
		VkDescriptorImageInfo samplerRepeatInfo {.sampler = mrTextureManager.mVkSamplerRepeat};
		VkDescriptorImageInfo samplerClampInfo {.sampler = mrTextureManager.mVkSamplerClamp};

		VkWriteDescriptorSet pWrites[]
		{
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &globalBufferInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &mainBufferInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = 3, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerRepeatInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = 4, .descriptorCount = static_cast<uint32_t>(mImageInfos.size()), .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = mImageInfos.data()},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets.at(i), .dstBinding = 12, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerClampInfo},
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
			.dstBinding = 4,
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

void TextureDescriptors::WriteArrayBindingDescriptors(TextureBinding& rBinding, VkSampler vkSampler)
{
	// Single-element write (per-island-slot bindings): touch only iArrayIndex so other slots'
	// descriptors are not clobbered by stale snapshot pointers.
	if (rBinding.iArrayIndex >= 0)
	{
		Texture* pTexture = rBinding.textures.at(rBinding.iArrayIndex);
		VkDescriptorImageInfo imageInfo
		{
			.sampler = vkSampler,
			.imageView = pTexture != nullptr ? pTexture->mVkImageView : mrTextureManager.mWhiteTexture.mVkImageView,
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
		rBinding.uiTextureGenerations.at(rBinding.iArrayIndex) = pTexture != nullptr ? pTexture->muiGeneration : 0;
		return;
	}

	int64_t iTextureCount = static_cast<int64_t>(rBinding.textures.size());
	auto pImageInfos = common::gpThreadLocal->mWorkbuffer.PushBuffer<VkDescriptorImageInfo*>(iTextureCount * static_cast<int64_t>(sizeof(VkDescriptorImageInfo)));
	for (int64_t i = 0; i < iTextureCount; ++i)
	{
		pImageInfos[i].sampler = vkSampler;
		pImageInfos[i].imageView = rBinding.textures.at(i) != nullptr ? rBinding.textures.at(i)->mVkImageView : mrTextureManager.mWhiteTexture.mVkImageView;
		pImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	for (VkDescriptorSet& rVkDescriptorSet : rBinding.pPipeline->mVkDescriptorSets)
	{
		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(rBinding.iBinding),
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(iTextureCount),
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = pImageInfos,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};
		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}

	rBinding.uiTextureGenerations.resize(iTextureCount);
	for (int64_t i = 0; i < iTextureCount; ++i)
	{
		rBinding.uiTextureGenerations.at(i) = rBinding.textures.at(i) != nullptr ? rBinding.textures.at(i)->muiGeneration : 0;
	}
}

void TextureDescriptors::RegisterTextureBinding(common::crc_t crc, Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags, Texture* pTexture, Texture** ppTextures, int64_t iCount, int64_t iArrayIndex)
{
	std::vector<Texture*> textures;
	std::vector<uint64_t> uiTextureGenerations;
	if (ppTextures != nullptr)
	{
		textures.assign(ppTextures, ppTextures + iCount);
		uiTextureGenerations.resize(iCount);
		for (int64_t i = 0; i < iCount; ++i)
		{
			uiTextureGenerations.at(i) = ppTextures[i] != nullptr ? ppTextures[i]->muiGeneration : 0;
		}
	}
	uint64_t uiTextureGeneration = pTexture != nullptr ? pTexture->muiGeneration : 0;
	mTextureBindings.try_emplace(crc).first->second.push_back({pPipeline, iBinding, samplerFlags, pTexture, std::move(textures), uiTextureGeneration, std::move(uiTextureGenerations), iArrayIndex});
}

void TextureDescriptors::RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags)
{
	mStandaloneSamplerBindings.push_back({pPipeline, iBinding, samplerFlags});
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
			if (!rBinding.textures.empty())
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

	// Ensure CRC has an assigned index and store updated imageView
	mImageInfos.at(static_cast<int64_t>(CrcToIndex(crc))).imageView = vkImageView;
}

void TextureDescriptors::UpdateArrayBindingsForKey(common::crc_t bindingKey)
{
	auto it = mTextureBindings.find(bindingKey);
	if (it == mTextureBindings.end())
	{
		return;
	}
	for (TextureBinding& rBinding : it->second)
	{
		ASSERT(!rBinding.textures.empty());
		VkSampler vkSampler = mrTextureManager.GetSampler(rBinding.samplerFlags);
		WriteArrayBindingDescriptors(rBinding, vkSampler);
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

			if (!rBinding.textures.empty())
			{
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else
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
					auto it = mrTextureManager.mTextureMap.find(rCrc);
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
		}
	}
}

void TextureDescriptors::ClearTextureBindings()
{
	mTextureBindings.clear();
	mStandaloneSamplerBindings.clear();
}

float TextureDescriptors::CrcToIndex(common::crc_t crc)
{
	auto it = mImageInfosMap.find(crc);
	if (it != mImageInfosMap.end())
	{
		return static_cast<float>(it->second);
	}

	// Heap: unordered_map emplace may allocate. Entries map CRC->index permanently for the texture array,
	//   so a workbuffer (frame-scoped) can't own them, and we can't pre-populate without knowing all CRCs
	ScopedSuppressAllocationTracking suppress;

	int64_t iIndex = miNextTextureIndex++;
	ASSERT(iIndex < static_cast<int64_t>(mImageInfos.size()));
	mImageInfosMap.emplace(crc, iIndex);
	return static_cast<float>(iIndex);
}

float TextureDescriptors::CrcToBlurredIndex(common::crc_t crc)
{
	static constexpr common::crc_t kBlurSalt = 0x424C5552; // "BLUR"
	common::crc_t blurredCrc = crc ^ kBlurSalt;
	auto it = mImageInfosMap.find(blurredCrc);
	if (it != mImageInfosMap.end())
		return static_cast<float>(it->second);
	return CrcToIndex(crc);
}

} // namespace engine

#endif // BT_CLIENT
