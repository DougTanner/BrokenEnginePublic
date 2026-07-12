# Extract Landing Lock Lifecycle Support

## Context

`Tools/AgentCli/LockCommands.cpp` is 910 lines after landing-lock lifecycle hardening. The file now owns both generic lock command dispatch and the landing-only implementation: `LandingLease`, UTC lease validation/classification, Git subprocess capture, and `AllRegisteredWorktreesClear`. Step-4 review and both step-10 session audits identified this as a natural extraction boundary; the current implementation is verified correct, so this is structural debt rather than a lifecycle correctness fix.

## Design

Extract the landing-only lease metadata validation, status classification, and registered-worktree Git-operation inspection into a focused AgentCli source/header pair. Keep `RunLockCommand` responsible for parsing commands, acquiring the transition guard, enforcing ownership, and selecting atomic record transitions; expose only the narrow data and operations it needs from the extracted module. Preserve the existing fail-closed semantics and schema-3 validation rules exactly.

## Critical files

- `Tools/AgentCli/LockCommands.cpp` — `LandingLease`, `ValidateLandingLease`, `LandingStatus`, `AllRegisteredWorktreesClear`, and their landing-only helpers; reduce the generic dispatcher to orchestration.
- `Tools/AgentCli/LockCommands.h` — retain the public `RunLockCommand` interface; avoid exposing landing internals to other AgentCli callers.
- `Tools/AgentCli/Platforms/VisualStudio2026/AgentCli.vcxproj` and `.vcxproj.filters` — register the extracted source/header pair with matching filters.

## Out of scope

- Changing lock metadata schemas, lease duration rules, ownership checks, recovery policy, or command-line behavior.
- Changing Git-operation marker coverage or worktree discovery semantics.
- Refactoring unrelated generic lock storage, hashing, parsing, or transition-guard code.
- Adding unit tests.

## Acceptance criteria

- Landing lease validation/classification and all-worktree inspection have one focused implementation outside `LockCommands.cpp`; generic command dispatch remains in `RunLockCommand`.
- `LockCommands.cpp` is materially reduced without duplicating metadata parsing, timestamp arithmetic, Git invocation, or marker checks.
- Existing AgentCli landing-lock command behavior remains byte-for-byte compatible at the JSON/exit-code boundary for claim, status, refresh, recover, release, and legacy steal cases.
- AgentCli Release/x64 builds and installs through `/compile`; the existing temporary two-worktree lifecycle matrix passes without behavior changes.

## Notes

- Tooling-only refactor: no determinism/CRC, replay, wire protocol, `kiVersion`/`.pack`, client/server guard, shader, or allocation-tracked runtime exposure.
- New files require `/update-vcxproj` verification for `AgentCli.vcxproj` and `.filters`.
- Preserve argument-safe Git process creation and fail-closed handling of malformed metadata, missing/prunable/unresolvable worktrees, and active Git-operation markers.
