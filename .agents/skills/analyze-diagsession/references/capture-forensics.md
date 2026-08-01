# Capture Forensics

## 1. Extract

`.diagsession` is an OPC/ZIP package. .NET reads it by content, so extract it
without renaming and without a platform-specific archive command:

```text
pwsh -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::ExtractToDirectory('<capture.diagsession>', '<scratch-directory>')"
```

Both paths must be absolute: `ExtractToDirectory` resolves relative paths
against the process working directory, not the shell's. Do not substitute
`Expand-Archive`, which rejects a non-`.zip` extension.

The CPU trace is `*/sc.user*.etl`; `.counters` is JSON metadata and
`metadata.xml` identifies the capture tools. Keep extraction in disposable
scratch space.

## 2. Symbolize

From repository root, run:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/analyze-diagsession/scripts/Invoke-DiagSessionSymbolization.ps1 -EtlPath <etl> -RepositoryRoot <repo> -OutputPath <profile.txt>
```

Use `-SymbolCacheRoot <short-path>` only when `%TEMP%` is unsuitable. The
script scopes `_NT_SYMBOL_PATH` and `_NT_SYMCACHE_PATH` to xperf.

- xperf is expected at the Windows Performance Toolkit path encoded by the
  script. Allow up to 600000 ms on a first symbol-server run.
- Keep the symbol cache path short; deep paths can fail partially with
  `0x80070003`.
- If a first-party module remains `Unknown`, compare the trace PDB GUID/age
  (`xperf -i <etl> -a symcache -dbgid`) with the build PDB.
- Each trace normally contains image rundown for its capture target; analyze
  client and server traces separately.

## 3. Establish build evidence

Module names definitively identify the captured target configuration:
`BrokenEngineSandbox.Debug.exe`, `.Profile`, or unsuffixed Release. Record that
before interpreting samples.

The following symbols are definitive evidence only when present in sampled
code; absence from a flat sample does not prove a flag or feature is disabled:

| Present marker | Evidence |
|---|---|
| `__CheckForDebuggerJustMyCode` | `/JMC` instrumentation; unexpected in current Debug projects |
| `_Iterator_base12` | checked-iterator machinery above level 0 |
| `_Debug_lt_pred` | level-2 checked STL machinery |
| `_RTC_CheckStackVars` | deliberate Debug `/RTC` instrumentation |
| `VkLayer_khronos_validation.dll` | deliberate Debug Vulkan validation activity |

Both `BrokenEngineSandbox` Debug projects pin `_ITERATOR_DEBUG_LEVEL=0` and
`SupportJustMyCode=false`, so `_Iterator_base12` and `__CheckForDebuggerJustMyCode`
frames signal a configuration regression, not expected Debug overhead; a
`std::_Lockit` frame corroborates only when it accompanies `_Iterator_base12` as
the iterator-debug lock.

Treat `std::_Lockit`, walls of tiny `XMVector*`/`std::` leaves, and a hotspot
that disappears in an optimized capture as attribution hints, not configuration
proof. Confirm compiler/project settings and caller context before proposing a
config regression. Debug no-inlining can smear one operation across leaves;
Profile/Release shapes are stronger algorithmic evidence, but disappearance is
still only a sampling observation. `BT_PROFILE` timer-overlay cost is expected.

`MoveSmall4/8`, `memcpy`, `memset`, and `memset_repstos` are CRT helpers rather
than source attribution. Cluster them under inspected callers; a significant
share remains investigable as excess copying, clearing, or data movement.

## 4. Compute per-process shares

Locate host Python first, from repository root:

```text
pwsh -ExecutionPolicy Bypass -File .agents/scripts/Detect-Python.ps1
```

Exit 0 prints `OK <python-exe-path> Python X.Y`; capture `<python-exe-path>`
and invoke the script with it (bare `python` may resolve to nothing). Exit 1
prints `MISSING ...` or `STALE ...` — report that instead of guessing an
interpreter.

```text
"<python-exe-path>" .agents/skills/analyze-diagsession/scripts/profile_shares.py <profile.txt> --process BrokenEngineSandbox [--top N]
```

From PowerShell, prefix the quoted path with the call operator
(`& "<python-exe-path>" ...`); Git Bash runs the line as written.

Weights approximate sampled microseconds. Use each process's own total, not the
global percentage that includes Idle and other processes. xperf profile output
is flat self-weight, so label caller attribution from source inspection as an
inference rather than a measurement. A parser diagnostic and nonzero exit means
the input or process selection must be corrected before interpretation.
