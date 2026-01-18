#pragma once

namespace common
{

// https://en.wikipedia.org/wiki/Xorshift
struct RandomEngine
{
	uint64_t uiState = (static_cast<uint64_t>(362436069) << 32) | 521288629;

	RandomEngine() = default;

	RandomEngine(uint32_t uiSeed)
	{
		uiState = uiSeed;
		if (uiState == 0)
		{
			uiState = 1;
		}
	}

	void TimeSeed()
	{
		uiState = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		if (uiState == 0)
		{
			uiState = 1;
		}
	}

	bool operator==(const RandomEngine& rOther) const
	{
		return common::BreakOnNotEqual(uiState, rOther.uiState);
	}

	crc_t Crc() const
	{
		return common::Crc(uiState);
	}
};

inline uint32_t Random(uint32_t uiMax, RandomEngine& rRandomEngine)
{
	uint64_t x = rRandomEngine.uiState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rRandomEngine.uiState = x;
	return static_cast<uint32_t>(x) % (uiMax + 1);
}

template<float MAX = 1.0f>
inline float Random(RandomEngine& rRandomEngine)
{
	static constexpr float kfDivisor = MAX / static_cast<float>(std::numeric_limits<uint64_t>::max());
	uint64_t x = rRandomEngine.uiState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rRandomEngine.uiState = x;
	return static_cast<float>(x) * kfDivisor;
}

template<std::floating_point T>
T UniformRandom(std::mt19937& rRandomEngine, T min, T max)
{
	std::uniform_real_distribution<T> uniformRealDistribution(min, max);
	return uniformRealDistribution(rRandomEngine);
}

template<std::integral T>
T UniformRandom(std::mt19937& rRandomEngine, T min, T max)
{
	std::uniform_int_distribution<T> uniformRealDistribution(min, max);
	return uniformRealDistribution(rRandomEngine);
}

} // namespace common
