#pragma once

namespace common
{

// https://en.wikipedia.org/wiki/Xorshift
// Not thread-safe — each worker/partition needs its own deterministically-seeded instance
// Default state is shared by design (deterministic for replay); explicitly seed every independent stream
struct RandomEngine
{
	RandomEngine() = default;

	RandomEngine(uint32_t uiSeed);

	explicit RandomEngine(uint64_t uiSeed)
	{
		Seed(uiSeed);
	}

	// NOT deterministic — never seed a simulation engine from this without broadcasting/recording the resulting seed
	void TimeSeed();
	bool operator==(const RandomEngine& rOther) const;
	crc_t Crc() const;

	// Always non-zero — every write path enforces it, so a stream/log/CRC consumer never observes the dead state
	uint64_t State() const
	{
		return uiState;
	}

	// Sole write path for a state arriving from a save, replay, or network stream; rejects the dead state
	void SetSerializedState(uint64_t uiValue);

private:

	void Seed(uint64_t uiSeed);

	friend uint64_t RandomNext(RandomEngine& rRandomEngine);

	uint64_t uiState = 0xe220a8397b1dcdaf; // splitmix64(0)
};

static_assert(sizeof(RandomEngine) == sizeof(uint64_t), "Extend operator== / Crc() / serialization when adding RandomEngine state");

// Outranks the generic Read<T> template, so a raw whole-object read is a compile error instead of an
// unvalidated state install; read the value into a uint64_t and pass it to SetSerializedState
void Read(std::istream& rStream, RandomEngine& rRandomEngine) = delete;

inline uint64_t RandomNext(RandomEngine& rRandomEngine)
{
	uint64_t x = rRandomEngine.uiState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rRandomEngine.uiState = x;
	return x;
}

// High-24-bit construction — int → float cast is exact and the result is strictly < 1.0f regardless of rounding mode
inline float RandomUnitFloat(RandomEngine& rRandomEngine)
{
	static constexpr float kfDivisor = 1.0f / 16'777'216.0f; // 2^-24
	return static_cast<float>(RandomNext(rRandomEngine) >> 40) * kfDivisor;
}

uint32_t Random(uint32_t uiMax, RandomEngine& rRandomEngine);

template<float MAX = 1.0f>
inline float Random(RandomEngine& rRandomEngine)
{
	return RandomUnitFloat(rRandomEngine) * MAX;
}

float Random(float fMax, RandomEngine& rRandomEngine);

} // namespace common
