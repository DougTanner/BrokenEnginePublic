#include "Random.h"

namespace common
{

RandomEngine::RandomEngine(uint32_t uiSeed)
{
	Seed(static_cast<uint64_t>(uiSeed));
}

void RandomEngine::Seed(uint64_t uiSeed)
{
	// Mix the seed into a full 64-bit state using splitmix64
	uint64_t uiValue = uiSeed + 0x9e3779b97f4a7c15;
	uiValue = (uiValue ^ (uiValue >> 30)) * 0xbf58476d1ce4e5b9;
	uiValue = (uiValue ^ (uiValue >> 27)) * 0x94d049bb133111eb;
	uiState = uiValue ^ (uiValue >> 31);
	if (uiState == 0)
	{
		uiState = 1;
	}
}

void RandomEngine::TimeSeed()
{
	static_assert(std::chrono::high_resolution_clock::is_steady, "Non-steady clock can run backwards");
	uiState = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	if (uiState == 0)
	{
		uiState = 1;
	}
}

void RandomEngine::SetSerializedState(uint64_t uiValue)
{
	// Trust boundary (save / replay file, network payload): zero is an absorbing state for the xorshift
	// step, so a zero here would make every later draw zero — fleet identifiers included — for the rest of
	// the run. Refuse the stream instead of silently repairing it, which would diverge from the recording.
	if (uiValue == 0)
	{
		throw CorruptStreamException("RandomEngine::SetSerializedState");
	}
	uiState = uiValue;
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
	// Lemire multiply-shift — division-free, uses the strong high 32 bits; 64-bit range cannot wrap at UINT32_MAX
	uint64_t uiRange = static_cast<uint64_t>(uiMax) + 1;
	return static_cast<uint32_t>(((RandomNext(rRandomEngine) >> 32) * uiRange) >> 32);
}

float Random(float fMax, RandomEngine& rRandomEngine)
{
	return RandomUnitFloat(rRandomEngine) * fMax;
}

} // namespace common
