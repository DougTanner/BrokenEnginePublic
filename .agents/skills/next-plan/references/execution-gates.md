# `/next-plan` Execution-Gate Contract

<!-- next-plan-gate-contract:v2 -->

This is the single source of truth for the user-visible gates from queue claim
through final primary mutation. Route-specific skills may summarize a state,
but must link here rather than define another approval or stop rule.

## 1. Claim and preparation

Queue validation, claim, current-code inspection, execution-card citation
corrections (the claimed plan file itself is never edited during execution),
classification, and execution-card preparation do not require approval and do
not create an approval gate. Start only when the latest user request explicitly
invokes `/next-plan` or `$next-plan`; an invocation in unrelated history is not
current authority. Once started, approval and blocker-resolution turns continue
the active claimed workflow.

<!-- next-plan-gate:implementation-approval -->

## 2. Complete-plan implementation approval

Present the complete resolved plan, including its full execution card, scope,
invariants, role dispositions, acceptance criteria, and unresolved decisions.
A claim or card summary is never a substitute.

On Codex in Plan Mode, return one complete `proposed_plan` and end the turn;
never enter or exit Plan Mode. A later explicit request to implement that latest
unchanged proposal is approval. On another host with a native plan-approval UI,
present the same complete proposal through that UI. Without either surface, ask
one direct approval question after the presentation.

A plain affirmative response approves only that exact, most recently displayed
presentation, and the transcript records it — no separate approval artifact is
required. A change to the plan bytes, execution card, scope, an invariant, an
acceptance criterion, or an unresolved decision invalidates approval and
requires a new complete presentation and response. A primary advance changes
none of those and never invalidates approval.

<!-- next-plan-gate:continuous-execution -->

## 3. Continuous execution

After implementation approval, continue without another resume request through
implementation, propagation, checks, review, accepted fixes, final verification,
queue completion, reconciliation, and finalization preparation. Queue completion
is not a terminal result. Do not stop at an implementation handoff or a
preparation-ready status.

A material scope or acceptance change returns to state 2. A safety blocker may
stop the workflow, but clearing it resumes the same approved route without
creating a new discretionary approval gate.

### Primary advance

Before claim, recover an advanced primary only from a clean session tree:
rebase onto the primary tip, verify the tree remains clean, re-baseline
`BROKEN_ENGINE_BASELINE`, then claim. After claim, stay at that baseline until
`/finalize-changes`; claim, approval, and completed work survive further primary
advances. An `ok: true` stale-baseline `missing-plan-file` notice is
non-blocking and reconciliation resolves it. A plan-digest mismatch is terminal.

<!-- next-plan-gate:primary-mutation-confirmation -->

## 4. Landing confirmation

### Canonical shared artifacts

A canonical shared artifact is a mutable machine-level resource outside Git
history that a landing replaces or rewrites and live wrapper worktrees consume.
The current exhaustive trigger is AgentTools promotion: a landed diff containing
any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or
`Tools/ToolCommon/` replaces the canonical primary `WorktreeCli.exe` and
`AgentHarness.exe` pair used by linked worktrees.

Ordinary Git landing is not a shared-artifact mutation, regardless of tracked
path. This explicitly excludes source, shader, script, skill, plan, and
documentation changes under `Common/`, `Engine/`, `Projects/`, `.agents/`, and
Markdown-only changes under the AgentTools trees. AgentTools candidate builds
under a worktree's `Temp/`, read-only consumption of primary generated data in
Shared build mode, queue publication, and lock or claim updates are also not
shared-artifact mutations. A workflow that adds another mutation of a canonical
output consumed by live worktrees must add its exact trigger here before using
this gate.

Only when a landing includes a canonical shared-artifact mutation defined above,
require the canonical session ledger to show no live session other than the
current cooperating owner and no maintenance claim before presenting landing
confirmation. Do not apply ledger quiescence to the excluded operations. If the
ledger is not quiescent, release any reconcile or landing lease, retain the
candidate and claims, and tell the user to wait until every other active session
has ended and any maintenance claim has cleared. After the user reports that
they have ended, recheck the ledger; that report clears only the safety blocker
and does not authorize landing. Present the normal landing confirmation only
after the recheck passes.

Immediately before the one operation that mutates primary history, state in one
short summary: what the change is in one sentence, the changed-file count and
kind (code vs docs/plans), the session branch, and the primary branch. Then ask
one direct confirmation question:

- `session-landing`: `Confirm landing this change from <session-branch> onto
  primary branch <primary-branch>?`
- A separately requested non-`/next-plan` `primary-commit`: `Confirm commit of
  this change on primary branch <primary-branch>?`

A plain affirmative response authorizes landing **this session diff onto
primary, wherever the primary tip is** — not one exact commit pair. Plan
approval, implementation approval, a request to finish or land, and a
reconciliation decision are not substitutes for it.

Because the confirmation binds the diff, a primary advance after confirmation
requires nothing from the user: rebase the approved candidate onto the new tip
and proceed to the landing transaction. The queue is machine-local state, never
in the tree, so it can never be a rebase conflict; the queue row publishes after
primary advances. Return to the user for a refreshed summary and response only
when:

- a rebase conflict needs user judgment (the resolution is not mechanical), or
- the session's own bytes change after confirmation — a manual conflict
  resolution, a late fix, or any edit to the verified change.
