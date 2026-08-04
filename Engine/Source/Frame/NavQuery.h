#pragma once

namespace engine
{

struct NavData;

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, const NavData& rNavData, XMVECTOR* pOutNextWaypoint = nullptr
#if defined(BT_SERVER)
	, bool* pOutEnteredAStar = nullptr
#endif // BT_SERVER
);
XMVECTOR XM_CALLCONV NavQuerySnapToNavigable(FXMVECTOR vecPosition, const NavData& rNavData);

// Is the position inside a no-navigation polygon? Reads x/y only, so unlike the two entry points above
// it places no requirement on Z.
bool XM_CALLCONV NavQueryPointBlocked(FXMVECTOR vecPosition, const NavData& rNavData);

} // namespace engine
