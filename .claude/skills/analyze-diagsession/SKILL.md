---
name: analyze-diagsession
description: Analyzes Visual Studio .diagsession CPU profiling dumps (client/server captures) — extracts the ETW trace, symbolizes with xperf against the build's PDBs, computes per-process hotspot shares, interprets build-config overhead vs real algorithmic cost, and creates scored plan files in Documents/Plans/ for the optimizations found. Use whenever the user provides or mentions .diagsession files, profiling dumps/captures, or asks to examine a profile for hotspots / performance problems — even if they only attach the files and say "look at these".
allowed-tools: [Read, Bash, Grep, Glob, Agent]
---

# Analyze .diagsession CPU Profiles

Deliverable: a hotspot report (per-process shares, interpreted) and plan files in `Documents/Plans/` (with scored `Order.md` rows) for each actionable optimization. Plan *execution* is out of scope — that belongs to the C++ Code Change Process / `/next-plan`.

## Scripts vs subagents (who does what)

Deterministic work goes to scripts/tools, judgment goes to models — per root CLAUDE.md model roles:

- **Deterministic scripts (no subagent)**: extraction, xperf invocations, share computation (`scripts/profile_shares.py`), PDB GUID checks, grepping profile text. Never ask a subagent to parse or summarize what a script parses exactly.
- **Sonnet subagents**: code searches to gather context for hotspot functions — must return verbatim quotes + file:line, never summarize; fan out one agent per hotspot cluster in a single message. Also any build verification via `/compile`.
- **Opus subagent**: writes the plan files + `Order.md` rows (step 6) — new-content authoring role.
- **Main session (Opus)**: interprets the numbers against §5's rubric, decides which hotspots become plans, writes the user-facing report.

## 1. Extract

`.diagsession` is an OPC/ZIP package. Copy to the scratchpad, rename `.zip`, `unzip`. The CPU data is the ETL file (`*/sc.user*.etl`, tens of MB); the `.counters` file is JSON metadata only. `metadata.xml` names the tools used.

## 2. Symbolize with xperf

xperf lives at `C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf`.

```bash
export _NT_SYMBOL_PATH="<repo>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output;srv*C:\Users\<user>\AppData\Local\Temp\symsrv*https://msdl.microsoft.com/download/symbols"
export _NT_SYMCACHE_PATH="C:\Users\<user>\AppData\Local\Temp\symc"
xperf -i sc.user_aux.etl -symbols -tle -tti -a profile -detail -ao out_profile.txt
```

Pitfalls (each cost real time once):
- **`_NT_SYMCACHE_PATH` must be a short path.** A deep scratchpad path fails with `0x80070003` (path-not-found) on longer PDB names while shorter ones succeed — maddeningly partial.
- The MS symbol server entry is what resolves `ntdll`/`ntoskrnl` — without it, OS time is one opaque "Unknown" blob. First run downloads symbols: run in background / timeout 600000.
- If a first-party module stays "Unknown": compare the trace's expected PDB GUID/age (`xperf -i <etl> -a symcache -dbgid`) against the on-disk PDB before suspecting anything else.
- Each trace carries image rundown only for its capture target — the other exe's modules may show unsymbolized. Analyze each dump for its own process.

## 3. Detect the build configuration — it changes everything downstream

The config is embedded in the module names in the trace (`BrokenEngineSandbox.Debug.exe`, `.Profile`, unsuffixed = Release). Confirm before interpreting; a Debug capture and a Profile capture of the same scene have almost disjoint hotspot lists.

**Debug traces** — expect build-machinery costs and separate them from algorithmic cost:

| Marker in profile | Meaning |
|---|---|
| `__CheckForDebuggerJustMyCode` | `/JMC` — should be **absent** (Debug sets `SupportJustMyCode=false`); reappearance = config regression |
| `RtlEnter/LeaveCriticalSection` + `std::_Lockit`, `_Debug_lt_pred`, `_Iterator_base12` | `_ITERATOR_DEBUG_LEVEL=2` machinery — should be mostly **absent** (Debug pins level 1); reappearance = a vcxproj lost the define |
| `_RTC_CheckStackVars` | `/RTC` — deliberate, keep |
| `VkLayer_khronos_validation.dll` | Vulkan validation (`kbVulkanDebugLayers`) — deliberate in Debug, accepted cost; do not plan its removal |
| `MoveSmall4/8`, `memset_repstos` | CRT memcpy/memset helpers, not codebase symbols |
| Walls of tiny `XMVector*`/`std::` self-weight | No inlining in Debug — attribute to their callers via code inspection |

Debug plan recommendations may include config-level fixes; Profile/Release plans must be algorithmic/data-layout.

**Profile/Release traces** — optimized and inlined: leaf costs fold into callers, so self-weight lands on real functions and is directly actionable. None of the Debug markers above should appear (validation layers off outside Debug); if one does, that itself is the finding. `BT_PROFILE` builds also carry the engine CPU/GPU timer overlay — its cost is expected.

## 4. Compute shares

```bash
python .claude/skills/analyze-diagsession/scripts/profile_shares.py out_profile.txt --process BrokenEngineSandbox [--top N]
```

Weights ≈ sampled µs. Judge everything as **share of that process's own total** — the file's global % column includes Idle and other processes. `xperf -a profile` output is **flat self-weight only** (no call trees): state caller attribution as code-inspection inference in the report, not measured fact.

## 5. Interpret — when a hotspot justifies a plan

Judge by share of the process's own total (§4), after clustering sibling leaves under one cause (mandatory in Debug — no inlining smears one algorithm across dozens of tiny frames):

- **< 1%**: never a plan on its own — mention in the report at most.
- **1–3%**: plan only if the fix is Effort 1–2 (Quick Win/Small), or several such hotspots share one root cause — cluster them into a single plan.
- **3–10%**: plan-worthy when the cost is algorithmic/data-layout; state expected gain as the measured share (it is the upper bound).
- **≥ 10%**: always chase to root cause, even when it looks like config overhead — confirm which it is.
- **Config overhead never becomes an algorithmic plan.** Debug-only machinery (§3 table) yields at most a config-regression plan (JMC back on, iterator-debug-level define lost); accepted costs (Vulkan validation, `/RTC`, `BT_PROFILE` overlay) yield none.
- **Cross-capture triangulation**: a hotspot in both client and server captures is sim-side — any optimization must be bit-identical (`/fp:strict`, same float ops, same order) and the plan must say so. Client-only hotspots are render/interpolate-side, free of that constraint.
- **Trust Profile/Release shapes over Debug shapes**: a Debug-only hotspot absent from a Profile capture of the same scene is build machinery, not a finding.

## 6. Gather code context

For each top hotspot cluster (skip OS/driver/CRT), fan out Sonnet search agents in one message: full function bodies, call sites with enclosing loop headers, container/comparator types behind `std::` template hits. Verbatim quotes + file:line only.

## 7. Report, then plans

Report to the user first: per-process table of top shares, config-overhead vs algorithmic split, expected gain per item.

Then dispatch one Opus subagent to write the plan files: it must read `Documents/Plans/CLAUDE.md` (file shape, required `## Out of scope`) and the canonical scoring anchors it links in `Documents/CLAUDE.md`, re-verify every code citation against current source, state invariant exposure per plan (determinism/CRC/`kiVersion` — sim-path optimizations must be **bit-identical**: same float ops, same order, `/fp:strict`), pre-stage grill decisions in `## Notes`, and insert scored `Order.md` rows at sort-correct positions (plus `## File Groups`/`## Dependencies` entries when plans share files). No 'Verification' sections.
