#pragma once

#include <source_location>

namespace common
{

void DebugBreak();
void Assert(bool bCondition, std::string_view expression, std::source_location loc = std::source_location::current());
void CheckHresult(HRESULT hresult, std::string_view expression, std::source_location loc = std::source_location::current());
void VerifySuccess(bool bCondition, std::string_view expression, std::source_location loc = std::source_location::current());

} // namespace common

using common::Assert;
using common::CheckHresult;
using common::VerifySuccess;

#define ASSERT(expr) Assert(expr, #expr)
#define CHECK_HRESULT(expr) CheckHresult(expr, #expr)
#define VERIFY_SUCCESS(expr) VerifySuccess(expr, #expr)
