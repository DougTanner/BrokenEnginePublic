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

	Buffer* CreateDynamicBuffer(common::crc_t crc, const char* name, VkDeviceSize size);
	void ResizeDynamicBuffer(common::crc_t crc, const char* name, VkDeviceSize newSize, int64_t iFramebuffer);

	std::unordered_map<common::crc_t, Buffer> mModelMap;

	std::vector<Buffer> mGlobalLayoutUniformBuffers;
	std::vector<Buffer> mMainLayoutUniformBuffers;

	std::vector<Buffer> mTextStorageBuffers;
	std::vector<Buffer> mWidgetsStorageBuffers;

	std::vector<Buffer> mSmokeSpreadStorageBuffers;

	Buffer mQuadsVertexBuffer;
	Buffer mTerrainMeshBuffer;
	Buffer mWaterMeshBuffer;

	std::vector<Buffer> mLongParticlesSpawnStorageBuffers;
	Buffer mLongParticlesStorageBuffer;

	std::vector<Buffer> mSquareParticlesSpawnStorageBuffers;
	Buffer mSquareParticlesStorageBuffer;

	std::unordered_map<common::crc_t, std::vector<Buffer>> mDynamicStorageBuffers;
	std::optional<Buffer> mPreviousBuffer;
};

inline BufferManager* gpBufferManager = nullptr;

} // namespace engine
