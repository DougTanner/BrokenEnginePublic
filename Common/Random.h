#pragma once

namespace common
{

// https://stackoverflow.com/questions/52514296/simplest-random-number-generator-without-c-library
// DT: TODO Consider using https://en.wikipedia.org/wiki/Xorshift
struct RandomEngine
{
	uint32_t uiW = 521288629;
	uint32_t uiZ = 362436069;

	RandomEngine() = default;

	RandomEngine(uint32_t uiSeed)
	{
		uiW = uiSeed;
		uiZ = uiSeed;
	}

	void TimeSeed()
	{
		uint64_t uiTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		uiW = static_cast<uint32_t>(uiTime);
		uiZ = static_cast<uint32_t>(uiTime >> 32);
	}

	bool operator==(const RandomEngine& rOther) const = default;
};

inline uint32_t Random(uint32_t uiMax, RandomEngine& rRandomEngine)
{
	rRandomEngine.uiW = 18000 * (rRandomEngine.uiW & 65535) + (rRandomEngine.uiW >> 16);
	rRandomEngine.uiZ = 36969 * (rRandomEngine.uiZ & 65535) + (rRandomEngine.uiZ >> 16);
	return ((rRandomEngine.uiZ << 16) + rRandomEngine.uiW) % (uiMax + 1);
}

template<float MAX = 1.0f>
inline float Random(RandomEngine& rRandomEngine)
{
	static constexpr float kfDivisor = MAX / static_cast<float>(std::numeric_limits<unsigned long>::max());
	rRandomEngine.uiW = 18000 * (rRandomEngine.uiW & 65535) + (rRandomEngine.uiW >> 16);
	rRandomEngine.uiZ = 36969 * (rRandomEngine.uiZ & 65535) + (rRandomEngine.uiZ >> 16);
	return static_cast<float>((rRandomEngine.uiZ << 16) + rRandomEngine.uiW) * kfDivisor;
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

/*

float as_float(uint32_t i)
{
	union
	{
		uint32_t i;
		float f;
	} pun = { i };
	return pun.f;
}

float rand_float_co()
{
	return as_float(0x3F800000U | (rand32() >> 9)) - 1.0f;
}

static inline double prng_one(prng_state *p)
{
	return prng_u64(p) / 18446744073709551616.0;
}
*/

/*
inline int fastrand() {
  g_seed = (214013*g_seed+2531011);
  return (g_seed>>16)&0x7FFF;
} 
*/

/*
uint32_t xorshift32(uint32_t state[])
{
  // Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs"
uint32_t x = state[0];
x ^= x << 13;
x ^= x >> 17;
x ^= x << 5;
state[0] = x;
return x;
}

uint64_t xorshift64(uint64_t state[])
{
	uint64_t x = state[0];
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	state[0] = x;
	return x;
}
*/

/*
typedef struct {
	uint64_t  state;
} prng_state;

static inline uint64_t prng_u64(prng_state *const p)
{
	uint64_t  state = p->state;
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	p->state = state;
	return state * UINT64_C(2685821657736338717);
}
*/

} // namespace common
