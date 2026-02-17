#pragma once

namespace common
{

void Assert(bool bCondition, std::string_view expression, std::source_location loc = std::source_location::current());
void CheckHresult(HRESULT hresult, std::string_view expression, std::source_location loc = std::source_location::current());

} // namespace common

#define DEBUG_BREAK() do { if constexpr (kbEnableDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } } while (false)
#define ASSERT(a) do { if (!(a)) [[unlikely]] { DEBUG_BREAK(); common::Assert(a, #a); } } while (false);
#define CHECK_HRESULT(a) do { HRESULT hresultMacro = a; if (hresultMacro < 0) [[unlikely]] { DEBUG_BREAK(); common::CheckHresult(hresultMacro, #a); } } while (false);
#define VERIFY_SUCCESS(a) do { bool bReturnMacro = a; if (!bReturnMacro) [[unlikely]] { DEBUG_BREAK(); common::Assert(bReturnMacro, #a); } } while (false);
