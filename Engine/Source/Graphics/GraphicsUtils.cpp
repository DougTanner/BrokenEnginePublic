#if defined(BT_CLIENT)

#include "GraphicsUtils.h"

#include "Ui/LightingWrappersBase.h"
#include "Ui/WrapperBase.h"

#include "Graphics/Camera.h"

namespace engine
{

void CheckVkFailed(VkResult vkResult, std::string_view expression, std::source_location loc)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	const char* pcResult = string_VkResult(vkResult);
	LOG(kDefault, kError, "CheckVk failed: {} - \"{}\" at {}:{} in {}", pcResult, expression, loc.file_name(), loc.line(), loc.function_name());

	// Format exception message with call site information
	auto pcException = rWorkbuffer.PushBuffer<char*>(1024);
	std::snprintf(pcException, 1023, "CheckVk failed: \"%.*s\" at %s:%u in %s\nVkResult: %s", static_cast<int>(expression.size()), expression.data(), loc.file_name(), loc.line(), loc.function_name(), pcResult);

	if (vkResult == VK_ERROR_OUT_OF_DATE_KHR || vkResult == VK_SUBOPTIMAL_KHR)
	{
		// std::max, not plain assign: a same-frame kSurface escalation must never downgrade to kSwapchain.
		gpGraphics->meDestroyType = std::max(DestroyType::kSwapchain, gpGraphics->meDestroyType);
		return;
	}

	if (vkResult == VK_ERROR_SURFACE_LOST_KHR)
	{
		gpGraphics->meDestroyType = DestroyType::kSurface;
		return;
	}

	if (vkResult == VK_ERROR_DEVICE_LOST)
	{
		throw DeviceLostException(pcException);
	}

	DEBUG_BREAK();
	throw std::runtime_error(pcException);
}

void VkNameImpl([[maybe_unused]] VkObjectType type, [[maybe_unused]] uint64_t handle, [[maybe_unused]] std::string_view name)
{
	if constexpr (kbVulkanDebugLayers)
	{
		if (vkSetDebugUtilsObjectNameEXT != nullptr)
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			// string_VkObjectType returns "Unhandled VkObjectType" for unrecognized types (no "VK_OBJECT_TYPE_"
			// prefix); strip the prefix only when present, else the fixed skip mis-truncates the fallback into a garbage tail.
			const char* pcTypeName = string_VkObjectType(type);
			size_t iPrefixLength = std::char_traits<char>::length("VK_OBJECT_TYPE_");
			const char* pcPrefix = std::char_traits<char>::compare(pcTypeName, "VK_OBJECT_TYPE_", iPrefixLength) == 0 ? pcTypeName + iPrefixLength : pcTypeName;
			common::ScopedWorkbufferArena innerArena = rWorkbuffer.Push();
			rWorkbuffer.Append(pcPrefix);
			rWorkbuffer.Append(" ");
			rWorkbuffer.Append(name);

			// Heap: emplace copies workbuffer string into a std::string in mDebugNames (unordered_set). Vulkan retains
			// the c_str() pointer, so the string must outlive the object. Can't use workbuffer (gone after Pop)
			ScopedSuppressAllocationTracking suppress;

			auto [it, bInserted] = gpGraphics->mDebugNames.emplace(rWorkbuffer.View());
			VkDebugUtilsObjectNameInfoEXT vkDebugUtilsObjectNameInfoEXT =
			{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
				.pNext = nullptr,
				.objectType = type,
				.objectHandle = handle,
				.pObjectName = it->c_str(),
			};
			vkSetDebugUtilsObjectNameEXT(gpDeviceManager->mVkDevice, &vkDebugUtilsObjectNameInfoEXT);
		}
	}
}

bool IsPointVisible(XMVECTOR vecPosition, XMFLOAT4A& rOutPosition)
{
	XMStoreFloat4A(&rOutPosition, vecPosition);
	return rOutPosition.x >= game::gpCamera->f4RenderVisibleArea.x && rOutPosition.x <= game::gpCamera->f4RenderVisibleArea.z && rOutPosition.y <= game::gpCamera->f4RenderVisibleArea.y && rOutPosition.y >= game::gpCamera->f4RenderVisibleArea.w;
}

XMVECTOR ProjectToBaseHeight(XMVECTOR vecPosition)
{
	float fElevation = gpIslandTerrain->GlobalElevation(vecPosition);
	return common::ToBaseHeight(vecPosition, game::gpCamera->mVecEyePosition, std::max(fElevation, gBaseHeight.Get()));
}

void BuildAxisAlignedQuad(shaders::AxisAlignedQuadLayout& rLayout, const XMFLOAT4A& f4Position, float fArea, const XMFLOAT4A& f4Params, uint32_t uiColor)
{
	rLayout.f4VertexRect = {f4Position.x - fArea, f4Position.y + fArea, 2.0f * fArea, -2.0f * fArea};
	rLayout.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rLayout.f4Params = f4Params;
	rLayout.fRotation = 0.0f; // non-island consumers render axis-aligned
	rLayout.uiTextureSlot = 0; // non-island consumers don't sample the island texture array
	rLayout.uiColor = uiColor;
}

float MinLightingDepositSize()
{
	// Minimum lighting size: clamp to 8 texels to prevent flickering from sub-texel lights. The deposit
	// texel world size is constant-density (visible width / base detail resolution) at any settled height,
	// independent of the lighting-headroom pre-size (the headroom cancels in the f4LightingArea texel
	// formula), so the un-bumped DetailTextureSize is the right basis here — NOT LightingDetailTextureSize,
	// which would shrink the floor by the headroom factor. The ceil only inflates the floor sub-texel.
	auto [iLightingTextureX, iLightingTextureY] = TextureManager::DetailTextureSize(gLightingDepositTextureMultiplier.Get());
	float fTexelSizeX = std::ceil(game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / static_cast<float>(iLightingTextureX);
	float fTexelSizeY = std::ceil(game::gpCamera->f4RenderVisibleArea.y - game::gpCamera->f4RenderVisibleArea.w) / static_cast<float>(iLightingTextureY);
	return std::max(fTexelSizeX, fTexelSizeY) * 8.0f;
}

bool SupportsStorageImage(VkFormat vkFormat)
{
	VkFormatProperties vkFormatProperties {};
	vkGetPhysicalDeviceFormatProperties(gpInstanceManager->mVkPhysicalDevice, vkFormat, &vkFormatProperties);
	return (vkFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
}

bool SupportsColorAttachmentBlend(VkFormat vkFormat)
{
	VkFormatProperties vkFormatProperties {};
	vkGetPhysicalDeviceFormatProperties(gpInstanceManager->mVkPhysicalDevice, vkFormat, &vkFormatProperties);
	return (vkFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
}

} // namespace engine

#endif // BT_CLIENT
