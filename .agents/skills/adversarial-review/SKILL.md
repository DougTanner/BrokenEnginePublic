---
name: adversarial-review
description: >-
  Bounded falsification review of C++ changes made this session. Use
  automatically only for Tier-3 changes or when the correctness review leaves
  one concrete reachable failure hypothesis needing independent falsification.
  ALSO use when the user asks to "attack this change", "assume it's broken",
  "find reasons this fails", or wants an adversarial second opinion on a diff.
  Findings only — never edits. Logic and correctness only; style belongs to
  code-style-review.
allowed-tools: [Read, Write, Grep, Glob, PowerShell]
---

# Adversarial Falsification Review

Try to falsify the change within a bounded scope. Do not optimize for rejection,
verify checklists, assess style, or implement fixes. Automatic invocation is
limited to an approved Tier-3 change or one explicit unresolved reachable
failure hypothesis from the correctness review. User-requested adversarial
review may start from the supplied diff, but the same evidence threshold
applies.

## Inputs (from caller)

Implementation handoff with changed regions, residuals, and focus areas; the
plan document or intent summary; and the approved risk tier and concrete Tier-3
trigger or exact unresolved failure hypothesis. If invoked directly with no
briefing, reconstruct the change set from conversation history.

## Reporting Mode

Return the bounded falsification result inline. It is not a final-evidence
gate.

## Method

Start from each Tier-3 trigger or supplied failure hypothesis and define the
smallest trace that could prove or refute it. Trace outward until the hypothesis
is proven or dies — diff-only reading is insufficient evidence. Read the
callers, callees, data producers/consumers, and sibling paths needed for that
trace, then stop. Do not expand a dying hypothesis into unrelated edge cases or
invent a replacement hypothesis merely to produce a finding. Relevant failure
surfaces include:

- Logic: inverted/off-by-one conditions, early returns skipping required work, order-of-operations
- Integration: callers whose assumptions the change silently violates; semantics changed without every call site following
- Lifetime/ownership: dangling references, use-after-move, pointers into reallocated storage (SOA collections resize)
- Threading: shared state written inside `Dispatch()` lambdas, cross-thread visibility, waits that can now hang
- Determinism: float-op ordering, RNG draw-count changes, phase placement (Update vs PostRender), anything CRC'd
- Edge paths: empty input, first/last tick, count==0/1/max, failed I/O, client-only vs server-only build reachability
- Interactions the implementer wouldn't have tested: the change plus an existing feature, save/load round-trip, cell transfer

Scale trace depth to the authorized risk trigger: a one-line fix in isolated
code does not warrant sweeping every subsystem; a semantics change to a shared
function may require tracing every caller. When all authorized hypotheses die,
return PASS and stop.

## Evidence rule (anti-confabulation)

Every finding must name `file:line` plus a concrete, reachable, in-scope,
material failure scenario: specific input/state → specific wrong outcome
(crash, desync, corruption, wrong value, hang, or failed approved acceptance
criterion). A suspicion that survives tracing becomes a finding; one that
cannot establish reachability, scope, and material impact is dropped, not
softened into a hedge — no "might be risky", no "Consider:" items.
Trust-boundary inputs that cannot reach the changed path, hypothetical polish,
and unrelated baseline defects are not findings. Zero findings is a valid
outcome: state what was falsified and return PASS without further review.

Before reporting a finding, flip stance and try to refute it: look for the alternate explanation, a guard elsewhere, a caller that establishes the precondition, an invariant that makes the bad state unreachable. Only findings that survive the refutation attempt are reported — if not certain the issue is real, don't flag it. Every `file:line` cited must come from a file actually Read during this review, never inferred from the diff, the plan, or memory.

Findings are material defects introduced or newly exposed by this session's
change only. A proven pre-existing or out-of-scope bug noticed while tracing
goes in the residuals footer for possible follow-up routing, not in Findings.
An in-scope structural acceptance failure remains a blocking finding; do not
relabel it as follow-up work. Also excluded: anything the compiler or
clang-tidy catches.

## API Verification

For non-obvious API usage a finding depends on (Vulkan entry points, DirectXMath alignment ops, rarely-used third-party calls), do not pull spec pages into context — emit an entry under `### API Verification Requests` (API/symbol, spec URL, exactly what to confirm, which finding depends on it); the caller resolves via Sonnet WebFetch subagents.

## Output

```
## Adversarial Review Results

### Findings
- file:line — [Critical: | (required)] failure scenario: <input/state → wrong outcome>, and why the code produces it

### API Verification Requests
- <api/symbol> — <spec URL> — <what to confirm, and which finding depends on it>

### Traced Clean
[Only if no findings: authorized hypotheses, what was traced, why each was
falsified, and `PASS — bounded falsification complete; stop.`]
```

Severity: **Critical:** = data loss, broken functionality, determinism break, allocation-tracker violation; no prefix = required fix. Nothing optional — anything below "required" fails the evidence rule and is dropped. Append this final footer in every case:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <pre-existing issue or incomplete review item, or none>
```
