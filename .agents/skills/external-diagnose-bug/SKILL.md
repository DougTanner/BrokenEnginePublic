---
name: external-diagnose-bug
description: >-
  Find and prove the root cause of a bug or performance regression before any
  fix exists. Use when the user says "diagnose" or "debug this", or reports
  something misbehaving, desyncing, mismatched CRC, or slow. Diagnosis only —
  never fixes, commits, or lands.
allowed-tools: [Read, Grep, Glob, Edit, PowerShell]
---

# Diagnose Bug

Own the front half of a bug: from "something is broken" to a root cause proven
by evidence. `/resolve-findings` and the Change Workflow own the fix; this skill
stops at the handoff.

Adapted from an external MIT-licensed skill; see [LICENSE](LICENSE).

Report progress at each numbered step so the user can re-rank or stop the work.

## 1. Establish a reproducing signal

Do this before forming any theory. The signal is the tightest practical
pass/fail evidence that goes red on *this* bug and green once it is fixed —
normally one agent-runnable command already run at least once, quoted with its
output.

Any of these satisfies the gate:

- a command that drives the actual failing path and shows the exact symptom the
  user described, not a nearby failure;
- close code inspection that shows the bug unambiguously and deterministically;
- supplied logs, a crash dump, or a capture that directly evidences the failure.

Sources to build the command from, in rough order of preference:

1. `/agent-harness` — a scenario driving the sim, UI, or scene queries; its
   replay determinism sequence for desync and CRC mismatch.
2. `get_logs` at a lower log-level threshold (`set_log_level`), so the failing
   transition is visible without unrelated noise.
3. `/compile` — for build, link, and static failures.
4. A differential run: the same input through the old and new build or two
   configurations, diffing the output.
5. `/analyze-diagsession` — for a performance regression, measure a baseline
   before touching anything.

Those skills own their own timeouts and run duration; do not invent a speed
budget for them.

Tighten whatever signal you get: assert on the specific symptom, cut unrelated
setup, and pin sources of variation (seed, tick count, fixed data baseline).

For an intermittent bug, aim for a higher reproduction rate rather than a clean
one — loop the trigger, add load, narrow the timing window. A failure that
appears half the runs is diagnosable; one in a hundred is not.

When no signal can be built, say so, list what you tried, and ask the user for
the environment, a captured artifact, or permission to add temporary
instrumentation. Stop there; do not hypothesize without a signal.

Done when you can name the signal and show it going red.

## 2. Reproduce and minimize

Rerun a runnable signal; recheck static or captured evidence against the same
symptom. Either way, confirm it produces the user's failure mode and capture the
exact symptom text, wrong value, or timing.

Then shrink the scenario one element at a time — inputs, units, cells, config,
steps — re-running after each cut, or narrowing the inspected code path or
captured evidence without losing what proves the failure. Done when removing any
remaining element makes the failure disappear.

## 3. Rank hypotheses

State three to five hypotheses before testing any of them, ranked most to least
likely. Each states its prediction: "if X is the cause, then changing Y removes
the failure."

A hypothesis you cannot state a prediction for is a guess — sharpen or drop it.

Show the ranked list to the user before testing; they often re-rank it
instantly. Proceed with your own ranking if they are away.

## 4. Instrument

Test one variable at a time, each probe tied to a named prediction. Prefer
targeted logs at the boundary that separates two hypotheses over broad logging.
For a performance regression, measure rather than log.

Tag every temporary log with a unique marker, for example `[DEBUG-a4f2]`, so
removal is one search.

Instrumentation is temporary session state. It stays in the diagnosis worktree,
never lands, and is removed before the handoff.

Done when the gathered evidence confirms exactly one hypothesis, or falsifies
the current set — which sends you back to step 3 to rerank.

## 5. Confirm and hand off

Confirm the root cause before reporting it: either close code inspection shows
it unambiguously and deterministically, or the evidence you gathered directly
demonstrates it. "I know what the bug is" is not confirmation. When uncertain,
say so and re-investigate; never fabricate a justification when challenged.

Before reporting, verify:

- every `[DEBUG-...]` log removed (search the marker);
- every throwaway script and scratch file deleted;
- the reproducing signal still goes red — rerun it when it is runnable,
  recheck the inspection or capture when it is not.

Propose the acceptance check that matches the reproducing signal: a harness
scenario or replay determinism check where one applies, otherwise the check that
settles that signal, such as a `/compile` result or a profiling baseline. Do not
add unit tests. When no check can exercise the real failure pattern at its call
site, report that missing verification seam as a residual instead of proposing
architecture work to create one.

The manager takes the handoff into Change Workflow Step 1, which classifies the
fix and routes it to `/resolve-findings` or a plan.

## Handoff

```markdown
Root cause: <one sentence, with file:line>
Evidence: <inspection, command output, or log lines that prove it>
Reproducing signal: <exact command or inspection, and its red result>
Hypotheses ruled out: <hypothesis — the check that killed it>
Proposed acceptance check: <check matching the signal — harness scenario, replay check, compile result, or profiling baseline>
Instrumentation removed: yes — <marker searched> | none added
```

Follow those extension fields with the shared handoff lines
(`../../references/subagent-reporting.md`, `## Handoffs`); this diagnosis-only
workflow reports `Status: DIAGNOSED | BLOCKED` instead of the shared values and
always reports `Changed files: none`, `Build required` names the exact targets
the manager must rebuild or `none`, and an unproven branch or missing
environment belongs in `Residuals`.

Use `DIAGNOSED` only with a confirmed root cause. Use `BLOCKED` when no
reproducing signal could be built, naming what you need.
