#include "GraphicsUtils.h"

#include "Graphics.h"
#include "Debug/EnumToString.h"
#include "Managers/DeviceManager.h"
#include "ThreadLocal.h"
#include "Memory/MemoryManager.h"

namespace engine
{

void CheckVkFailed(VkResult vkResult, std::string_view expression, std::source_location loc)
{
	const char* pcResult = gEnumToString.Convert(vkResult);
	Log("CheckVk failed: \"{}\" at {}:{} in {} - {}", expression, loc.file_name(), loc.line(), loc.function_name(), pcResult);

	// Format exception message with call site information
	thread_local char spcException[1024] {};
	snprintf(spcException, std::size(spcException) - 1, "CheckVk failed: \"%.*s\" at %s:%u in %s\nVkResult: %s", static_cast<int>(expression.size()), expression.data(), loc.file_name(), loc.line(), loc.function_name(), pcResult);

	if (vkResult == VK_ERROR_OUT_OF_DATE_KHR || vkResult == VK_SUBOPTIMAL_KHR)
	{
		gpGraphics->meDestroyType = DestroyType::kSwapchain;
		return;
	}

	if (vkResult == VK_ERROR_SURFACE_LOST_KHR)
	{
		gpGraphics->meDestroyType = DestroyType::kSurface;
		return;
	}

	if (vkResult == VK_ERROR_DEVICE_LOST)
	{
		throw DeviceLostException(spcException);
	}

	common::DebugBreak();
	throw std::runtime_error(spcException);
}

void VkNameImpl([[maybe_unused]] VkObjectType type, [[maybe_unused]] uint64_t handle, [[maybe_unused]] std::string_view name)
{
	if constexpr (kbEnableVulkanDebugLayers)
	{
		if (vkSetDebugUtilsObjectNameEXT != nullptr)
		{
			const char* pcFullName = gEnumToString.Convert(type);
			const char* pcPrefix = pcFullName + std::char_traits<char>::length("VK_OBJECT_TYPE_");
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			rWorkbuffer.Push();
			rWorkbuffer.Append(pcPrefix);
			rWorkbuffer.Append(" ");
			rWorkbuffer.Append(name);

			ScopedSuppressAllocationTracking suppressTracking;

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

} // namespace engine
