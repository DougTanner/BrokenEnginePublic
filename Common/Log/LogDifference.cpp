#include "LogDifference.h"

namespace common
{

bool XM_CALLCONV LogDifference_Vec(const char* pcName, FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if (!bEqual) [[unlikely]]
	{
		LOG(kNetwork, kError, "LogDifferences {} {} Client: {} Server: {}", gpLogDifferenceContext, pcName, WbV4(rOne, kiLogDifferencePrecision), WbV4(rTwo, kiLogDifferencePrecision));
	}

	return bEqual;
}

bool XM_CALLCONV LogDifference_Vec(const char* pcName, int64_t iIndex, FXMVECTOR rOne, FXMVECTOR rTwo)
{
	XMFLOAT4 f4One, f4Two;
	XMStoreFloat4(&f4One, rOne);
	XMStoreFloat4(&f4Two, rTwo);
	bool bEqual = std::memcmp(&f4One, &f4Two, sizeof(XMFLOAT4)) == 0;

	if (!bEqual) [[unlikely]]
	{
		LOG(kNetwork, kError, "LogDifferences {} {}[{}] Client: {} Server: {}", gpLogDifferenceContext, pcName, iIndex, WbV4(rOne, kiLogDifferencePrecision), WbV4(rTwo, kiLogDifferencePrecision));
	}

	return bEqual;
}

} // namespace common
