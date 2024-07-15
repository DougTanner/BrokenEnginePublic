#include "HexShields.h"

#include "Frame/Render.h"
#include "Graphics/Managers/BufferManager.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

void HexShields::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const game::Frame& __restrict rFrame)
{
	const HexShields& rCurrent = rFrame.hexShields;

	auto pLayouts = reinterpret_cast<shaders::HexShieldLayout*>(gpBufferManager->mHexShieldsStorageBuffers.at(iCommandBuffer).mpMappedMemory);

	int64_t iCount = 0;
	int64_t iRendered = 0;
	for (decltype(rCurrent.uiMaxIndex) i = 0; i <= rCurrent.uiMaxIndex; ++i)
	{
		if (!rCurrent.pbUsed[i])
		{
			continue;
		}

		const HexShieldInfo& rHexShieldInfo = rCurrent.pObjectInfos[i];

		++iCount;

		static constexpr float kfAdjust = 20.0f;
		if (!InVisibleArea(gf4RenderVisibleArea, rHexShieldInfo.hexShieldLayout.f4Position, kfAdjust, kfAdjust, kfAdjust, kfAdjust))
		{
			continue;
		}

		shaders::HexShieldLayout& rHexShieldLayout = pLayouts[iRendered];
		rHexShieldLayout = rHexShieldInfo.hexShieldLayout;

		++iRendered;
	}
	PROFILE_SET_COUNT(kCpuCounterHexShields, iCount);
	PROFILE_SET_COUNT(kCpuCounterHexShieldsRendered, iRendered);

	gpPipelineManager->mpPipelines[kPipelineHexShields].WriteIndirectBuffer(iCommandBuffer, iRendered);
	gpPipelineManager->mpPipelines[kPipelineHexShieldsLighting].WriteIndirectBuffer(iCommandBuffer, iRendered);
}

} // namespace engine
