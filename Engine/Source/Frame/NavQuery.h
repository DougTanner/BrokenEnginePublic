#pragma once

namespace engine
{

struct NavData;

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, const NavData& rNavData, XMVECTOR* pOutNextWaypoint = nullptr);
XMVECTOR XM_CALLCONV NavQuerySnapToNavigable(FXMVECTOR vecPosition, const NavData& rNavData);

} // namespace engine
