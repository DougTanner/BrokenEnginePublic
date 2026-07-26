<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T18:05:49.108Z","dependsOn":[]} -->
# Mandatory Executable Plan Metadata

## Context

`Documents/Plans/AGENTS.md:11` states "Files lacking the marker are manual/reference documents", and `:23` allows a plan to "remain manual/reference documents until they receive executable metadata". `Documents/AGENTS.md:23` repeats it. The scheduler implements the same escape hatch: `Tools/WorktreeCli/PlanScheduler.cpp:432` and `:475` suppress marker-less files from diagnostics under `diagnostic == "manual"`, so such a file disappears silently instead of reporting a problem.

The repository owner does not review `Documents/Plans` by hand. A marker-less file there is therefore executed by nobody: the scheduler ignores it and no human reads it. The concept is removed — every *plan document* under `Documents/Plans/**` carries byte-zero `broken-engine-plan/v1` metadata, and the scheduler reports a marker-less plan document as a validation error rather than skipping it.

### Measured scope

Measured on the tree at the time of writing, over `git ls-files -- Documents/Plans` filtered to `*.md`, testing the first line against `^<!-- broken-engine-plan/v1 `:

| Category | Count |
| --- | --- |
| Tracked `.md` files under `Documents/Plans` | 50 |
| Carry a valid marker | 42 |
| Lack a marker | 8 |
| — of which are directory guidance (`AGENTS.md`, `CLAUDE.md`) | 2 |
| **Marker-less plan documents to disposition** | **6** |

The six:

- `Documents/Plans/Documents/BlindSpotAndInterviewGuidance.md`
- `Documents/Plans/Documents/HtmlArtifactPlansAndReports.md`
- `Documents/Plans/Documents/ReviewRubricReferences.md`
- `Documents/Plans/Documents/TranscriptFinderSubagentAmbiguity.md`
- `Documents/Plans/Graphics/IslandResidentMemoryScaling_Overview.md`
- `Documents/Plans/Graphics/ShaderReview/00_Overview.md`

Counts move as primary advances; the implementer re-measures with the command above and dispositions whatever set exists then. The enumeration is the deliverable, not the number.

### Directory guidance is exempt and must stay exempt

`Documents/Plans/AGENTS.md` and `Documents/Plans/CLAUDE.md` are directory guidance and its import stub, not plans. They must never carry scheduler metadata — marking them would feed non-work to `/next-plan` as claimable. Every rule and check in this plan applies to plan documents only and must exclude `AGENTS.md` and `CLAUDE.md` by name at every level of the tree.

### The hazard this must not create

The remaining marker-less documents are not ready-to-run work, and bulk-marking them would enrol undecided material into automated execution. Three carry an explicit "Status: exploratory / investigation … presents options rather than a decision-complete implementation" banner:

- `Documents/Plans/Documents/BlindSpotAndInterviewGuidance.md`
- `Documents/Plans/Documents/HtmlArtifactPlansAndReports.md`
- `Documents/Plans/Documents/ReviewRubricReferences.md`

Two are findings/coordination records rather than work items:

- `Documents/Plans/Graphics/IslandResidentMemoryScaling_Overview.md`
- `Documents/Plans/Graphics/ShaderReview/00_Overview.md`

One is a design awaiting re-audit, with its own Status section explaining why:

- `Documents/Plans/Documents/TranscriptFinderSubagentAmbiguity.md`

Each must be dispositioned deliberately.

## Design

### 1. Triage every marker-less plan document [~1h]

Enumerate with `git ls-files -- Documents/Plans`, filter to `*.md`, exclude `AGENTS.md` and `CLAUDE.md`, and select files whose first line does not match `^<!-- broken-engine-plan/v1 `. Assign each exactly one disposition:

- **(a) Enrol as-is** — already decision-complete; add a marker with a `createdUtc` and any real `dependsOn` edges.
- **(b) Complete then enrol** — a sound work item missing a decision, acceptance criteria, or an `## Out of scope` boundary. Finish it to the `Documents/Plans/AGENTS.md:21` bar, then add the marker.
- **(c) Relocate** — a findings record, overview, or option-presenting investigation that is not work. Move it out of `Documents/Plans/` and update every inbound reference.
- **(d) Delete** — obsolete or already landed.

Record the disposition table in the change's report; it is the acceptance evidence for criterion 1.

`createdUtc` for a newly enrolled file is the time of enrolment, never backdated — it drives scheduler ordering, so a backdated value would jump the new arrival ahead of the existing queue.

### 2. Add the relocation target for disposition (c) [~30m]

`Documents/Features/` is defined as brand-new capability additions and is the wrong home for a shader-review findings record or a workflow investigation. Add `Documents/Investigations/` with its own `AGENTS.md` stating it holds non-executable reference material and is never a scheduler input, and route (c) files there. This keeps `Documents/Plans/` entirely executable without discarding real content.

### 3. Make the scheduler enforce it [~1h]

In `Tools/WorktreeCli/PlanScheduler.cpp`, stop suppressing the `manual` diagnostic at `:432` and `:475`: a tracked plan document under `Documents/Plans/**` without a byte-zero marker becomes an `invalid-metadata` diagnostic naming the exact path, surfaced by `plan validate` and by the validation the landing path already runs. `AGENTS.md` and `CLAUDE.md` are excluded from this rule.

Marker-less files must not become *claimable*; they must become *loud*. Quarantine semantics are unchanged — an invalid file quarantines only its own component, leaving unrelated plans claimable.

### 4. Remove the concept from the documentation [~45m]

- `Documents/Plans/AGENTS.md:11` — delete "Files lacking the marker are manual/reference documents"; state that the marker is mandatory for plan documents, that its absence is a validation error, and that `AGENTS.md`/`CLAUDE.md` are exempt.
- `Documents/Plans/AGENTS.md:23` — delete the "remain manual/reference documents until they receive executable metadata" allowance; state that an option-presenting document does not belong in `Documents/Plans/`.
- `Documents/AGENTS.md:23` — remove "Files without the marker … are manual" and point at the new `Investigations/` tree.
- `.agents/skills/create-follow-up-plans/SKILL.md` — ensure it can never emit a marker-less Plan.
- Per-file "Status: exploratory … manual/reference document" banners removed by disposition (a) or (b), and rewritten for files relocated by (c).

### 5. Replacement for "not ready yet" [~30m]

Removing the escape hatch removes the only mechanism for parking an unready plan in place. The replacement is explicit:

- Work blocked on another change expresses that as a `dependsOn` edge, which the scheduler already honours.
- Work blocked on a **decision** is not a Plan yet and belongs in `Documents/Investigations/` until the decision exists.

State this in `Documents/Plans/AGENTS.md` so the next agent has a routed answer instead of reinventing the marker-less park.

## Critical files

- `Documents/Plans/AGENTS.md`
- `Documents/AGENTS.md`
- `Documents/Investigations/AGENTS.md` (new)
- `Tools/WorktreeCli/PlanScheduler.cpp`
- `.agents/skills/create-follow-up-plans/SKILL.md`
- The marker-less plan documents enumerated at implementation time

## Scope contract

The listed scope is both target and ceiling. Make the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities the named change requires.

**In scope — named regions only:**

- `Tools/WorktreeCli/PlanScheduler.cpp`: the two `diagnostic == "manual"` suppressions at `:432` and `:475`, plus the minimal predicate needed to exempt `AGENTS.md` and `CLAUDE.md`.
- `Documents/Plans/AGENTS.md`: the sentences at `:11` and `:23` only, plus the "not ready yet" routing statement added by design 5.
- `Documents/AGENTS.md`: the marker/manual sentence at `:23` only.
- `Documents/Investigations/AGENTS.md`: new file.
- `.agents/skills/create-follow-up-plans/SKILL.md`: only the statements that permit or imply emitting a marker-less Plan.
- The enumerated marker-less plan documents: marker addition, the minimum content needed for disposition (b), relocation for (c), or deletion for (d).

**Out of scope:**

- `Documents/Features/`, which remains manual and never a scheduler input. If the owner wants Features scheduled, that is a separate decision.
- `Documents/Plans/AGENTS.md` and `Documents/Plans/CLAUDE.md` acquiring markers, at any level of the tree.
- Scheduler selection order, claim lifecycle, terminal preparation, `dependsOn` semantics, and the `broken-engine-plan/v1` marker format.
- Rewriting the technical content of any enrolled plan beyond what disposition (b) requires.
- The `Documents/Plans/Tools/TerminalManifestReconciliationTolerance.md` change, which separately edits `PlanScheduler.cpp`; see Coordination.

## Coordination

`Documents/Plans/Tools/TerminalManifestReconciliationTolerance.md` edits `RunPrepare`'s child loop at `PlanScheduler.cpp:1606-1645`; this plan edits the diagnostic suppressions at `:432` and `:475`. The regions are disjoint and neither is a prerequisite of the other; whichever lands second rebases cleanly. `Documents/Plans/Tools/ReducePlanScheduler.md` extracts `ParsePlanBytes`/`BuildPlans` into a new translation unit and may move the `:432`/`:475` diagnostic assembly with them; whichever of the two lands second applies the same edit at its new location.

## Risk tier

Tier 3 — changes `plan validate` behaviour that every session's `/next-plan` and landing path depends on, and can block other sessions if a newly loud diagnostic quarantines unexpectedly. Also spans independently owned areas: the scheduler tool, repository documentation policy, and the enumerated plan documents.

Invariant exposure: scheduler validity classification and claim eligibility. The primary failure mode to guard against is silent enrolment — no previously marker-less file may become claimable without an explicit recorded disposition, because that would hand an agent an undecided investigation to implement. The second is over-application: marking `AGENTS.md` or `CLAUDE.md` would inject directory guidance into the work queue.

## Acceptance criteria

- Every tracked `.md` file under `Documents/Plans/**`, excluding `AGENTS.md` and `CLAUDE.md` at every level, has a first line matching `^<!-- broken-engine-plan/v1 `. Verified by the enumeration command in design 1 returning an empty set.
- `Documents/Plans/AGENTS.md` and `Documents/Plans/CLAUDE.md` remain marker-less and absent from `plan validate`'s reported plan set.
- `WorktreeCli plan validate` returns `status: valid` with a plan count equal to the number of tracked `.md` files under `Documents/Plans/` minus the exempt guidance files.
- A deliberately marker-less plan document placed under `Documents/Plans/` produces an `invalid-metadata` diagnostic naming that exact path, at the existing exit code, and does not become claimable.
- Unrelated valid plans remain claimable while that invalid file exists.
- Every file in the implementation-time enumeration has a recorded disposition in the change report; none is enrolled without one.
- No occurrence of the manual/reference-document concept remains in `Documents/Plans/AGENTS.md`, `Documents/AGENTS.md`, or any enrolled plan's status banner.
- `/create-follow-up-plans` cannot produce a marker-less Plan.
