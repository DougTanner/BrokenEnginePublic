#include "FileManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/GltfAnimationData.h"
#include "Graphics/GltfComparisonLog.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

#include "Data/Data.h"

namespace engine
{

using enum FileFlags;

FileManager::FileManager()
{
	gpFileManager = this;

	// Get Windows AppData directory and append game name
	PWSTR pWideChar = nullptr;
	SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
	mAppDataDirectory = pWideChar;
	CoTaskMemFree(pWideChar);
	mAppDataDirectory.append(game::kGameName);
	std::filesystem::create_directory(mAppDataDirectory);
	mLogFileStream.open(LogFile(), std::ofstream::out);
	common::gpLogFileStream = &mLogFileStream;
	Log("AppData directory: \"{}\"", mAppDataDirectory.string());

	// Get Windows temp directory and append game name
	char pcDirectory[MAX_PATH] {};
	GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	mTempDirectory.append(game::kGameName);
	Log("Temp directory: \"{}\"", mTempDirectory.string());
	std::filesystem::create_directory(mTempDirectory);

	// Get the file path of the executable, the /Data/ folder will be beside it
	GetModuleFileName(nullptr, pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	mDataDirectory = pcDirectory;
	mDataDirectory.remove_filename();
	mDataDirectory /= "Data";
	Log("Data directory: \"{}\"", mDataDirectory.string());

	LoadPackFiles();
}

FileManager::~FileManager()
{
	// Shutdown background loading thread
	{
		std::unique_lock lock(mQueueMutex);
		mShutdown = true;
	}
	mWakeCondition.notify_one();
	mLoadingThread.join();
	
	common::gpLogFileStream = nullptr;

	gpFileManager = nullptr;
}

bool FileManager::Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	return std::filesystem::exists(GetFilePath(rFlags, rFilename));
}

int64_t FileManager::GetFileSize(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	return file_size(GetFilePath(rFlags, rFilename));
}

std::filesystem::path FileManager::GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path filePath;
	if (rFlags & kAppDataDirectory)
	{
		filePath = mAppDataDirectory;
	}
	else if (rFlags & kTempDirectory)
	{
		filePath = mTempDirectory;
	}
	else
	{
		common::DebugBreak();
		return "";
	}

	filePath /= rFilename;
	return filePath;
}

std::fstream FileManager::OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path file = GetFilePath(rFlags, rFilename);

	if (rFlags & kBackup && std::filesystem::exists(file))
	{
		ASSERT((rFlags & kWrite) != 0);
		std::filesystem::path backupFile(file);
		std::time_t time = std::time(nullptr);
		std::tm timeStruct = *std::localtime(&time);
		std::ostringstream oss;
		oss << std::put_time(&timeStruct, ".%d-%m-%Y-%H-%M-%S");
		std::string timeString = oss.str();
		backupFile += timeString;
		std::filesystem::copy_file(file, backupFile);
	}

	std::fstream fileStream(file, (rFlags & kRead ? std::ios::in : std::ios::out) | std::ios::binary);
	Log("{} \"{}\" at \"{}\"", fileStream.is_open() ? (rFlags & kRead ? "Reading" : "Writing") : "Failed to open", rFilename.string(), file.string());
	return fileStream;
}

void FileManager::RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	std::filesystem::path file = GetFilePath(rFlags, rFilename);
	Log("Remove \"{}\" at \"{}\"", rFilename.string(), file.string());
	std::filesystem::remove(file);
}

// Build full path to a data file (pack or manifest) for the given data type
std::filesystem::path FileManager::GetDataFilePath(data::DataTypes eDataType, std::string_view extension) const
{
	return mDataDirectory / (std::string(data::kpcDataTypeNames[eDataType]) + std::string(extension));
}

constexpr bool IsEagerChunk(data::DataTypes eDataType)
{
	return eDataType == data::kDataTypeFont || eDataType == data::kDataTypeGltf || eDataType == data::kDataTypeModel || eDataType == data::kDataTypeShader || eDataType == data::kDataTypeRaw;
}

void FileManager::LoadPackFiles()
{
	for (int64_t i = 0; i < data::kDataTypeCount; ++i)
	{
		// Read chunk locations from manifest
		std::filesystem::path manifestPath = GetDataFilePath(static_cast<data::DataTypes>(i), ".manifest");
		std::fstream manifestStream(manifestPath, std::ios::in | std::ios::binary);
		common::DataHeader dataHeader {};
		manifestStream.read(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
		ASSERT(dataHeader.iMagic == common::DataHeader::kiMagic && dataHeader.iVersion == common::DataHeader::kiVersion);

		manifestStream.seekg(common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::DataHeader))));
		mpChunkLocations[i].resize(dataHeader.iChunkCount);
		manifestStream.read(reinterpret_cast<char*>(mpChunkLocations[i].data()), dataHeader.iChunkCount * sizeof(common::ChunkLocation));
		manifestStream.close();

		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			continue;
		}

		for (const common::ChunkLocation& rChunkLocation : mpChunkLocations[i])
		{
			// Read the header
			common::ChunkHeader chunkHeader {};
			std::fstream packStream(GetDataFilePath(static_cast<data::DataTypes>(i), ".pack"), std::ios::in | std::ios::binary);
			packStream.seekg(rChunkLocation.uiOffset);
			packStream.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));

			// Add to lazy chunk map
			auto [it, bInserted] = mLazyChunkMap.try_emplace(rChunkLocation.crc, LazyChunk {.eDataType = static_cast<data::DataTypes>(i), .location = rChunkLocation, .header = chunkHeader});
			if (!bInserted)
			{
				Log("Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
				common::DebugBreak();
			}

		}
	}

	mLoadingFuture = std::async(std::launch::async, [this]()
	{
		common::ThreadLocal threadLocal(0, common::kThreadEagerLoad);

		for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
		{
			if (!IsEagerChunk(static_cast<data::DataTypes>(i)))
			{
				continue;
			}

			std::filesystem::path packPath = GetDataFilePath(static_cast<data::DataTypes>(i), ".pack");
			std::vector<byte>& rPackBytes = mPackFileData[i];

			// If eager loading, read the entire .pack file into memory
			rPackBytes.resize(std::filesystem::file_size(packPath));
			std::fstream packStream(packPath, std::ios::in | std::ios::binary);
			packStream.read(reinterpret_cast<char*>(rPackBytes.data()), rPackBytes.size());
			packStream.close();

			// Process chunks from pack file
			for (const common::ChunkLocation& rChunkLocation : mpChunkLocations[i])
			{
				// Add to eager chunk map
				auto pChunkHeader = reinterpret_cast<common::ChunkHeader*>(&rPackBytes[rChunkLocation.uiOffset]);
				ASSERT(pChunkHeader->iMagic == common::ChunkHeader::kiMagic && pChunkHeader->crc == rChunkLocation.crc);
				uint64_t uiDataOffset = rChunkLocation.uiOffset + common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));

				auto [it, bInserted] = mEagerChunkMap.try_emplace(rChunkLocation.crc, EagerChunk { .pHeader = pChunkHeader, .pData = &rPackBytes[uiDataOffset], });
				if (!bInserted)
				{
					Log("Duplicate chunk CRC {:#018x} found in {}", rChunkLocation.crc, data::kpcDataTypeNames[i]);
					common::DebugBreak();
				}

				// Log GLTF chunk info for debugging animation loading
				if (pChunkHeader->flags & common::ChunkFlags::kGltf)
				{
					Log("GLTF chunk CRC {:#018x}: bHasAnimation={}, uiMaterialCount={}, sizeof(GltfShaderData)={}",
						rChunkLocation.crc,
						pChunkHeader->gltfHeader.bHasAnimation,
						pChunkHeader->gltfHeader.uiMaterialCount,
						sizeof(common::GltfShaderData));
				}

				// Load animation data for GLTF chunks that have it
				if (pChunkHeader->flags & common::ChunkFlags::kGltf && pChunkHeader->gltfHeader.bHasAnimation)
				{
					// Animation data comes after the material data (aligned to 16 bytes, matching export)
					int64_t iMaterialDataSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(pChunkHeader->gltfHeader.uiMaterialCount * sizeof(common::GltfShaderData));
					const byte* pAnimationData = &rPackBytes[uiDataOffset + iMaterialDataSize];
					Log("  Animation data offset: uiDataOffset={} + iMaterialDataSize={} = {}",
						uiDataOffset, iMaterialDataSize, uiDataOffset + iMaterialDataSize);

					// Initialize comparison logging before Load() so SKELETON_LOAD gets logged
					InitComparisonLog(rChunkLocation.crc);
					if (gbComparisonLoggingEnabled)
					{
						CompLog("PACK_LOAD:");
						CompLog("  crc: %llu", rChunkLocation.crc);
						CompLog("  chunk_path: %s", pChunkHeader->pcPath);
						CompLog("  has_animation: true");
						CompLog("  material_count: %u", pChunkHeader->gltfHeader.uiMaterialCount);
					}

					GltfAnimationData& rAnimData = gAnimationDataMap[rChunkLocation.crc];
					rAnimData.Load(pAnimationData, rChunkLocation.crc);
					Log("Loaded animation data for GLTF CRC {:#018x}: {} nodes, {} skin joints, {} animations",
						rChunkLocation.crc,
						rAnimData.GetHeader().skeleton.uiNodeCount,
						rAnimData.GetHeader().skeleton.uiSkinJointCount,
						rAnimData.GetHeader().uiAnimationCount);
				}
			}
		}

		// Start background loading thread
		mLoadingThread = std::thread(&FileManager::LoadingThread, this);
	});

	// Queue up any priority loads already set (these vectors may be expanded later and RequestChunkLoad will be called again)
	RequestChunkLoad(Islands::smPriorityIslands, LoadPriority::kRealtime);
	RequestChunkLoad(TextureManager::smPriorityTextures, LoadPriority::kRealtime);
}

const std::unordered_map<common::crc_t, EagerChunk>& FileManager::GetEagerChunkMap() const
{
	if (gpFileManager->mLoadingFuture.valid()) [[unlikely]]
	{
		gpProfileManager->BootStart(kBootTimerWaitForDataFile);
		gpFileManager->mLoadingFuture.get();
		gpProfileManager->BootStop(kBootTimerWaitForDataFile);
	}

	return mEagerChunkMap;
}

const std::unordered_map<common::crc_t, LazyChunk>& FileManager::GetLazyChunkMap() const
{
	return mLazyChunkMap;
}

bool FileManager::IsChunkReady(common::crc_t crc) const
{
	ASSERT(mEagerChunkMap.find(crc) == mEagerChunkMap.end());
	auto it = mLazyChunkMap.find(crc);
	return it != mLazyChunkMap.end() ? it->second.bLoaded == true : false;
}

void FileManager::RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority priority)
{
	bool bAddedAny = false;

	{
		std::unique_lock lock(mQueueMutex);

		for (common::crc_t crc : crcs)
		{
			LazyChunk& rLazyChunk = mLazyChunkMap.at(crc);
			if (rLazyChunk.bLoaded)
			{
				continue;
			}

			if (!rLazyChunk.bLoadRequested)
			{
				mRequestQueue.push({crc, priority});
				rLazyChunk.bLoadRequested = true;
				bAddedAny = true;
			}
		}
	}

	if (bAddedAny)
	{
		mWakeCondition.notify_one();
	}
}

void FileManager::WaitForChunks(std::span<const common::crc_t> crcs)
{
	RequestChunkLoad(crcs, LoadPriority::kRealtime);

	std::unique_lock lock(mQueueMutex);
	mCompletionCondition.wait(lock, [&]
	{
		for (common::crc_t crc : crcs)
		{
			if (!mLazyChunkMap.at(crc).bLoaded)
			{
				return false;
			}
		}
		return true;
	});
}

void FileManager::LoadingThread()
{
	common::ThreadLocal threadLocal(0, common::kThreadLazyLoad);
	
	while (!mShutdown)
	{
		LoadRequest loadRequest {};

		{
			std::unique_lock lock(mQueueMutex);

			if (!mShutdown && mRequestQueue.empty())
			{
				// Wait for requests
				mWakeCondition.wait(lock, [this] { return mShutdown || !mRequestQueue.empty(); });
			}
			
			if (mShutdown)
			{
				break;
			}
			
			loadRequest = mRequestQueue.top();
			mRequestQueue.pop();
		}
		
		LoadChunk(loadRequest);
	}
}

void FileManager::LoadChunk(const LoadRequest& rRequest)
{
	LazyChunk& rLazyChunk = mLazyChunkMap.at(rRequest.crc);

	// Load the data from the pack file
	constexpr int64_t iDataOffset = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));
	std::fstream packStream(GetDataFilePath(rLazyChunk.eDataType, ".pack"), std::ios::in | std::ios::binary);
	packStream.seekg(rLazyChunk.location.uiOffset + iDataOffset);
	rLazyChunk.data.resize(rLazyChunk.location.uiSize - iDataOffset);
	packStream.read(reinterpret_cast<char*>(rLazyChunk.data.data()), rLazyChunk.location.uiSize - iDataOffset);
	packStream.close();

	// GPU upload before setting bLoaded to avoid data race: the main thread may read rLazyChunk.data
	// in the fallback path as soon as bLoaded is true, so the upload must finish first
	if (rLazyChunk.header.flags & common::ChunkFlags::kTexture)
	{
		UploadTextureToGpu(rLazyChunk);
	}

	{
		std::unique_lock lock(mQueueMutex);
		rLazyChunk.bLoaded = true;
		mCompletionCondition.notify_all();
	}
}

void FileManager::InitTransferResources()
{
	VkCommandPoolCreateInfo vkCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mTransferVkCommandPool));
	VkName(VK_OBJECT_TYPE_COMMAND_POOL, mTransferVkCommandPool, "Transfer");

	VkFenceCreateInfo vkFenceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &mTransferVkFence));
	VkName(VK_OBJECT_TYPE_FENCE, mTransferVkFence, "Transfer");
}

void FileManager::DestroyTransferResources()
{
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		return;
	}

	// Clean up any GPU-uploaded texture images that were not adopted by TextureManager
	for (auto& [rCrc, rLazyChunk] : mLazyChunkMap)
	{
		if (rLazyChunk.vkImage != VK_NULL_HANDLE)
		{
			vmaDestroyImage(gpDeviceManager->mpAllocator, rLazyChunk.vkImage, rLazyChunk.vmaAllocation);
			rLazyChunk.vkImage = VK_NULL_HANDLE;
			rLazyChunk.vmaAllocation = VK_NULL_HANDLE;
			rLazyChunk.vkDeviceMemory = VK_NULL_HANDLE;
		}
	}

	vkDestroyFence(gpDeviceManager->mVkDevice, mTransferVkFence, nullptr);
	mTransferVkFence = VK_NULL_HANDLE;

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mTransferVkCommandPool, nullptr);
	mTransferVkCommandPool = VK_NULL_HANDLE;
}

void FileManager::ClearTransferredImage(common::crc_t crc)
{
	LazyChunk& rLazyChunk = mLazyChunkMap.at(crc);
	rLazyChunk.vkImage = VK_NULL_HANDLE;
	rLazyChunk.vmaAllocation = VK_NULL_HANDLE;
	rLazyChunk.vkDeviceMemory = VK_NULL_HANDLE;
}

void FileManager::UploadTextureToGpu(LazyChunk& rLazyChunk)
{
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		return;
	}

	// Skip background GPU upload when using the same queue as graphics (concurrent vkQueueSubmit is not thread-safe)
	if (gpDeviceManager->mTransferVkQueue == gpDeviceManager->mGraphicsVkQueue)
	{
		return;
	}

	bool bCubemap = rLazyChunk.header.flags & common::ChunkFlags::kCubemap;
	uint32_t uiArrayLayers = bCubemap ? 6u : 1u;

	// Create VkImage via VMA
	VkImageCreateInfo vkImageCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
		.imageType = VK_IMAGE_TYPE_2D,
		.format = rLazyChunk.header.textureHeader.vkFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth), static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight), 1},
		.mipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels),
		.arrayLayers = uiArrayLayers,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo vmaAllocationCreateInfo = {};
	vmaAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	VmaAllocationInfo vmaAllocationInfo {};
	CHECK_VK(vmaCreateImage(gpDeviceManager->mpAllocator, &vkImageCreateInfo, &vmaAllocationCreateInfo, &rLazyChunk.vkImage, &rLazyChunk.vmaAllocation, &vmaAllocationInfo));
	rLazyChunk.vkDeviceMemory = vmaAllocationInfo.deviceMemory;

	// Calculate total staging buffer size
	VkDeviceSize vkStagingSize = 0;
	uint32_t uiWidth = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth);
	uint32_t uiHeight = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight);
	uint32_t uiMipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels);
	for (uint32_t i = 0; i < uiMipLevels; ++i)
	{
		vkStagingSize += uiArrayLayers * common::SizeInBytes(rLazyChunk.header.textureHeader.vkFormat, uiWidth, uiHeight);
		uiWidth /= 2;
		uiHeight /= 2;
	}

	// Create staging buffer
	VkBuffer stagingVkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingVkDeviceMemory = VK_NULL_HANDLE;
	VmaAllocation stagingVmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo stagingVmaAllocationInfo {};
	Buffer::CreateBuffer("TransferStaging", vkStagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVkBuffer, stagingVkDeviceMemory, stagingVmaAllocation, &stagingVmaAllocationInfo);
	memcpy(stagingVmaAllocationInfo.pMappedData, rLazyChunk.data.data(), vkStagingSize);

	// Wait for previous upload to finish, then reset fence
	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNs.count()));
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence));

	// Allocate command buffer
	VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mTransferVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &vkCommandBuffer));

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));

	// Barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = rLazyChunk.vkImage,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = uiMipLevels, .baseArrayLayer = 0, .layerCount = uiArrayLayers},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);

	// Record buffer-to-image copies for all mip levels and array layers
	size_t uiOffset = 0;
	for (uint32_t iLayer = 0; iLayer < uiArrayLayers; ++iLayer)
	{
		uiWidth = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth);
		uiHeight = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight);
		for (uint32_t iMip = 0; iMip < uiMipLevels; ++iMip)
		{
			VkBufferImageCopy vkBufferImageCopy = {};
			vkBufferImageCopy.bufferOffset = uiOffset;
			vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vkBufferImageCopy.imageSubresource.mipLevel = iMip;
			vkBufferImageCopy.imageSubresource.baseArrayLayer = iLayer;
			vkBufferImageCopy.imageSubresource.layerCount = 1;
			vkBufferImageCopy.imageExtent.width = uiWidth;
			vkBufferImageCopy.imageExtent.height = uiHeight;
			vkBufferImageCopy.imageExtent.depth = 1;

			uiOffset += common::SizeInBytes(rLazyChunk.header.textureHeader.vkFormat, uiWidth, uiHeight);
			uiWidth /= 2;
			uiHeight /= 2;

			vkCmdCopyBufferToImage(vkCommandBuffer, stagingVkBuffer, rLazyChunk.vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);
		}
	}

	// Post-copy barrier: queue family ownership transfer or layout transition
	bool bSeparateTransferFamily = gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex;
	if (bSeparateTransferFamily)
	{
		if (gpDeviceManager->mbTransferQfotOptional)
		{
			// QFOT optional (VK_KHR_maintenance9): no ownership transfer needed, transition layout directly
			vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkImageMemoryBarrier.dstAccessMask = 0;
			vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			vkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		}
		else
		{
			// Queue family release barrier (transfer -> graphics)
			vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkImageMemoryBarrier.dstAccessMask = 0;
			vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			vkImageMemoryBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex);
			vkImageMemoryBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);
		}
		vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
	}
	else
	{
		// Same family: transition directly to SHADER_READ_ONLY
		vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkImageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
	}

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));

	// Submit to transfer queue
	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &vkCommandBuffer,
		.signalSemaphoreCount = 0,
	};
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mTransferVkQueue, 1, &vkSubmitInfo, mTransferVkFence));

	// Wait for completion (blocking is fine on the loading thread)
	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNs.count()));

	// Cleanup
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mTransferVkCommandPool, 1, &vkCommandBuffer);
	vmaDestroyBuffer(gpDeviceManager->mpAllocator, stagingVkBuffer, stagingVmaAllocation);

	// Free CPU data (no longer needed after GPU upload)
	rLazyChunk.data.clear();
	rLazyChunk.data.shrink_to_fit();

	// Signal GPU upload complete (atomic store with release semantics)
	rLazyChunk.bGpuUploaded.store(true, std::memory_order_release);
}

bool FileManager::ReadChunkData(common::crc_t crc, uint64_t offset, std::span<byte> buffer)
{
	// Check eager chunks first (no locking needed as they're read-only after initialization)
	auto eagerIt = mEagerChunkMap.find(crc);
	if (eagerIt != mEagerChunkMap.end())
	{
		const EagerChunk& rEagerChunk = eagerIt->second;
		int64_t iDataSize = rEagerChunk.pHeader->iSize - common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));

		// Validate read bounds
		if (offset + buffer.size() > static_cast<uint64_t>(iDataSize))
		{
			return false;
		}

		// Copy data from eager chunk
		memcpy(buffer.data(), rEagerChunk.pData + offset, buffer.size());
		return true;
	}
	
	// Check lazy chunks
	auto lazyIt = mLazyChunkMap.find(crc);
	if (lazyIt != mLazyChunkMap.end())
	{
		LazyChunk& rLazyChunk = lazyIt->second;
		
		// If chunk is loaded, read from memory
		{
			std::unique_lock lock(mQueueMutex);
			if (rLazyChunk.bLoaded)
			{
				// Validate read bounds
				if (offset + buffer.size() > rLazyChunk.data.size())
				{
					return false;
				}

				// Copy data from lazy chunk
				memcpy(buffer.data(), rLazyChunk.data.data() + offset, buffer.size());
				return true;
			}
		}
		
		// Chunk not loaded - read directly from pack file
		// This path is used for streaming audio data without loading entire chunk
		std::filesystem::path packPath = GetDataFilePath(rLazyChunk.eDataType, ".pack");
		std::fstream packStream(packPath, std::ios::in | std::ios::binary);
		
		if (!packStream.is_open())
		{
			return false;
		}
		
		// Calculate actual data offset in pack file
		constexpr int64_t iHeaderSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader)));
		int64_t iDataOffset = rLazyChunk.location.uiOffset + iHeaderSize;
		int64_t iDataSize = rLazyChunk.location.uiSize - iHeaderSize;
		
		// Validate read bounds
		if (offset + buffer.size() > static_cast<uint64_t>(iDataSize))
		{
			packStream.close();
			return false;
		}

		// Seek and read requested data
		packStream.seekg(iDataOffset + offset);
		packStream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
		packStream.close();
		
		return packStream.good();
	}
	
	// Chunk not found
	return false;
}

int64_t FileManager::GetEagerMemoryBytes() const
{
	int64_t iTotalBytes = 0;
	for (uint32_t i = 0; i < data::kDataTypeCount; ++i)
	{
		if (IsEagerChunk(static_cast<data::DataTypes>(i)))
		{
			iTotalBytes += static_cast<int64_t>(mPackFileData[i].size());
		}
	}
	return iTotalBytes;
}

int64_t FileManager::GetLazyMemoryBytes() const
{
	int64_t iTotalBytes = 0;
	std::unique_lock lock(mQueueMutex);
	for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		if (rLazyChunk.bLoaded)
		{
			iTotalBytes += static_cast<int64_t>(rLazyChunk.data.size());
		}
	}
	return iTotalBytes;
}

int64_t FileManager::GetEagerAllocationCount() const
{
	return static_cast<int64_t>(mEagerChunkMap.size());
}

int64_t FileManager::GetLazyAllocationCount() const
{
	int64_t iCount = 0;
	std::unique_lock lock(mQueueMutex);
	for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
	{
		if (rLazyChunk.bLoaded)
		{
			++iCount;
		}
	}
	return iCount;
}

MemoryStats FileManager::GetMemoryStats(data::DataTypes eDataType) const
{
	MemoryStats stats;
	if (IsEagerChunk(eDataType))
	{
		stats.iBytes = static_cast<int64_t>(mPackFileData[eDataType].size());
		stats.iCount = static_cast<int64_t>(mpChunkLocations[eDataType].size());
	}
	else
	{
		std::unique_lock lock(mQueueMutex);
		for (const auto& [crc, rLazyChunk] : mLazyChunkMap)
		{
			if (rLazyChunk.eDataType == eDataType && rLazyChunk.bLoaded)
			{
				stats.iBytes += static_cast<int64_t>(rLazyChunk.data.size());
				++stats.iCount;
			}
		}
	}
	return stats;
}

} // namespace engine
