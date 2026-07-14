# AgentCli - Harness Client and Workflow Coordinator

## Overview

Standalone Windows console application for the agent socket protocol and local development coordination. Use `Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe` in the current checkout under the live wrapper-held session claim. Linked worktrees reach the immutable primary Output through the provisioned whole-directory symlink. Use the `/compile` skill for exclusive primary-maintenance commands.

## Modes

- **Socket** sends length-prefixed JSON to a loopback agent port. Harness commands pass the current owner token so each command atomically refreshes that claim's heartbeat before connecting.
- **Lock** owns PC-global harness and landing claims beneath `%LOCALAPPDATA%\BrokenEngineLocks`. Logical keys are normalized and SHA-256 hashed; transitions serialize through an exclusive guard and update metadata atomically. Landing claims use owner leases (`claim --lease-seconds`, `status`, owner-only `refresh`, guarded expired `recover`, and owner-only `release`).
- **Plan** owns dedicated PC-global plan queue and row coordination. Queue commands are `lock`, read-only `list`, `status`, conditional `steal`, and owner-only `unlock`; a successful lock returns the authoritative sorted row-claim snapshot. Row commands are `claim`, `status`, conditional `steal`, and owner-only `unclaim`; claim and steal require ownership of the matching queue lock. Queue identity is canonical repository plus normalized repo-relative Order.md path; row identity adds the normalized Order.md-relative plan path.
- **Build** serializes writers per target basename within the current worktree, launches MSBuild in a kill-on-close Job Object, and forwards its exit code. Selective builds query evaluated project items and `IntDir`, then invalidate only the requested `.cpp` objects.

## Invariants

- Exit code `0` is success, `2` is a valid state conflict or negative socket response, and `1` is usage, transport, or OS failure.
- `harness` locks use `--key`; `landing` locks use the canonicalized `--repo` path. Release is conditional on the recorded owner token.
- Landing lease duration is 60–86,400 seconds. Status is `live`, `expired`, or fail-closed `unverifiable`; malformed records remain held/unverifiable. Expired recovery is conditional on the expected owner and lease state.
- Plan queue and row locators reject rooted paths and parent traversal, normalize case and separators, and fail closed when stored repository, order, or plan metadata does not match the requested locator. Queue snapshots fail closed if any row record is unreadable, malformed, misplaced, or scoped to another queue.
- Expired landing recovery requires the expected owner and atomically replaces the record only after every registered non-bare, non-prunable worktree is inspectable and clear of `MERGE_HEAD`, `rebase-merge`, `rebase-apply`, `CHERRY_PICK_HEAD`, `REVERT_HEAD`, `BISECT_LOG`, and `sequencer`. This is a fail-closed snapshot, not exclusion against Git operations starting afterward.
- Socket protocol framing and response limits must stay compatible with the client/server agent endpoints.
- Keep all new source and header files in both the AgentCli project and filters file.
- `Platforms/VisualStudio2026/Output` is immutable primary-checkout output shared into linked worktrees as a whole-directory symlink. Routine worktrees consume its prebuilt `AgentCli.exe`; they never build AgentCli or write through the link.
- Worktree wrappers may build Release/x64 in the primary checkout only when `AgentCli.exe` is absent, covering first run on a new clone; an existing executable with an invalid command surface fails closed. Other AgentCli source or build changes require the explicitly authorized `/compile` primary-maintenance workflow, which rebuilds and validates the primary Output directly with MSBuild before routine worktrees consume it.
- `.agents/scripts/AgentCliSessionExclusion.psm1` serializes a versioned, atomically replaced per-repository ledger under `%LOCALAPPDATA%\BrokenEngineLocks`. Concurrent wrapper sessions exclude primary maintenance; maintenance excludes new sessions. Both directions wait at most 660 seconds and report owner/session/worktree evidence. Stale recovery is PID-and-start-time checked and malformed or unverifiable state fails closed.
- `Get-AgentCliExclusionStatus` reads validated live session and maintenance owner/label/worktree evidence under the same bounded mutex without acquiring a claim.
- The shared session host and direct maintenance create children suspended and atomically associated with a kill-on-close Job Object, then resume and propagate exit status. Provisioning validates the inherited wrapper owner or holds a transient session claim for its complete direct invocation.

## See Also

- [Root AGENTS.md](../../AGENTS.md) - Landing workflow and harness command families
- [`agent-harness` skill](../../.claude/skills/agent-harness/SKILL.md) - Ownership, launch, command, and shutdown workflow
- [`compile` skill](../../.claude/skills/compile/SKILL.md) - Bootstrap and supported build invocations
