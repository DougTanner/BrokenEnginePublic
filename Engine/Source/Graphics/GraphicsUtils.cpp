#if defined(BT_CLIENT)

#include "GraphicsUtils.h"

#include "Memory/MemoryManager.h"
#include "Ui/WrapperBase.h"

#include "Game.h"

namespace engine
{

void CheckVkFailed(VkResult vkResult, std::string_view expression, std::source_location loc)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	auto pcResult = gEnumToString.Convert(vkResult, rWorkbuffer);
	Log(kLogError, "CheckVk failed: {} - \"{}\" at {}:{} in {}", pcResult, expression, loc.file_name(), loc.line(), loc.function_name());

	// Format exception message with call site information
	char* pcException = rWorkbuffer.PushBuffer<char*>(1024);
	snprintf(pcException, 1023, "CheckVk failed: \"%.*s\" at %s:%u in %s\nVkResult: %s", static_cast<int>(expression.size()), expression.data(), loc.file_name(), loc.line(), loc.function_name(), static_cast<const char*>(pcResult));

	if (vkResult == VK_ERROR_OUT_OF_DATE_KHR || vkResult == VK_SUBOPTIMAL_KHR)
	{
		gpGraphics->meDestroyType = DestroyType::kSwapchain;
		rWorkbuffer.Pop();
		return;
	}

	if (vkResult == VK_ERROR_SURFACE_LOST_KHR)
	{
		gpGraphics->meDestroyType = DestroyType::kSurface;
		rWorkbuffer.Pop();
		return;
	}

	// Pop exception buffer before throw — data survives in workbuffer memory until next write
	rWorkbuffer.Pop();

	if (vkResult == VK_ERROR_DEVICE_LOST)
	{
		throw DeviceLostException(pcException);
	}

	DEBUG_BREAK();
	throw std::runtime_error(pcException);
}

void VkNameImpl([[maybe_unused]] VkObjectType type, [[maybe_unused]] uint64_t handle, [[maybe_unused]] std::string_view name)
{
	if constexpr (kbEnableVulkanDebugLayers)
	{
		if (vkSetDebugUtilsObjectNameEXT != nullptr)
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			auto pcFullName = gEnumToString.Convert(type, rWorkbuffer);
			const char* pcPrefix = static_cast<const char*>(pcFullName) + std::char_traits<char>::length("VK_OBJECT_TYPE_");
			rWorkbuffer.Push();
			rWorkbuffer.Append(pcPrefix);
			rWorkbuffer.Append(" ");
			rWorkbuffer.Append(name);

			// Heap: emplace copies workbuffer string into a std::string in mDebugNames (unordered_set). Vulkan retains
			// the c_str() pointer, so the string must outlive the object. Can't use workbuffer (gone after Pop)
			ScopedSuppressAllocationTracking suppressAllocationTracking;

			auto [it, bInserted] = gpGraphics->mDebugNames.emplace(rWorkbuffer.View());
			rWorkbuffer.Pop();
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
	rLayout.uiColor = uiColor;
}

} // namespace engine

#endif // BT_CLIENT
