#pragma once

#include "Graphics/Managers/PipelineManager.h"

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
	if constexpr (kbEnableVulkanDebugLayers)
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

// Flag a per-frame render state's dirty index for re-initialization on the render thread.
// Called from Remove() on the game thread; Render() reads iMinDirtyIndex to know what to re-init.
template<typename TRenderState>
void FlagRenderStateDirty(std::unordered_map<uint16_t, TRenderState>& rPerFrameStates, uint16_t uiFrameId, int64_t iIndex)
{
	auto it = rPerFrameStates.find(uiFrameId);
	if (it != rPerFrameStates.end())
	{
		it->second.iMinDirtyIndex = std::min(it->second.iMinDirtyIndex, iIndex);
	}
}

// Snapshot previous positions and finalize per-frame render state after processing a segment.
template<typename TRenderState>
void SnapshotRenderState(TRenderState& rState, const XMVECTOR* pSourcePositions, int64_t iCount)
{
	std::memcpy(rState.pVecPreviousPositions, pSourcePositions, iCount * sizeof(XMVECTOR));
	rState.iRenderedCount = iCount;
	rState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
}

// Shared rendering helpers for collections
bool IsPointVisible(XMVECTOR vecPosition, XMFLOAT4A& rOutPosition);
XMVECTOR ProjectToBaseHeight(XMVECTOR vecPosition);
void BuildAxisAlignedQuad(shaders::AxisAlignedQuadLayout& rLayout, const XMFLOAT4A& f4Position, float fArea, const XMFLOAT4A& f4Params, uint32_t uiColor);

} // namespace engine

#define CHECK_VK(a) do { VkResult vkResultMacro = a; if (vkResultMacro != VK_SUCCESS) [[unlikely]] { DEBUG_BREAK(); CheckVk(vkResultMacro, #a); } } while (false);
