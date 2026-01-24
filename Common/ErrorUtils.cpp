#include "ErrorUtils.h"

#include "Log.h"
#include "WindowsUtils.h"

namespace common
{

void DebugBreak()
{
	if constexpr (kbEnableDebugBreak)
	{
		if (IsDebuggerPresent() == TRUE)
		{
			__debugbreak();
		}
	}
}

void Assert(bool bCondition, std::source_location loc)
{
	if (!bCondition) [[unlikely]]
	{
		Log("Assert failed at {}:{} in {}", loc.file_name(), loc.line(), loc.function_name());
		DebugBreak();
		throw std::runtime_error(std::format("Assert failed at {}:{}", loc.file_name(), loc.line()));
	}
}

void CheckHresult(HRESULT hresult, std::source_location loc)
{
	if (hresult < 0) [[unlikely]]
	{
		Log("CheckHresult failed at {}:{} in {} - {} 0x{:X}: {}", loc.file_name(), loc.line(), loc.function_name(), hresult, static_cast<uint32_t>(hresult), HresultToString(hresult).data());
		DebugBreak();
		throw std::runtime_error(std::format("CheckHresult failed at {}:{}", loc.file_name(), loc.line()));
	}
}

void VerifySuccess(bool bCondition, std::source_location loc)
{
	if (!bCondition) [[unlikely]]
	{
		Log("VerifySuccess failed at {}:{} in {} - {}", loc.file_name(), loc.line(), loc.function_name(), LastErrorString().data());
		DebugBreak();
		throw std::runtime_error(std::format("VerifySuccess failed at {}:{}", loc.file_name(), loc.line()));
	}
}

} // namespace common
