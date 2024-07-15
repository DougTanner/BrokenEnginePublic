#include "BufferManager.h"

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

using enum BufferFlags;

BufferManager::BufferManager()
{
	gpBufferManager = this;

	SCOPED_BOOT_TIMER(kBootTimerBufferManager);

	CreateTerrainMesh();
	CreateWaterMesh();

	uint16_t puiQuads[] = {0, 1, 3, 2, 3, 1};
	float pfQuads[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
	mQuadsVertexBuffer.Create(
	{
		.pcName = "Quads",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = 6,
		.vkIndexType = VK_INDEX_TYPE_UINT16,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(puiQuads) + sizeof(pfQuads),
	},
	[&](void* pData)
	{
		memcpy(pData, puiQuads, sizeof(puiQuads));
		memcpy(reinterpret_cast<char*>(pData) + sizeof(puiQuads), pfQuads, sizeof(pfQuads));
	});

	auto& rChunkMap = gpFileManager->GetDataChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kModel))
		{
			continue;
		}

		auto [it, bInserted] = mModelMap.try_emplace(rCrc, BufferInfo
		{
			.pcName = rChunk.pHeader->pcPath,
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

	int64_t iCommandBufferCount = gpCommandBufferManager->CommandBufferCount();

	mGlobalLayoutUniformBuffers.resize(iCommandBufferCount);
	mMainLayoutUniformBuffers.resize(iCommandBufferCount);
	mVisibleLightsStorageBuffers.resize(iCommandBufferCount);
	mAreaLightsStorageBuffers.resize(iCommandBufferCount);
	mPointLightsStorageBuffers.resize(iCommandBufferCount);
	mTextStorageBuffers.resize(iCommandBufferCount);
	mPlayerStorageBuffers.resize(iCommandBufferCount);
	mPlayerMissilesStorageBuffers.resize(iCommandBufferCount);
	mSpaceshipsStorageBuffers.resize(iCommandBufferCount);
	mWidgetsStorageBuffers.resize(iCommandBufferCount);
	mSmokeSpreadStorageBuffers.resize(iCommandBufferCount);
	mSmokePuffsStorageBuffers.resize(iCommandBufferCount);
	mSmokeTrailsStorageBuffers.resize(iCommandBufferCount);
	mLongParticlesSpawnStorageBuffers.resize(iCommandBufferCount);
	mSquareParticlesSpawnStorageBuffers.resize(iCommandBufferCount);
	mBillboardsStorageBuffers.resize(iCommandBufferCount);
	mHexShieldsStorageBuffers.resize(iCommandBufferCount);
#if defined(ENABLE_GLTF_TEST)
	mGltfsStorageBuffers.resize(iCommandBufferCount);
#endif

	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		mGlobalLayoutUniformBuffers.at(i).Create(
		{
			.pcName = "GlobalLayout",
			.flags = {kUniform, kCopyToDeviceLocalEveryFrame},
			.dataVkDeviceSize = sizeof(shaders::GlobalLayout),
		});

		mMainLayoutUniformBuffers.at(i).Create(
		{
			.pcName = "MainLayout",
			.flags = {kUniform, kCopyToDeviceLocalEveryFrame},
			.dataVkDeviceSize = sizeof(shaders::MainLayout),
		});

		mVisibleLightsStorageBuffers.at(i).Create(
		{
			.pcName = "VisibleLights",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = (kuiMaxAreaLights + kuiMaxPointLights) * sizeof(shaders::VisibleLightQuadLayout),
		});

		mAreaLightsStorageBuffers.at(i).Create(
		{
			.pcName = "AreasLights",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kuiMaxAreaLights * sizeof(shaders::QuadLayout),
		});

		mPointLightsStorageBuffers.at(i).Create(
		{
			.pcName = "PointLights",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kuiMaxPointLights * sizeof(shaders::AxisAlignedQuadLayout),
		});

		mTextStorageBuffers.at(i).Create(
		{
			.pcName = "Text",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kiMaxTextQuads * sizeof(shaders::AxisAlignedQuadLayout),
		});

		mPlayerStorageBuffers.at(i).Create(
		{
			.pcName = "Player",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::ObjectLayout),
		});

		mPlayerMissilesStorageBuffers.at(i).Create(
		{
			.pcName = "PlayerMissiles",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = game::Missiles::kiMax * sizeof(shaders::GltfLayout),
		});

		mSpaceshipsStorageBuffers.at(i).Create(
		{
			.pcName = "Spaceships",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = game::Spaceships::kiMax * sizeof(shaders::GltfLayout),
		});

		mWidgetsStorageBuffers.at(i).Create(
		{
			.pcName = "Widget",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = shaders::kiMaxWidgets * sizeof(shaders::WidgetLayout),
		});

		mSmokeSpreadStorageBuffers.at(i).Create(
		{
			.pcName = "SmokeSpread",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::AxisAlignedQuadLayout),
		});

		mSmokePuffsStorageBuffers.at(i).Create(
		{
			.pcName = "SmokePuff",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kuiMaxPuffs * sizeof(shaders::AxisAlignedQuadLayout),
		 });

		mSmokeTrailsStorageBuffers.at(i).Create(
		{
			.pcName = "SmokeTrail",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kuiMaxTrails * sizeof(shaders::QuadLayout),
		});

		mLongParticlesSpawnStorageBuffers.at(i).Create(
		{
			.pcName = "LongParticlesSpawn",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::ParticlesSpawnLayout),
		});

		mSquareParticlesSpawnStorageBuffers.at(i).Create(
		{
			.pcName = "SquareParticlesSpawn",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::ParticlesSpawnLayout),
		});

		mBillboardsStorageBuffers.at(i).Create(
		{
			.pcName = "Billboards",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kuiMaxBillboards * sizeof(shaders::BillboardLayout),
		});

		mHexShieldsStorageBuffers.at(i).Create(
		{
			.pcName = "HexShields",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = kuiMaxHexShields * sizeof(shaders::HexShieldLayout),
		});

	#if defined(ENABLE_GLTF_TEST)
		mGltfsStorageBuffers.at(i).Create(
		{
			.pcName = "Gltfs",
			.flags = {kStorage, kHostVisible},
			.dataVkDeviceSize = sizeof(shaders::GltfLayout),
		});
	#endif
	}

	mLongParticlesStorageBuffer.Create(
	{
		.pcName = "LongParticles",
		.flags = {kStorage, kDeviceLocal},
		.dataVkDeviceSize = sizeof(shaders::ParticlesLayout),
	},
	[&](void* pData)
	{
		memset(pData, 0, sizeof(shaders::ParticlesLayout));
	});

	mSquareParticlesStorageBuffer.Create(
	{
		.pcName = "SquareParticles",
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
	gpBufferManager = nullptr;
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
	LOG("iTerrainQuad: {} x {}", iTerrainQuadX, iTerrainQuadY);

	int64_t iIndexCount = 6 * iTerrainQuadX * iTerrainQuadY;
	std::vector<uint32_t> indices(iIndexCount);
	std::vector<byte> vertices(2 * sizeof(float) * (iTerrainQuadX + 1) * (iTerrainQuadY + 1));
	CreateVisibleAreaMesh(iTerrainQuadX + 1, iTerrainQuadY + 1, indices, vertices);

	mTerrainMeshBuffer.Destroy();
	mTerrainMeshBuffer.Create(
	{
		.pcName = "TerrainMesh",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = iIndexCount,
		.vkIndexType = VK_INDEX_TYPE_UINT32,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(uint32_t) * indices.size() + vertices.size(),
	},
	[&](void* pData)
	{
		memcpy(pData, indices.data(), sizeof(uint32_t) * indices.size());
		memcpy(reinterpret_cast<char*>(pData) + sizeof(uint32_t) * indices.size(), vertices.data(), vertices.size());
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
		.pcName = "WaterMesh",
		.flags = {kIndexVertex, kDeviceLocal},
		.iCount = iIndexCount,
		.vkIndexType = VK_INDEX_TYPE_UINT32,
		.iVertexStride = sizeof(float) * 2,
		.dataVkDeviceSize = sizeof(uint32_t) * indices.size() + vertices.size(),
	},
	[&](void* pData)
	{
		memcpy(pData, indices.data(), sizeof(uint32_t) * indices.size());
		memcpy(reinterpret_cast<char*>(pData) + sizeof(uint32_t) * indices.size(), vertices.data(), vertices.size());
	});
}

} // namespace engine
