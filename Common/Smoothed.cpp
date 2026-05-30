#include "Smoothed.h"

namespace common
{

void InTheLastSecond::Set(int64_t count)
{
	std::chrono::steady_clock::time_point timePointCurrent = std::chrono::steady_clock::now();
	if (miCount == kiCapacity)
	{
		miHead = (miHead + 1) % kiCapacity;
		--miCount;
	}
	mFramesInTheLastSecond[(miHead + miCount) % kiCapacity] = {timePointCurrent, count};
	++miCount;
	while (miCount > 0 && std::chrono::duration_cast<std::chrono::nanoseconds>(timePointCurrent - mFramesInTheLastSecond[miHead].first) > 1'000'000'000ns)
	{
		miHead = (miHead + 1) % kiCapacity;
		--miCount;
	}
}

int64_t InTheLastSecond::Get()
{
	int64_t total = 0;
	for (int64_t i = 0; i < miCount; ++i)
	{
		total += mFramesInTheLastSecond[(miHead + i) % kiCapacity].second;
	}
	return total;
}

} // namespace common
