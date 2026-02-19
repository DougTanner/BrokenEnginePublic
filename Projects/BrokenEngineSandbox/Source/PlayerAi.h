#pragma once

#include "Input/Input.h"

namespace game
{

struct Frame;

class PlayerAi
{
public:
	void UpdatePlayer(const Frame& rCurrentFrame, int64_t iPlayerIndex, PlayerInput& rPlayerInput);
	void Reset()
	{
		std::memset(mVecDirections, 0, sizeof(mVecDirections));
		std::memset(mfFireTimers, 0, sizeof(mfFireTimers));
		std::memset(mfMissileTimers, 0, sizeof(mfMissileTimers));
	}

private:
	XMVECTOR mVecDirections[kiMaxPlayers] {};
	float mfFireTimers[kiMaxPlayers] {};
	float mfMissileTimers[kiMaxPlayers] {};
	common::RandomEngine mRandomEngine {};
};

} // namespace game
