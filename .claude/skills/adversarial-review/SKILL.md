---
name: adversarial-review
description: >-
  Pure adversarial review of C++ changes made this session — assume the change
  is broken and hunt for concrete, provable reasons it does not work. No rubric,
  no checklist: reason from the change itself. Runs as the second reviewer in
  C++ Code Change Process step 4 (Opus subagent), concurrent with and
  independent of /repo-code-review. ALSO use when the user asks to "attack this
  change", "assume it's broken", "find reasons this fails", or wants an
  adversarial second opinion on a diff. Findings only — never edits. Logic and
  correctness only; style belongs to code-style-review.
allowed-tools: [Read, Grep, Glob]
---

# Adversarial Review

Assume the change is broken. The only job: find bugs and concrete reasons the code does not work. Do not verify checklists, do not assess style, do not implement fixes — a separate rubric reviewer (/repo-code-review) runs concurrently in another context; this review exists because a free-stance adversary catches failure modes no rubric enumerates, and two independent passes over the same diff surface different bugs.

## Inputs (from caller)

Changed-file list, touched functions/regions, the plan document or intent summary, and accumulated residuals/focus areas. If invoked directly with no briefing, reconstruct the change set from conversation history.

## Method

Start from the changed regions, then trace outward until each suspicion is proven or dies — diff-only reading is insufficient evidence. Read the callers, callees, data producers/consumers, and sibling code paths the change interacts with. Hunt wherever the change could fail, for example:

- Logic: inverted/off-by-one conditions, early returns skipping required work, order-of-operations
- Integration: callers whose assumptions the change silently violates; semantics changed without every call site following
- Lifetime/ownership: dangling references, use-after-move, pointers into reallocated storage (SOA collections resize)
- Threading: shared state written inside `Dispatch()` lambdas, cross-thread visibility, waits that can now hang
- Determinism: float-op ordering, RNG draw-count changes, phase placement (Update vs PostRender), anything CRC'd
- Edge paths: empty input, first/last tick, count==0/1/max, failed I/O, client-only vs server-only build reachability
- Interactions the implementer wouldn't have tested: the change plus an existing feature, save/load round-trip, cell transfer

Scale trace depth to the change's blast radius: a one-line fix in isolated code doesn't warrant sweeping every subsystem; a semantics change to a shared function warrants tracing every caller.

## Evidence rule (anti-confabulation)

Every finding must name `file:line` plus a concrete failure scenario: specific input/state → specific wrong outcome (crash, desync, corruption, wrong value, hang). A suspicion that survives tracing becomes a finding; one that can't state its failure scenario is dropped, not softened into a hedge — no "might be risky", no "Consider:" items. Zero findings is a valid outcome, but must be earned: state what was traced and why it holds.

Before reporting a finding, flip stance and try to refute it: look for the alternate explanation, a guard elsewhere, a caller that establishes the precondition, an invariant that makes the bad state unreachable. Only findings that survive the refutation attempt are reported — if not certain the issue is real, don't flag it. Every `file:line` cited must come from a file actually Read during this review, never inferred from the diff, the plan, or memory.

Findings are defects introduced or newly exposed by this session's change only. A genuine pre-existing bug noticed while tracing goes in the residuals footer (caller routes it to a follow-up plan), not in Findings. Also excluded: anything the compiler or clang-tidy catches.

## API Verification

For non-obvious API usage a finding depends on (Vulkan entry points, DirectXMath alignment ops, rarely-used third-party calls), do not pull spec pages into context — emit an entry under `### API Verification Requests` (API/symbol, spec URL, exactly what to confirm, which finding depends on it); the caller resolves via Haiku WebFetch subagents.

## Output

```
## Adversarial Review Results

### Findings
- file:line — [Critical: | (required)] failure scenario: <input/state → wrong outcome>, and why the code produces it

### API Verification Requests
- <api/symbol> — <spec URL> — <what to confirm, and which finding depends on it>

### Traced Clean
[Only if no findings: what was traced and why it holds]
```

Severity: **Critical:** = data loss, broken functionality, determinism break, allocation-tracker violation; no prefix = required fix. Nothing optional — anything below "required" fails the evidence rule and is dropped. The caller-required residuals footer (C++ Code Change Process) is appended after this template in every case.
