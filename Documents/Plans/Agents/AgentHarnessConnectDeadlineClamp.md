<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:50:38.621Z","dependsOn":[]} -->
# Fix: AgentHarness connect phase can overrun the --timeout-ms deadline

## Context

`Tools/AgentHarness/AGENTS.md` requires the connect phase to retry until the `--timeout-ms` deadline and requires each connect, send, and receive phase to bound no-progress waiting by its absolute deadline. The connect-retry loop in `ExchangeFramedSocketCommand` (`Tools/AgentHarness/AgentHarness.cpp:479-524`) violates the bound in two places:

- Every attempt passes the fixed `kiConnectAttemptTimeoutMilliseconds` (500 ms, `AgentHarness.cpp:28`) to `ConnectWithTimeout` (`AgentHarness.cpp:499`) regardless of how little time remains before the absolute deadline computed at `AgentHarness.cpp:475`.
- The retry sleep (`AgentHarness.cpp:514-523`) is capped by the next heartbeat due time only, never by the remaining deadline, so it can sleep the full 150 ms (`kiConnectRetrySleepMilliseconds`, `AgentHarness.cpp:29`) when less remains.

A failed connect can therefore return up to roughly 650 ms after the caller's `--timeout-ms` budget. Reachability of the 500 ms component was externally verified (deep-analysis claim ECVR-1) against official documentation: Microsoft Learn's `connect` (https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect, Remarks) and `select` (https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select, Remarks) pages give no immediacy guarantee for nonblocking connect failure signaling ("When the success or failure outcome becomes known, it may be reported in one of two ways"), and Windows' default initial TCP retransmission timeout is 3 seconds applied to the connection request SYN (TCPInitialRtt default 0xBB8, https://learn.microsoft.com/en-us/troubleshoot/windows-server/networking/modify-tcp-ip-maximum-retransmission-time-out), so a locally unanswered SYN consumes the whole 500 ms `select` window instead of failing immediately. The 150 ms sleep overshoot follows directly from the code.

Root cause: neither wait derives its duration from the remaining time before the existing absolute deadline. Found by `/external-deep-analysis` over `Tools/AgentHarness` (Directory scope); pre-existing debt outside the boundary of `Documents/Plans/Agents/AgentToolsDeepAnalysis.md`, whose in-scope work is behavior-preserving decomposition and duplication removal only.

## Design

Inside `ExchangeFramedSocketCommand`'s connect-retry loop only:

- Immediately before the `ConnectWithTimeout` call, compute the remaining milliseconds until the loop's existing `deadline` and pass `min(kiConnectAttemptTimeoutMilliseconds, remaining)` (the loop already guarantees remaining > 0 at that point via the preceding deadline checks).
- Immediately before the retry sleep, additionally cap `retrySleep` by the same remaining-until-`deadline` duration, keeping the existing next-heartbeat cap and the existing `count() > 0` guard.

No other timing, control flow, diagnostics, or constants change. `ConnectWithTimeout` itself is unchanged — it already accepts the timeout as a parameter.

## Critical files

- `Tools/AgentHarness/AgentHarness.cpp` — `ExchangeFramedSocketCommand` connect-retry loop.

## In scope

- `ExchangeFramedSocketCommand` in `Tools/AgentHarness/AgentHarness.cpp`: the connect-attempt timeout argument and the retry-sleep duration calculation within the connect-retry loop (current lines 479-524).

## Out of scope

- Any change to command names, arguments, exit codes, JSON envelope bytes, or emitted diagnostic text.
- `ConnectWithTimeout`, `SendAll`, `ReceiveAll`, `WaitForSocketReadiness`, heartbeat cadence, and every lock command.
- Decomposition or duplication work owned by `Documents/Plans/Agents/AgentToolsDeepAnalysis.md`.

## Risk tier and invariants

Change Workflow Tier 2 — scoped runtime behavior of one tool executable; no wire format, envelope, exit-code, or cross-subsystem surface changes. Invariants: exit codes 0/2/1 and all diagnostic text unchanged; connect still retried until the deadline (never a single attempt); heartbeat refresh cadence unchanged.

## Acceptance criteria

- The diff is decisive for the attempt-timeout clamp: the `ConnectWithTimeout` timeout argument is derived immediately before the call as the minimum of `kiConnectAttemptTimeoutMilliseconds` and the positive remaining time before the loop's existing `deadline`, and the retry-sleep duration carries the same remaining-time cap alongside the existing next-heartbeat cap.
- Observable scenario for the sleep clamp: with no listener on the target port and a `--timeout-ms` value below 500, AgentHarness reports the connect failure and exits no later than the deadline plus scheduling noise; the existing "connect to 127.0.0.1 failed or timed out after N ms" diagnostic's elapsed value evidences the bound.
- AgentHarness builds clean through `/compile`.
- Diagnostic text, exit codes, and envelope bytes are byte-identical for unchanged paths.

## Notes

The retry-sleep cap and the attempt-timeout cap are one root cause (missing remaining-time derivation) with one verification scenario, so they land as one plan.
