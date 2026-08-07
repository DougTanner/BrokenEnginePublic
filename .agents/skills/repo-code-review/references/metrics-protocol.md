# Metrics Protocol

Operating rules for the `## Workflow` step 1 Compare run in
[../SKILL.md](../SKILL.md).

## Running Compare

Pass the supplied targets file, the session baseline as a fixed full SHA, and the
absolute checkout root, and request the digest: `-Mode Compare -Targets
<supplied-targets-file> -Baseline <fixed-full-sha> -RepositoryRoot
<absolute-checkout-root> -Digest`. Omit `-OutputPath` so this review retains no
file.

Record the digest's `profile`, `targetSelection`, `coverage`, and `comparison`
evidence. Never reconstruct the digest's field selection or summarization
inline.

This findings-only review permits no changes except the entry point's validated
ignored `Temp/CodeQualityMetrics` cache and capture writes; do not write a
targets file, output file, source, or repository metadata.

Record `comparison.contextChanges` without widening the review scope.

## Failures

Every failure exits `2` with diagnostics on stderr and no digest on stdout. An
operational failure leaves the review incomplete with `NEEDS_ACTION`, not a
correctness finding.

## Advisory status and target failures

Metrics remain advisory. A regression or classification never becomes a finding
or changes a clean review to non-PASS. Only independent source inspection may
promote a substantial new near-copy under the duplication rule in
[../SKILL.md](../SKILL.md), or another reachable correctness violation. Changes
that quietly weaken the code's structure are advisory follow-up evidence.

A target failure arrives as one JSON line on stderr; classify it by that line's
`code` field. A `target-parse-failure` makes the review incomplete with
`NEEDS_ACTION`, not a finding: request a separate authorized implementer to apply the listed narrow
sanitizer spot-fix, then rerun Compare in focused review. A
`target-signature-extraction-failure` is also incomplete; investigate it and
rerun, without treating it as a sanitizer instruction. Do not return PASS until
every authorized target has complete parsing and signature extraction. A
corpus-only `upstream-omitted` row — a corpus file the analyzer's parser left
out — is an advisory residual and preserves PASS. Report coverage and the
outcome from `comparison`.
