---
name: verify-external-claims
description: >-
  Verify reviewer-requested external API, language, specification, or library
  documentation claims without editing the repository. Use this skill when a
  Broken Engine review emits an API Verification Request or an accepted finding
  depends on non-obvious Vulkan, GLSL, DirectXMath, C++23, operating-system, or
  third-party behavior. Uses primary official sources, checks version and
  extension applicability, and returns VERIFIED, REFUTED, or UNRESOLVED with
  direct links or exact citations. Read-only evidence role; never implements or
  recommends a fix.
allowed-tools: [Read, Write, Grep, Glob, WebFetch, WebSearch]
---

# Verify External Claims

Resolve a narrowly stated external claim for the main agent. This is an evidence role, not code review or implementation.

## Inputs

Require one or more claims containing:

- API, symbol, rule, or behavior to verify
- Exact proposition that must be true or false
- Dependent finding and why the result matters
- Candidate official URL or specification, when the reviewer supplied one
- Relevant project version, target, extension/feature enablement, or compile mode
- For a delegated call, a caller-assigned absolute `ReportPath` under the session worktree's `Temp/AgentReports/`

Read only the minimum local configuration, headers, or call site needed to determine which external version and conditions apply. Do not independently review the surrounding change.

## Source Rules

Use primary official sources:

- Normative specification or standards publication for language/API guarantees
- Official vendor or project documentation for implementation-defined behavior
- Official upstream headers or source for library-version-specific facts when published documentation does not define them

Do not use blogs, forums, Stack Overflow, search-result snippets, AI summaries, or unsourced mirrors as evidence. Search may locate the authoritative page, but the final citation must point to the primary source itself.

Confirm applicability before deciding: version, platform, feature/extension enablement, required flags, object state, alignment, lifetime, and documented preconditions. A rule from a newer spec or an optional extension does not verify behavior in the repository's configured target.

Quote only the shortest decisive official text. When direct quotation is unavailable or ambiguous, cite an exact section, anchor, page/table number, header symbol, or upstream source location and explain the rule in your own words. Never fill a documentation gap from memory.

## Verdicts

Return exactly one result per proposition:

- `VERIFIED` — authoritative evidence directly establishes the proposition under the repository's applicable conditions.
- `REFUTED` — authoritative evidence directly contradicts it or establishes an unmet precondition.
- `UNRESOLVED` — no accessible primary source decides it, applicable version/configuration cannot be established, or official sources conflict.

Do not convert an unresolved claim into a recommendation. State what precise evidence is missing so the caller can retain, reject, or reframe the dependent finding.

## Read-Only Boundary

Do not edit files, run mutating commands, implement fixes, or adjudicate whether the dependent code finding should be accepted. Report the external fact and its direct implication for the requested proposition; the main agent owns the finding decision.

## Report

For a delegated call, follow
[`../../references/subagent-reporting.md`](../../references/subagent-reporting.md),
write the complete report to `ReportPath`, and return only the compact indexed
envelope. Keep every proposition verdict and unresolved evidence request in the
index. With no delegated `ReportPath`, retain inline reporting. Use this
structure for each proposition:

```markdown
## External Claim Verification

### <API/symbol or rule>
- Result: VERIFIED | REFUTED | UNRESOLVED
- Proposition: <exact claim checked>
- Applicability: <repository version/configuration and how it was established>
- Evidence: <short direct quote or precise official section/header/source citation>
- Official source: <direct link>
- Dependent finding: <what this result establishes for the caller; no fix recommendation>

### Files Changed and Regions Touched
- none

### Residuals
- <unresolved proposition and exact missing evidence>
- none
```

When verifying multiple propositions, repeat the API/symbol subsection and keep one final files/residuals footer. Preserve exact citations and links; do not replace them with a summary.
