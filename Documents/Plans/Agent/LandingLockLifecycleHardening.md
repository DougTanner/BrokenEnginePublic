# Landing Lock Lifecycle Hardening

## Context

AgentCli landing locks can survive an interrupted finalization indefinitely and then block unrelated sessions. Two stale landing claims have been observed, including `CollisionSemanticsEdges`; its recorded process was already dead. The current record is no longer held, so this plan addresses the reproducible lifecycle defect rather than that individual claim.

The root cause is in `agentcli::NewMetadata` (`Tools/AgentCli/LockCommands.cpp:344-357`): `claimantPid` is `GetCurrentProcessId()` from the short-lived `AgentCli.exe lock claim` invocation, which exits immediately and therefore cannot represent the session that owns the lock. `RunLockCommand` rejects every existing record on `claim` (`:468-483`) and deletes it only through an owner-matched explicit `release` (`:485-515`). The only refresh path is `RefreshHarnessHeartbeat` (`:518-541`); landing claims never refresh `heartbeatAt`. The finalization workflow acquires the landing claim before reconciliation and deliberately leaves it held on several blockers (`.agents/skills/finalize-changes/SKILL.md:33-38`), so an interrupted or abandoned session deterministically leaves persistent coordination state with no trustworthy liveness signal.

Acceptance gap: landing serialization must continue to prevent concurrent Git integration, but an abandoned session must expire predictably and be recoverable only after proving that no registered worktree is mid-Git-operation.

## Design

- Use an owner-token lease, not process identity. For landing-domain `claim`, require `--owner`, `--session`, `--worktree`, and `--lease-seconds`; accept 60 through 86,400 seconds. Write schema 2 metadata containing `leaseDurationSeconds`, `heartbeatAt`, and `expiresAt = heartbeatAt + leaseDurationSeconds`; retain the existing owner token and descriptive fields. Do not use `claimantPid` for landing liveness.
- Add `lock refresh --domain landing --repo <git-common-dir> --owner <token>`. Under the existing transition guard, require the exact owner and a schema-2 landing record, then atomically set `heartbeatAt` to current UTC and `expiresAt` using the recorded lease duration. Refresh never changes the owner, session, worktree, or duration.
- Make landing `status` report `leaseState` as exactly `live`, `expired`, or `unverifiable`, plus `heartbeatAt`, `expiresAt`, and `leaseDurationSeconds` when valid. `live` means current UTC is strictly before the parsed deadline; `expired` means it is at or after the deadline. Missing legacy fields, invalid ranges/timestamps, unreadable metadata, or a clock value earlier than the recorded heartbeat are `unverifiable`, never expired. Preserve the current `held` and ownership fields for callers.
- Add an atomic `lock recover --domain landing --repo <git-common-dir> --expect <expired-owner> --owner <new-token> --session <label> --worktree <path> --lease-seconds <duration>`. Recovery is allowed only when the guarded record still matches `--expect`, is schema-2 `expired`, and every registered worktree is proven free of an in-progress Git operation. On success, replace the record directly with the new schema-2 lease; do not create an unlocked interval. `claim` continues to reject every existing record, including expired ones.
- For recovery, enumerate every entry from `git worktree list --porcelain` for the repository. For each registered worktree, resolve through that worktree's Git paths and refuse recovery if any of `MERGE_HEAD`, `rebase-merge`, `rebase-apply`, `CHERRY_PICK_HEAD`, `REVERT_HEAD`, `BISECT_LOG`, or `sequencer` exists. Failure to enumerate worktrees, resolve any path, inspect any marker, or parse metadata makes recovery fail as unverifiable. Re-read and compare the owner, schema, heartbeat, deadline, and lease duration under the transition guard immediately before replacement so a concurrent refresh or owner transition wins safely.
- Preserve backward compatibility: read and print schema-1 records, but classify landing records without valid lease fields as `unverifiable`; they remain releasable by their owner and conditionally stealable through the existing explicit-approval `steal` path. Restrict `lock steal --domain landing` to schema-1/legacy records. Every schema-2 landing record rejects `steal`, whether live or expired and regardless of approval; schema-2 takeover exists only through `recover` and its expiry plus all-worktree Git-operation gates. Harness and plan lock metadata/commands keep their current behavior. Newly created landing records use schema 2.
- Tighten `.agents/skills/finalize-changes/SKILL.md`: finish user sign-off, primary/worktree identity, cleanliness, manifest, and other read-only preflight before claiming; use a 3,600-second lease and acquire immediately before the first session rebase. Refresh before and after each rebase, review/build/verification or fix phase, at least every 15 minutes while waiting on active work, immediately before primary-side landing, immediately after it, and before release. On every stop/cancel/failure, enumerate all registered worktrees with the same marker set: release when none is active and ownership is still exact; retain and report the claim when any marker exists or inspection is unverifiable.
- Update AgentCli usage and `Tools/AgentCli/AGENTS.md` for the schema-2 lease, exact CLI verbs, status states, all-worktree Git-operation gate, and legacy behavior.

## Critical files

- `Tools/AgentCli/LockCommands.cpp` — metadata versioning, `RunLockCommand`, owner-token lease refresh/status/recovery, guarded transition, and all-worktree Git-operation classification.
- `Tools/AgentCli/LockCommands.h` and `Tools/AgentCli/AgentCli.cpp` — public command/refresh dispatch if the revised CLI requires it.
- `.agents/skills/finalize-changes/SKILL.md` — landing claim timing, heartbeat cadence, stale recovery, and safe-path release policy.
- `Tools/AgentCli/AGENTS.md` — authoritative lock lifecycle and CLI invariants.

## Out of scope

- Automatically stealing live plan or harness locks.
- Changing lease duration after claim or broadly deleting `%LOCALAPPDATA%\BrokenEngineLocks`.
- Recovering, aborting, or completing an in-progress Git operation on behalf of another session.
- Changing Git history integration rules, branch policy, or landing verification requirements.
- Adding a long-running lock daemon or network coordination service.

## Acceptance criteria

- A schema-2 landing claim reports `live` before its deadline and `expired` at/after it; owner refresh extends the deadline by the recorded duration without changing any ownership field. Invalid or legacy lease metadata reports `unverifiable` and cannot use automatic recovery.
- Finalization uses the 3,600-second lease and refresh points above, so a continuously operating session remains live while an abandoned claim becomes expired without PID or process-host assumptions.
- `recover` atomically replaces only the expected expired record. It refuses a live lease, an unverifiable/legacy lease, changed owner or heartbeat metadata, and inspection failure without modifying the record.
- Recovery is refused when any registered worktree has `MERGE_HEAD`, `rebase-merge`, `rebase-apply`, `CHERRY_PICK_HEAD`, `REVERT_HEAD`, `BISECT_LOG`, or `sequencer`, including when the active operation is in a linked worktree other than the recorded claimant worktree; it succeeds only after all registered worktrees are clear.
- Landing `steal` refuses without mutation for a live schema-2 record, an expired schema-2 record while any worktree has an active Git operation, and an expired schema-2 record after every worktree is clear. Exercise the expired-clear refusal both without takeover approval inputs and with explicit approval/expected-owner inputs; neither path may bypass `recover`. An explicitly approved, owner-matched schema-1 conditional steal remains compatible.
- Safe finalization blockers and cancellations release their landing claim; blockers encountered during an active/uncertain Git operation retain it and report why.
- Concurrent claim/release/refresh/recovery attempts remain serialized and cannot replace a newly changed owner record.
- Build and install AgentCli through `/compile`; exercise claim/status/refresh/release/recover/steal against a temporary repository with at least two registered worktrees. Verify live, deadline expiry, refresh extension, legacy/unverifiable metadata, owner mismatch, concurrent refresh-vs-recover, every enumerated Git marker, an operation active only in the second worktree, path/enumeration failure, live schema-2 steal refusal, expired-clear schema-2 steal refusal both without and with explicit takeover approval/expected-owner inputs, expired-active-operation schema-2 steal refusal, schema-1 approved conditional steal compatibility, successful clear-worktree recovery, and finalization safe-stop release/retain behavior. Do not add unit tests.

## Notes

- Developer tooling only: no runtime client/server code, deterministic simulation/CRC, wire protocol, replay, `kiVersion`, `.pack`, shader, client/server guard, or allocation-tracked exposure.
- Lock metadata schema changes must read existing AgentCli-v2/schema-1 records safely. Legacy landing records are unverifiable, not automatically expired; owner release and explicitly approved conditional steal remain available.
- This plan overlaps the finalization workflow and `Tools/AgentCli/LockCommands.*`; reconcile any concurrent edits to those surfaces before landing. The active loopback-firewall session does not edit these files.
