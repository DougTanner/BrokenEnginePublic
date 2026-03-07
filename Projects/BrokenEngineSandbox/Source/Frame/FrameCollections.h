#pragma once

#include "Frame/Frame.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"

namespace game
{

inline auto GameInterpolateCollections(auto&& rSelf)
{
	return std::tie(*rSelf.pBlasters, *rSelf.pMissiles, *rSelf.pSpaceships, *rSelf.pTargets);
}

inline auto GamePostRenderCollections(auto&& rSelf)
{
	return std::tie(*rSelf.pBlasters, *rSelf.pMissiles, *rSelf.pSpaceships, *rSelf.pTargets);
}

using GameInterpolateTypes = engine::TupleToTypeList_t<decltype(GameInterpolateCollections(std::declval<FrameInterpolate&>()))>;
using GamePostRenderTypes = engine::TupleToTypeList_t<decltype(GamePostRenderCollections(std::declval<FramePostRender&>()))>;

} // namespace game
