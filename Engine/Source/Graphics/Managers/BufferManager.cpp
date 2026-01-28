#include "BufferManager.h"

#include "Graphics/Graphics.h"
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
		Assert(bInserted);
	}

	int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();

	mGlobalLayoutUniformBuffers.resize(iCommandBufferCount);
	mMainLayoutUniformBuffers.resize(iCommandBufferCount);
	mTextStorageBuffers.resize(iCommandBufferCount);
	mSmokeSpreadStorageBuffers.resize(iCommandBufferCount);
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

	// Joint matrices buffer for glTF skeletal animation (128 joints per instance, 64 instances max)
	// Per-framebuffer with copy-every-frame for CPU updates during Render()
	// Initialized with identity matrices so non-animated models render correctly
	constexpr int64_t kiJointsPerInstance = 128;
	constexpr int64_t kiMaxInstances = 64;
	constexpr int64_t kiJointMatrixSize = sizeof(XMFLOAT4X4);
	mJointMatricesStorageBuffers.resize(iCommandBufferCount);
	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		mJointMatricesStorageBuffers.at(i).Create(
		{
			.name = "JointMatrices",
			.flags = {kStorage, kCopyToDeviceLocalEveryFrame},
			.dataVkDeviceSize = kiJointsPerInstance * kiMaxInstances * kiJointMatrixSize,
		},
		[&](void* pData)
		{
			XMFLOAT4X4* pMatrices = static_cast<XMFLOAT4X4*>(pData);

			// Standard identity for joint matrices (w=1.0)
			XMFLOAT4X4 identity;
			XMStoreFloat4x4(&identity, XMMatrixIdentity());

			// Identity with marker for mesh matrices (w=2.0) - signals non-skinned path in shader
			XMFLOAT4X4 meshIdentity;
			XMMATRIX matMeshIdentity = XMMatrixIdentity();
			matMeshIdentity.r[3] = XMVectorSetW(matMeshIdentity.r[3], 2.0f);
			XMStoreFloat4x4(&meshIdentity, matMeshIdentity);

			for (int64_t i = 0; i < kiMaxInstances; ++i)
			{
				int64_t iBase = i * kiJointsPerInstance;
				// Slots 0-63: joint matrices (standard identity w=1.0)
				for (int64_t j = 0; j < 64; ++j)
				{
					pMatrices[iBase + j] = identity;
				}
				// Slots 64-79: mesh matrices for materials 0-15 (identity with w=2.0 marker)
				for (int64_t j = 64; j < 80; ++j)
				{
					pMatrices[iBase + j] = meshIdentity;
				}
				// Slots 80-127: remaining (standard identity)
				for (int64_t j = 80; j < kiJointsPerInstance; ++j)
				{
					pMatrices[iBase + j] = identity;
				}
			}
		});
	}
}

BufferManager::~BufferManager()
{
	gpBufferManager = nullptr;
}

Buffer* BufferManager::CreateDynamicBuffer(common::crc_t crc, std::string_view name, VkDeviceSize size)
{
	if (mDynamicStorageBuffers.contains(crc))
	{
		return mDynamicStorageBuffers.at(crc).data();
	}

	std::vector<Buffer>& rBuffers = mDynamicStorageBuffers[crc];

	int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
	rBuffers.resize(iCommandBufferCount);
	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		rBuffers.at(i).Create(
		{
			.name = name,
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = size,
		});
	}

	return rBuffers.data();
}

void BufferManager::ResizeDynamicBuffer(common::crc_t crc, std::string_view name, VkDeviceSize newSize, int64_t iFramebuffer)
{
	mPreviousBuffer.reset();

	Buffer& rBuffer = mDynamicStorageBuffers.at(crc).at(iFramebuffer);
	mPreviousBuffer = std::move(rBuffer);

	rBuffer.Create(
	{
		.name = name,
		.flags = {kStorage, kHostVisible},
		.dataVkDeviceSize = newSize,
	});
}

void CreateVisibleAreaMesh(int64_t iMeshX, int64_t iMeshY, std::vector<uint32_t>& rIndices, std::vector<byte>& rVertices)
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

	auto pfVertices = reinterpret_cast<float*>(rVertices.data());
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
	std::vector<byte> vertices(2 * sizeof(float) * (iTerrainQuadX + 1) * (iTerrainQuadY + 1));
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
	std::vector<byte> vertices(2 * sizeof(float) * (iWaterQuadX + 1) * (iWaterQuadY + 1));
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
