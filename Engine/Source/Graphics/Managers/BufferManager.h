#pragma once

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

	void DestroySwapchainDependentBuffers();
	void CreateSwapchainDependentBuffers();

	void CreateTerrainMesh();
	void CreateWaterMesh();

	Buffer* CreateDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize elementSize);
	void ResizeDynamicBuffer(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize newSize, int64_t iFramebuffer);
	Buffer* ResizeDynamicBufferIfNeeded(common::crc_t crc, DynamicBufferType eType, std::string_view name, VkDeviceSize layoutSize, int64_t iCapacity, int64_t iCommandBuffer);

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
		// CreateDynamicBuffer() sets iElementSize = its size parameter, so passing N * sizeof(T) breaks this assert.
		// Fix: pass sizeof(T) to CreateDynamicBuffer(), then ResizeDynamicBuffer() to grow (preserves iElementSize).
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

	// Smoke hierarchical dispatch buffers (device-local, single instance)
	VkBuffer mSmokeOccupancyVkBuffer = VK_NULL_HANDLE;
	VmaAllocation mSmokeOccupancyVmaAllocation = VK_NULL_HANDLE;
	VkDeviceSize mSmokeOccupancyBufferSize = 0;
	VkBuffer mSmokeActiveTileVkBuffer = VK_NULL_HANDLE;
	VmaAllocation mSmokeActiveTileVmaAllocation = VK_NULL_HANDLE;
	VkDeviceSize mSmokeActiveTileBufferSize = 0;
	void CreateSmokeHierarchicalBuffers();
	void DestroySmokeHierarchicalBuffers();

	// Wind hierarchical dispatch buffers (two pairs: A for TextureOne, B for TextureTwo)
	VkBuffer mWindOccupancyVkBuffers[2] = {};
	VmaAllocation mWindOccupancyVmaAllocations[2] = {};
	VkDeviceSize mWindOccupancyBufferSize = 0;
	VkBuffer mWindActiveTileVkBuffers[2] = {};
	VmaAllocation mWindActiveTileVmaAllocations[2] = {};
	VkDeviceSize mWindActiveTileBufferSize = 0;
	void CreateWindHierarchicalBuffers();
	void DestroyWindHierarchicalBuffers();

	// Lighting occupancy buffer
	static constexpr int64_t kiMaxCascadeLevels = 8;
	VkBuffer mLightOccupancyVkBuffers[kiMaxCascadeLevels] {};
	VmaAllocation mLightOccupancyVmaAllocations[kiMaxCascadeLevels] {};
	VkDeviceSize mLightOccupancyBufferSizes[kiMaxCascadeLevels] {};
	void CreateLightingSpreadBuffers();
	void DestroyLightingSpreadBuffers();

	Buffer mQuadsVertexBuffer;
	Buffer mTerrainMeshBuffer;
	Buffer mWaterMeshBuffer;

	Buffer mDebugBoxVertexBuffer;
	Buffer mDebugSphereVertexBuffer;
	Buffer mDebugCircleVertexBuffer;
	Buffer mDebugLineVertexBuffer;

	std::vector<Buffer> mLongParticlesSpawnStorageBuffers;
	Buffer mLongParticlesStorageBuffer;

	std::vector<Buffer> mSquareParticlesSpawnStorageBuffers;
	Buffer mSquareParticlesStorageBuffer;

	std::vector<Buffer> mMeshDataStorageBuffers;
	std::vector<Buffer> mJointMatrixStorageBuffers;

	int64_t AllocateMeshData(int64_t iCommandBuffer, int64_t iCount);
	int64_t AllocateJointMatrices(int64_t iCommandBuffer, int64_t iCount);
	void ResetSkinningAllocations(int64_t iCommandBuffer);

	std::unordered_map<common::crc_t, std::vector<Buffer>> mDynamicStorageBuffers[kBufferTypeCount];
	std::optional<Buffer> mPreviousBuffer;

	void InitializePerCommandBufferBuffers(int64_t iCommandBufferCount);

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
