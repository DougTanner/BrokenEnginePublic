---
name: analyze-diagsession
description: >-
  Analyze Visual Studio .diagsession and extracted ETL CPU captures for the
  Broken Engine client or server. Extract and symbolize traces, compute
  per-process hotspot shares, distinguish sampled build overhead from
  actionable algorithmic cost, inspect source attribution, and report or route
  evidence-backed optimization plan proposals. Use when the user supplies a
  Visual Studio .diagsession or its CPU ETL trace, or explicitly asks to analyze
  one of those captures.
allowed-tools: [Read, Bash, PowerShell, Grep, Glob, Agent]
---

# Analyze Visual Studio CPU Captures

Deliver a per-process hotspot report and evidence-backed plan proposals. Plan
execution remains in the Change Workflow.

## Roles

- Use deterministic tools for extraction, xperf, share computation, PDB checks,
  and profile-text searches.
- Use `locator` agents for source context: verbatim quotes and file:line only,
  one agent per independent hotspot cluster.
- Use `builder` through `/compile` only when build verification is required.
- Main interprets measurements, confirms source attribution, and reports.

## 1. Extract

`.diagsession` is an OPC/ZIP package. Extract it without renaming or a
platform-specific archive command:

```text
python -m zipfile -e <capture.diagsession> <scratch-directory>
```

The CPU trace is `*/sc.user*.etl`; `.counters` is JSON metadata and
`metadata.xml` identifies the capture tools. Keep extraction in disposable
scratch space.

## 2. Symbolize

From repository root, run:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/analyze-diagsession/scripts/Invoke-DiagSessionSymbolization.ps1 -EtlPath <etl> -RepositoryRoot <repo> -OutputPath <profile.txt>
```

Use `-SymbolCacheRoot <short-path>` only when `%TEMP%` is unsuitable. The
sidecar scopes `_NT_SYMBOL_PATH` and `_NT_SYMCACHE_PATH` to xperf.

- xperf is expected at the Windows Performance Toolkit path encoded by the
  sidecar. Allow up to 600000 ms on a first symbol-server run.
- Keep the symbol cache path short; deep paths can fail partially with
  `0x80070003`.
- If a first-party module remains `Unknown`, compare the trace PDB GUID/age
  (`xperf -i <etl> -a symcache -dbgid`) with the build PDB.
- Each trace normally contains image rundown for its capture target; analyze
  client and server traces separately.

## 3. Establish build evidence

Module names definitively identify the captured target configuration:
`BrokenEngineSandbox.Debug.exe`, `.Profile`, or unsuffixed Release. Record that
before interpreting samples.

The following symbols are definitive evidence only when present in sampled
code; absence from a flat sample does not prove a flag or feature is disabled:

| Present marker | Evidence |
|---|---|
| `__CheckForDebuggerJustMyCode` | `/JMC` instrumentation; unexpected in current Debug projects |
| `_Iterator_base12` | checked-iterator machinery above level 0 |
| `_Debug_lt_pred` | level-2 checked STL machinery |
| `_RTC_CheckStackVars` | deliberate Debug `/RTC` instrumentation |
| `VkLayer_khronos_validation.dll` | deliberate Debug Vulkan validation activity |

Both `BrokenEngineSandbox` Debug projects pin `_ITERATOR_DEBUG_LEVEL=0` and
`SupportJustMyCode=false`, so `_Iterator_base12` and `__CheckForDebuggerJustMyCode`
frames signal a configuration regression, not expected Debug overhead; a
`std::_Lockit` frame corroborates only when it accompanies `_Iterator_base12` as
the iterator-debug lock.

Treat `std::_Lockit`, walls of tiny `XMVector*`/`std::` leaves, and a hotspot
that disappears in an optimized capture as attribution hints, not configuration
proof. Confirm compiler/project settings and caller context before proposing a
config regression. Debug no-inlining can smear one operation across leaves;
Profile/Release shapes are stronger algorithmic evidence, but disappearance is
still only a sampling observation. `BT_PROFILE` timer-overlay cost is expected.

`MoveSmall4/8`, `memcpy`, `memset`, and `memset_repstos` are CRT helpers rather
than source attribution. Cluster them under inspected callers; a significant
share remains investigable as excess copying, clearing, or data movement.

## 4. Compute per-process shares

```text
python .agents/skills/analyze-diagsession/scripts/profile_shares.py <profile.txt> --process BrokenEngineSandbox [--top N]
```

Weights approximate sampled microseconds. Use each process's own total, not the
global percentage that includes Idle and other processes. xperf profile output
is flat self-weight, so label caller attribution from source inspection as an
inference rather than a measurement. A parser diagnostic and nonzero exit means
the input or process selection must be corrected before interpretation.

## 5. Decide what is actionable

Cluster sibling leaves under one cause, especially in Debug:

- Below 1%: never a standalone plan.
- 1–3%: plan only for Effort 1–2 or a shared clustered root cause.
- 3–10%: plan algorithmic/data-layout cost; measured share is the gain ceiling.
- At least 10%: always investigate the root cause, including config-looking or
  memory-helper cost.

Accepted Debug costs (`/RTC`, Vulkan validation) and expected Profile overlay
cost yield no plan. A proven configuration regression may yield a config plan;
Profile/Release findings should target algorithm or data layout.

Capture membership only narrows the source search. Do not infer simulation,
render phase, or determinism from client/server presence or absence. Before
classifying a hotspot or drafting a plan, inspect its call sites and enclosing
frame phase, and confirm whether its inputs or writes can affect PostRender/CRC
state. Only confirmed PostRender exposure triggers the bit-identical constraint
(`same float operations and order`, `/fp:strict`). Record client-only visual or
Interpolate classification only after the same source confirmation.

## 6. Gather source context and report

For each top non-OS/driver cluster, gather full function bodies, call sites with
enclosing loop and frame-phase context, and container/comparator types behind
template hits. Include memory helpers when their clustered share is material.

Report first:

- capture, target process, and module-proven configuration;
- top per-process shares and clustered causes;
- measured facts versus source-attribution inferences;
- build overhead versus algorithmic/data-movement cost;
- confirmed frame phase and PostRender/CRC exposure;
- expected gain ceiling and actionable plan proposals.

## 7. Route plan proposals

Do not author Plan files directly. When the active session holds a live Plan
claim, route proven optimization residuals through `/create-follow-up-plans`,
which owns duplicate checks, Plan shape, tracked metadata, and dependencies.
Without a live claim, report the proposed Plan title, evidence, scope, tier
rationale, invariants, and acceptance checks to the user without creating
tracked Plan files.

When Plans change, complete `/verify-changes` and `/finalize-changes`; no
post-landing row publication exists.
