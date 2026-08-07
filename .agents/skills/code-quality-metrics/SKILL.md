---
name: code-quality-metrics
description: >-
  Capture deterministic C++ code-quality metrics for an exact file, directory, or recursive
  repository scope, or compare a listed set of target paths against a full Git baseline.
  Use when a quality snapshot, clone/complexity trend, or review advisory is needed without
  changing source, grading contributors, or automatically prescribing refactors.
allowed-tools: [Read, PowerShell]
disable-model-invocation: false
---

# Code Quality Metrics

Run the public PowerShell entry point from the repository root. Read
[MetricContract.md](references/MetricContract.md) before creating a targets file or consuming a
report; read [Remediation.md](references/Remediation.md) only when explaining advisory results.

## Snapshot

Capture one current scope:

```powershell
pwsh -NoProfile -File .agents/skills/code-quality-metrics/scripts/Invoke-CodeQualityMetrics.ps1 `
  -Mode Snapshot -Target Engine/Source -Scope Recursive -RepositoryRoot (Get-Location).Path
```

Use `Exact` for one file, `Directory` for direct files, and `Recursive` for descendants. `.h` files
are C++ inputs, except a `.h` beneath contiguous `Data/Shaders` components is pure GLSL and rejected
(`ShaderLayouts.h` and `ShaderLayoutsBase.h` stay C++). Treat corpus-only parse omissions as reported
advisory coverage, not failures.

## Compare

Compare only the paths listed in a UTF-8 targets file against a full-SHA baseline:

```powershell
pwsh -NoProfile -File .agents/skills/code-quality-metrics/scripts/Invoke-CodeQualityMetrics.ps1 `
  -Mode Compare -Targets Temp/targets.json -Baseline <full-commit-sha> `
  -RepositoryRoot (Get-Location).Path
```

The analyzer derives pairing itself: a listed path in both corpora pairs with itself, a current-only
path pairs with the first similar baseline-only path as a rename, and the rest become one-sided
add or delete pairs. Do not broaden targets from checkout changes. Context changes remain visible
but suppress target attribution.

## Output and failures

Both modes write canonical compact JSON to stdout and, when requested, the same full report to
`-OutputPath`. Add `-Digest` to emit a compact `broken-engine-code-quality-evidence/v2` summary to
stdout instead of the full report, or `-Phase0Hints` (which implies `-Digest`) to add capped
outlier, clone, complexity, and skip hints for the target paths.

Diagnostics go to stderr; exit `2` means inputs, capture, bootstrap, analyzer, drift, digest, or
output persistence failed. A target parse or signature-extraction failure exits `2` with the
analyzer's one-line JSON diagnostic forwarded verbatim. Read
[MetricContract.md](references/MetricContract.md) `## Failures` for both target-failure contracts,
advisory `upstream-omitted` rows, and the complete-parsing requirement for PASS.

## Interpretation

Report the result as advisory evidence. Name the scope, coverage omissions, suppression reasons, and
comparison cohort before interpreting a delta. Do not turn a metric into a landing gate, person
score, or automatic refactor instruction.
