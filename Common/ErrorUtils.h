#pragma once

namespace common
{

void DebugBreak();
void Assert(bool bCondition, std::string_view expression, std::source_location loc = std::source_location::current());
void CheckHresult(HRESULT hresult, std::string_view expression, std::source_location loc = std::source_location::current());

} // namespace common

#define ASSERT(a) do { if (!(a)) [[unlikely]] { if constexpr (kbEnableDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } common::Assert(a, #a); } } while (false);
#define CHECK_HRESULT(a) do { HRESULT hresultMacro = a; if (hresultMacro < 0) [[unlikely]] { if constexpr (kbEnableDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } common::CheckHresult(hresultMacro, #a); } } while (false);
#define VERIFY_SUCCESS(a) do { bool bReturnMacro = a; if (!bReturnMacro) [[unlikely]] { if constexpr (kbEnableDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } common::Assert(bReturnMacro, #a); } } while (false);
