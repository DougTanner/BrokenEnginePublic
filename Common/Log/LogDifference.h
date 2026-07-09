#pragma once

namespace common
{

inline thread_local const char* gpLogDifferenceContext = "";

struct ScopedLogDifferenceContext
{
	const char* pcPrevious;
	ScopedLogDifferenceContext(const char* pcContext) : pcPrevious(gpLogDifferenceContext) { gpLogDifferenceContext = pcContext; }
	~ScopedLogDifferenceContext() { gpLogDifferenceContext = pcPrevious; }

	ScopedLogDifferenceContext(const ScopedLogDifferenceContext&) = delete;
	ScopedLogDifferenceContext& operator=(const ScopedLogDifferenceContext&) = delete;
};

inline constexpr int kiLogDifferencePrecision = 3;

// Routes a difference value into the allocation-free Wb/WbV* formatters (the no-float-format rule) so a float-bearing
// field diff never reaches the default heap-allocating std::format path; non-float types pass through unchanged.
template <typename T>
inline auto WrapLogDifferenceValue(const T& rValue)
{
	if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
	{
		return Wb(static_cast<float>(rValue), kiLogDifferencePrecision);
	}
	else if constexpr (std::is_same_v<T, XMFLOAT2>)
	{
		return WbV2(XMLoadFloat2(&rValue), kiLogDifferencePrecision);
	}
	else if constexpr (std::is_same_v<T, XMFLOAT3>)
	{
		return WbV3(XMLoadFloat3(&rValue), kiLogDifferencePrecision);
	}
	else if constexpr (std::is_same_v<T, XMFLOAT4>)
	{
		return WbV4(XMLoadFloat4(&rValue), kiLogDifferencePrecision);
	}
	else if constexpr (std::is_same_v<T, XMFLOAT4A>)
	{
		return WbV4(XMLoadFloat4A(&rValue), kiLogDifferencePrecision);
	}
	else
	{
		return rValue;
	}
}

// Non-indexed version for scalar frame fields
template <FixedString NAME, typename T>
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
		LOG(kNetwork, kError, "LogDifferences {} {} Client: {} Server: {}", gpLogDifferenceContext, static_cast<const char*>(NAME), WrapLogDifferenceValue(rOne), WrapLogDifferenceValue(rTwo));
	}

	return bEqual;
}

// Indexed version for collection element fields (pVecPositions[i], etc.)
template <FixedString NAME, typename T>
inline bool LogDifference(int64_t iIndex, const T& rOne, const T& rTwo)
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
		LOG(kNetwork, kError, "LogDifferences {} {}[{}] Client: {} Server: {}", gpLogDifferenceContext, static_cast<const char*>(NAME), iIndex, WrapLogDifferenceValue(rOne), WrapLogDifferenceValue(rTwo));
	}

	return bEqual;
}

// XMVECTOR overloads (declared here, defined in LogDifference.cpp)
bool XM_CALLCONV LogDifference_Vec(const char* pcName, FXMVECTOR rOne, FXMVECTOR rTwo);
bool XM_CALLCONV LogDifference_Vec(const char* pcName, int64_t iIndex, FXMVECTOR rOne, FXMVECTOR rTwo);

} // namespace common
