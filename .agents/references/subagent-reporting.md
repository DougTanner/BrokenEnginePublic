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

Each final-evidence gate (root `AGENTS.md` definition) uses one verifier-owned
immutable report under `%LOCALAPPDATA%/BrokenEngine/AgentReports/<repository-hash>/`.
`/verify-changes` allocates the path through `New-AgentReportPath.ps1` and calls
[`Write-AgentVerificationReport.ps1`](../scripts/Write-AgentVerificationReport.ps1)
once with the finalized ledger and receipt lines. The writer computes the
manifest, writes the report, reads it back, and returns its lowercase SHA-256
and exact manifest range. It contains:

```text
Schema: be-agent-report/v1
Worktree: <canonical path>
Baseline: <fixed commit>
Plan/intent: <approved plan or concise task>

## Acceptance ledger
- <criterion or invariant> | <decisive check> | PASS | <result>

## Final manifest
<canonical changed-file entries>

## Queue receipts and residuals
<relevant WorktreeCli receipts and unresolved blockers, or none>
```

The writer owns the `## Final manifest` heading and its raw
`path<TAB>blob-or-DELETED` rows; never wrap those rows in Markdown, regenerate
them inline, or calculate their line range manually. `/finalize-changes` may
consume the returned range through
[`Read-AgentReportSection.ps1`](../scripts/Read-AgentReportSection.ps1), using
that one global report path and hash. Intermediate delegates never produce compact
envelopes, hashes, IDs, dependency graphs, or evidence locators.
