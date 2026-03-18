# Tech Debt: Thread Safety

Source: /external-tech-debt on Engine/Source/Debug

## Changes

### Engine/Source/Debug/EnumToString.h
- Change `static char spcResult[32]` (line 68) to `thread_local char spcResult[32]` — the current static buffer is a data race if `Convert()` is called from multiple threads simultaneously (e.g., concurrent CHECK_VK failures on submit threads). `GraphicsUtils.cpp` line 19 already uses `thread_local char spcException[1024]` for the same reason [~5m]

## Verification Notes
- PASS: Item verified against source
- `static char spcResult[32]` confirmed at line 68 inside the `else` branch (when `kbEnableLogging` is false)
- `GraphicsUtils.cpp` line 19 reference confirmed: `thread_local char spcException[1024] {};`
- Race condition is real: multiple threads could call Convert() simultaneously when logging is disabled, corrupting the shared static buffer
- Follows existing codebase pattern (GraphicsUtils.cpp already uses thread_local for same reason)
