# Refactor: AgentHarness Command Boundary

## Context
Source: /external-refactor-clean on `Tools/` recursively. `RunSocketCommand` mixes argument parsing, ownership, Winsock lifetime, framed transport, and response interpretation, and several trust-boundary failures currently execute a command with ambiguous input or an unenforced timeout.

## Design

### `Tools/AgentHarness/AgentHarness.cpp` — `RunSocketCommand`
- After the ownership/heartbeat contract in `Architecture_HarnessLockIntegrity.md` and the parser choice in `Architecture_LibraryReplacement.md` are settled, make `RunSocketCommand` consume one validated socket-command argument value, then extract a framed socket-exchange helper with local RAII owners and a response-to-exit-code helper; retain a short coordinator in the existing order. [~1h]
- In the argument value, require full `std::wcstoll` consumption for port/timeout and exactly one request source/value; reject `27100junk`, inline-plus-stdin, and duplicate inline requests through the existing usage/failure path. [~15m]
- Change `ReadAllStandardInput` at lines 35-45 to distinguish EOF from `std::ferror(stdin)` and stop before connecting when a partial read fails. [~15m]
- Check both `setsockopt` results at lines 222-225, report the failed option with `WSAGetLastError`, and fail before sending when the validated timeout could not be installed. [~15m]
- Replace manual `WSAStartup`/`WSACleanup` and raw `SOCKET` cleanup at lines 196-207 and 256-283 with local scoped owners that cover allocation and JSON exceptions. [~30m]

## Critical files
- `Tools/AgentHarness/AgentHarness.cpp`

## Out of scope
- Changing ownership/heartbeat/metadata semantics owned by `Architecture_HarnessLockIntegrity.md`.
- Choosing or importing the CLI parser owned by `Architecture_LibraryReplacement.md`; this plan consumes whichever validated argument value that decision leaves.
- Changing framing, limits, output channels, or exit codes.
- Generalizing AgentHarness socket ownership into ToolCommon.

## Acceptance criteria
- Malformed numeric values, duplicate request values, and mixed stdin/inline sources fail before connection.
- stdin read errors and socket-timeout installation errors produce decisive diagnostics and no request bytes.
- Winsock and socket resources release on every return and exception path.
- Valid inline and piped commands preserve framing, response parsing, and exit codes.

## Notes
- Invariant exposure: AgentHarness command-line and loopback transport contract; no engine CRC, `.pack`, replay format, client/server layout, or allocation-tracked runtime exposure.
- Tier 3 trigger: cross-process harness command transport; execute after `Architecture_HarnessLockIntegrity.md` and after the parser decision in `Architecture_LibraryReplacement.md`.

## Verification Notes
- Removed the original `HarnessLockCommands.cpp` parser item because its exact range is already owned by `Architecture_LibraryReplacement.md`; the surviving work is limited to the distinct socket-command validation, transport, and cleanup boundary.
