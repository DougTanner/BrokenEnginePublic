#if defined(BT_CLIENT)

#include "BufferManager.h"

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"

namespace engine
{

using enum BufferFlags;

BufferManager::BufferManager()
{
	ASSERT(gpBufferManager == nullptr);

	gpBufferManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerBufferManager);

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
		std::memcpy(pData, puiQuads, sizeof(puiQuads));
		std::memcpy(static_cast<char*>(pData) + sizeof(puiQuads), pfQuads, sizeof(pfQuads));
	});

	CreateDebugMeshBuffers();

	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kModel))
		{
			continue;
		}

		// Trust boundary: iSize is an on-disk ChunkHeader field. Bound it against the chunk's true in-memory
		// extent (EagerChunk::iDataSize) before it drives the buffer allocation size AND the copy, so a corrupt
		// header can neither size a bogus allocation nor overread off pData. Reject non-positive too: a negative
		// int64 iSize would pass the upper bound and convert to a huge memcpy size_t.
		if (rChunk.pHeader->iSize <= 0 || rChunk.pHeader->iSize > rChunk.iDataSize)
		{
			throw common::CorruptStreamException("BufferManager model");
		}

		const common::ModelHeader& rModelHeader = rChunk.pHeader->modelHeader;
		if (rModelHeader.iIndexCount < 0 || rModelHeader.iIndexCount > UINT32_MAX || rModelHeader.iVertexCount < 0 || rModelHeader.iStride <= 0)
		{
			throw common::CorruptStreamException("BufferManager model");
		}

		int64_t iIndexElementSize = common::ModelHeader::UsesU16Indices(rModelHeader.iVertexCount) ? sizeof(uint16_t) : sizeof(uint32_t);
		if (rModelHeader.iIndexCount > (std::numeric_limits<int64_t>::max() - 3) / iIndexElementSize
			|| rModelHeader.iVertexCount > std::numeric_limits<int64_t>::max() / rModelHeader.iStride)
		{
			throw common::CorruptStreamException("BufferManager model");
		}

		int64_t iVerticesOffset = common::ModelHeader::VerticesOffset(rModelHeader.iIndexCount, iIndexElementSize);
		int64_t iVertexBytes = rModelHeader.iVertexCount * rModelHeader.iStride;
		if (iVerticesOffset > rChunk.pHeader->iSize || iVertexBytes > rChunk.pHeader->iSize - iVerticesOffset)
		{
			throw common::CorruptStreamException("BufferManager model");
		}

		auto [it, bInserted] = mModelMap.try_emplace(rCrc, BufferInfo
		{
			.name = rChunk.pHeader->pcPath,
			.flags = {kIndexVertex, kDeviceLocal},
			.iCount = rModelHeader.iIndexCount,
			.vkIndexType = common::ModelHeader::UsesU16Indices(rModelHeader.iVertexCount) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32,
			.iVertexStride = rModelHeader.iStride,
			.dataVkDeviceSize = static_cast<VkDeviceSize>(rChunk.pHeader->iSize),
		},
		[&](void* pData)
		{
			std::memcpy(pData, rChunk.pData, rChunk.pHeader->iSize);
		});
		ASSERT(bInserted);
	}

	int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
	ASSERT(iCommandBufferCount <= kiMaxFramebuffers);

	InitializePerCommandBufferBuffers(iCommandBufferCount);

	mLongParticlesStorageBuffer.Create(
	{
		.name = "LongParticles",
		.flags = {kStorage, kDeviceLocal},
		.dataVkDeviceSize = sizeof(shaders::ParticlesLayout),
	},
	[&](void* pData)
	{
		std::memset(pData, 0, sizeof(shaders::ParticlesLayout));
	});

	mSquareParticlesStorageBuffer.Create(
	{
		.name = "SquareParticles",
		.flags = {kStorage, kDeviceLocal},
		.dataVkDeviceSize = sizeof(shaders::ParticlesLayout),
	},
	[&](void* pData)
	{
		std::memset(pData, 0, sizeof(shaders::ParticlesLayout));
	});
}

void BufferManager::CreateDebugMeshBuffers()
{
	if constexpr (kbDebugRender)
	{
		// Box: 8 vertices (unit cube -0.5..+0.5), 12 edges = 24 indices
		{
			constexpr float h = 0.5f;
			float pfVertices[] =
			{
				-h, -h, -h,  h, -h, -h,  h,  h, -h, -h,  h, -h,
				-h, -h,  h,  h, -h,  h,  h,  h,  h, -h,  h,  h,
			};
			uint16_t puiIndices[] =
			{
				0, 1, 1, 2, 2, 3, 3, 0,
				4, 5, 5, 6, 6, 7, 7, 4,
				0, 4, 1, 5, 2, 6, 3, 7,
			};
			mDebugBoxVertexBuffer.Create(
			{
				.name = "DebugBox",
				.flags = {kIndexVertex, kDeviceLocal},
				.iCount = std::size(puiIndices),
				.vkIndexType = VK_INDEX_TYPE_UINT16,
				.iVertexStride = sizeof(float) * 3,
				.dataVkDeviceSize = sizeof(puiIndices) + sizeof(pfVertices),
			},
			[&](void* pData)
			{
				std::memcpy(pData, puiIndices, sizeof(puiIndices));
				std::memcpy(static_cast<char*>(pData) + sizeof(puiIndices), pfVertices, sizeof(pfVertices));
			});
		}

		// Sphere: 3 great circles (XY, XZ, YZ), 32 segments each
		{
			constexpr int64_t kiSegments = 32;
			constexpr int64_t kiCircles = 3;
			float pfVertices[kiCircles * kiSegments * 3] {};
			uint16_t puiIndices[kiCircles * kiSegments * 2] {};

			for (int64_t c = 0; c < kiCircles; ++c)
			{
				for (int64_t s = 0; s < kiSegments; ++s)
				{
					float fAngle = XM_2PI * static_cast<float>(s) / static_cast<float>(kiSegments);
					float fCos = std::cosf(fAngle);
					float fSin = std::sinf(fAngle);
					int64_t iVertex = (c * kiSegments + s) * 3;
					switch (c)
					{
					case 0: pfVertices[iVertex] = fCos; pfVertices[iVertex + 1] = fSin; pfVertices[iVertex + 2] = 0.0f; break; // XY
					case 1: pfVertices[iVertex] = fCos; pfVertices[iVertex + 1] = 0.0f; pfVertices[iVertex + 2] = fSin; break; // XZ
					case 2: pfVertices[iVertex] = 0.0f; pfVertices[iVertex + 1] = fCos; pfVertices[iVertex + 2] = fSin; break; // YZ
					default: break;
					}
					int64_t iIndex = (c * kiSegments + s) * 2;
					puiIndices[iIndex] = static_cast<uint16_t>(c * kiSegments + s);
					puiIndices[iIndex + 1] = static_cast<uint16_t>(c * kiSegments + ((s + 1) % kiSegments));
				}
			}
			mDebugSphereVertexBuffer.Create(
			{
				.name = "DebugSphere",
				.flags = {kIndexVertex, kDeviceLocal},
				.iCount = std::size(puiIndices),
				.vkIndexType = VK_INDEX_TYPE_UINT16,
				.iVertexStride = sizeof(float) * 3,
				.dataVkDeviceSize = sizeof(puiIndices) + sizeof(pfVertices),
			},
			[&](void* pData)
			{
				std::memcpy(pData, puiIndices, sizeof(puiIndices));
				std::memcpy(static_cast<char*>(pData) + sizeof(puiIndices), pfVertices, sizeof(pfVertices));
			});
		}

		// Circle: 1 circle in XY plane, 32 segments
		{
			constexpr int64_t kiSegments = 32;
			float pfVertices[kiSegments * 3] {};
			uint16_t puiIndices[kiSegments * 2] {};

			for (int64_t s = 0; s < kiSegments; ++s)
			{
				float fAngle = XM_2PI * static_cast<float>(s) / static_cast<float>(kiSegments);
				int64_t iVertex = s * 3;
				pfVertices[iVertex] = std::cosf(fAngle);
				pfVertices[iVertex + 1] = std::sinf(fAngle);
				pfVertices[iVertex + 2] = 0.0f;
				int64_t iIndex = s * 2;
				puiIndices[iIndex] = static_cast<uint16_t>(s);
				puiIndices[iIndex + 1] = static_cast<uint16_t>((s + 1) % kiSegments);
			}
			mDebugCircleVertexBuffer.Create(
			{
				.name = "DebugCircle",
				.flags = {kIndexVertex, kDeviceLocal},
				.iCount = std::size(puiIndices),
				.vkIndexType = VK_INDEX_TYPE_UINT16,
				.iVertexStride = sizeof(float) * 3,
				.dataVkDeviceSize = sizeof(puiIndices) + sizeof(pfVertices),
			},
			[&](void* pData)
			{
				std::memcpy(pData, puiIndices, sizeof(puiIndices));
				std::memcpy(static_cast<char*>(pData) + sizeof(puiIndices), pfVertices, sizeof(pfVertices));
			});
		}

		// Line: 2 vertices along +X
		{
			float pfVertices[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
			uint16_t puiIndices[] = {0, 1};
			mDebugLineVertexBuffer.Create(
			{
				.name = "DebugLine",
				.flags = {kIndexVertex, kDeviceLocal},
				.iCount = std::size(puiIndices),
				.vkIndexType = VK_INDEX_TYPE_UINT16,
				.iVertexStride = sizeof(float) * 3,
				.dataVkDeviceSize = sizeof(puiIndices) + sizeof(pfVertices),
			},
			[&](void* pData)
			{
				std::memcpy(pData, puiIndices, sizeof(puiIndices));
				std::memcpy(static_cast<char*>(pData) + sizeof(puiIndices), pfVertices, sizeof(pfVertices));
			});
		}
	}
}

BufferManager::~BufferManager()
{
	DestroyWindHierarchicalBuffers();
	DestroySmokeHierarchicalBuffers();

	if (gpBufferManager == this)
	{
		gpBufferManager = nullptr;
	}
}

void BufferManager::DestroySwapchainDependentBuffers()
{
	DestroyWindHierarchicalBuffers();
	DestroySmokeHierarchicalBuffers();

	mGlobalLayoutUniformBuffers.clear();
	mMainLayoutUniformBuffers.clear();
	mUiRectStorageBuffers.clear();
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

	for (int64_t i = 0; i < kiMaxFramebuffers; ++i)
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
	ASSERT(iCommandBufferCount <= kiMaxFramebuffers);

	InitializePerCommandBufferBuffers(iCommandBufferCount);
}

void BufferManager::InitializePerCommandBufferBuffers(int64_t iCommandBufferCount)
{
	mGlobalLayoutUniformBuffers.resize(iCommandBufferCount);
	mMainLayoutUniformBuffers.resize(iCommandBufferCount);
	mUiRectStorageBuffers.resize(iCommandBufferCount);
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

		mUiRectStorageBuffers.at(i).Create(
		{
			.name = "UiRects",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = ImGuiManager::kiMaxUiRects * sizeof(XMFLOAT4),
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

			XMFLOAT4X4A identity {};
			XMStoreFloat4x4A(&identity, XMMatrixIdentity());

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
	// Separate dynamically-sized buffer keeps MeshData small and fixed-size with no embedded joint cap
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
	auto [it, bInserted] = rMap.try_emplace(crc);
	std::vector<Buffer>& rBuffers = it->second;
	if (!bInserted)
	{
		return rBuffers.data();
	}

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
	// Single-slot stash, safe even though a second same-frame resize would destroy this old buffer
	//   immediately, via a three-point chain: (1) every dynamic buffer is a per-framebuffer instance
	//   (CreateDynamicBuffer), so only framebuffer i's command buffer ever referenced the (crc, i) buffer;
	//   (2) all resize callers run after Graphics::RenderGlobal's top-of-frame fence wait for framebuffer i,
	//   so the prior submission completed; (3) callers immediately rewrite the per-framebuffer descriptor set
	//   (e.g. BillboardsRender / PlayersRender) and record-once command buffers reference buffers only through
	//   descriptor sets. (GrowMeshDataBuffer / GrowJointMatrixBuffer apply this same three-point chain to their
	//   per-framebuffer mPreviousMeshDataBuffer / mPreviousJointMatrixBuffer stash arrays, where a second same-frame
	//   grow -- not a resize -- is the overwrite case.)
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
		GrowMeshDataBuffer(iCommandBuffer, iOffset);
	}
	return iOffset;
}

int64_t BufferManager::AllocateJointMatrices(int64_t iCommandBuffer, int64_t iCount)
{
	int64_t iOffset = miJointMatrixOffset[iCommandBuffer];
	miJointMatrixOffset[iCommandBuffer] += iCount;
	if (miJointMatrixOffset[iCommandBuffer] > miJointMatrixCapacity[iCommandBuffer])
	{
		GrowJointMatrixBuffer(iCommandBuffer, iOffset);
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

void BufferManager::GrowMeshDataBuffer(int64_t iCommandBuffer, int64_t iValidCount)
{
	while (miMeshDataCapacity[iCommandBuffer] < miMeshDataOffset[iCommandBuffer])
	{
		miMeshDataCapacity[iCommandBuffer] *= 2;
	}

	void* pOldData = mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory;

	// Per-framebuffer single-slot stash, safe even when a SECOND same-frame grow on this iCommandBuffer
	//   overwrites the slot and frees the buffer the previous same-frame grow stashed, via the same three-point
	//   chain as ResizeDynamicBuffer: (1) mMeshDataStorageBuffers is a per-framebuffer instance, so only framebuffer
	//   iCommandBuffer's command buffer ever referenced this buffer; (2) all grow callers run from RenderFrameMain,
	//   after Graphics::RenderGlobal's top-of-frame fence wait for framebuffer iCommandBuffer drained the prior
	//   submission; (3) the repoint below points framebuffer iCommandBuffer's model descriptors at the NEW buffer,
	//   and record-once command buffers reference buffers only through descriptor sets, so this frame's
	//   not-yet-submitted CB reads the latest buffer, never the freed one. ResetSkinningAllocations releases only
	//   the final grow's buffer, once per frame.
	mPreviousMeshDataBuffer[iCommandBuffer] = std::move(mMeshDataStorageBuffers.at(iCommandBuffer));

	mMeshDataStorageBuffers.at(iCommandBuffer).Create(
	{
		.name = "MeshData",
		.flags = {kStorage, kHostVisible},
		.dataVkDeviceSize = miMeshDataCapacity[iCommandBuffer] * sizeof(common::MeshData),
	});

	// Copy only the iValidCount elements written before this allocation; the old buffer holds nothing past them
	std::memcpy(mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory, pOldData, iValidCount * sizeof(common::MeshData));

	// Update MeshData descriptor on all model pipelines
	Buffer* pNewBuffer = &mMeshDataStorageBuffers.at(iCommandBuffer);
	gpPipelineManager->mDynamicPipelines.UpdateAllModelPipelineDescriptors(iCommandBuffer, shaders::kiModelBindingMeshData, pNewBuffer);
}

void BufferManager::GrowJointMatrixBuffer(int64_t iCommandBuffer, int64_t iValidCount)
{
	while (miJointMatrixCapacity[iCommandBuffer] < miJointMatrixOffset[iCommandBuffer])
	{
		miJointMatrixCapacity[iCommandBuffer] *= 2;
	}

	void* pOldData = mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory;

	// Per-framebuffer stash overwrite, safe by the same three-point chain as GrowMeshDataBuffer above.
	mPreviousJointMatrixBuffer[iCommandBuffer] = std::move(mJointMatrixStorageBuffers.at(iCommandBuffer));

	mJointMatrixStorageBuffers.at(iCommandBuffer).Create(
	{
		.name = "JointMatrices",
		.flags = {kStorage, kHostVisible},
		.dataVkDeviceSize = miJointMatrixCapacity[iCommandBuffer] * sizeof(common::JointMatrix),
	});

	// Copy only the iValidCount elements written before this allocation; the old buffer holds nothing past them
	std::memcpy(mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory, pOldData, iValidCount * sizeof(common::JointMatrix));

	// Update JointMatrix descriptor on all model pipelines
	Buffer* pNewBuffer = &mJointMatrixStorageBuffers.at(iCommandBuffer);
	gpPipelineManager->mDynamicPipelines.UpdateAllModelPipelineDescriptors(iCommandBuffer, shaders::kiModelBindingJointMatrix, pNewBuffer);
}

void BufferManager::CreateSmokeHierarchicalBuffers()
{
	DestroySmokeHierarchicalBuffers();

	uint32_t uiMaxWidth = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width);
	uint32_t uiMaxHeight = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.height, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.height);
	uint32_t uiTilesX = TileCount(uiMaxWidth);
	uint32_t uiTilesY = TileCount(uiMaxHeight);
	uint32_t uiTotalTiles = uiTilesX * uiTilesY;

	// Bit-packed occupancy: 1 bit per tile, packed into uint32s
	uint32_t uiOccupancyUints = (uiTotalTiles + 31) / 32;
	mSmokeOccupancyBufferSize = static_cast<VkDeviceSize>(uiOccupancyUints) * sizeof(uint32_t);
	for (int64_t i = 0; i < 2; ++i)
	{
		Buffer::CreateBuffer(i == 0 ? "SmokeOccupancyA" : "SmokeOccupancyB", mSmokeOccupancyBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mSmokeOccupancyVkBuffers[i], mSmokeOccupancyVmaAllocations[i]);
	}

	// Occupancy describes persistent texture contents, so both buffers must start empty alongside the
	// creation-time smoke texture clears. The one-shot barrier also makes the fills visible to the first spread.
	OneShotCommandBuffer oneShotCommandBuffer;
	for (VkBuffer vkSmokeOccupancyBuffer : mSmokeOccupancyVkBuffers)
	{
		vkCmdFillBuffer(oneShotCommandBuffer.mVkCommandBuffer, vkSmokeOccupancyBuffer, 0, mSmokeOccupancyBufferSize, 0);
	}
	VkBufferMemoryBarrier pSmokeOccupancyInitBarriers[2] {};
	for (int64_t i = 0; i < 2; ++i)
	{
		pSmokeOccupancyInitBarriers[i] =
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = mSmokeOccupancyVkBuffers[i],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		};
	}
	vkCmdPipelineBarrier(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pSmokeOccupancyInitBarriers)), pSmokeOccupancyInitBarriers, 0, nullptr);
	oneShotCommandBuffer.Execute();

	// Active tile list: VkDispatchIndirectCommand (12 bytes) + packed tile indices (4 bytes each)
	mSmokeActiveTileBufferSize = sizeof(VkDispatchIndirectCommand) + static_cast<VkDeviceSize>(uiTotalTiles) * sizeof(uint32_t);
	Buffer::CreateBuffer("SmokeActiveTile", mSmokeActiveTileBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mSmokeActiveTileVkBuffer, mSmokeActiveTileVmaAllocation);
}

void BufferManager::DestroySmokeHierarchicalBuffers()
{
	for (int64_t i = 0; i < 2; ++i)
	{
		if (mSmokeOccupancyVkBuffers[i] != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mSmokeOccupancyVkBuffers[i], mSmokeOccupancyVmaAllocations[i]);
			mSmokeOccupancyVkBuffers[i] = VK_NULL_HANDLE;
			mSmokeOccupancyVmaAllocations[i] = VK_NULL_HANDLE;
		}
	}
	mSmokeOccupancyBufferSize = 0;
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
	uint32_t uiHeight = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.height;
	uint32_t uiTilesX = TileCount(uiWidth);
	uint32_t uiTilesY = TileCount(uiHeight);
	uint32_t uiTotalTiles = uiTilesX * uiTilesY;

	// Bit-packed occupancy: 1 bit per tile, packed into uint32s
	uint32_t uiOccupancyUints = (uiTotalTiles + 31) / 32;
	mWindOccupancyBufferSize = static_cast<VkDeviceSize>(uiOccupancyUints) * sizeof(uint32_t);

	// Active tile list: VkDispatchIndirectCommand (12 bytes) + packed tile indices (4 bytes each)
	mWindActiveTileBufferSize = sizeof(VkDispatchIndirectCommand) + static_cast<VkDeviceSize>(uiTotalTiles) * sizeof(uint32_t);

	for (int64_t i = 0; i < 2; ++i)
	{
		Buffer::CreateBuffer(i == 0 ? "WindOccupancyA" : "WindOccupancyB", mWindOccupancyBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mWindOccupancyVkBuffers[i], mWindOccupancyVmaAllocations[i]);

		Buffer::CreateBuffer(i == 0 ? "WindActiveTileA" : "WindActiveTileB", mWindActiveTileBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mWindActiveTileVkBuffers[i], mWindActiveTileVmaAllocations[i]);
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

static void CreateVisibleAreaMesh(int64_t iMeshX, int64_t iMeshY, uint32_t* puiIndices, std::byte* pVertices)
{
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

	float* pfVertices = reinterpret_cast<float*>(pVertices);
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

// Build N LODs of the visible-area mesh into one concat (indices then vertices) byte stream and
// populate per-LOD draw params. LOD k has each-dim quad count / 2^k (total / 4^k). Each LOD's
// indices reference vertex indices starting from 0 within its own LOD; the per-LOD `iVertexOffset`
// is supplied to vkCmdDrawIndexedIndirect via the indirect command's `vertexOffset` field at draw time.
static void BuildLodConcatMesh(int64_t iLod0QuadX, int64_t iLod0QuadY, BufferManager::VisibleAreaMeshLod* pLods, std::vector<uint32_t>& rIndices, std::vector<std::byte>& rVertices)
{
	int64_t iTotalIndices = 0;
	int64_t iTotalVertices = 0;
	for (int64_t iLod = 0; iLod < BufferManager::kiVisibleAreaLodCount; ++iLod)
	{
		int64_t iQuadX = std::max<int64_t>(1, iLod0QuadX >> iLod);
		int64_t iQuadY = std::max<int64_t>(1, iLod0QuadY >> iLod);
		iTotalIndices  += 6 * iQuadX * iQuadY;
		iTotalVertices += (iQuadX + 1) * (iQuadY + 1);
	}

	rIndices.resize(iTotalIndices);
	rVertices.resize(2 * sizeof(float) * iTotalVertices);

	int64_t iIndexCursor = 0;
	int64_t iVertexCursor = 0;
	for (int64_t iLod = 0; iLod < BufferManager::kiVisibleAreaLodCount; ++iLod)
	{
		int64_t iQuadX = std::max<int64_t>(1, iLod0QuadX >> iLod);
		int64_t iQuadY = std::max<int64_t>(1, iLod0QuadY >> iLod);
		int64_t iIdxCount  = 6 * iQuadX * iQuadY;
		int64_t iVertCount = (iQuadX + 1) * (iQuadY + 1);

		CreateVisibleAreaMesh(iQuadX + 1, iQuadY + 1, rIndices.data() + iIndexCursor, rVertices.data() + iVertexCursor * 2 * sizeof(float));

		pLods[iLod] =
		{
			.iIndexOffset  = iIndexCursor,
			.iIndexCount   = iIdxCount,
			.iVertexOffset = iVertexCursor,
			.iQuadCountX   = iQuadX,
			.iQuadCountY   = iQuadY,
		};

		iIndexCursor  += iIdxCount;
		iVertexCursor += iVertCount;
	}
}

void BufferManager::CreateWaterMesh()
{
	auto [iFullX, iFullY] = gpTextureManager->WaterDetailTextureSize(gWaterShapeDetail.Get());
	int64_t iLod0QuadX = iFullX - 1;
	int64_t iLod0QuadY = iFullY - 1;

	std::vector<uint32_t> indices;
	std::vector<std::byte> vertices;
	BuildLodConcatMesh(iLod0QuadX, iLod0QuadY, mWaterMeshLods, indices, vertices);

	mWaterMeshBuffer.Create(
	{
		.name = "WaterMesh",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = static_cast<int64_t>(indices.size()),
		.vkIndexType = VK_INDEX_TYPE_UINT32,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(uint32_t) * indices.size() + vertices.size(),
	},
	[&](void* pData)
	{
		std::memcpy(pData, indices.data(), sizeof(uint32_t) * indices.size());
		std::memcpy(static_cast<char*>(pData) + sizeof(uint32_t) * indices.size(), vertices.data(), vertices.size());
	});
}

} // namespace engine

#endif // defined(BT_CLIENT)
