#include "BufferManager.h"

#include "Profile/ProfileManager.h"

namespace engine
{

using enum BufferFlags;

BufferManager::BufferManager()
{
	gpBufferManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerBufferManager);

	CreateTerrainMesh();
	CreateWaterMesh();

	uint16_t puiQuads[] = {0, 1, 3, 2, 3, 1};
	float pfQuads[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
	mQuadsVertexBuffer.Create(
	{
		.name = "Quads",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = 6,
		.vkIndexType = VK_INDEX_TYPE_UINT16,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(puiQuads) + sizeof(pfQuads),
	},
	[&](void* pData)
	{
		memcpy(pData, puiQuads, sizeof(puiQuads));
		memcpy(static_cast<char*>(pData) + sizeof(puiQuads), pfQuads, sizeof(pfQuads));
	});

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kModel))
		{
			continue;
		}

		auto [it, bInserted] = mModelMap.try_emplace(rCrc, BufferInfo
		{
			.name = rChunk.pHeader->pcPath,
			.flags = {kIndexVertex, kDeviceLocal},
			.iCount = rChunk.pHeader->modelHeader.iIndexCount,
			.vkIndexType = rChunk.pHeader->modelHeader.iVertexCount < std::numeric_limits<uint16_t>::max() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32,
			.iVertexStride = rChunk.pHeader->modelHeader.iStride,
			.dataVkDeviceSize = static_cast<uint32_t>(rChunk.pHeader->iSize),
		},
		[&](void* pData)
		{
			memcpy(pData, rChunk.pData, rChunk.pHeader->iSize);
		});
		ASSERT(bInserted);
	}

	int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
	ASSERT(iCommandBufferCount <= 4);

	InitializePerCommandBufferBuffers(iCommandBufferCount);

	mLongParticlesStorageBuffer.Create(
	{
		.name = "LongParticles",
		.flags = {kStorage, kDeviceLocal},
		.dataVkDeviceSize = sizeof(shaders::ParticlesLayout),
	},
	[&](void* pData)
	{
		memset(pData, 0, sizeof(shaders::ParticlesLayout));
	});

	mSquareParticlesStorageBuffer.Create(
	{
		.name = "SquareParticles",
		.flags = {kStorage, kDeviceLocal},
		.dataVkDeviceSize = sizeof(shaders::ParticlesLayout),
	},
	[&](void* pData)
	{
		memset(pData, 0, sizeof(shaders::ParticlesLayout));
	});
}

BufferManager::~BufferManager()
{
	DestroyLightingSpreadBuffers();
	DestroyWindHierarchicalBuffers();
	DestroySmokeHierarchicalBuffers();

	gpBufferManager = nullptr;
}

void BufferManager::DestroySwapchainDependentBuffers()
{
	DestroyLightingSpreadBuffers();
	DestroyWindHierarchicalBuffers();
	DestroySmokeHierarchicalBuffers();

	mGlobalLayoutUniformBuffers.clear();
	mMainLayoutUniformBuffers.clear();
	mTextStorageBuffers.clear();
	mSmokeSpreadStorageBuffers.clear();
	mWindSpreadStorageBuffers.clear();
	mLongParticlesSpawnStorageBuffers.clear();
	mSquareParticlesSpawnStorageBuffers.clear();
	mMeshDataStorageBuffers.clear();
	mJointMatrixStorageBuffers.clear();

	for (std::unordered_map<common::crc_t, std::vector<Buffer>>& rMap : mDynamicStorageBuffers)
	{
		rMap.clear();
	}

	mPreviousBuffer.reset();
	for (std::optional<Buffer>& rPrevious : mPreviousMeshDataBuffer)
	{
		rPrevious.reset();
	}
	for (std::optional<Buffer>& rPrevious : mPreviousJointMatrixBuffer)
	{
		rPrevious.reset();
	}

	for (int64_t i = 0; i < 4; ++i)
	{
		miMeshDataOffset[i] = 0;
		miJointMatrixOffset[i] = 0;
		miMeshDataCapacity[i] = 0;
		miJointMatrixCapacity[i] = 0;
	}
}

void BufferManager::CreateSwapchainDependentBuffers()
{
	int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
	ASSERT(iCommandBufferCount <= 4);

	InitializePerCommandBufferBuffers(iCommandBufferCount);
}

void BufferManager::InitializePerCommandBufferBuffers(int64_t iCommandBufferCount)
{
	mGlobalLayoutUniformBuffers.resize(iCommandBufferCount);
	mMainLayoutUniformBuffers.resize(iCommandBufferCount);
	mTextStorageBuffers.resize(iCommandBufferCount);
	mSmokeSpreadStorageBuffers.resize(iCommandBufferCount);
	mWindSpreadStorageBuffers.resize(iCommandBufferCount);
	mLongParticlesSpawnStorageBuffers.resize(iCommandBufferCount);
	mSquareParticlesSpawnStorageBuffers.resize(iCommandBufferCount);

	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		mGlobalLayoutUniformBuffers.at(i).Create(
		{
			.name = "GlobalLayout",
			.flags = {kUniform, kCopyToDeviceLocalEveryFrame},
			.dataVkDeviceSize = sizeof(shaders::GlobalLayout),
		});

		mMainLayoutUniformBuffers.at(i).Create(
		{
			.name = "MainLayout",
			.flags = {kUniform, kCopyToDeviceLocalEveryFrame},
			.dataVkDeviceSize = sizeof(shaders::MainLayout),
		});

		mTextStorageBuffers.at(i).Create(
		{
			.name = "Text",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kiMaxTextQuads * sizeof(shaders::AxisAlignedQuadLayout),
		});

		mSmokeSpreadStorageBuffers.at(i).Create(
		{
			.name = "SmokeSpread",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::AxisAlignedQuadLayout),
		});

		mWindSpreadStorageBuffers.at(i).Create(
		{
			.name = "WindSpread",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::AxisAlignedQuadLayout),
		});

		mLongParticlesSpawnStorageBuffers.at(i).Create(
		{
			.name = "LongParticlesSpawn",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::ParticlesSpawnLayout),
		});

		mSquareParticlesSpawnStorageBuffers.at(i).Create(
		{
			.name = "SquareParticlesSpawn",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::ParticlesSpawnLayout),
		});
	}

	// MeshData buffer for glTF skeletal animation
	// Per-framebuffer with host-visible for CPU updates during Render()
	mMeshDataStorageBuffers.resize(iCommandBufferCount);
	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		mMeshDataStorageBuffers.at(i).Create(
		{
			.name = "MeshData",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = common::MeshData::kiMaxMeshes * sizeof(common::MeshData),
		},
		[&](void* pData)
		{
			common::MeshData* pMeshData = static_cast<common::MeshData*>(pData);

			XMFLOAT4X4 identity {};
			XMStoreFloat4x4(&identity, XMMatrixIdentity());

			for (int64_t iMesh = 0; iMesh < common::MeshData::kiMaxMeshes; ++iMesh)
			{
				pMeshData[iMesh].matrix = identity;
				pMeshData[iMesh].normalMatrix[0] = {1.0f, 0.0f, 0.0f, 0.0f};
				pMeshData[iMesh].normalMatrix[1] = {0.0f, 1.0f, 0.0f, 0.0f};
				pMeshData[iMesh].normalMatrix[2] = {0.0f, 0.0f, 1.0f, 0.0f};
				pMeshData[iMesh].uiJointCount = 0;
				pMeshData[iMesh].uiJointMatrixOffset = 0;
			}
		});
	}

	// Joint matrix buffer for glTF skeletal animation (separate from MeshData)
	// Separate buffer avoids NVIDIA driver hang when dynamically indexing large mat4 arrays
	mJointMatrixStorageBuffers.resize(iCommandBufferCount);
	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		mJointMatrixStorageBuffers.at(i).Create(
		{
			.name = "JointMatrices",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = common::kiInitialJointMatrixCapacity * sizeof(common::JointMatrix),
		},
		[&](void* pData)
		{
			common::JointMatrix* pJointMatrices = static_cast<common::JointMatrix*>(pData);

			common::JointMatrix identity
			{
				.rows =
				{
					{1.0f, 0.0f, 0.0f, 0.0f},
					{0.0f, 1.0f, 0.0f, 0.0f},
					{0.0f, 0.0f, 1.0f, 0.0f},
				},
			};

			for (int64_t j = 0; j < common::kiInitialJointMatrixCapacity; ++j)
			{
				pJointMatrices[j] = identity;
			}
		});
	}

	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		miMeshDataCapacity[i] = common::MeshData::kiMaxMeshes;
		miJointMatrixCapacity[i] = common::kiInitialJointMatrixCapacity;
	}
}

Buffer* BufferManager::CreateDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize elementSize)
{
	std::unordered_map<common::crc_t, std::vector<Buffer>>& rMap = mDynamicStorageBuffers[eType];
	if (rMap.contains(crc))
	{
		return rMap.at(crc).data();
	}

	std::vector<Buffer>& rBuffers = rMap.try_emplace(crc).first->second;

	int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
	rBuffers.resize(iCommandBufferCount);
	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		rBuffers.at(i).Create(
		{
			.name = name,
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = elementSize,
			.iElementSize = elementSize,
		});
	}

	return rBuffers.data();
}

void BufferManager::ResizeDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize newSize, int64_t iFramebuffer)
{
	mPreviousBuffer.reset();

	std::unordered_map<common::crc_t, std::vector<Buffer>>& rMap = mDynamicStorageBuffers[eType];
	Buffer& rOldBuffer = rMap.at(crc).at(iFramebuffer);
	VkDeviceSize elementSize = rOldBuffer.mInfo.iElementSize;
	mPreviousBuffer = std::move(rOldBuffer);

	Buffer& rBuffer = rMap.at(crc).at(iFramebuffer);
	rBuffer.Create(
	{
		.name = name,
		.flags = {kStorage, kHostVisible},
		.dataVkDeviceSize = newSize,
		.iElementSize = elementSize,
	});
}

Buffer* BufferManager::ResizeDynamicBufferIfNeeded(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize layoutSize, int64_t iCapacity, int64_t iCommandBuffer)
{
	VkDeviceSize requiredSize = layoutSize * iCapacity;
	Buffer& rBuffer = mDynamicStorageBuffers[eType].at(crc).at(iCommandBuffer);
	if (rBuffer.mInfo.dataVkDeviceSize >= requiredSize)
	{
		return nullptr;
	}
	ResizeDynamicBuffer(crc, eType, name, requiredSize, iCommandBuffer);
	return &rBuffer;
}

int64_t BufferManager::AllocateMeshData(int64_t iCommandBuffer, int64_t iCount)
{
	int64_t iOffset = miMeshDataOffset[iCommandBuffer];
	miMeshDataOffset[iCommandBuffer] += iCount;
	if (miMeshDataOffset[iCommandBuffer] > miMeshDataCapacity[iCommandBuffer])
	{
		GrowMeshDataBuffer(iCommandBuffer);
	}
	return iOffset;
}

int64_t BufferManager::AllocateJointMatrices(int64_t iCommandBuffer, int64_t iCount)
{
	int64_t iOffset = miJointMatrixOffset[iCommandBuffer];
	miJointMatrixOffset[iCommandBuffer] += iCount;
	if (miJointMatrixOffset[iCommandBuffer] > miJointMatrixCapacity[iCommandBuffer])
	{
		GrowJointMatrixBuffer(iCommandBuffer);
	}
	return iOffset;
}

void BufferManager::ResetSkinningAllocations(int64_t iCommandBuffer)
{
	miMeshDataOffset[iCommandBuffer] = 0;
	miJointMatrixOffset[iCommandBuffer] = 0;
	mPreviousMeshDataBuffer[iCommandBuffer].reset();
	mPreviousJointMatrixBuffer[iCommandBuffer].reset();
}

void BufferManager::GrowMeshDataBuffer(int64_t iCommandBuffer)
{
	miMeshDataCapacity[iCommandBuffer] *= 2;

	void* pOldData = mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory;
	mPreviousMeshDataBuffer[iCommandBuffer] = std::move(mMeshDataStorageBuffers.at(iCommandBuffer));

	mMeshDataStorageBuffers.at(iCommandBuffer).Create(
	{
		.name = "MeshData",
		.flags = {kStorage, kHostVisible},
		.dataVkDeviceSize = miMeshDataCapacity[iCommandBuffer] * sizeof(common::MeshData),
	});

	memcpy(mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory, pOldData, miMeshDataOffset[iCommandBuffer] * sizeof(common::MeshData));

	// Update MeshData descriptor on all model pipelines
	Buffer* pNewBuffer = &mMeshDataStorageBuffers.at(iCommandBuffer);
	gpPipelineManager->mDynamicPipelines.UpdateAllModelPipelineDescriptors(iCommandBuffer, kModelPipelineBindingMeshData, pNewBuffer);
}

void BufferManager::GrowJointMatrixBuffer(int64_t iCommandBuffer)
{
	miJointMatrixCapacity[iCommandBuffer] *= 2;

	void* pOldData = mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory;
	mPreviousJointMatrixBuffer[iCommandBuffer] = std::move(mJointMatrixStorageBuffers.at(iCommandBuffer));

	mJointMatrixStorageBuffers.at(iCommandBuffer).Create(
	{
		.name = "JointMatrices",
		.flags = {kStorage, kHostVisible},
		.dataVkDeviceSize = miJointMatrixCapacity[iCommandBuffer] * sizeof(common::JointMatrix),
	});

	memcpy(mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory, pOldData, miJointMatrixOffset[iCommandBuffer] * sizeof(common::JointMatrix));

	// Update JointMatrix descriptor on all model pipelines
	Buffer* pNewBuffer = &mJointMatrixStorageBuffers.at(iCommandBuffer);
	gpPipelineManager->mDynamicPipelines.UpdateAllModelPipelineDescriptors(iCommandBuffer, kModelPipelineBindingJointMatrix, pNewBuffer);
}

void BufferManager::CreateSmokeHierarchicalBuffers()
{
	DestroySmokeHierarchicalBuffers();

	uint32_t uiMaxWidth = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width);
	uint32_t uiMaxHeight = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.height, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.height);
	uint32_t uiTilesX = (uiMaxWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	uint32_t uiTilesY = (uiMaxHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	uint32_t uiTotalTiles = uiTilesX * uiTilesY;

	// Bit-packed occupancy: 1 bit per tile, packed into uint32s
	uint32_t uiOccupancyUints = (uiTotalTiles + 31) / 32;
	mSmokeOccupancyBufferSize = static_cast<VkDeviceSize>(uiOccupancyUints) * sizeof(uint32_t);
	VkDeviceMemory unusedMemory = VK_NULL_HANDLE;
	Buffer::CreateBuffer("SmokeOccupancy", mSmokeOccupancyBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		mSmokeOccupancyVkBuffer, unusedMemory, mSmokeOccupancyVmaAllocation);

	// Active tile list: VkDispatchIndirectCommand (12 bytes) + packed tile indices (4 bytes each)
	mSmokeActiveTileBufferSize = sizeof(VkDispatchIndirectCommand) + static_cast<VkDeviceSize>(uiTotalTiles) * sizeof(uint32_t);
	VkDeviceMemory unusedActiveTileMemory = VK_NULL_HANDLE;
	Buffer::CreateBuffer("SmokeActiveTile", mSmokeActiveTileBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		mSmokeActiveTileVkBuffer, unusedActiveTileMemory, mSmokeActiveTileVmaAllocation);
}

void BufferManager::DestroySmokeHierarchicalBuffers()
{
	if (mSmokeOccupancyVkBuffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mSmokeOccupancyVkBuffer, mSmokeOccupancyVmaAllocation);
		mSmokeOccupancyVkBuffer = VK_NULL_HANDLE;
		mSmokeOccupancyVmaAllocation = VK_NULL_HANDLE;
		mSmokeOccupancyBufferSize = 0;
	}
	if (mSmokeActiveTileVkBuffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mSmokeActiveTileVkBuffer, mSmokeActiveTileVmaAllocation);
		mSmokeActiveTileVkBuffer = VK_NULL_HANDLE;
		mSmokeActiveTileVmaAllocation = VK_NULL_HANDLE;
		mSmokeActiveTileBufferSize = 0;
	}
}

void BufferManager::CreateWindHierarchicalBuffers()
{
	DestroyWindHierarchicalBuffers();

	uint32_t uiWidth = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width;
	uint32_t uiTilesX = (uiWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	uint32_t uiTotalTiles = uiTilesX * uiTilesX;

	// Bit-packed occupancy: 1 bit per tile, packed into uint32s
	uint32_t uiOccupancyUints = (uiTotalTiles + 31) / 32;
	mWindOccupancyBufferSize = static_cast<VkDeviceSize>(uiOccupancyUints) * sizeof(uint32_t);

	// Active tile list: VkDispatchIndirectCommand (12 bytes) + packed tile indices (4 bytes each)
	mWindActiveTileBufferSize = sizeof(VkDispatchIndirectCommand) + static_cast<VkDeviceSize>(uiTotalTiles) * sizeof(uint32_t);

	for (int64_t i = 0; i < 2; ++i)
	{
		VkDeviceMemory unusedMemoryOccupancy = VK_NULL_HANDLE;
		Buffer::CreateBuffer(i == 0 ? "WindOccupancyA" : "WindOccupancyB", mWindOccupancyBufferSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			mWindOccupancyVkBuffers[i], unusedMemoryOccupancy, mWindOccupancyVmaAllocations[i]);

		VkDeviceMemory unusedMemoryActive = VK_NULL_HANDLE;
		Buffer::CreateBuffer(i == 0 ? "WindActiveTileA" : "WindActiveTileB", mWindActiveTileBufferSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			mWindActiveTileVkBuffers[i], unusedMemoryActive, mWindActiveTileVmaAllocations[i]);
	}
}

void BufferManager::DestroyWindHierarchicalBuffers()
{
	for (int64_t i = 0; i < 2; ++i)
	{
		if (mWindOccupancyVkBuffers[i] != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mWindOccupancyVkBuffers[i], mWindOccupancyVmaAllocations[i]);
			mWindOccupancyVkBuffers[i] = VK_NULL_HANDLE;
			mWindOccupancyVmaAllocations[i] = VK_NULL_HANDLE;
		}
		if (mWindActiveTileVkBuffers[i] != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mWindActiveTileVkBuffers[i], mWindActiveTileVmaAllocations[i]);
			mWindActiveTileVkBuffers[i] = VK_NULL_HANDLE;
			mWindActiveTileVmaAllocations[i] = VK_NULL_HANDLE;
		}
	}
	mWindOccupancyBufferSize = 0;
	mWindActiveTileBufferSize = 0;
}

void BufferManager::CreateLightingSpreadBuffers()
{
	DestroyLightingSpreadBuffers();

	int64_t iCascadeCount = gpTextureManager->mRenderTargetTextures.miCascadeLevelCount;

	for (int64_t iLevel = 0; iLevel < iCascadeCount; ++iLevel)
	{
		uint32_t uiWidth = 0;
		uint32_t uiHeight = 0;
		if (iLevel == 0)
		{
			uiWidth = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.width;
			uiHeight = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.height;
		}
		else
		{
			uiWidth = gpTextureManager->mRenderTargetTextures.mpCascadeTextures[iLevel][0].mInfo.extent.width;
			uiHeight = gpTextureManager->mRenderTargetTextures.mpCascadeTextures[iLevel][0].mInfo.extent.height;
		}

		uint32_t uiTilesX = (uiWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
		uint32_t uiTilesY = (uiHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
		uint32_t uiTotalTiles = uiTilesX * uiTilesY;

		// Bit-packed occupancy: 1 bit per tile, packed into uint32s
		uint32_t uiOccupancyUints = (uiTotalTiles + 31) / 32;
		VkDeviceSize occupancySize = static_cast<VkDeviceSize>(uiOccupancyUints) * sizeof(uint32_t);
		mLightOccupancyBufferSizes[iLevel] = occupancySize;
		VkDeviceMemory unusedOccupancyMemory = VK_NULL_HANDLE;
		Buffer::CreateBuffer("LightOccupancy", occupancySize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			mLightOccupancyVkBuffers[iLevel], unusedOccupancyMemory, mLightOccupancyVmaAllocations[iLevel]);

		// Active tile list: VkDispatchIndirectCommand (12 bytes) + packed tile indices (4 bytes each)
		VkDeviceSize activeTileSize = sizeof(VkDispatchIndirectCommand) + static_cast<VkDeviceSize>(uiTotalTiles) * sizeof(uint32_t);
		mLightActiveTileBufferSizes[iLevel] = activeTileSize;
		VkDeviceMemory unusedActiveTileMemory = VK_NULL_HANDLE;
		Buffer::CreateBuffer("LightActiveTile", activeTileSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			mLightActiveTileVkBuffers[iLevel], unusedActiveTileMemory, mLightActiveTileVmaAllocations[iLevel]);
	}
}

void BufferManager::DestroyLightingSpreadBuffers()
{
	for (int64_t i = 0; i < kiMaxCascadeLevels; ++i)
	{
		if (mLightOccupancyVkBuffers[i] != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mLightOccupancyVkBuffers[i], mLightOccupancyVmaAllocations[i]);
			mLightOccupancyVkBuffers[i] = VK_NULL_HANDLE;
			mLightOccupancyVmaAllocations[i] = VK_NULL_HANDLE;
		}
		if (mLightActiveTileVkBuffers[i] != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mLightActiveTileVkBuffers[i], mLightActiveTileVmaAllocations[i]);
			mLightActiveTileVkBuffers[i] = VK_NULL_HANDLE;
			mLightActiveTileVmaAllocations[i] = VK_NULL_HANDLE;
		}
	}
}

void CreateVisibleAreaMesh(int64_t iMeshX, int64_t iMeshY, std::vector<uint32_t>& rIndices, std::vector<std::byte>& rVertices)
{
	uint32_t* puiIndices = rIndices.data();
	for (int64_t j = 0; j < iMeshY - 1; ++j)
	{
		for (int64_t i = 0; i < iMeshX - 1; ++i)
		{
			int64_t iTrianglesStart = 6 * (j * (iMeshX - 1) + i);
			int64_t iIndexStart = j * iMeshX + i;
			puiIndices[iTrianglesStart + 0] = static_cast<uint32_t>(iIndexStart + 0);
			puiIndices[iTrianglesStart + 1] = static_cast<uint32_t>(iIndexStart + 0 + iMeshX);
			puiIndices[iTrianglesStart + 2] = static_cast<uint32_t>(iIndexStart + 1);
			puiIndices[iTrianglesStart + 3] = static_cast<uint32_t>(iIndexStart + 1);
			puiIndices[iTrianglesStart + 4] = static_cast<uint32_t>(iIndexStart + 0 + iMeshX);
			puiIndices[iTrianglesStart + 5] = static_cast<uint32_t>(iIndexStart + 1 + iMeshX);
		}
	}

	float* pfVertices = reinterpret_cast<float*>(rVertices.data());
	float fQuadWidthX = 1.0f / static_cast<float>(iMeshX - 1);
	float fQuadWidthY = 1.0f / static_cast<float>(iMeshY - 1);
	for (int64_t j = 0; j < iMeshY; ++j)
	{
		for (int64_t i = 0; i < iMeshX; ++i)
		{
			float fX = static_cast<float>(i);
			float fY = static_cast<float>(j);

			pfVertices[2 * (j * iMeshX + i) + 0] = fX * fQuadWidthX;
			pfVertices[2 * (j * iMeshX + i) + 1] = fY * fQuadWidthY;
		}
	}
}

void BufferManager::CreateTerrainMesh()
{
	// Adding 1 to match fQuadWidth exactly in visible area
	auto [iTerrainQuadX, iTerrainQuadY] = gpTextureManager->DetailTextureSize(gWorldDetail.Get());
	iTerrainQuadX -= 1;
	iTerrainQuadY -= 1;
	Log("iTerrainQuad: {} x {}", iTerrainQuadX, iTerrainQuadY);

	int64_t iIndexCount = 6 * iTerrainQuadX * iTerrainQuadY;
	std::vector<uint32_t> indices(iIndexCount);
	std::vector<std::byte> vertices(2 * sizeof(float) * (iTerrainQuadX + 1) * (iTerrainQuadY + 1));
	CreateVisibleAreaMesh(iTerrainQuadX + 1, iTerrainQuadY + 1, indices, vertices);

	mTerrainMeshBuffer.Destroy();
	mTerrainMeshBuffer.Create(
	{
		.name = "TerrainMesh",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = iIndexCount,
		.vkIndexType = VK_INDEX_TYPE_UINT32,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(uint32_t) * indices.size() + vertices.size(),
	},
	[&](void* pData)
	{
		memcpy(pData, indices.data(), sizeof(uint32_t) * indices.size());
		memcpy(static_cast<char*>(pData) + sizeof(uint32_t) * indices.size(), vertices.data(), vertices.size());
	});
}

void BufferManager::CreateWaterMesh()
{
	auto [iWaterQuadX, iWaterQuadY] = gpTextureManager->DetailTextureSize(gWorldDetail.Get());
	iWaterQuadX -= 1;
	iWaterQuadY -= 1;

	int64_t iIndexCount = 6 * iWaterQuadX * iWaterQuadY;
	std::vector<uint32_t> indices(iIndexCount);
	std::vector<std::byte> vertices(2 * sizeof(float) * (iWaterQuadX + 1) * (iWaterQuadY + 1));
	CreateVisibleAreaMesh(iWaterQuadX + 1, iWaterQuadY + 1, indices, vertices);

	mWaterMeshBuffer.Destroy();
	mWaterMeshBuffer.Create(
	{
		.name = "WaterMesh",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = iIndexCount,
		.vkIndexType = VK_INDEX_TYPE_UINT32,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(uint32_t) * indices.size() + vertices.size(),
	},
	[&](void* pData)
	{
		memcpy(pData, indices.data(), sizeof(uint32_t) * indices.size());
		memcpy(static_cast<char*>(pData) + sizeof(uint32_t) * indices.size(), vertices.data(), vertices.size());
	});
}

} // namespace engine
