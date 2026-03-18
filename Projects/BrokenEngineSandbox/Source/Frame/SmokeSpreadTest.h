#pragma once

#include "Frame/Collections/Players/Players.h"

namespace game
{

struct Frame;

void XM_CALLCONV SmokeSpreadTestUpdatePlayer(XMVECTOR& vecVelocity, XMVECTOR& vecWantedDirection, PlayerFlags_t& flags, FXMVECTOR vecPosition);
void SmokeSpreadTestSpawnSpaceship(Frame& rFrame);

} // namespace game
