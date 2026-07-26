<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Refactor: AgentHarness Command Boundary

## Context

Source: /external-refactor-clean on `Tools/` recursively. In `Tools/AgentHarness/AgentHarness.cpp`, `toolcli::RunSocketCommand` mixes argument parsing, the mandatory continuous ownership/heartbeat contract, Winsock lifetime, framed transport, and response interpretation in one ~200-line function, and several trust-boundary failures currently execute a command with ambiguous input or leak its transport resources:

- `std::wcstoll(pArgumentValues[i], nullptr, 10)` for `--port` and `--timeout-ms` passes a null end pointer, so `27100junk` parses as `27100` and is accepted.
- The argument loop overwrites `request` on each positional argument and sets `bReadStandardInput` independently, so duplicate inline requests and inline-plus-stdin combinations are silently accepted (last/stdin wins) instead of rejected.
- `ReadAllStandardInput` loops on `std::fread` until it returns 0 and cannot distinguish EOF from a stream error (`std::ferror(stdin)`), so a partial read is sent as a complete request.
- Winsock lifetime is manual: `::WSAStartup` at function start, `::closesocket`/`::WSACleanup` after the `do { ... } while (false)` block. The `nlohmann::json::parse` path and `std::string response(uiResponseLength, '\0')` allocation can throw, and any exception escapes past the manual cleanup, leaking the socket and the WSA reference.

Ordering: this plan executes after the ownership/heartbeat contract in `Architecture_HarnessLockIntegrity.md` (metadata dependency).

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code beyond it. Naming a file grants no permission to touch anything else in that file beyond the named regions plus the mechanical necessities (includes, forward declarations, anonymous-namespace placement) the named change requires.

### In scope — `Tools/AgentHarness/AgentHarness.cpp` only

- `toolcli::RunSocketCommand` (anonymous namespace): restructure into a short coordinator plus extracted helpers and tighten argument validation, as specified in Design.
- `toolcli::ReadAllStandardInput` (anonymous namespace): distinguish EOF from stream error.
- New anonymous-namespace helpers this plan introduces: a framed socket-exchange helper, a response-to-exit-code helper, and local RAII owner types for the WSA lifetime and the `SOCKET`.

### Out of scope

- Mandatory ownership proof, command-thread refresh when the 60-second interval becomes due, final pre-output refresh, and ownership-loss handling are owned by `Architecture_HarnessLockIntegrity.md`; this refactor consumes and preserves that contract without changing its ordering or semantics.
- Choosing or importing a CLI parser; this plan consumes the validated argument value.
- Framing, request/response byte limits (`kuiMaxRequestBytes`, `kuiMaxResponseBytes`), output channels, exit codes (`kiExitOk`, `kiExitStateConflict`, `kiExitFailure`), and the connect-retry-until-deadline loop semantics.
- `PrintUsage`, `SendAll`, `ReceiveAll`, `ConnectWithTimeout`, `wmain`, and everything in `HarnessLockCommands.h`/`.cpp` — unchanged except that extracted helpers may call the existing transport functions.
- Generalizing AgentHarness socket ownership into `Tools/ToolCommon`.

## Design

All work is in `Tools/AgentHarness/AgentHarness.cpp`.

1. **Decompose `RunSocketCommand`.** [~1h] After the ownership/heartbeat contract and the parser choice are settled, make `RunSocketCommand` consume one validated socket-command argument value, then extract:
   - a framed socket-exchange helper covering connect-with-retry, the prerequisite's same-thread readiness waits with absolute logical deadlines, length-prefixed send, and length-prefixed receive (reusing `ConnectWithTimeout`, `SendAll`, `ReceiveAll`);
   - a response-to-exit-code helper wrapping the current `nlohmann::json::parse` / boolean `"ok"` interpretation (`kiExitOk` / `kiExitStateConflict` / missing-field and invalid-JSON failure messages unchanged);
   - a short coordinator in `RunSocketCommand` retaining the existing order: validate arguments, read stdin if requested, enforce `kuiMaxRequestBytes`, then perform the ownership-aware exchange and interpret.
2. **Strict argument validation.** [~15m] In the validated argument value, require full `std::wcstoll` consumption (end pointer at the terminating null) for `--port` and `--timeout-ms`, and exactly one request source with exactly one value: reject `27100junk`-style trailing garbage, an inline request combined with `-` (stdin), and more than one inline request, all through the existing `Fail(...)` + `PrintUsage(std::cerr)` + `kiExitFailure` path. Existing range checks (`1..65535`, `1..600000`) stay.
3. **stdin error detection.** [~15m] Change `ReadAllStandardInput` to distinguish EOF from `std::ferror(stdin)` and surface the error so `RunSocketCommand` fails before connecting when a partial read occurred (no request bytes sent).
4. **RAII Winsock/socket lifetime.** [~30m] Replace the manual `::WSAStartup`/`::WSACleanup` pair and the trailing raw `::closesocket` cleanup with local scoped owner types (anonymous namespace, this file only) so the WSA reference and the `SOCKET` release on every return path and across the allocation/JSON exceptions in the response path.

## Critical files

- `Tools/AgentHarness/AgentHarness.cpp`

## Risk tier

Tier 3 — cross-process harness command transport (AgentHarness command-line and loopback transport contract). Execute after `Architecture_HarnessLockIntegrity.md`.

Invariant exposure: AgentHarness CLI and loopback framing/exit-code contract per `Tools/AgentHarness/AGENTS.md` (length-prefixed JSON framing, request/response limits, connect retry until `--timeout-ms` deadline, `--owner` heartbeat behavior, exit codes 0/2/1) must be preserved. No engine CRC, `.pack`, replay format, client/server layout, or allocation-tracked runtime exposure.

## Acceptance criteria

- Malformed numeric values (trailing garbage), duplicate inline request values, and mixed stdin/inline sources fail before any connection attempt, via the existing usage/failure path.
- stdin read errors produce decisive diagnostics and send no request bytes.
- Winsock and socket resources release on every return and exception path.
- Valid inline and piped commands preserve framing, response parsing, output, and exit codes exactly as before.

## Notes

- Removed the original `HarnessLockCommands.cpp` parser item; the surviving work is limited to the distinct socket-command validation, transport, and cleanup boundary.
