---
name: analyze-diagsession
description: >-
  Analyze Visual Studio .diagsession and extracted ETL CPU captures for the
  Broken Engine client or server, reporting per-process hotspots and
  evidence-backed optimization proposals. Use when the user supplies such a
  capture or explicitly asks to analyze one.
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

A dispatch holding a capture must read `references/capture-forensics.md`,
which owns steps 1-4, before analyzing or reporting.

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
template hits. Include memory helpers when their clustered share is meaningful.

Report first:

- capture, target process, and module-proven configuration;
- top per-process shares and clustered causes;
- measured facts versus source-attribution inferences;
- build overhead versus algorithmic/data-movement cost;
- confirmed frame phase and PostRender/CRC exposure;
- expected gain ceiling and actionable plan proposals.

## 7. Route plan proposals

Do not author Plan files directly. Route proven optimization residuals through
`/create-follow-up-plans`, which owns duplicate checks, Plan shape, tracked
metadata, and dependencies; no Plan claim is required.

When a landing gate applies (defined in root `AGENTS.md`), complete
`/verify-changes` and `/finalize-changes`; there is no step that adds a plan row
after the change lands.
