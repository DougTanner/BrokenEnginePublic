# Tier 3 Preparation

This route inherits the [canonical execution-gate
contract](execution-gates.md); Tier 3 risk does not add another approval or
resume gate.

Read this reference only after `/next-plan` has claimed and classified a plan as
Tier 3. It prepares a high-risk change without making its administration the
main task.

## Execution card

Write a short current-state card before implementation:

- goal and explicit out-of-scope boundary;
- concrete Tier 3 trigger;
- changed interfaces or invariants;
- acceptance checks and expected observations;
- required roles and conditional roles.

Refresh stale source citations only where the plan depends on them. Search for
an affected mirrored pattern only when the intended change alters a signature,
identity, semantics, layout, guard scope, or named invariant.

## Conditional plan review

Run `/plan-audit` followed by `/external-grill-plan` only when the execution
card leaves a material scope, architecture, or acceptance decision unresolved.
Use their inline findings to update the card, then continue once those decisions
are resolved. When the plan is already decision-complete, use the execution card
as the implementation contract; do not manufacture an audit or interview.

## Implementation and stop rule

Implementation follows the normal Tier 3 checks: targeted
compile/static checks, one correctness review, bounded adversarial review, and
triggered hygiene. Permit one focused fix/retest. A second pass requires a
reproduced decisive blocker and is limited to invalidated regions and checks.

Use `/verify-changes` only when queue mutation, reconciliation, or landing
needs the final immutable acceptance ledger.

After verified Tier 3 queue completion, invoke `/finalize-changes` and remain in
the contract's continuous-execution state through reconciliation and
primary-mutation preparation. The only ordinary later stop is the exact
primary-mutation confirmation defined by that contract.
