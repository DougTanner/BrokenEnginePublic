#pragma once

#if defined(BT_CLIENT)

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

void CheckVkFailed(VkResult vkResult, std::string_view expression, std::source_location loc);

void VkNameImpl(VkObjectType type, uint64_t handle, std::string_view name);

inline void VkName([[maybe_unused]] VkObjectType type, [[maybe_unused]] auto handle, [[maybe_unused]] std::string_view name)
{
	if constexpr (kbVulkanDebugLayers)
	{
		VkNameImpl(type, reinterpret_cast<uint64_t>(handle), name);
	}
}

inline void CheckVk(VkResult vkResult, std::string_view expression, std::source_location loc = std::source_location::current())
{
	if (vkResult != VK_SUCCESS) [[unlikely]]
	{
		CheckVkFailed(vkResult, expression, loc);
	}
}

inline constexpr uint32_t TileCount(uint32_t uiCount)
{
	return (uiCount + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
}

// Shared rendering helpers for collections
bool IsPointVisible(XMVECTOR vecPosition, XMFLOAT4A& rOutPosition);
XMVECTOR ProjectToBaseHeight(XMVECTOR vecPosition);
void BuildAxisAlignedQuad(shaders::AxisAlignedQuadLayout& rLayout, const XMFLOAT4A& f4Position, float fArea, const XMFLOAT4A& f4Params, uint32_t uiColor);
float MinLightingDepositSize();

bool SupportsStorageImage(VkFormat vkFormat);
bool SupportsColorAttachmentBlend(VkFormat vkFormat);

} // namespace engine

#define CHECK_VK(a) do { VkResult vkResultMacro = a; if (vkResultMacro != VK_SUCCESS) [[unlikely]] { CheckVk(vkResultMacro, #a); } _Analysis_assume_(vkResultMacro == VK_SUCCESS); } while (false)

#endif // defined(BT_CLIENT)
