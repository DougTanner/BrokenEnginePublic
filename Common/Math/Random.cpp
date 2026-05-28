#include "Random.h"

namespace common
{

RandomEngine::RandomEngine(uint32_t uiSeed)
{
	// Mix the 32-bit seed into a full 64-bit state using splitmix64
	uint64_t x = static_cast<uint64_t>(uiSeed) + 0x9e3779b97f4a7c15;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
	x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
	uiState = x ^ (x >> 31);
	if (uiState == 0)
	{
		uiState = 1;
	}
}

void RandomEngine::TimeSeed()
{
	uiState = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	if (uiState == 0)
	{
		uiState = 1;
	}
}

bool RandomEngine::operator==(const RandomEngine& rOther) const
{
	return uiState == rOther.uiState;
}

crc_t RandomEngine::Crc() const
{
	return common::Crc(uiState);
}

uint32_t Random(uint32_t uiMax, RandomEngine& rRandomEngine)
{
	return static_cast<uint32_t>(RandomNext(rRandomEngine)) % (uiMax + 1);
}

float Random(float fMax, RandomEngine& rRandomEngine)
{
	return static_cast<float>(RandomNext(rRandomEngine)) * (fMax / static_cast<float>(std::numeric_limits<uint64_t>::max()));
}

} // namespace common
