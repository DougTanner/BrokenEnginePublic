# Tech Debt: Deprecated API Replacement

Source: /external-tech-debt on Common/

## Changes

### Common/Utils.cpp
- Replace `std::wstring_convert<std::codecvt_utf8<wchar_t>>` in `ToString(std::wstring_view)` (line 131) with `WideCharToMultiByte(CP_UTF8, ...)` Win32 API
- Replace `std::wstring_convert<std::codecvt_utf8<char32_t>>` in `ToString(std::u32string_view)` (line 142) with equivalent Win32 API or manual UTF-32 to UTF-8 conversion
- After migration, the `_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING` define in ExternalHeaders.h:25 can potentially be removed (check if any other files use codecvt)
- After migration, the `#include <codecvt>` in ExternalHeaders.h:56 can potentially be removed (check if any other files use codecvt)

## Verification Notes
- ToString(std::wstring_view) at line 131 is valid — used by CrashReport and LogFormatters
- ToString(std::u32string_view) at line 142 — check if still needed after ToU32string removal (TechDebt_DeadCode plan). If no callers remain, delete instead of migrating
- Win32 WideCharToMultiByte handles wchar_t (UTF-16) but NOT char32_t (UTF-32) directly; UTF-32 version needs manual conversion or two-step (UTF-32→UTF-16→UTF-8)
