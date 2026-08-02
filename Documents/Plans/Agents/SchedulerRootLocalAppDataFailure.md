<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T15:22:53.682Z","dependsOn":[]} -->
# Scheduler Root LOCALAPPDATA Failure

## Context

Every `WorktreeCli plan` verb derives its machine-local claim storage from `SchedulerRoot` in `Tools/WorktreeCli/PlanScheduler.cpp:205`:

```cpp
return GetLocalApplicationDataPath() / L"BrokenEngineLocks" / L"plan-scheduler" / Utf8ToWide(hash);
```

`common::GetLocalApplicationDataPath` (`Tools/ToolCommon/ToolCliCommon.cpp:333`) returns an empty path when the `LOCALAPPDATA` environment variable is absent or unreadable — `::GetEnvironmentVariableW` returning `0`, or a written length that does not match the queried length. `SchedulerRoot` does not check for that empty result, and `std::filesystem::path{} / L"BrokenEngineLocks"` yields the *relative* path `BrokenEngineLocks\plan-scheduler\<hash>`. The scheduler guard file, claim directory, and every claim record are then created, read, and healed beneath whatever the process working directory happens to be, instead of machine-local storage. Two invocations from different working directories would see disjoint claim state, so a claim taken by one session would be invisible to the next, and `plan claim-next` could hand the same Plan to two sessions.

The repository's other consumer of the same helper already treats the empty result as a failure: `coordination::MakeLocator` (`Tools/ToolCommon/CoordinationStore.cpp:244-248`) returns `std::nullopt` when `localApplicationData.empty()`, which is what makes landing-lock storage fail closed. The scheduler root is the one path that does not.

Reach: the affected `SchedulerRoot` call sites are `RunValidate` (`:498`), `RunList` (`:595`), `RunClaimNext` (`:661`), `RunClaimStatus` (`:771`), `RunUnclaim` (`:803`), and `RunTerminal` for `complete`/`reject` (`:867`) — the whole scheduler surface. It is unreachable from a wrapper session, where Windows always sets `LOCALAPPDATA`; it is reachable from a stripped or sanitized environment (a service account, a scrubbed CI environment block, or a deliberately cleared environment). Documented behavior in `Tools/WorktreeCli/AGENTS.md` states that scheduler claims live under `%LOCALAPPDATA%\BrokenEngineLocks`, so the silent relative fallback contradicts the documented contract as well.

Originating gap: accepted as a real finding by this session's `/repo-code-review`, and classified out of scope because it is pre-existing shared behavior on a Tier-3 surface that every existing verb already executes, while that session's approved boundary was the new read-only `plan list` verb. No live Plan owns this root cause; `plan validate` over the current tree reports `status: valid`, `code: ok`, with no diagnostics or notices.

## Design

Make the scheduler fail explicitly instead of silently relocating its state.

Change `SchedulerRoot` to report the unavailable local application data directory to its callers rather than returning a relative path — the smallest shape that does this is returning `std::optional<std::filesystem::path>` (or an empty path the callers test), mirroring the existing `MakeLocator` convention rather than inventing a second one. Each of the six call sites returns the existing `Failure(...)` helper (`Tools/WorktreeCli/PlanScheduler.cpp:66`) with a new stable code such as `local-app-data-unavailable`, which prints the standard `{"status":"error","code":...}` result and exits with `kiExitFailure` (`1`), the documented OS-failure exit. Do not use exit `2`: this is an environment/OS failure, not a state conflict or negative result.

The check belongs at the trust boundary that already exists — the OS environment read — and adds no defensive validation between our own functions. No claim format, storage layout, hashing, guard, healing rule, or JSON field other than the new error code changes.

Update `Tools/WorktreeCli/AGENTS.md` Coordination State only if the new error code needs stating there alongside the existing exit-code contract.

## Critical files

- `Tools/WorktreeCli/PlanScheduler.cpp` — `SchedulerRoot` (`:205-209`) and its six call sites (`:498`, `:595`, `:661`, `:771`, `:803`, `:867`).
- `Tools/ToolCommon/ToolCliCommon.cpp` — `GetLocalApplicationDataPath` (`:333-349`), read-only: it already reports failure as an empty path and must keep doing so.
- `Tools/ToolCommon/CoordinationStore.cpp` — `MakeLocator` (`:241-255`), read-only reference for the accepted failure convention.
- `Tools/WorktreeCli/AGENTS.md` — Coordination State and exit-code prose, edited only if the new code must be named.

## In scope

- `SchedulerRoot`'s signature and body in `Tools/WorktreeCli/PlanScheduler.cpp`, so an unavailable local application data directory is reported instead of producing a relative path.
- The six `SchedulerRoot` call sites listed above: each converts that failure into the existing `Failure(...)` result with the new error code and exit `1`.
- `Tools/WorktreeCli/AGENTS.md`, only to name the new failure code if the change makes that necessary.

## Out of scope

- `GetLocalApplicationDataPath` itself, and every other consumer of it, including `CoordinationStore` locators and landing locks.
- Claim record schema, claim filenames, hashing, expiry, healing rules, the scheduler guard protocol, and selection order.
- Any other diagnostic, validation, or defensive check in the `plan` verbs.
- Any change to `plan list`, `claim-next`, `complete`, or `reject` behavior beyond the new failure return.
- Adding a fallback location, a `--scheduler-root` option, or any environment-variable override.

## Risk tier and invariants

Tier 3 — shared AgentTools coordination code that every `plan` verb of every session executes, touching machine-local claim storage location; a regression here can block or corrupt other sessions' scheduler state.

Invariants: with `LOCALAPPDATA` set, every `plan` verb produces byte-identical output, exit codes, and on-disk claim state to today's build; no scheduler claim, guard, or lock file is ever created outside `%LOCALAPPDATA%\BrokenEngineLocks`; the failure path takes no scheduler guard and writes nothing.

## Acceptance criteria

- With `LOCALAPPDATA` removed from the environment block, `WorktreeCli plan validate`, `list`, `claim-status`, and `claim-next` each print `{"status":"error","code":"<new code>"}` and exit `1`, and create no `BrokenEngineLocks` directory beneath the working directory.
- With `LOCALAPPDATA` set normally, `plan validate` and `plan list` over the current tree return the same JSON result (`status`, `code`, plan rows and their order) as the pre-change executable.
- A claim taken by `plan claim-next` and then released by `plan unclaim` still round-trips under `%LOCALAPPDATA%\BrokenEngineLocks\plan-scheduler`.
- `.agents/scripts/Test-WorktreeCliPlanScheduler.ps1` passes against the rebuilt executable.

## Notes

Landing changes non-Markdown paths under `Tools/WorktreeCli/`, so this work lands through the landing gate with AgentTools promotion. Building the candidate binary is the subject of `Documents/Plans/Agents/AgentToolsCandidateBuildDocumentation.md`; that Plan is not a prerequisite — it only records the mechanism this work would otherwise rediscover.
