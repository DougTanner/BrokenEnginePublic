#include "Random.h"

namespace common
{

RandomEngine::RandomEngine(uint32_t uiSeed)
{
	uiState = uiSeed;
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
	uint64_t x = rRandomEngine.uiState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rRandomEngine.uiState = x;
	return static_cast<uint32_t>(x) % (uiMax + 1);
}

float Random(float fMax, RandomEngine& rRandomEngine)
{
	uint64_t x = rRandomEngine.uiState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rRandomEngine.uiState = x;
	return static_cast<float>(x) * (fMax / static_cast<float>(std::numeric_limits<uint64_t>::max()));
}

} // namespace common
