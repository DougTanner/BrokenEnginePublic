# Delegated Reporting

Delegation normally returns a concise inline handoff for the manager to
adjudicate and route. Do not create a report artifact, request a fresh context,
or forward hashes and line ranges merely to prove that ordinary implementation,
review, build, or documentation work ran.

## Context isolation

Delegation defaults to no inherited conversation context: Codex workers
dispatch with `fork_turns: "none"`, and Claude workers receive a fresh,
self-contained prompt. Use the smallest positive Codex turn fork only when
exact conversation text is authoritative and cannot safely be summarized.

Every core delegation prompt carries these two records:

```text
Delegation basis: mandated <workflow/skill> | task owner <bounded concern>
Context: none — Codex | fresh — Claude | turns <N> — Codex exception: <reason>
```

## Bounded brief

An isolated worker acts only on what its prompt carries, so supply every field:

- role and objective;
- in-scope and out-of-scope work;
- fixed user decisions;
- governing instructions and artifact paths;
- known files, symbols, or regions;
- baseline/tree when material;
- acceptance check;
- return format.

Workers start from the named artifacts. Expand beyond them only for a concrete
dependency, decision, or failure, and verify every supplied hint against the
current tree before relying on it.

A skill needing more enumerates only its own additional fields; it never
restates or contradicts the list above.

## Default handoff

Return only the information the next role needs:

```text
Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: <paths or none>
Decisive checks: <command/read and result>
Build required: <exact targets, or none>
Residuals: <actionable blocker or none>
```

A build the delegation cannot run itself belongs on the `Build required` line
naming exact targets, never reported as a check that passed; the manager or
calling orchestration context dispatches it to one `builder`, then adjudicates
the returned handoff (root `AGENTS.md`, Change Workflow step 4). Skills defining
their own handoff report carry the same line.

Return large evidence by reference: name an existing artifact or log path plus
the selector — line range, search pattern, or key — the next role needs to
recover the relevant part. Create a `Temp/` artifact only when the owning
workflow requires one.

Context follows the isolation default above. Beyond that default, an
independent review, an explicitly requested second opinion, and a focused
correction/retest each require an independent context — one that did not
produce the work under examination.

## Final-evidence gate

Required order: terminal preparation -> candidate creation -> reconciliation/single-parent squash -> exact candidate verification -> finalization summary and explicit confirmation -> primary mutation.

A final-evidence gate (root `AGENTS.md` definition) returns the `/verify-changes`
acceptance table inline — one row per criterion
(`criterion | decisive check | status | evidence`), the Git-derived changed-file
list, and residuals, plus the fixed baseline and exact reconciled candidate
commit and tree. `/finalize-changes` consumes those immutable identities and
must block if the candidate is missing, ceases to resolve to that tree, has the
wrong parent, or (on a session route) is no longer the verified session tip;
the primary route instead requires current primary to remain its verified
parent. There is no report
artifact or agent-built content-identity envelope to forward. Apart from those
candidate identities, intermediate delegates never manufacture compact
envelopes, hashes, IDs, dependency graphs, or evidence locators; citing an
existing artifact or log path with its selector (Default handoff) is not
manufacturing one.

## Liveness and interruption

A wait boundary or elapsed-time threshold alone is never evidence a delegate is
stuck. The manager judges liveness only from host agent status and explicit
progress or partial handoffs; it never inspects raw task transcripts or logs.
Forward progress = newly reported distinct activity, narrowed searches, new
evidence, or in-progress synthesis. A loop = explicitly reported repeated
equivalent operations or unchanged failures without narrowing or new evidence.

Interruption sequence, in order:

- Inspect available host status and explicit progress or partial-handoff evidence.
- Request an immediate return of findings gathered so far (the Default handoff
  format above).
- Allow a bounded response window.
- Interrupt or replace only after terminal failure or documented
  no-progress/loop evidence.

Continuation: prefer resuming the same reviewer when the host supports it.
Otherwise the replacement receives the recovery capsule below and continues from
the interruption point — it never repeats completed repository exploration from
scratch.

Recovery capsule for interrupted or resumed work: objective and scope,
baseline/tree, completed evidence, unresolved issue, and next action. The
manager routes the capsule; workers do not route work to other workers.
