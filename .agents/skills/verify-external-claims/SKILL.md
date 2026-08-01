---
name: verify-external-claims
description: >-
  Verify reviewer-requested external API, language, specification, or library
  claims without editing the repository. Use when a Broken Engine review, plan
  audit, grill, or finding-resolution pass needs an authoritative verdict on
  non-obvious external behavior. Returns one verdict per single checkable
  claim.
allowed-tools: [Read, Grep, Glob, Agent]
---

# Verify External Claims

Resolve requested external facts for the caller to decide. This is a read-only
evidence workflow: never edit, recommend a fix, review surrounding code, or
decide whether a dependent finding or plan choice is accepted.

## External Claim Requests

Require each request to contain:

- a stable claim ID, API/symbol/rule, and one checkable proposition;
- the dependent finding, item, or decision and why the verdict matters;
- applicable project target, version, platform, extensions/features, flags, or
  constraints, plus the smallest relevant repository paths or symbols;
- a proposed official URL or exact upstream identifier when known.

Preserve supplied IDs. Before delegation, assign missing IDs as `VEC-EXT-###`
in input order and split compound propositions into separately suffixed IDs
without changing their meaning. Proposed URLs are discovery hints, not
evidence.

## Delegation

The main session dispatches exactly one self-contained `locator` containing all
external-claim requests, applicable repository instructions, the checkout path, and the
minimum local read/search scope. Do not dispatch one agent per claim or ask the
locator to inspect unrelated code.

Instruct the locator to:

1. Establish repository applicability independently for every claim. Cite
   exact `path:line` evidence for target/version, platform, enabled extensions
   or features, compile flags, and relevant preconditions. Missing applicable
   configuration makes that proposition `UNRESOLVED`.
2. Use the host's official browse/search mechanism to locate primary evidence:
   a normative specification or standard, official vendor/project
   documentation, or official upstream headers/source for version-specific
   facts. Never use memory, search snippets, blogs, forums, AI summaries, or
   unofficial mirrors.
3. Identify each authoritative source by title/project and applicable
   version, revision, tag, or commit. Give the exact section, anchor, page/table,
   symbol, or source location and the shortest decisive quotation or faithful
   rule statement. Add an official immutable link when available.
4. Return exactly one `VERIFIED`, `REFUTED`, or `UNRESOLVED` verdict per stable
   ID. `VERIFIED` requires both an authoritative rule and proven repository
   applicability. `REFUTED` requires completed authoritative evidence that
   contradicts the proposition or proves an unmet precondition. State the
   precise missing evidence for `UNRESOLVED`.
5. Make no repository changes, recommendations, or decisions about findings.

Official upstream headers and locally pinned standards may use an exact
citation without a URL. If the host cannot dispatch the locator, return
`BLOCKED`; do not investigate from memory. If the locator runs but official
browsing, a primary source, or applicability evidence is unavailable, preserve
the affected verdict as `UNRESOLVED`.

## Result Handling

Check that the returned evidence preserves every ID, separates local
applicability from source identity, and directly decides each proposition. Do
not upgrade incomplete evidence. All `VERIFIED` and `REFUTED` results are
completed evidence returned to the caller to decide; a refutation is not
itself permission to dismiss or modify the dependent finding.

Use `PASS` only when every claim is `VERIFIED` or `REFUTED`. Any `UNRESOLVED`
claim makes the report `NEEDS_ACTION`. Use `BLOCKED` only when the required
locator cannot be dispatched or its result cannot be obtained at all.

## Report

Return the complete evidence inline:

```text
Sources:
- <claim ID> — <official source identity and version/revision/tag/commit; exact section/symbol/citation; optional official immutable link | none — exact unavailable evidence>
Decisive checks:
- <claim ID> — applicability: <path:line and target/version/extensions/features/flags | none — exact missing configuration>; rule: <short evidence that settles the question on its own | none — exact missing primary evidence>
Per-proposition verdicts:
- <claim ID> — VERIFIED | REFUTED | UNRESOLVED — <exact proposition> — <direct implication for dependent item, without deciding it>
```

Complete the report with the remaining shared handoff lines
(`../../references/subagent-reporting.md`, `## Handoffs`); this read-only
workflow never changes a file and never requires a build, and each unresolved
claim with its exact missing evidence belongs in `Residuals`. Preserve exact
citations; do not replace evidence with a summary.
