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

	Buffer* CreateDynamicBuffer(common::crc_t crc, std::string_view name, VkDeviceSize elementSize);
	void ResizeDynamicBuffer(common::crc_t crc, std::string_view name, VkDeviceSize newSize, int64_t iFramebuffer);

	template<typename T>
	T* GetDynamicStorageBuffer(common::crc_t crc, int64_t iCommandBuffer)
	{
		Buffer& rBuffer = mDynamicStorageBuffers.at(crc).at(iCommandBuffer);
		ASSERT(sizeof(T) == rBuffer.mInfo.iElementSize);
		return reinterpret_cast<T*>(rBuffer.mpMappedMemory);
	}

	std::unordered_map<common::crc_t, Buffer> mModelMap;

	std::vector<Buffer> mGlobalLayoutUniformBuffers;
	std::vector<Buffer> mMainLayoutUniformBuffers;

	std::vector<Buffer> mTextStorageBuffers;

	std::vector<Buffer> mSmokeSpreadStorageBuffers;

	Buffer mQuadsVertexBuffer;
	Buffer mTerrainMeshBuffer;
	Buffer mWaterMeshBuffer;

	std::vector<Buffer> mLongParticlesSpawnStorageBuffers;
	Buffer mLongParticlesStorageBuffer;

	std::vector<Buffer> mSquareParticlesSpawnStorageBuffers;
	Buffer mSquareParticlesStorageBuffer;

	std::vector<Buffer> mMeshDataStorageBuffers;
	std::vector<Buffer> mJointMatrixStorageBuffers;

	std::unordered_map<common::crc_t, std::vector<Buffer>> mDynamicStorageBuffers;
	std::optional<Buffer> mPreviousBuffer;
};

inline BufferManager* gpBufferManager = nullptr;

} // namespace engine
