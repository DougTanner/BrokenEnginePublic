#include "BufferManager.h"

#include "Graphics/Graphics.h"
#include "PipelineManager.h"
#include "SwapchainManager.h"
#include "TextManager.h"
#include "TextureManager.h"
#include "Graphics/Objects/ModelPipeline.h"
#include "Profile/ProfileManager.h"

#include "Game.h"
// DT: GAMELOGIC
#include "Frame/Collections/Missiles.h"

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

	// MeshData buffer for glTF skeletal animation (small struct without embedded joints)
	// Per-framebuffer with host-visible for CPU updates during Render()
	// Each mesh gets: matrix (mesh world) + normalMatrix + jointCount + jointMatrixOffset
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

			// Initialize all mesh data with identity matrices and zero joint count
			XMFLOAT4X4 identity;
			XMStoreFloat4x4(&identity, XMMatrixIdentity());

			for (int64_t iMesh = 0; iMesh < common::MeshData::kiMaxMeshes; ++iMesh)
			{
				pMeshData[iMesh].matrix = identity;
				// Initialize normal matrix to identity (mat3 as 3 vec4s)
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
	// Uses JointMatrix (3 vec4s, 48 bytes) instead of mat4 (64 bytes) - translation packed into .w
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

			// Initialize all joint matrices to identity
			// Format: rows[i].xyz = rotation row i, rows[i].w = translation component i
			// Identity: rotation = I, translation = (0,0,0)
			common::JointMatrix identity
			{
				.rows =
				{
					{1.0f, 0.0f, 0.0f, 0.0f},  // rotation row 0, Tx=0
					{0.0f, 1.0f, 0.0f, 0.0f},  // rotation row 1, Ty=0
					{0.0f, 0.0f, 1.0f, 0.0f},  // rotation row 2, Tz=0
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

BufferManager::~BufferManager()
{
	gpBufferManager = nullptr;
}

Buffer* BufferManager::CreateDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize elementSize)
{
	std::unordered_map<common::crc_t, std::vector<Buffer>>& rMap = mDynamicStorageBuffers[eType];
	if (rMap.contains(crc))
	{
		return rMap.at(crc).data();
	}

	std::vector<Buffer>& rBuffers = rMap[crc];

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

	// Update MeshData descriptor (binding 15) on all model pipelines
	Buffer* pNewBuffer = &mMeshDataStorageBuffers.at(iCommandBuffer);
	for (auto& [rCrc, rpPipeline] : gpPipelineManager->mDynamicModelPipelineMaps[kDynamicModelPipelineModel])
	{
		rpPipeline->UpdateStorageBufferDescriptors(iCommandBuffer, 15, pNewBuffer);
	}
	for (auto& [rCrc, rpPipeline] : gpPipelineManager->mDynamicModelPipelineMaps[kDynamicModelPipelineModelShadow])
	{
		rpPipeline->UpdateStorageBufferDescriptors(iCommandBuffer, 15, pNewBuffer);
	}
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

	// Update JointMatrix descriptor (binding 16) on all model pipelines
	Buffer* pNewBuffer = &mJointMatrixStorageBuffers.at(iCommandBuffer);
	for (auto& [rCrc, rpPipeline] : gpPipelineManager->mDynamicModelPipelineMaps[kDynamicModelPipelineModel])
	{
		rpPipeline->UpdateStorageBufferDescriptors(iCommandBuffer, 16, pNewBuffer);
	}
	for (auto& [rCrc, rpPipeline] : gpPipelineManager->mDynamicModelPipelineMaps[kDynamicModelPipelineModelShadow])
	{
		rpPipeline->UpdateStorageBufferDescriptors(iCommandBuffer, 16, pNewBuffer);
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
