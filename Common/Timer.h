#pragma once

namespace common
{

class Timer
{
	static_assert(std::chrono::high_resolution_clock::is_steady);

public:

	inline Timer()
	: mLastTimePoint(std::chrono::high_resolution_clock::now())
	{
	}

	inline void Reset()
	{
		mLastTimePoint = std::chrono::high_resolution_clock::now();
	}

	inline std::chrono::nanoseconds GetDeltaNs(bool bReset = false)
	{
		std::chrono::high_resolution_clock::time_point currentTimePoint = std::chrono::high_resolution_clock::now();
		std::chrono::nanoseconds elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTimePoint - mLastTimePoint);

		if (bReset)
		{
			mLastTimePoint = currentTimePoint;
		}

		return elapsedNs;
	}

private:

	std::chrono::high_resolution_clock::time_point mLastTimePoint;
};

} // namespace common
