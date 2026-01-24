#include "GraphicsUtils.h"

#include "Graphics.h"

namespace engine
{

void CheckVkFailed(VkResult vkResult, std::source_location loc)
{
	const char* pcResult = gEnumToString.Convert(vkResult);
	Log("CheckVk failed at {}:{} in {} - {}", loc.file_name(), loc.line(), loc.function_name(), pcResult);

	thread_local char spcException[1024] {};
	snprintf(spcException, std::size(spcException) - 1, "CheckVk failed at %s:%u in %s\nVkResult: %s", loc.file_name(), loc.line(), loc.function_name(), pcResult);

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

void VkNameImpl([[maybe_unused]] VkObjectType type, [[maybe_unused]] uint64_t handle, [[maybe_unused]] const char* name)
{
	if constexpr (kbEnableVulkanDebugLayers)
	{
		if (vkSetDebugUtilsObjectNameEXT != nullptr)
		{
			const char* pcFullName = gEnumToString.Convert(type);
			const char* pcPrefix = pcFullName + std::char_traits<char>::length("VK_OBJECT_TYPE_");
			std::string prefixedName = std::format("{} {}", pcPrefix, name);
			auto [it, inserted] = gpGraphics->mDebugNames.insert(std::move(prefixedName));
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
