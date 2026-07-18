# `/next-plan` Execution-Gate Contract

<!-- next-plan-gate-contract:v2 -->

This is the single source of truth for the user-visible gates from queue claim
through final primary mutation. Route-specific skills may summarize a state,
but must link here rather than define another approval or stop rule.

## 1. Claim and preparation

Queue validation, claim, current-code inspection, execution-card citation
corrections (the claimed plan file itself is never edited during execution),
classification, and execution-card preparation do not require approval and do
not create an approval gate. A claim or execution-card summary is never a
substitute for the complete resolved plan.

<!-- next-plan-gate:implementation-approval -->

## 2. Complete-plan implementation approval

Present the complete resolved plan, including its full execution card — which
opens with the plain-language what and why summary — scope, invariants, role
dispositions, acceptance criteria, and unresolved decisions. On a host that
exposes plan mode, ask through the plan-approval UI: write the resolved plan to
the host plan file and present it; plan-mode approval is the affirmative
response. Hosts without plan mode ask the one approval question directly.

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

### Primary-advance recovery

The primary branch advancing under an open session is expected
parallel-session behavior, never a reason to abandon the session or request a
fresh wrapper. The session continues at its wrapper baseline and rebases only
during `/finalize-changes` reconciliation, when its verified commit is ready to
land. A primary advance voids nothing: the claim, the approval, and completed
work all survive it.

The one place an advance can block is the claim itself, when primary moved
between wrapper creation and the claim (WorktreeCli requires primary and
session at the same head to claim). Recover in place without user input:
fast-forward the session branch to the current primary tip, re-baseline
`BROKEN_ENGINE_BASELINE` to that tip for subsequent tool invocations, and claim.
The claim no longer requires a clean session tree, so there is no stash step;
`plan-byte-mismatch` (primary plan bytes) is the sole plan-content guard, and
`git-operation-in-progress` still applies.

<!-- next-plan-gate:primary-mutation-confirmation -->

## 4. Landing confirmation

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
