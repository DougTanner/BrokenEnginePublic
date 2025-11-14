#pragma once

namespace common
{

class InTheLastSecond
{
public:

	void Set(int64_t count = 1)
	{
		std::chrono::high_resolution_clock::time_point timePointCurrent = std::chrono::high_resolution_clock::now();
		mFramesInTheLastSecond.push_back({timePointCurrent, count});
		while (!mFramesInTheLastSecond.empty() && std::chrono::duration_cast<std::chrono::nanoseconds>(timePointCurrent - mFramesInTheLastSecond.front().first) > 1'000'000'000ns)
		{
			mFramesInTheLastSecond.pop_front();
		}
	}

	int64_t Get()
	{
		int64_t total = 0;
		for (const std::pair<std::chrono::high_resolution_clock::time_point, int64_t>& entry : mFramesInTheLastSecond)
		{
			total += entry.second;
		}
		return total;
	}

private:

	std::deque<std::pair<std::chrono::high_resolution_clock::time_point, int64_t>> mFramesInTheLastSecond;
};

template <typename VALUE_TYPE, int64_t COUNT = 128>
class Smoothed
{
public:

	void operator=(VALUE_TYPE value)
	{
		mpValues[miNext++] = value;
		if (miNext == COUNT)
		{
			miNext = 0;
		}

		miCount = std::min(miCount + 1, COUNT);
	}

	VALUE_TYPE Get()
	{
		return mSmoothedValue;
	}

	VALUE_TYPE Max()
	{
		if (miCount == 0)
		{
			return {};
		}

		VALUE_TYPE max = {};

		int64_t iCurrent = miNext - 1;
		int64_t iCountLeft = miCount;
		while (iCountLeft > 0)
		{
			if (iCurrent < 0)
			{
				iCurrent = COUNT - 1;
			}

			max = std::max(max, mpValues[iCurrent]);

			--iCurrent;
			--iCountLeft;
		}

		return max;
	}

	VALUE_TYPE Current()
	{
		if (miCount == 0)
		{
			return {};
		}

		int64_t iCurrent = miNext - 1;
		if (iCurrent < 0)
		{
			iCurrent = COUNT - 1;
		}

		return mpValues[iCurrent];
	}

	VALUE_TYPE Average() const
	{
		if (miCount == 0)
		{
			return {};
		}

		VALUE_TYPE total = {};

		int64_t iCurrent = miNext - 1;
		int64_t iCountLeft = miCount;
		while (iCountLeft > 0)
		{
			if (iCurrent < 0)
			{
				iCurrent = COUNT - 1;
			}

			total += mpValues[iCurrent];

			--iCurrent;
			--iCountLeft;
		}

		return total /= static_cast<VALUE_TYPE>(miCount);
	}

	VALUE_TYPE Update()
	{
		if (miCount == 0)
		{
			return {};
		}

		VALUE_TYPE targetValue = Average();

		VALUE_TYPE diff = targetValue - mSmoothedValue;
		if (diff > -COUNT && diff < 0)
		{
			diff = -1;
		}
		else if (diff > 0 && diff < COUNT)
		{
			diff = 1;
		}
		mSmoothedValue += diff;

		return mSmoothedValue;
	}

	VALUE_TYPE mpValues[COUNT] {};
	int64_t miNext = 0;
	int64_t miCount = 0;

private:

	VALUE_TYPE mSmoothedValue {};
};

} // namespace common
