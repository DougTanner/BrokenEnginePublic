# Tier 3 Preparation

This route inherits the [canonical execution-gate
contract](execution-gates.md); Tier 3 risk does not add another approval or
resume gate.

Read this reference only after `/next-plan` has claimed and classified a plan as
Tier 3. It prepares a high-risk change without making its administration the
main task.

## Execution card

Write a short current-state card before implementation with the root
`AGENTS.md` execution-card fields; for Tier 3 the trigger must be concrete and
each acceptance check names its expected observation.

Never edit the claimed plan file during execution; carry materially stale
source-citation corrections in the execution card instead. Search for
an affected mirrored pattern only when the intended change alters a signature,
identity, semantics, layout, guard scope, or named invariant.

## Plan review

Run `/plan-audit` in a subagent, then `/external-grill-plan` in the main
session with its accepted findings. Use their inline findings to update the
card, then continue once those decisions are resolved. A decision-complete
plan yields a PASS audit and a no-question grill; do not manufacture findings
or interview questions to justify the review.

## Implementation and stop rule

Implementation follows the normal Tier 3 checks: targeted
compile/static checks, one correctness review, bounded adversarial review, and
triggered hygiene. Permit one focused fix/retest. A second pass requires a
reproduced decisive blocker and is limited to invalidated regions and checks.

Use `/verify-changes` only when a final-evidence gate (root `AGENTS.md`
definition) applies.

After verified Tier 3 queue completion, invoke `/finalize-changes` and remain in
the contract's continuous-execution state through reconciliation and
primary-mutation preparation. The only ordinary later stop is the exact
primary-mutation confirmation defined by that contract.
