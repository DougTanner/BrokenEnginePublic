# `/next-plan` Execution-Gate Contract

<!-- next-plan-gate-contract:v1 -->

This is the single source of truth for the user-visible gates from queue claim
through final primary mutation. Route-specific skills may summarize a state,
but must link here rather than define another approval or stop rule.

## 1. Claim and preparation

Queue validation, claim, current-code inspection, citation refresh,
classification, and execution-card preparation do not require approval and do
not create an approval gate. A claim or execution-card summary is never a
substitute for the complete resolved plan.

<!-- next-plan-gate:implementation-approval -->

## 2. Complete-plan implementation approval

Present the complete resolved plan through the immutable presentation artifact,
including its full execution card, finalization mode, scope, invariants, role
dispositions, acceptance criteria, and unresolved decisions. Display every
hash-validated range in order before asking the sole pre-implementation
question. The `/next-plan` finalization mode is always `session-landing`.

A plain affirmative response approves only that exact, most recently displayed
presentation. The user does not need to repeat a digest or token. Persist the
approval receipt before implementation. A changed presentation artifact, plan
bytes, execution card, claim identity, baseline, finalization mode, scope,
invariant, acceptance criterion, or unresolved decision invalidates approval
and requires a new complete presentation and response.

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

<!-- next-plan-gate:receipt-chain -->

### Receipt-chain binding

After `Complete-NextPlan.ps1` succeeds, validate its immutable completion
receipt through `Test-NextPlanReceiptChain.ps1`. That validator follows the
hash-bound completion, approval, and presentation receipts and is the sole
authority for their canonical paths, SHA-256 values, and `finalizationMode`.
Require its PASS result and `session-landing` mode before final-tree verification
or finalization.

The `/verify-changes` acceptance ledger records all three receipt paths and
hashes, the chain-validator PASS result, and the authoritative mode.
`/finalize-changes` independently reruns the validator and takes the mode only
from its result. A missing chain or a caller-supplied mode that disagrees with
the validated mode is a blocker. Routes that did not originate in
`/next-plan` retain their existing finalization-mode inputs.

<!-- next-plan-gate:primary-mutation-confirmation -->

## 4. Exact primary-mutation confirmation

Immediately before the one operation that mutates primary history, render the
current summary bound to the verified manifest and exact primary state. State
that primary has not yet changed, identify the finalization mode, primary
checkout and branch, current primary tip, verified manifest identity, intended
commit or session tip, reconciliation disposition, queue-changing status,
maintenance operation and candidate receipt when applicable, and the exact
remaining mutation.

Ask one direct confirmation question for those exact values. A plain
affirmative response authorizes only that displayed primary mutation. Plan
approval, implementation approval, a request to finish or land, and a
reconciliation decision are not substitutes.

For the `/next-plan` `session-landing`, the remaining mutation advances primary
to the verified session commit. For a separately requested non-`/next-plan`
`primary-commit`, the remaining mutation commits the exact verified manifest in
the primary checkout. A change to the manifest, primary
branch or tip, session branch or tip, intended commit, reconciliation
disposition, queue-changing status, maintenance receipt, scope, residuals, or
verification status invalidates confirmation and requires a refreshed summary
and response.
