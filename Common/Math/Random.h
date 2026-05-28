#pragma once

namespace common
{

// https://en.wikipedia.org/wiki/Xorshift
struct RandomEngine
{
	uint64_t uiState = 0xe220a8397b1dcdaf; // splitmix64(0)

	RandomEngine() = default;

	RandomEngine(uint32_t uiSeed);

	void TimeSeed();
	bool operator==(const RandomEngine& rOther) const;
	crc_t Crc() const;
};

inline uint64_t RandomNext(RandomEngine& rRandomEngine)
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
	return static_cast<float>(RandomNext(rRandomEngine)) * kfDivisor;
}

float Random(float fMax, RandomEngine& rRandomEngine);

} // namespace common
