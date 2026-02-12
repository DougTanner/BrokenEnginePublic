#pragma once

#include "Graphics/Objects/Buffer.h"

namespace engine
{

class DeviceManager;
struct RenderFrame;

enum DynamicBufferType
{
	kBufferMain,
	kBufferVisibleLights,
	kBufferWindDeposit,

	kBufferTypeCount,
};

class BufferManager
{
public:

	BufferManager();
	~BufferManager();

	void CreateTerrainMesh();
	void CreateWaterMesh();

	Buffer* CreateDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize elementSize);
	void ResizeDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize newSize, int64_t iFramebuffer);

	template<typename T>
	struct DynamicStorageBufferResult
	{
		T* pData;
		int64_t iCapacity;
	};

	template<typename T>
	DynamicStorageBufferResult<T> GetDynamicStorageBuffer(common::crc_t crc, DynamicBufferType eType, int64_t iCommandBuffer)
	{
		Buffer& rBuffer = mDynamicStorageBuffers[eType].at(crc).at(iCommandBuffer);
		ASSERT(sizeof(T) == rBuffer.mInfo.iElementSize);
		return {
			.pData = reinterpret_cast<T*>(rBuffer.mpMappedMemory),
			.iCapacity = static_cast<int64_t>(rBuffer.mInfo.dataVkDeviceSize / rBuffer.mInfo.iElementSize),
		};
	}

	std::unordered_map<common::crc_t, Buffer> mModelMap;

	std::vector<Buffer> mGlobalLayoutUniformBuffers;
	std::vector<Buffer> mMainLayoutUniformBuffers;

	std::vector<Buffer> mTextStorageBuffers;

	std::vector<Buffer> mSmokeSpreadStorageBuffers;
	std::vector<Buffer> mWindSpreadStorageBuffers;

	Buffer mQuadsVertexBuffer;
	Buffer mTerrainMeshBuffer;
	Buffer mWaterMeshBuffer;

	std::vector<Buffer> mLongParticlesSpawnStorageBuffers;
	Buffer mLongParticlesStorageBuffer;

	std::vector<Buffer> mSquareParticlesSpawnStorageBuffers;
	Buffer mSquareParticlesStorageBuffer;

	std::vector<Buffer> mMeshDataStorageBuffers;
	std::vector<Buffer> mJointMatrixStorageBuffers;

	int64_t AllocateMeshData(int64_t iCommandBuffer, int64_t iCount);
	int64_t AllocateJointMatrices(int64_t iCommandBuffer, int64_t iCount);
	void ResetSkinningAllocations(int64_t iCommandBuffer);

	std::array<std::unordered_map<common::crc_t, std::vector<Buffer>>, kBufferTypeCount> mDynamicStorageBuffers;
	std::optional<Buffer> mPreviousBuffer;

private:

	void GrowMeshDataBuffer(int64_t iCommandBuffer);
	void GrowJointMatrixBuffer(int64_t iCommandBuffer);

	int64_t miMeshDataOffset[4] {};
	int64_t miJointMatrixOffset[4] {};
	int64_t miMeshDataCapacity[4] {};
	int64_t miJointMatrixCapacity[4] {};
	std::optional<Buffer> mPreviousMeshDataBuffer[4];
	std::optional<Buffer> mPreviousJointMatrixBuffer[4];
};

inline BufferManager* gpBufferManager = nullptr;

} // namespace engine
