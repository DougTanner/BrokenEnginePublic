# AgentCli - Harness Client and Workflow Coordinator

## Overview

Standalone Windows console application for the agent socket protocol and local development coordination. Use `Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe` in the current checkout under the live wrapper session claim; linked worktrees reach the immutable primary Output through a provisioned directory symlink. Use `/compile` for exclusive primary maintenance.

## Modes

- **Socket** sends length-prefixed JSON to a loopback agent port. Harness commands pass the current owner token so each command atomically refreshes that claim's heartbeat before connecting.
- **Lock** owns PC-global harness and landing claims beneath `%LOCALAPPDATA%\BrokenEngineLocks`. Logical keys are normalized and SHA-256 hashed; transitions serialize through an exclusive guard and update metadata atomically. Landing claims use owner leases (`claim --lease-seconds`, `status`, owner-only `refresh`, guarded expired `recover`, and owner-only `release`).
- **Plan coordination** owns PC-global queue locks and row claims. Claim/steal requires the matching queue lock; unlock/unclaim is owner-only. Queue identity is the Git common directory plus repository-relative `Order.md`; row identity adds the `Order.md`-relative plan path, preserving legacy row-command compatibility.
- **Plan order** owns executable-row parsing, graph validation, deterministic selection, and mechanical mutation through `validate`, `add`, `update`, `claim-next`, and `complete`. It treats Plans and Features as one graph; semantic judgment and plan prose remain agent-owned.
- **Build** serializes writers per target basename within the current worktree, launches MSBuild in a kill-on-close Job Object, and forwards its exit code. Selective builds query evaluated project items and `IntDir`, then invalidate only the requested `.cpp` objects.

## Invariants

- Exit code `0` is success, `2` is a valid state conflict or negative socket response, and `1` is usage, transport, or OS failure.
- `harness` locks use `--key`; `landing` locks use the canonicalized `--repo` path. Release is conditional on the recorded owner token.
- Landing lease duration is 60–86,400 seconds. Status is `live`, `expired`, or fail-closed `unverifiable`; malformed records remain held/unverifiable. Expired recovery is conditional on the expected owner and lease state.
- Plan locators reject rooted paths and parent traversal, normalize case and separators, and fail closed on mismatched stored repository, order, or plan metadata. Queue acquisition validates the complete row-claim snapshot and fails closed on unreadable, malformed, misplaced, or cross-queue records.
- `--repo` identifies the Git common directory; `--primary-worktree` must be its registered primary attached to `--branch`; and `--worktree` must be a registered checkout in that repository. `validate` checks the named tree's queue content, but dirty, in-progress, and unverifiable Git-authority diagnostics apply only to the registered primary; session validation is content-only. `claim-next` reads clean primary and proves the session current and byte-matched. Mutations target the named session unless it is the authorized primary.
- Plans and Features default to their `Documents/*/Order.md` paths; normalized repository-relative overrides are accepted. Executable columns are `Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes`. Repository-relative dependencies block while their rows exist and must resolve without duplicates, self-edges, or cycles.
- `validate` covers both queues with deterministic path/line/code diagnostics. `add|update --request <Temp repo-relative JSON>` accepts schema-version `1` input beneath session `Temp/`. Add entries carry row data and dependencies; grouped sequences gain immediate-predecessor edges. Updates carry expected plan/row hashes, opaque staged content, and replacement row data.
- `plan order claim-next --repo ... --primary-worktree ... --worktree ... --branch ... --owner ... --session ... --queue plans|features [--plan ...]` locks both queues in canonical order, requires matching commits and selected-plan bytes, creates the global row claim, and never edits repository files. Claim metadata and locators use the legacy `Order.md`-relative identity; the public result returns the selected row's canonical repository-relative plan identity. Automatic selection skips blocked rows; explicit selection reports exact blockers.
- `plan order complete --repo ... --worktree ... --owner ... --session ... --plan ... [--reapply]` requires the owned row claim, removes the session row/file and inbound dependency edges transactionally, and leaves the row claim held until verified landing. Reconciliation-time `--reapply` is idempotent for an already-absent target and emits a receipt for the reconciled bytes.
- Queue mutations lock Plans and Features in canonical path order and release them in reverse. Release revalidates queue metadata and owner under guard; an unproven release retains and reports the exact lock. Queue-changing landing holds landing before both queues across the final identity check and ref advance, preventing mixed-primary selection. Handled pre-commit failures restore exact originals; committed mutations preserve untouched accepted row bytes, and post-commit unlock failure retains the complete validated post-state.
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
