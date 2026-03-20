#pragma once

namespace common
{

// https://en.wikipedia.org/wiki/Xorshift
struct RandomEngine
{
	uint64_t uiState = (static_cast<uint64_t>(362436069) << 32) | 521288629;

	RandomEngine() = default;

	RandomEngine(uint32_t uiSeed);

	void TimeSeed();
	bool operator==(const RandomEngine& rOther) const;
	crc_t Crc() const;
};

inline uint64_t XorshiftNext(RandomEngine& rRandomEngine)
{
	uint64_t x = rRandomEngine.uiState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rRandomEngine.uiState = x;
	return x;
}

uint32_t Random(uint32_t uiMax, RandomEngine& rRandomEngine);

template<float MAX = 1.0f>
inline float Random(RandomEngine& rRandomEngine)
{
	static constexpr float kfDivisor = MAX / static_cast<float>(std::numeric_limits<uint64_t>::max());
	uint64_t x = XorshiftNext(rRandomEngine);
	return static_cast<float>(x) * kfDivisor;
}

float Random(float fMax, RandomEngine& rRandomEngine);

} // namespace common
