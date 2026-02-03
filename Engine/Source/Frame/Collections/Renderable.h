#pragma once

using VkDeviceSize = uint64_t;

namespace engine
{

class Buffer;
class BufferManager;
class GltfPipeline;
class PipelineManager;

extern BufferManager* gpBufferManager;
extern PipelineManager* gpPipelineManager;

// Layout sizes for Renderable mixin (must match shaders::QuadLayout, shaders::GltfLayout, shaders::VisibleLightQuadLayout, shaders::AxisAlignedQuadLayout, shaders::BillboardLayout, shaders::HexShieldLayout)
inline constexpr VkDeviceSize kQuadLayoutSize = 160;
inline constexpr VkDeviceSize kGltfLayoutSize = 144;
inline constexpr VkDeviceSize kVisibleLightQuadLayoutSize = 176;
inline constexpr VkDeviceSize kAxisAlignedQuadLayoutSize = 64;
inline constexpr VkDeviceSize kBillboardLayoutSize = 32;
inline constexpr VkDeviceSize kHexShieldLayoutSize = 544;

// CRC flag for visible lights buffers (uses bit 63 to distinguish from main buffers)
inline constexpr common::crc_t kVisibleLightsCrcFlag = 0x8000'0000'0000'0000ULL;

enum class RenderableFlags : uint32_t
{
	kGltf                 = 0x0001,   // glTF mode (implies GltfLayout)
	kGltfShadow           = 0x0002,   // glTF mode with shadow pipeline (implies GltfLayout)
	kLighting             = 0x0004,   // Lighting mode (implies QuadLayout)
	kVisibleLights        = 0x0008,   // Lighting mode: create visible lights pipeline
	kAxisAlignedLighting  = 0x0010,   // Axis-aligned lighting mode (implies AxisAlignedQuadLayout)
	kBillboards           = 0x0020,   // Billboard mode (implies BillboardLayout)
	kSmokeAxisAligned     = 0x0040,   // Smoke emit pass with axis-aligned quads (implies AxisAlignedQuadLayout)
	kSmoke                = 0x0080,   // Smoke emit pass with generic quads (implies QuadLayout)
	kHexShields           = 0x0100,   // HexShields mode (uses DualGeodesicIcosahedron mesh, implies HexShieldLayout)
	kHexShieldsLighting   = 0x0200,   // HexShields lighting pass (combines with kHexShields)
};
using RenderableFlags_t = common::Flags<RenderableFlags>;

// ============================================================================
// RENDERABLE MIXIN
// ============================================================================
// Provides dynamic buffer management for collections that render to GPU via pipelines.
// Supports both glTF pipelines (default) and lighting pipelines (via kLighting flag).
// Template parameters provide explicit configuration instead of requiring derived class constants.
// NAME is passed directly as a template parameter using C++20 NTTP (non-type template parameters).
// FLAGS controls mode and features: kGltf/kGltfShadow (glTF), kLighting + kVisibleLights (lighting).

template <typename T, common::FixedString NAME, common::Flags<RenderableFlags> FLAGS, common::crc_t GLTF_CRC = 0>
struct Renderable
{
	static constexpr const char* kName = NAME.data;
	static constexpr common::crc_t kCrc = common::CrcConsteval(NAME.data);
	static constexpr VkDeviceSize kLayoutSize =
		(FLAGS & RenderableFlags::kHexShields) ? kHexShieldLayoutSize :
		(FLAGS & RenderableFlags::kBillboards) ? kBillboardLayoutSize :
		(FLAGS & RenderableFlags::kSmokeAxisAligned) ? kAxisAlignedQuadLayoutSize :
		(FLAGS & RenderableFlags::kAxisAlignedLighting) ? kAxisAlignedQuadLayoutSize :
		(FLAGS & RenderableFlags::kSmoke) ? kQuadLayoutSize :
		(FLAGS & RenderableFlags::kLighting) ? kQuadLayoutSize : kGltfLayoutSize;
	static constexpr common::crc_t kGltfCrc = GLTF_CRC;
	static constexpr common::Flags<RenderableFlags> kFlags = FLAGS;

	// Creates dynamic storage buffer with minimal initial size.
	// Called from derived class GraphicsResources().
	// Returns pointer to buffer array for pipeline creation.
	static inline Buffer* AllocateDynamicBuffer()
	{
		return gpBufferManager->CreateDynamicBuffer(kCrc, kName, kLayoutSize);
	}

	// Creates dynamic storage buffer and pipelines based on mode.
	// glTF mode: Creates glTF pipeline + optional shadow pipeline.
	// Lighting mode: Creates lighting pipeline + optional visible lights pipeline.
	// Axis-aligned lighting mode: Creates axis-aligned lighting pipeline.
	// Called from derived class GraphicsResources().
	static inline void AllocatePipelines()
	{
		Buffer* pStorageBuffers = AllocateDynamicBuffer();
		if constexpr (kFlags & RenderableFlags::kHexShields)
		{
			gpPipelineManager->CreateDynamicPipelineHexShields(kCrc, kName, kLayoutSize);
			if constexpr (kFlags & RenderableFlags::kHexShieldsLighting)
			{
				gpPipelineManager->CreateDynamicPipelineHexShieldsLighting(kCrc, kName);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kSmokeAxisAligned)
		{
			gpPipelineManager->CreateDynamicPipelineSmokeAxisAligned(kCrc, kName, kLayoutSize);
		}
		else if constexpr (kFlags & RenderableFlags::kSmoke)
		{
			gpPipelineManager->CreateDynamicPipelineSmoke(kCrc, kName, kLayoutSize);
		}
		else if constexpr (kFlags & RenderableFlags::kAxisAlignedLighting)
		{
			gpPipelineManager->CreateDynamicPipelineAxisAlignedLighting(kCrc, kName, kLayoutSize);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				Buffer* pVisibleLightsBuffers = gpBufferManager->CreateDynamicBuffer(kCrc | kVisibleLightsCrcFlag, kName, kVisibleLightQuadLayoutSize);
				gpPipelineManager->CreateDynamicPipelineVisibleLights(kCrc, kName, pVisibleLightsBuffers);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kLighting)
		{
			gpPipelineManager->CreateDynamicPipelineLighting(kCrc, kName, kLayoutSize);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				Buffer* pVisibleLightsBuffers = gpBufferManager->CreateDynamicBuffer(kCrc | kVisibleLightsCrcFlag, kName, kVisibleLightQuadLayoutSize);
				gpPipelineManager->CreateDynamicPipelineVisibleLights(kCrc, kName, pVisibleLightsBuffers);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kGltf)
		{
			gpPipelineManager->CreateDynamicGltfPipeline(kCrc, kName, kGltfCrc, pStorageBuffers);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				gpPipelineManager->CreateDynamicGltfPipelineShadow(kCrc, kName, kGltfCrc, pStorageBuffers);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kBillboards)
		{
			gpPipelineManager->CreateDynamicPipelineBillboards(kCrc, kName, kLayoutSize);
		}
	}

	// Runtime CRC overload for glTF pipelines.
	// Use when specifying CRC in .cpp to avoid header dependency on Data/Gltf.h.
	static inline void AllocatePipelines(common::crc_t gltfCrc)
	{
		Buffer* pStorageBuffers = AllocateDynamicBuffer();
		if constexpr (kFlags & RenderableFlags::kGltf)
		{
			gpPipelineManager->CreateDynamicGltfPipeline(kCrc, kName, gltfCrc, pStorageBuffers);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				gpPipelineManager->CreateDynamicGltfPipelineShadow(kCrc, kName, gltfCrc, pStorageBuffers);
			}
		}
	}

	// Backward-compatible alias for glTF mode.
	// Called from derived class GraphicsResources().
	static inline void AllocateGltfPipelines()
	{
		static_assert(!(kFlags & RenderableFlags::kLighting), "Use AllocatePipelines() for lighting mode");
		AllocatePipelines();
	}

	// Checks if buffer resize needed and updates descriptor sets.
	// Uses VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT so no command buffer re-recording needed.
	// Called from derived class Render() method.
	static inline void ResizeBufferUpdateDescriptor(const T& rCollection, int64_t iCommandBuffer)
	{
		VkDeviceSize requiredSize = kLayoutSize * rCollection.iCapacity;
		Buffer& rBuffer = gpBufferManager->mDynamicStorageBuffers.at(kCrc).at(iCommandBuffer);
		if (rBuffer.mInfo.dataVkDeviceSize >= requiredSize)
		{
			return;
		}

		gpBufferManager->ResizeDynamicBuffer(kCrc, kName, requiredSize, iCommandBuffer);

		int64_t iFramebuffer = iCommandBuffer;

		if constexpr (kFlags & RenderableFlags::kHexShields)
		{
			// HexShields pipeline has storage buffer at binding 2
			gpPipelineManager->mDynamicPipelinesHexShieldsMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kHexShieldsLighting)
			{
				gpPipelineManager->mDynamicPipelinesHexShieldsLightingMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rBuffer);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kSmokeAxisAligned)
		{
			// Smoke axis-aligned pipeline has storage buffer at binding 1
			gpPipelineManager->mDynamicPipelinesSmokeAxisAlignedMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, &rBuffer);
		}
		else if constexpr (kFlags & RenderableFlags::kSmoke)
		{
			// Smoke pipeline has storage buffer at binding 1
			gpPipelineManager->mDynamicPipelinesSmokeMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, &rBuffer);
		}
		else if constexpr (kFlags & RenderableFlags::kAxisAlignedLighting)
		{
			// Axis-aligned lighting pipeline has storage buffer at binding 1 (binding 2 is sampler)
			gpPipelineManager->mDynamicPipelinesAxisAlignedLightingMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				// Visible lights pipeline has storage buffer at binding 2
				VkDeviceSize visibleLightsRequiredSize = kVisibleLightQuadLayoutSize * rCollection.iCapacity;
				Buffer& rVisibleLightsBuffer = gpBufferManager->mDynamicStorageBuffers.at(kCrc | kVisibleLightsCrcFlag).at(iCommandBuffer);
				if (rVisibleLightsBuffer.mInfo.dataVkDeviceSize < visibleLightsRequiredSize)
				{
					gpBufferManager->ResizeDynamicBuffer(kCrc | kVisibleLightsCrcFlag, kName, visibleLightsRequiredSize, iCommandBuffer);
					gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rVisibleLightsBuffer);
				}
			}
		}
		else if constexpr (kFlags & RenderableFlags::kLighting)
		{
			// Lighting pipeline has storage buffer at binding 1 (binding 2 is sampler)
			gpPipelineManager->mDynamicPipelinesLightingMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 1, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				// Visible lights pipeline has storage buffer at binding 2
				VkDeviceSize visibleLightsRequiredSize = kVisibleLightQuadLayoutSize * rCollection.iCapacity;
				Buffer& rVisibleLightsBuffer = gpBufferManager->mDynamicStorageBuffers.at(kCrc | kVisibleLightsCrcFlag).at(iCommandBuffer);
				if (rVisibleLightsBuffer.mInfo.dataVkDeviceSize < visibleLightsRequiredSize)
				{
					gpBufferManager->ResizeDynamicBuffer(kCrc | kVisibleLightsCrcFlag, kName, visibleLightsRequiredSize, iCommandBuffer);
					gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rVisibleLightsBuffer);
				}
			}
		}
		else if constexpr (kFlags & RenderableFlags::kGltf)
		{
			gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, &rBuffer);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kBillboards)
		{
			// Billboard pipeline has storage buffer at binding 2
			gpPipelineManager->mDynamicPipelinesBillboardsMap.at(kCrc)->UpdateStorageBufferDescriptor(iFramebuffer, 2, &rBuffer);
		}
	}

	// Writes indirect buffer counts to all pipelines.
	// Called from derived class Render() method.
	static inline void WritePipelineIndirectBuffers(int64_t iCommandBuffer, int64_t iCount)
	{
		if constexpr (kFlags & RenderableFlags::kHexShields)
		{
			gpPipelineManager->mDynamicPipelinesHexShieldsMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kHexShieldsLighting)
			{
				gpPipelineManager->mDynamicPipelinesHexShieldsLightingMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kSmokeAxisAligned)
		{
			gpPipelineManager->mDynamicPipelinesSmokeAxisAlignedMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
		}
		else if constexpr (kFlags & RenderableFlags::kSmoke)
		{
			gpPipelineManager->mDynamicPipelinesSmokeMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
		}
		else if constexpr (kFlags & RenderableFlags::kAxisAlignedLighting)
		{
			gpPipelineManager->mDynamicPipelinesAxisAlignedLightingMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kLighting)
		{
			gpPipelineManager->mDynamicPipelinesLightingMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kVisibleLights)
			{
				gpPipelineManager->mDynamicPipelinesVisibleLightsMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kGltf)
		{
			gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			if constexpr (kFlags & RenderableFlags::kGltfShadow)
			{
				gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
			}
		}
		else if constexpr (kFlags & RenderableFlags::kBillboards)
		{
			gpPipelineManager->mDynamicPipelinesBillboardsMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iCount);
		}
	}
};

} // namespace engine
