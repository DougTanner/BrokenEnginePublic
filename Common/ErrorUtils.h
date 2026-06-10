#pragma once

namespace common
{

void Assert(bool bCondition, std::string_view expression, std::source_location loc = std::source_location::current());
void CheckHresult(HRESULT hresult, std::string_view expression, std::source_location loc = std::source_location::current());

} // namespace common

#define DEBUG_BREAK() do { if constexpr (kbDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } } while (false)
#define ASSERT(a) do { bool bAssertMacro = a; if (!bAssertMacro) [[unlikely]] { common::Assert(bAssertMacro, #a); } _Analysis_assume_(bAssertMacro); } while (false)
#define CHECK_HRESULT(a) do { HRESULT hresultMacro = a; if (hresultMacro < 0) [[unlikely]] { common::CheckHresult(hresultMacro, #a); } _Analysis_assume_(hresultMacro >= 0); } while (false)
#define VERIFY_SUCCESS(a) do { bool bReturnMacro = a; if (!bReturnMacro) [[unlikely]] { common::Assert(bReturnMacro, #a); } _Analysis_assume_(bReturnMacro); } while (false)
