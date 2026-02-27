#pragma once

namespace common
{

class InTheLastSecond
{
public:

	void Set(int64_t count = 1);
	int64_t Get();

private:

	std::pair<std::chrono::high_resolution_clock::time_point, int64_t> mFramesInTheLastSecond[1024] {};
	int64_t miHead = 0;
	int64_t miCount = 0;
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
