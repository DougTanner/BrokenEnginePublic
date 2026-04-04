#pragma once

#include "Graphics/IslandsFlip.h"

namespace engine
{

struct NavData;

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, FXMVECTOR vecArea, const NavData& rNavData, IslandsFlip eFlip, XMFLOAT2 f2IslandOffset);

} // namespace engine
