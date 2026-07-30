# Tier 3 Preparation

This route inherits the canonical execution-gate contract
(`execution-gates.md`); Tier 3 risk does not add another approval or
resume gate.

Read this reference only after `/next-plan` has claimed and classified a plan as
Tier 3. It prepares a high-risk change without making its administration the
main task.

## Execution card

The preparation `implementer` writes a short current-state card before
implementation with the root `AGENTS.md` execution-card fields; for Tier 3 the
trigger must be concrete and each acceptance check names its expected
observation.

Never edit the claimed plan file during execution; carry materially stale
source-citation corrections in the execution card instead. Search for
an affected mirrored pattern only when the intended change alters a signature,
identity, semantics, layout, guard scope, or named invariant.

## Plan review

Main dispatches `/plan-audit` to one `reviewer`, then returns its accepted
findings to the preparation `implementer`. That worker performs every repository
read, search, and WorktreeCli validation `/external-grill-plan` requires and
returns an immutable decision brief in that skill's question format, plus its
external claim packets and unresolved residuals; it never interviews, chooses,
or delegates. Main routes claim packets to a `locator` through
`/verify-external-claims`, resumes the same worker with the exact verdicts and
with each answer that unlocks dependent repository-backed work, and receives the
next brief. Main alone interviews the user and adjudicates, recording the exact
questions, answers, decisions, and refinements; it performs no repository read,
search, or WorktreeCli work. The final handoff preserves that record and the
worker updates the card. A decision-complete plan yields a PASS audit and a
no-question brief; do not manufacture findings or interview questions to justify
the review.

## Implementation and stop rule

Required order: terminal preparation -> candidate creation -> reconciliation/single-parent squash -> exact candidate verification -> finalization summary and explicit confirmation -> primary mutation.

Main dispatches the bounded roles for implementation and the normal Tier 3
checks: targeted compile/static checks, one correctness review, bounded
adversarial review, and triggered hygiene. Permit one focused fix/retest. A
second pass requires a reproduced decisive blocker and is limited to invalidated
regions and checks.

At a final-evidence gate, an `implementer` performs terminal Plan preparation,
candidate creation, and reconciliation/single-parent squash before main
dispatches `/verify-changes` to a fresh read-only `reviewer`. The reviewer binds
the acceptance matrix to that exact candidate commit/tree and fixed baseline;
a missing, non-commit, wrong-parent, wrong-tree, or changed-tip candidate is a
blocker, not a reason to verify mutable worktree bytes.

After candidate verification, main dispatches `/finalize-changes` to an
`implementer` and remains in the contract's continuous-execution state while
that worker evaluates the v2 session-audit evidence and prepares the primary
mutation. The only ordinary later stop is the exact primary-mutation
confirmation that main presents and obtains under that contract; after
confirmation, main resumes the worker for the mutation. The mandatory order is
terminal preparation -> candidate creation -> reconciliation/single-parent
squash -> exact candidate verification -> finalization summary and explicit
confirmation -> primary mutation.
