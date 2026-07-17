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
question. On a host that exposes plan mode, ask that question through the
plan-approval UI: after the range display, enter plan mode, write the resolved
plan to the host plan file, and present it; plan-mode approval is the
affirmative response. The `/next-plan` finalization mode is always
`session-landing`.

A plain affirmative response approves only that exact, most recently displayed
presentation. The user does not need to repeat a digest or token. Persist the
approval receipt before implementation. A changed presentation artifact, plan
bytes, execution card, claim identity, baseline, finalization mode, scope,
invariant, acceptance criterion, or unresolved decision invalidates approval
and requires a new complete presentation and response.

Exception: a primary-advance recovery (state 3) changes only the wrapper
baseline and claim receipt, so the regenerated presentation artifact always
differs — it embeds the new baseline and claim-receipt hash. The identity test
is the receipts' content hashes: when the regenerated presentation receipt's
`planSha256` and `executionCardSha256` equal the approved presentation
receipt's values, and the finalization mode, scope, invariants, acceptance
criteria, and unresolved decisions are unchanged, the
prior affirmative response carries forward: display the refreshed ranges,
record the carried approval, and bind it without a new approval question.

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
fresh wrapper. Every post-claim sidecar — presentation, approval, completion,
and the receipt chain — tolerates an advanced primary tip: the session
continues at its wrapper baseline and rebases only during `/finalize-changes`
reconciliation, when its verified commit is ready to land — including the
carried post-approval re-rebase in state 4 — before the primary-mutation
confirmation. When a sidecar still blocks
because primary advanced from the wrapper baseline (normally only at the claim
gate), recover in place without user input: fast-forward or rebase the session
branch onto the current primary tip and resolve conflicts, re-baseline
`BROKEN_ENGINE_BASELINE` to that tip for every subsequent sidecar invocation,
release any held row claim with WorktreeCli `plan row unclaim`, re-claim, and
regenerate the invalidated receipts (approval per the state-2 carry-forward
exception), then continue the approved route.

<!-- next-plan-gate:receipt-chain -->

### Receipt-chain binding

The receipt chain is validated exactly once per queue run, by
`/finalize-changes` at its entry: it runs `Test-NextPlanReceiptChain.ps1` with
the completion receipt path and SHA-256 from the PASS ledger. That validator
follows the hash-bound completion, approval, and presentation receipts and is
the sole authority for their canonical paths, SHA-256 values, and
`finalizationMode`; finalization takes the mode only from its result and
requires `session-landing`.

`/next-plan` retains the completion receipt path/hash after
`Complete-NextPlan.ps1` succeeds, and the `/verify-changes` acceptance ledger
records that receipt identity; neither re-runs the validator. A missing chain
or a caller-supplied mode that disagrees with the validated mode is a blocker.
Routes that did not originate in `/next-plan` retain their existing
finalization-mode inputs and do not require a receipt chain.

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

Exception (`session-landing` only): when the only post-confirmation change is
that primary advanced, continue without a new question or review-tool launch.
Re-reconcile automatically under the same safety checks; when reconciliation
needs no user judgment, any required completed-plan reapply succeeds, and the
regenerated manifest against the new primary parent is identical to the
confirmed manifest, the
landing confirmation carries forward to the re-rebased session tip — rerun
approval preparation without the review tool, adopt its returned tip as the
approved candidate, state the carried confirmation and new tip pair in one
visible line, and proceed to the landing transaction. Manifest identity is
always proven against the confirmed report's rows: when a reapply regenerated
verification, compare the new report's manifest rows to the confirmed
report's rows — a same-tree check against the new report alone proves
nothing. A manifest difference, an uncleared safety check, or a conflict
requiring user judgment still invalidates confirmation and requires a
refreshed summary and response.
