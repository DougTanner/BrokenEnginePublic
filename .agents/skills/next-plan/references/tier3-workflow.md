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
read, search, and WorktreeCli validation required by `/external-grill-plan` and
returns an immutable decision brief containing ordered and conditional
questions; for each question, repository evidence and why the decision changes
the plan, exactly two or three meaningful mutually exclusive choices, the
recommended-first choice and tradeoff, and the exact refinement for every
option; plus external claim packets and exact `/verify-external-claims`
verdicts, and unresolved residuals. Main routes external claim packets to a
`locator` and resumes the same preparation worker with its exact verdicts before
presenting any dependent choice. Main otherwise uses only the brief to
adjudicate and present user choices, records the exact questions, answers,
decisions, and refinements, and performs no repository read, search, or
WorktreeCli work. When an answer unlocks dependent repository-backed work, main
resumes the same preparation worker with the accumulated exact interaction
state and receives the next immutable brief; the worker never interviews,
chooses, or delegates. The final resolved-card handoff preserves every exact
question, answer, decision, refinement, claim verdict, and residual, and the
worker updates the card. Then continue once those decisions are resolved. A
decision-complete plan yields a PASS audit and a no-question brief; do not
manufacture findings or interview questions to justify the review.

## Implementation and stop rule

Main dispatches the bounded roles for implementation and the normal Tier 3
checks: targeted compile/static checks, one correctness review, bounded
adversarial review, and triggered hygiene. Permit one focused fix/retest. A
second pass requires a reproduced decisive blocker and is limited to invalidated
regions and checks.

Main dispatches `/verify-changes` to a fresh read-only `reviewer` only when a
final-evidence gate (root `AGENTS.md` definition) applies.

After verified Tier 3 queue completion, main dispatches `/finalize-changes` to
an `implementer` and remains in the contract's continuous-execution state while
that worker performs reconciliation and primary-mutation preparation. The only
ordinary later stop is the exact primary-mutation confirmation that main
presents and obtains under that contract; after confirmation, main resumes the
worker for the mutation.
