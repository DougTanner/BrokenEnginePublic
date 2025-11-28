#pragma once

#include "Graphics/Objects/Buffer.h"

namespace engine
{

class DeviceManager;
struct RenderFrame;

class BufferManager
{
public:

	BufferManager();
	~BufferManager();

	void CreateTerrainMesh();
	void CreateWaterMesh();

	Buffer* CreateDynamicBuffer(common::crc_t crc, const char* pcName, VkDeviceSize size);
	void ResizeDynamicBuffer(common::crc_t crc, const char* pcName, VkDeviceSize newSize, int64_t iFramebuffer);

	Buffer* CreateDynamicVisibleLightsBuffer(common::crc_t crc, const char* pcName, VkDeviceSize size);
	void ResizeDynamicVisibleLightsBuffer(common::crc_t crc, const char* pcName, VkDeviceSize newSize, int64_t iFramebuffer);

	std::unordered_map<common::crc_t, Buffer> mModelMap;

	std::vector<Buffer> mGlobalLayoutUniformBuffers;
	std::vector<Buffer> mMainLayoutUniformBuffers;

	std::vector<Buffer> mPointLightsStorageBuffers;

	std::vector<Buffer> mHexShieldsStorageBuffers;
	std::vector<Buffer> mBillboardsStorageBuffers;
	std::vector<Buffer> mTextStorageBuffers;
	std::vector<Buffer> mWidgetsStorageBuffers;
	std::vector<Buffer> mPlayerMissilesStorageBuffers;

	std::vector<Buffer> mSmokeSpreadStorageBuffers;
	std::vector<Buffer> mSmokePuffsStorageBuffers;
	std::vector<Buffer> mSmokeTrailsStorageBuffers;

	Buffer mQuadsVertexBuffer;
	Buffer mTerrainMeshBuffer;
	Buffer mWaterMeshBuffer;

	std::vector<Buffer> mLongParticlesSpawnStorageBuffers;
	Buffer mLongParticlesStorageBuffer;

	std::vector<Buffer> mSquareParticlesSpawnStorageBuffers;
	Buffer mSquareParticlesStorageBuffer;

#if defined(ENABLE_GLTF_TEST)
	std::vector<Buffer> mGltfsStorageBuffers;
#endif

	std::unordered_map<common::crc_t, std::vector<Buffer>> mDynamicStorageBuffers;
	std::unordered_map<common::crc_t, std::vector<Buffer>> mDynamicVisibleLightsStorageBuffers;
	std::optional<Buffer> mPreviousBuffer;
};

inline BufferManager* gpBufferManager = nullptr;

} // namespace engine
