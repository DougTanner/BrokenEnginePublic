# Architecture: Extract Exception Handling from ThreadLocal

Source: /external-architecture-review on Common/

SetupExceptionHandling() in ThreadLocal.cpp is 137 lines of exception/crash handling logic mixed
with the core thread-local storage class. This is an orthogonal concern that should be separated.

## Changes

### Common/ThreadLocal.cpp
- Extract `SetupExceptionHandling()` function (lines 34-177) and its static mutex (`static std::mutex sMutex` at line 8) into a new file
- Keep the call to `SetupExceptionHandling()` in the ThreadLocal constructor (line 23-26)
- Update the forward declaration `void SetupExceptionHandling()` (line 6) to remain visible

### New File: Common/ExceptionHandling.cpp
- Move `SetupExceptionHandling()` function body (lines 34-177) here
- Move `static std::mutex sMutex` (line 8) here
- Include "ThreadLocal.h" for access to Log(), ToHex(), etc.

### Affected .vcxproj files
- Add ExceptionHandling.cpp to the appropriate filter in the vcxproj files that compile Common/

## Verification Notes
- Clean extraction: SetupExceptionHandling is self-contained, only references sMutex and Common globals
- ThreadLocal.cpp goes from 179 lines to ~36 lines after extraction
- ExceptionHandling.cpp will be ~145 lines (function + mutex + includes)
- Functions used by SetupExceptionHandling (Log, ToString, ToHex, LogStackWalker) are all available via Common headers
