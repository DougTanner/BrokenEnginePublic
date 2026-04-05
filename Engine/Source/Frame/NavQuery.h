#pragma once

namespace engine
{

struct NavData;

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, const NavData& rNavData);

} // namespace engine
