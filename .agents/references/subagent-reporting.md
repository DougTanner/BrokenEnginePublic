# Delegated Reporting

Delegation normally returns a concise inline handoff. Do not create a report
artifact, request a fresh context, or forward hashes and line ranges merely to
prove that ordinary implementation, review, build, or documentation work ran.

## Default handoff

Return only the information the next role needs:

```text
Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: <paths or none>
Decisive checks: <command/read and result>
Build required: <exact targets the caller must compile, or none>
Residuals: <actionable blocker or none>
```

A build the delegation cannot run itself belongs on the `Build required` line
naming exact targets, never reported as a check that passed; the caller runs it
(root `AGENTS.md`, Change Workflow step 4). Skills defining their own handoff
report carry the same line.

Use a fresh context only for an independent review, an explicitly requested
second opinion, or a focused correction/retest.

## Final-evidence gate

A final-evidence gate (root `AGENTS.md` definition) returns the `/verify-changes`
acceptance table inline — one row per criterion
(`criterion | decisive check | status | evidence`), the changed-file list, and
residuals, plus the fixed baseline and the inline final manifest carrying each
entry's mode and content identity, which `/finalize-changes` recomputes and
compares before it commits. There is no report artifact or manifest range to
forward; `/finalize-changes` consumes the inline result. Apart from that manifest
column, intermediate delegates never produce compact envelopes, hashes, IDs,
dependency graphs, or evidence locators.

## Liveness and interruption

A wait boundary or elapsed-time threshold alone is never evidence a delegate is
stuck. Judge liveness only by observable transcript/status evidence. Forward
progress = recent distinct tool activity, narrowing searches, new evidence, or
in-progress synthesis. A loop = repeated equivalent operations or unchanged
failures without narrowing or new evidence.

Interruption sequence, in order:

- Inspect available status/transcript evidence.
- Request an immediate return of findings gathered so far (the Default handoff
  format above).
- Allow a bounded response window.
- Interrupt or replace only after terminal failure or documented
  no-progress/loop evidence.

Continuation: prefer resuming the same reviewer when the host supports it.
Otherwise the replacement receives the prior findings and decisive evidence and
continues from the interruption point — it never repeats completed repository
exploration from scratch.
