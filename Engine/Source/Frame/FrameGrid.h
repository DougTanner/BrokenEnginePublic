#pragma once

#include "Frame/GridCoord.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

} // namespace game

namespace engine
{

void MergeFramesForRender(game::FrameInterpolate& rDest, const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& rCurrentFrames, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord, float fDeltaTime);

} // namespace engine
