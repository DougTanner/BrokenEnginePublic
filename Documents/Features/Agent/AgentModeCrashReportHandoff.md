# Agent-Mode Crash Report Handoff

## Context

The replay transfer-capture acceptance run on 2026-07-29 crashed the Debug server while exercising the missile fixture. `Engine/Source/CrashReport.cpp::HandleException` saved `Broken-Engine-Sandbox-Server-Crash-Report.txt` only after showing the user a system-modal `MessageBox`, and the active agent did not learn that the file existed until the user reported it. Agent-launched processes are already identified by `gLaunchOptions.iAgentPort != 0` and are intended to run without stealing focus or blocking on user interaction.

This is a new agent-operation capability, so it belongs in `Documents/Features` rather than the executable bugfix scheduler under `Documents/Plans`.

## Design

1. Preserve the current interactive crash-report prompt for ordinary launches. When `gLaunchOptions.iAgentPort != 0`, skip the prompt and always save the crash report automatically.
2. Give each agent-launched client and server deterministic primary and fallback report identities that the harness can derive before launch: the existing per-game roaming-AppData path and the existing fixed-buffer Desktop path used only when the known-folder lookup fails. Keep client and server filenames distinct through `game::kGameName`; baseline both paths before launch.
3. Preserve the crash-path constraints in `Engine/Source/AGENTS.md`: path construction and notification state must use bounded fixed storage and must remain safe when `HandleException` is reached from `SIGABRT` or suspected heap corruption. Do not add allocator-dependent JSON construction, filesystem traversal, or a network round trip to the failing process.
4. Add `.agents/skills/agent-harness/scripts/Invoke-AgentHarnessProcessCheck.ps1` as the surviving launch-health owner. Its launch operation records each exact PID, process role, launch time, both report paths, and each report's pre-launch existence/size/last-write identity in the run evidence directory. Its check operation returns one typed result covering whether the PID is still running, intentional-stop state, transport loss, and whether either report changed after launch.
5. Make the `agent-harness` skill invoke that check after every command, inside every polling iteration, and before/after cleanup. The operator marks a process intentional immediately before sending `quit` or stopping that exact owned PID; every earlier disappearance is unexpected regardless of process exit code, because a handled exception may write a report and still return `0` from `wWinMain`. A transport failure also triggers the check before retry or cleanup.
6. When an unexpected exit has a new report, the next mandatory check inspects both baselines and surfaces whichever report is new in an explicit crash notification containing the process role, report path, assertion/exception headline, and retained harness evidence directory, then stops the active scenario for diagnosis. When no new report exists, it reports an unexpected exit without inventing crash-report evidence. The dying command socket is not a notification channel, and no independent push is promised while no agent harness command or poll is active.
7. Update the project harness documentation so the typed process check and crash discovery are mandatory launch/cleanup evidence. A crash report must be retained, never silently deleted during cleanup, and its presence must turn the active runtime criterion into a failure requiring diagnosis.
8. Fixed harness ports and the harness lock permit only one owned client and server pair, while `game::kGameName` gives those roles distinct filenames. If that exclusivity changes, add a bounded launch-provided discriminator available without heap allocation on the crash path; do not introduce timestamp generation or directory enumeration inside `HandleException` merely to create uniqueness.

This is a Tier 3 integration change because it spans the engine exception path, process-launch lifecycle, and agent workflow, and it must remain correct under heap-corruption and unexpected-process-exit conditions.

## Critical files

- `Engine/Source/CrashReport.cpp` and `CrashReport.h` — agent-mode prompt bypass, deterministic report path, and crash-path write contract.
- `Engine/Source/Main.cpp` and launch-option declarations/parsing — existing `iAgentPort` authority and any bounded launch discriminator.
- `.agents/skills/agent-harness/SKILL.md` — mandatory unexpected-exit discovery, notification, retention, and cleanup contract.
- `Projects/BrokenEngineSandbox/Documents/AgentHarness.md` — project report identities and observable launch behavior.
- `.agents/skills/agent-harness/scripts/Invoke-AgentHarnessProcessCheck.ps1` — typed launch baseline, intentional-stop marker, comparison of whether the process is still running and whether reports changed, and notification evidence.

## Out of scope

- Uploading or emailing crash reports.
- Suppressing crash reports for agent-launched processes.
- Changing interactive non-agent report placement without a separate user decision.
- General telemetry, remote crash collection, or monitoring processes not launched by the active harness session.
- An out-of-band push while no agent harness command or polling loop is active; notification is delivered by the next mandatory process check.
- Refactoring the exception, logging, or DxDiag systems beyond what the silent-save and handoff require.

## Acceptance criteria

- A controlled Debug client and server exception under `--agent-port` shows no modal dialog, saves a complete report automatically, and exits without requiring user input.
- The active harness workflow detects the unexpected owned-process exit on its next mandatory command/poll check, inspects both deterministic report identities, and surfaces the newly written report path and exception headline without the user having to locate or mention the Desktop/AppData file.
- The same handled exception is detected when the process exit code is `0`; an explicitly marked `quit` or exact-PID cleanup exit is not misreported as a crash.
- A normal launch still presents the current Yes/No prompt and retains its existing Desktop-versus-roaming-AppData behavior.
- A stale report from before launch is not announced as a new crash. Concurrent agent client/server launches cannot overwrite or misattribute each other's reports.
- The saved report retains the exception text, callstack, DxDiag, and log buffers, and the crash path satisfies the fixed-buffer/no-new-allocation review contract.
- Debug x64 client and server builds pass, a harness-driven crash scenario proves both silent save and notification, and cleanup leaves no owned process or harness lock while retaining the report as evidence.

## Notes

- The 2026-07-29 motivating report was `C:\Users\dougt\OneDrive\Desktop\Broken-Engine-Sandbox-Server-Crash-Report.txt`; it asserted finite missile velocity in `MissilesPostRender::Update` and was saved at approximately 09:02 local time.
- The current command channel is request/response and terminates with the process. Reliable notification therefore belongs to the surviving harness launch/cleanup side, using a path known before launch, rather than a best-effort push from `HandleException`.
