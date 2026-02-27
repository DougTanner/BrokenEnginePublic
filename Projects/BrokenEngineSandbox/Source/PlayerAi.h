#pragma once

namespace game
{

struct Frame;

class PlayerAi
{
public:
	void UpdatePlayer(const Frame& rCurrentFrame, int64_t iPlayerIndex, PlayerInput& rPlayerInput);
	void Reset()
	{
		mVecDirections.clear();
		mfFireTimers.clear();
		mfMissileTimers.clear();
	}

private:
	std::vector<XMVECTOR> mVecDirections;
	std::vector<float> mfFireTimers;
	std::vector<float> mfMissileTimers;
	common::RandomEngine mRandomEngine {};
};

} // namespace game
