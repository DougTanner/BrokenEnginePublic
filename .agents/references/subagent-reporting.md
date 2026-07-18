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
Residuals: <actionable blocker or none>
```

Use a fresh context only for an independent review, an explicitly requested
second opinion, or a focused correction/retest.

## Final-evidence gate

A final-evidence gate (root `AGENTS.md` definition) returns the `/verify-changes`
acceptance table inline — one row per criterion
(`criterion | decisive check | status | evidence`), the changed-file list, and
residuals. There is no report artifact, hash, or manifest range to forward;
`/finalize-changes` consumes the inline result. Intermediate delegates never
produce compact envelopes, hashes, IDs, dependency graphs, or evidence
locators.
