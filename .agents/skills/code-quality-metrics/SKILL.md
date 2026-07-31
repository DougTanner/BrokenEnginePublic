---
name: code-quality-metrics
description: >-
  Capture deterministic C++ code-quality metrics for an exact file, directory, or recursive
  repository scope, or compare an authorized target manifest against a full Git baseline.
  Use when a quality snapshot, clone/complexity trend, or review advisory is needed without
  changing source, grading contributors, or automatically prescribing refactors.
allowed-tools: [Read, PowerShell]
disable-model-invocation: false
---

# Code Quality Metrics

Run the public PowerShell entry point from the repository root. Read
[MetricContract.md](references/MetricContract.md) before creating or consuming a target manifest;
read [Remediation.md](references/Remediation.md) only when explaining advisory results.

## Snapshot

Capture one current scope:

```powershell
pwsh -NoProfile -File .agents/skills/code-quality-metrics/scripts/Invoke-CodeQualityMetrics.ps1 `
  -Mode Snapshot -Target Engine/Source -Scope Recursive -RepositoryRoot (Get-Location).Path
```

Use `Exact` for one file, `Directory` for direct files, and `Recursive` for descendants. The
default profile is `BrokenEngineExtended`; it classifies `.h` files beneath contiguous `Data/Shaders`
components as GLSL, except the dual-language `ShaderLayouts.h` and `ShaderLayoutsBase.h`, and its fixed
normalizer writes a parser-only, byte-preserving capture for the limited Broken Engine C++ extensions while real C++ remains untouched. Use
`StrictUpstream` only when `.h` inputs must remain unsupported; it parses raw capture bytes. Treat
corpus-only parse omissions as reported advisory coverage, not failures.

## Compare

Compare only identities explicitly authorized by a UTF-8 target manifest:

```powershell
pwsh -NoProfile -File .agents/skills/code-quality-metrics/scripts/Invoke-CodeQualityMetrics.ps1 `
  -Mode Compare -TargetManifest Temp/targets.json -Baseline <full-commit-sha> `
  -RepositoryRoot (Get-Location).Path
```

Do not broaden targets from checkout changes. Context changes remain visible but suppress target
attribution. The command writes canonical compact JSON to stdout and, when requested, an identical
`-OutputPath` file. It logs diagnostics to stderr; exit `2` means inputs, capture, bootstrap,
analyzer, drift, or output persistence failed. A target dispatch-parse failure instead emits one
compact `target-parse-failure` object: apply the listed narrow parser-only sanitizer spot-fix and
rerun Compare before interpreting metrics. A target signature-extraction failure is separate:
investigate it and rerun; do not treat it as a sanitizer instruction. Neither target failure has
success JSON or an output file. A Compare result cannot support PASS until every authorized target
has complete parsing and signature extraction; corpus-only `upstream-omitted` rows stay advisory.

Compare authenticates the provisioned analyzer internally, archives it into a fresh ignored scratch directory,
and imports that copied source. Its gitlink pin is only an archive selector, not a metric, cache, or
report identity. The report schema is
`broken-engine-code-quality-metrics/v2`; its `tool` object is
`{adapterVersion,lockSha256,python,disableSg}` with `adapterVersion` set to `"4"`.

## Interpretation

Report the result as advisory evidence. Name the profile, scope, coverage omissions, suppression
reasons, and comparison cohort before interpreting a delta. Do not turn a metric into a landing gate,
person score, or automatic refactor instruction.
