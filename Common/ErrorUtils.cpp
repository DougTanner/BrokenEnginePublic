#include "ErrorUtils.h"

#include "Log.h"
#include "WindowsUtils.h"

namespace common
{

void DebugBreak()
{
	if constexpr (kbEnableDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } }
}

void Assert(bool bCondition, std::string_view expression, std::source_location sourceLocation)
{
	if (!bCondition) [[unlikely]]
	{
		Log("Assert failed: \"{}\" at {}:{} in {}", expression, sourceLocation.file_name(), sourceLocation.line(), sourceLocation.function_name());
		common::DebugBreak();
		throw std::runtime_error(std::format("Assert failed: \"{}\" at {}:{}", expression, sourceLocation.file_name(), sourceLocation.line()));
	}
}

void CheckHresult(HRESULT hresult, std::string_view expression, std::source_location loc)
{
	if (hresult < 0) [[unlikely]]
	{
		Log("CheckHresult failed: \"{}\" at {}:{} in {} - {} 0x{:X}: {}", expression, loc.file_name(), loc.line(), loc.function_name(), hresult, static_cast<uint32_t>(hresult), HresultToString(hresult).data());
		common::DebugBreak();
		throw std::runtime_error(std::format("CheckHresult failed: \"{}\" at {}:{}", expression, loc.file_name(), loc.line()));
	}
}

} // namespace common
