#pragma once

#include <source_location>

namespace common
{

void DebugBreak();
void Assert(bool bCondition, std::source_location loc = std::source_location::current());
void CheckHresult(HRESULT hresult, std::source_location loc = std::source_location::current());
void VerifySuccess(bool bCondition, std::source_location loc = std::source_location::current());

} // namespace common

using common::Assert;
using common::CheckHresult;
using common::VerifySuccess;
