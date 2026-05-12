# Refactor: Capture Gaea Output for Post-Mortem

Source: post-audit follow-up from the island bake/export pipeline refactor.

## Context

When `Gaea.Swarm.exe` exits non-zero, the bake error path reports only the exit code and instructs the user to "re-run interactively to diagnose." This is a poor story in CI / batch contexts and forces a manual second run on the developer's machine to recover the same Gaea console output that the failing run already produced — but discarded.

The existing `common::RunExecutableInNewConsole` helper exists because the alternative (piped capture) trips an `IOException` inside Gaea at startup; Gaea checks for a real console for stdin/stdout/stderr. So we cannot simply switch to a captured-output variant — the new-console invocation is load-bearing. The fix has to preserve "Gaea sees a real console" while teeing the console's output to a log file.

## Background

- `DataPacker/Source/BakeIslandIntermediates.cpp:305-308` — comment block explaining the IOException-on-piped-IO constraint that motivates the new-console helper.
- `DataPacker/Source/BakeIslandIntermediates.cpp:319` — `common::RunExecutableInNewConsole(rGaeaExecutable, commandLine)` call site.
- `DataPacker/Source/BakeIslandIntermediates.cpp:321-324` — current failure path: throws `std::runtime_error` with exit code and the "re-run interactively" hint, nothing else.
- `Common/` — investigate where `RunExecutableInNewConsole` lives; mirror its location for any sibling helper.
- `gpFileManager->mTempDirectory` — the existing temp directory used for atomic asset writes; appropriate landing zone for Gaea log files.

## Proposed Approach

Tee the console output to a log file in `gpFileManager->mTempDirectory` named `gaea-<island>-<timestamp>.log` while preserving the real-console attach.

Investigate two implementation shapes — pick whichever validates against the IOException constraint:

1. **Wrap Gaea invocation in `cmd.exe /c "<gaea> ... > <log> 2>&1"`.** Spawn `cmd.exe` via the existing new-console helper (so Gaea's *parent* is `cmd.exe`, which has a real console attached). Pro: minimal new code, no Win32 API expansion. Con: must verify Gaea still sees a real console when launched from a `cmd /c`-redirected parent — the redirection `> log` reassigns the child's stdout/stderr handles, which is exactly what triggered the original IOException. If Gaea passes its IOException check against `GetStdHandle(STD_OUTPUT_HANDLE)` returning a console handle (not a file handle), this approach fails. Probe before committing.

2. **Add a sibling helper to `RunExecutableInNewConsole` that creates the new console, then redirects the new console's screen buffer to a file via `WriteConsoleOutput` polling or by injecting a console-output hook.** Pro: keeps the real console attached. Con: substantial Win32 surface; likely a multi-day investigation.

If option 1 fails the IOException probe, defer to a less ambitious fallback: log the *commandLine* string to a file alongside the existing exit-code message, so the developer can paste-and-run interactively without retyping. Still beats the status quo.

Once a working capture path lands:

- Construct the log path early in `BakeOne` so the failure-path message can cite it: `std::filesystem::path gaeaLogFile = gpFileManager->mTempDirectory / std::format(L"gaea-{}-{}.log", rIslandFolder.stem().wstring(), <timestamp>);`
- Update the `runtime_error` at line 321-324 to include the log file path: `"...exited with code {}; output captured to \"{}\""`.
- On bake success, optionally delete the log (or leave for diagnostics — Gaea logs are small and the temp dir is wiped between runs anyway). Default: leave.

Name interfaces: change site is the `BakeOne` body in `BakeIslandIntermediates.cpp` around the `RunExecutableInNewConsole` call and the subsequent error-construction `runtime_error`. The new sibling helper (if option 2 lands) belongs in `Common/` next to `RunExecutableInNewConsole`.

## Out of scope

- Streaming Gaea output to the DataPacker's own log in real time — that requires inter-process pipe semantics that conflict with Gaea's IOException constraint. File-tee is the pragmatic compromise.
- Parsing Gaea's output text to surface specific error messages in the throw — the user wants the log file path, not heuristic message extraction.
- Generalizing the capture beyond Gaea — DataPacker only shells out to a small fixed set of executables, and only Gaea has the new-console requirement.
- Adding a CLI flag to opt out of capture — always on; the log overhead is trivial relative to the bake.

## Acceptance criteria

- A failing Gaea bake prints a log file path in the `runtime_error` message that, when opened, contains Gaea's stdout/stderr from the failed run.
- A successful Gaea bake either leaves a log file or cleans it up — pick one in implementation; document the choice in the failure-message wording so it's not misleading.
- `RunExecutableInNewConsole` itself is not regressed for non-Gaea callers (verify by inspecting other call sites in `DataPacker/` if any).

## Notes

The comment at `BakeIslandIntermediates.cpp:305-308` is load-bearing context — preserve it (or update it to reflect the new capture-via-tee mechanism) when the implementation lands.

If the option-1 probe succeeds (Gaea tolerates `cmd /c > log`), this becomes a Quick Win — single call-site change plus a one-line message update. If it requires option 2, escalate to Medium and reassess scope.
