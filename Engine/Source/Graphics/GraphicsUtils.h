#pragma once

namespace engine
{

class DeviceLostException : public std::exception
{
public:

	DeviceLostException(const char* pcWhat)
	: std::exception(pcWhat)
	{
	}
};

void CheckVkFailed(VkResult vkResult, std::source_location loc);

void VkNameImpl(VkObjectType type, uint64_t handle, std::string_view name);

inline void VkName([[maybe_unused]] VkObjectType type, [[maybe_unused]] auto handle, [[maybe_unused]] std::string_view name)
{
	if constexpr (kbEnableVulkanDebugLayers)
	{
		VkNameImpl(type, reinterpret_cast<uint64_t>(handle), name);
	}
}

inline void CheckVk(VkResult vkResult, std::source_location loc = std::source_location::current())
{
	if (vkResult != VK_SUCCESS) [[unlikely]]
	{
		CheckVkFailed(vkResult, loc);
	}
}

} // namespace engine

using engine::CheckVk;
