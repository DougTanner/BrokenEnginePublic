#if defined(BT_CLIENT)

#include "Blasters.h"

#include "Profile/ProfileManager.h"

namespace game
{

void BlastersInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const BlastersInterpolate& rCurrent = *rFrameInterpolate.pBlasters;
	gpProfileManager->SetCount(game::kCpuCounterBlasters, rCurrent.iCount);
	gpProfileManager->SetCount(game::kCpuCounterBlastersRendered, rCurrent.iCount);
}

} // namespace game

#endif // BT_CLIENT
