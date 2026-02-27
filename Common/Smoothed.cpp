#include "Smoothed.h"

namespace common
{

void InTheLastSecond::Set(int64_t count)
{
	std::chrono::high_resolution_clock::time_point timePointCurrent = std::chrono::high_resolution_clock::now();
	if (miCount == 1024)
	{
		miHead = (miHead + 1) % 1024;
		--miCount;
	}
	mFramesInTheLastSecond[(miHead + miCount) % 1024] = {timePointCurrent, count};
	++miCount;
	while (miCount > 0 && std::chrono::duration_cast<std::chrono::nanoseconds>(timePointCurrent - mFramesInTheLastSecond[miHead].first) > 1'000'000'000ns)
	{
		miHead = (miHead + 1) % 1024;
		--miCount;
	}
}

int64_t InTheLastSecond::Get()
{
	int64_t total = 0;
	for (int64_t i = 0; i < miCount; ++i)
	{
		total += mFramesInTheLastSecond[(miHead + i) % 1024].second;
	}
	return total;
}

} // namespace common
