#pragma once

#if defined(BT_CLIENT)

namespace engine
{

// Max swapchain framebuffer count. Sizes the per-command-buffer skinning arrays below and Islands'
// triple-buffered SSBO/indirect arrays; guarded by ASSERTs where the live framebuffer count is read.
inline constexpr int64_t kiMaxFramebuffers = 4;

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

	std::vector<Buffer> mUiRectStorageBuffers;

	// Smoke hierarchical dispatch buffers (per-texture occupancy, shared active tile list)
	VkBuffer mSmokeOccupancyVkBuffers[2] {};
	VmaAllocation mSmokeOccupancyVmaAllocations[2] {};
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

	Buffer mQuadsVertexBuffer;

	// Visible-area mesh LOD: each LOD divides total quad count by 4 (each dim by 2). LOD k starts at
	// eyeDistance >= kfMinEyeHeight * 4^k. All LODs concatenated into mWaterMeshBuffer so the pipeline
	// binds once and switches LODs by writing per-frame indirect-draw params (firstIndex, indexCount,
	// vertexOffset) — no command-buffer re-record on LOD change. (Water only — terrain draws
	// per-island Gaea2 Mesher-baked meshes on each IslandTemplate in CommandBufferRecordMain instead.
	// miVisibleAreaLod still drives camera-snap math via CameraBase.)
	static constexpr int64_t kiVisibleAreaLodCount = 4;
	struct VisibleAreaMeshLod
	{
		int64_t iIndexOffset;   // First index for this LOD inside the concat index region
		int64_t iIndexCount;
		int64_t iVertexOffset;  // Vertex base added by vkCmdDrawIndexedIndirect's vertexOffset
		int64_t iQuadCountX;    // For visible-area snap math
		int64_t iQuadCountY;
	};
	VisibleAreaMeshLod mWaterMeshLods[kiVisibleAreaLodCount] {};
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

	void CreateDebugMeshBuffers();

	void GrowMeshDataBuffer(int64_t iCommandBuffer, int64_t iValidCount);
	void GrowJointMatrixBuffer(int64_t iCommandBuffer, int64_t iValidCount);

	int64_t miMeshDataOffset[kiMaxFramebuffers] {};
	int64_t miJointMatrixOffset[kiMaxFramebuffers] {};
	int64_t miMeshDataCapacity[kiMaxFramebuffers] {};
	int64_t miJointMatrixCapacity[kiMaxFramebuffers] {};
	// One-frame buffer parking for the two skinning bump allocators; a same-frame second grow may overwrite a
	//   slot -- safe by the three-point chain documented in GrowMeshDataBuffer. Drained by ResetSkinningAllocations.
	std::optional<Buffer> mPreviousMeshDataBuffer[kiMaxFramebuffers];
	std::optional<Buffer> mPreviousJointMatrixBuffer[kiMaxFramebuffers];
};

inline BufferManager* gpBufferManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
