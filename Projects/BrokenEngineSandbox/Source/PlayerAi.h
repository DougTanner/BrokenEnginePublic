#pragma once

#include "Input/Input.h"

namespace game
{

struct Frame;

class PlayerAi
{
public:
	void Update(const Frame& rCurrentFrame, FrameInput& rFrameInput);
	void Reset() { std::memset(mfTimers, 0, sizeof(mfTimers)); }

private:
	float mfTimers[kiMaxPlayers] {};
	XMVECTOR mVecDirections[kiMaxPlayers] {};
	common::RandomEngine mRandomEngine {};
};

} // namespace game
