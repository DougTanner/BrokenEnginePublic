#pragma once

namespace common
{

inline thread_local const char* gpLogDifferenceContext = "";

struct ScopedLogDifferenceContext
{
	const char* pcPrevious;
	ScopedLogDifferenceContext(const char* pcContext) : pcPrevious(gpLogDifferenceContext) { gpLogDifferenceContext = pcContext; }
	~ScopedLogDifferenceContext() { gpLogDifferenceContext = pcPrevious; }
};

// Non-indexed version for scalar frame fields
template<FixedString NAME, typename T>
inline bool LogDifference(const T& rOne, const T& rTwo)
{
	bool bEqual = false;
	if constexpr (std::is_same_v<T, XMFLOAT2> || std::is_same_v<T, XMFLOAT3> || std::is_same_v<T, XMFLOAT4> || std::is_same_v<T, XMFLOAT4A>)
	{
		bEqual = std::memcmp(&rOne, &rTwo, sizeof(T)) == 0;
	}
	else
	{
		bEqual = rOne == rTwo;
	}

	if (!bEqual) [[unlikely]]
	{
		Log("LogDifferences {} {} Client: {} Server: {}", gpLogDifferenceContext, static_cast<const char*>(NAME), rOne, rTwo);
	}

	return bEqual;
}

// Indexed version for collection element fields (pVecPositions[i], etc.)
template<FixedString NAME, typename T>
inline bool LogDifference(int64_t iIndex, const T& rOne, const T& rTwo)
{
	bool bEqual = false;
	if constexpr (std::is_same_v<T, XMFLOAT2> || std::is_same_v<T, XMFLOAT3> || std::is_same_v<T, XMFLOAT4> || std::is_same_v<T, XMFLOAT4A>)
		bEqual = std::memcmp(&rOne, &rTwo, sizeof(T)) == 0;
	else
		bEqual = rOne == rTwo;

	if (!bEqual) [[unlikely]]
		Log(kLogNetwork, "LogDifferences {} {}[{}] Client: {} Server: {}", gpLogDifferenceContext, static_cast<const char*>(NAME), iIndex, rOne, rTwo);

	return bEqual;
}

// XMVECTOR overloads (declared here, defined in Utils.cpp)
bool XM_CALLCONV LogDifference_Vec(const char* pcName, FXMVECTOR rOne, FXMVECTOR rTwo);
bool XM_CALLCONV LogDifference_Vec(const char* pcName, int64_t iIndex, FXMVECTOR rOne, FXMVECTOR rTwo);

} // namespace common
