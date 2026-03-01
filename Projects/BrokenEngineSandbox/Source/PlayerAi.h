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
		mfEdgeCrossCooldowns.clear();
		miEdgeCrossTargets.clear();
	}

private:
	std::vector<XMVECTOR> mVecDirections;
	std::vector<float> mfFireTimers;
	std::vector<float> mfMissileTimers;
	std::vector<float> mfEdgeCrossCooldowns;
	std::vector<int8_t> miEdgeCrossTargets;
	common::RandomEngine mRandomEngine {};
};

} // namespace game
