#pragma once

namespace common
{

// Converts nanosecond durations to floating-point seconds
// Used for delta time calculations in the game loop
// Parameters: nanoseconds - Duration to convert, FLOAT_TYPE - Target floating-point type (float or double)
// Returns: Time in seconds as specified floating-point type
template<typename FLOAT_TYPE>
constexpr FLOAT_TYPE NanosecondsToFloatSeconds(std::chrono::nanoseconds nanoseconds)
{
	static_assert(std::is_floating_point_v<FLOAT_TYPE>, "NanosecondsToFloatSeconds requires a floating-point type");

	return std::chrono::duration_cast<std::chrono::duration<FLOAT_TYPE, std::ratio<1, 1>>>(nanoseconds).count();
}

class Timer
{
	static_assert(std::chrono::steady_clock::is_steady);

public:

	inline Timer()
	: mLastTimePoint(std::chrono::steady_clock::now())
	{
	}

	inline void Reset()
	{
		mLastTimePoint = std::chrono::steady_clock::now();
	}

	inline std::chrono::nanoseconds GetDeltaNs(bool bReset = false)
	{
		std::chrono::steady_clock::time_point currentTimePoint = std::chrono::steady_clock::now();
		std::chrono::nanoseconds elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTimePoint - mLastTimePoint);

		if (bReset)
		{
			mLastTimePoint = currentTimePoint;
		}

		return elapsedNs;
	}

private:

	std::chrono::steady_clock::time_point mLastTimePoint;
};

} // namespace common
