<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Architecture: CLI Parser Library Replacement — Worthwhileness Investigation

## Context

Source: `/external-architecture-review` on `Tools/` recursively. Both AgentTools (AgentHarness, WorktreeCli) hand-write usage-text generation, argument parsing, and verb dispatch across six files, and the pattern deepens with every added verb. The review proposed replacing that mechanical surface with CLI11 (single header, BSD-3-Clause).

That proposal was never validated. It is not obviously worthwhile: the tools' command-line contracts are exacting, and CLI11's defaults violate several of them out of the box, so an adoption is mostly suppression-and-wrapping code. Wrapper code that replaces parsing code is not a win. Nobody has measured either side.

This Plan is that measurement. **It decides whether the replacement is worth doing; it does not do it.** No library is vendored, no `Tools/` source changes, no project membership moves. Adoption, if the verdict is ADOPT, is a separate Plan gated on the user's explicit ThirdParty dependency approval per `ThirdParty/AGENTS.md` ("Adding a library requires explicit user approval and license review"). No such approval exists today, and this investigation does not need one — it imports nothing into the tree.

Three live Plans already defer to this decision and consume its outcome: `ReducePlanScheduler.md`, `Refactor_AgentHarnessCommandBoundary.md` (which states it "executes after the CLI-parser decision"), and `Refactor_BuildCommandDecomposition.md`. Resolving the decision either way unblocks them.

## Design

Four steps, in order. Each later step runs only if the prior one passes; a failure at any step is a REJECT verdict and the investigation stops there — do not continue gathering evidence for a decision already made.

**Step 1 — Baseline measurement.** Measure every candidate region named under **Candidate regions** with `.agents/scripts/Measure-Tokens.ps1` using explicit `-StartLine`/`-EndLine` ranges. Record each region and the sum. Gate: sum **≥ 500 `bt-token-v1`**. Below that, no replacement can clear the floor from `/external-architecture-review`'s own criterion, and the verdict is REJECT.

**Step 2 — Contract adjudication.** For each hard contract below, determine from upstream CLI11 documentation and source whether it can be preserved, and name the exact CLI11 configuration that preserves it. A contract that can only be preserved "probably", or by changing an accepted command line, output channel, or exit code, fails.

1. Exit codes `0` success / `2` state-conflict or negative result / `1` usage, transport, or OS failure — unchanged for every verb, including CLI11's own parse failures, which must route through the existing `Fail` / `FailWindows` / `FailBuild` helpers.
2. Stdout vs. stderr placement and the existing `PrintUsage` text — CLI11's built-in auto-print-and-exit for `--help` and parse errors must be fully suppressed.
3. AgentHarness's positional JSON request stays opaque: a leading `-` or `--` inside `"<json>"` is not interpreted as an option, and the `-` stdin sentinel still works.
4. WorktreeCli `build`'s `--files <cpp…> --` passthrough: the `--` terminator ends selected-file collection; the target and every trailing token reach MSBuild verbatim.
5. BuildCommand emits exactly one `broken-engine-build-result/v1` JSON object on stdout — CLI11 writes nothing to stdout, ever.

**Step 3 — Out-of-tree spike.** Obtain the upstream CLI11 single header **into the scratchpad directory, never into the repository tree**. Port one representative command as a throwaway spike: `HarnessLockCommands.cpp`'s `ParseLocator` option loop plus the `RunHarnessLockCommand` verb dispatch and per-verb required-option validation — self-contained, and it exercises required-option validation, verb dispatch, and the exit-code contract together. Measure the spike's replacement code (CLI11 setup, suppression, error routing, and the validation glue) with `Measure-Tokens.ps1`. Extrapolate to the full candidate set by the per-region ratio and record both the spike number and the extrapolation.

If the upstream header cannot be obtained, report the investigation UNRESOLVED with the reason. Do not guess wrapper cost, and do not substitute a different library.

**Step 4 — Build-cost measurement.** Compile the spike translation unit PCH-less with the `ToolCliCommon.h` include set, with and without the CLI11 include, three runs each. Record the wall-clock delta per translation unit. CLI11 is a single ~11k-line header consumed by both tools through a shared header, so this cost is paid by every AgentTools translation unit.

**Verdict rule — pre-committed, all four required for ADOPT:**

- Step 1 baseline ≥ 500 `bt-token-v1`.
- All five contracts in Step 2 preserved, each with its named CLI11 configuration.
- Projected net removal (Step 1 baseline − Step 3 extrapolated replacement cost) ≥ 500 `bt-token-v1`.
- Step 4 per-translation-unit compile delta ≤ 20%.

Any failure → REJECT. Do not re-weigh a failed criterion against a passing one; the rule is conjunctive by construction, because the whole point is that a marginal win is not worth a permanent third-party dependency.

**Terminal outcomes.** Exactly one:

- **ADOPT** — author `Documents/Plans/Tools/CliParserLibraryAdoption.md` through `/create-follow-up-plans`, carrying the measured baseline, the proven per-contract CLI11 configuration from Step 2, the spike's measured wrapper cost, and the build delta. That Plan names the user's ThirdParty dependency approval as an explicit precondition and carries the migration scope (the candidate regions, `ThirdParty/CLI11/`, `ToolCliCommon.h`, and both tool `.vcxproj` + `.filters` via `/update-vcxproj`). This investigation is then complete.
- **REJECT** — record the verdict and the failing criterion with its measurement in the change report, and strike the now-resolved ordering language at the nine cross-reference sites listed under **In scope** so no Plan keeps waiting on a decision that has been made. The hand-written parsing stays. This investigation is then complete.

## Scope contract

The listed scope is both target and ceiling: gather exactly the evidence the verdict rule consumes, record the verdict, and stop. Add no further analysis, no alternative-library evaluation, no prototype beyond the single spike command, and no cleanup of adjacent code encountered. Naming a file below grants permission only to touch the named regions.

### In scope

- **Measurement and analysis only** against the **Candidate regions** below. These files are read-only evidence throughout this Plan — no `Tools/` source is edited under any verdict.
- Scratchpad-only spike: the upstream CLI11 header and the throwaway ported `ParseLocator` / `RunHarnessLockCommand` translation unit. Never written into the repository tree.
- `Documents/Plans/Tools/Architecture_LibraryReplacement.md` (this file) — completed and deleted at terminal state per the scheduler's completion path, whichever verdict lands.
- On **ADOPT** only: new `Documents/Plans/Tools/CliParserLibraryAdoption.md` via `/create-follow-up-plans`.
- On **REJECT** only: strike the ordering/deferral language naming this Plan at exactly these sites, leaving each Plan's own scope untouched — `ReducePlanScheduler.md:50` and `:94`; `Refactor_AgentHarnessCommandBoundary.md:14`, `:29`, `:53`, `:66`; `Refactor_BuildCommandDecomposition.md:20` and `:52`.

### Out of scope

- Vendoring CLI11, or any library, under `ThirdParty/` — including "just to test". The spike lives in the scratchpad.
- Editing any file under `Tools/`, either tool's `.vcxproj` / `.filters`, or `Tools/ToolCommon/ToolCliCommon.h`.
- Evaluating a library other than CLI11, or designing a first-party parsing abstraction. A REJECT verdict rejects CLI11-shaped adoption at the measured cost; it is not a finding that no library could ever help, and it authorizes no replacement work of its own.
- Performing the migration under any verdict, including a partial or "obviously safe" subset.
- Moving domain validation (ownership, lease bounds, required-option checks, key/repo normalization) anywhere; the investigation only measures, and the adoption Plan keeps that validation in the command bodies.

## Candidate regions

Read-only evidence. Measure these; do not edit them.

- `Tools/AgentHarness/AgentHarness.cpp` — the anonymous-namespace `PrintUsage(std::ostream&)`; the argument-parsing loop and range/emptiness validation inside `RunSocketCommand` (the `for` over `pArgumentValues` handling `--port`, `--timeout-ms`, `--owner`, the `-` stdin sentinel, and the positional JSON request); and the `wmain` mode dispatch (`--help`, `lock` routing, else `RunSocketCommand`).
- `Tools/AgentHarness/HarnessLockCommands.cpp` — the anonymous-namespace `ParseLocator` option loop (`--key`, `--owner`, `--expect`, `--session`, `--worktree`) and the verb dispatch + per-verb required-option validation at the top of `RunHarnessLockCommand` (`token|claim|status|release|steal|heartbeat`). **Also the Step 3 spike subject.**
- `Tools/WorktreeCli/WorktreeCli.cpp` — the anonymous-namespace `PrintUsage(std::ostream&)` and the `wmain` mode dispatch (`--help`, `lock` including the `lock token` fast path, `plan`, `build`).
- `Tools/WorktreeCli/LandingLockCommands.cpp` — the anonymous-namespace `ParseLocator` option loop (`--repo`, `--owner`, `--expect`, `--session`, `--worktree`, `--lease-seconds`) and the verb dispatch + per-verb required-option / lease validation at the top of `RunLandingLockCommand` (`claim|status|refresh|recover|release|steal`).
- `Tools/WorktreeCli/PlanScheduler.cpp` — the anonymous-namespace `ParseArguments` option loop (all `--repo`/`--worktree`/`--primary-worktree`/`--branch`/`--owner`/`--session`/`--plan`/`--baseline`/`--new-baseline`/`--claim-receipt`/`--claim-receipt-sha256`/`--terminal-receipt`/`--terminal-receipt-sha256`/`--write-claim-receipt`/`--landed-commit` value options plus the `--user-authorized-rejection` flag) and the operation dispatch in `RunPlanSchedulerCommand` (`validate|claim-next|claim-status|unclaim|prepare-completion|prepare-rejection|release-after-landing|reparent-claims`).
- `Tools/WorktreeCli/BuildCommand.cpp` — inside `RunBuildCommandUnguarded`, the `--files <cpp…> --` scan, target extraction, and trailing-MSBuild-argument collection.
- `Tools/ToolCommon/ToolCliCommon.h` — the PCH-less shared include set the spike's build-cost measurement reproduces (`#include "tinygltf/json.hpp"` is the consumption precedent an adoption would mirror).

## Risk tier and invariants

**Tier 1.** Investigation and planning artifacts only: no tracked source, shader, project-membership, or ThirdParty change under any verdict, so no runtime, build, or coordination surface is exposed. Invariants to hold:

- The repository tree gains no third-party file. The spike header and spike translation unit live in the scratchpad and are never added, staged, or committed.
- No `Tools/` behavior changes, so the AgentHarness / WorktreeCli command-line protocol and the build and Plan-claim operational contracts are untouched by this Plan — they are the *subject* of the analysis, not its target.
- The verdict rule is applied as pre-committed. A criterion that fails is not renegotiated against one that passes.
- Cross-reference edits under REJECT strike only the ordering/deferral language naming this Plan; no other Plan's scope, metadata, or `createdUtc` is touched.

## Acceptance criteria

- Step 1 records a per-region and total `bt-token-v1` baseline for all seven candidate entries, each with the explicit `-StartLine`/`-EndLine` range used, reproducible by re-running the command.
- Step 2 records a per-contract verdict for all five hard contracts, each either preserved with its named CLI11 configuration or failed with the reason — no "likely", "should", or unadjudicated entry.
- Step 3 records the spike's measured replacement cost and the extrapolated full-set cost, or reports UNRESOLVED with the reason the upstream header could not be obtained. The repository tree is unchanged by the spike: `git status` shows no new or modified file under `ThirdParty/` or `Tools/`.
- Step 4 records the per-translation-unit PCH-less compile delta from three runs with and without the CLI11 include.
- A single verdict — ADOPT, REJECT, or UNRESOLVED — is recorded with the measurements that produced it, and exactly one terminal outcome is executed: the adoption Plan authored, or the nine cross-reference sites struck. Not both, not neither.
- `git status` shows no modification under `Tools/`, `ThirdParty/`, or either tool's `.vcxproj` / `.filters` at completion.

## Notes

- The candidate spans were once estimated near 3,578 `bt-token-v1`. That figure is drifted pre-review human context, not a measurement — Step 1 re-measures, and the old number is not cited as evidence for anything.
- The 500 `bt-token-v1` floor is `/external-architecture-review`'s own replacement-candidate threshold (`.agents/skills/external-architecture-review/SKILL.md:62`), applied here to both the baseline and the projected net.
- BSD-3-Clause is on the accepted license list (`ThirdParty/AGENTS.md:18`), and CLI11 is not currently vendored. That makes CLI11 *eligible* for a dependency review; it is not permission to vendor, and this Plan never needs it.
- Non-submodule vendored directories have precedent under `ThirdParty/` (`Clipper2`, `glm`, `PerlinNoise`), so an ADOPT verdict's follow-up Plan does not need to solve library-registration mechanics — the single header would be consumed exactly as `tinygltf/json.hpp` already is.
