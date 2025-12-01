#pragma once

namespace game
{

struct FrameInterpolate;

}

namespace engine
{

inline bool gbSmokeClear = true;
inline float gbSmokeSpread = false;

void RenderSmokeGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& __restrict rFrameInterpolate);

}
