<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-30T23:01:40.000Z","dependsOn":[]} -->
# Compressed Phrase Rewrites

## Context

Governing objective: reduce jargon in AGENTS.md and skill files so smarter agents remain understandable to the user. Simplify language as much as possible WITHOUT losing specificity. Keep standardized terms — exactly one term per concept, defined once at the owning doc — and eliminate cases where two terms refer to the same thing or one term means several things.

This plan is the full rewrite of every COMPRESSED finding: noun-stacks that hide the concrete action. Each site below gives the current phrase and a proposed plain rewrite. The proposed rewrites are normative for **meaning**; the implementer fits grammar and surrounding sentence structure at each site. Because the scan captured phrases without full surrounding context, the implementer verifies each rewrite against the sentence in place before applying it; if the context shows the phrase means something different, the implementer writes the plain phrase that matches the actual meaning and reports the delta in the handoff. Standardized terms defined by the glossary plan (session baseline, lease, affinity, hub, harvest, hydrate, reuse counter, …) are used inside rewrites instead of re-explaining.

Where a phrase names a C++ identifier or literal token (e.g. `LogDifferences`, mode numbers), the identifier keeps its name and only the surrounding prose is unpacked.

## Design — rewrites by file

### Root AGENTS.md

| Line | Current | Proposed rewrite |
| --- | --- | --- |
| :15 | "a host default withholding subagent dispatch until the user asks does not gate them" | "even if the host tool normally waits for the user to ask before delegating, these dispatches do not wait for that" |
| :21 | "claim mutation" | "creating, changing, or deleting a Plan claim" |
| :41 | "that invocation IS the delegated-reviewer execution context" | "calling that skill is itself the delegated reviewer running — no separate reviewer dispatch is needed" |
| :47 | "unrouted case" | "a case this workflow does not assign to anyone" |
| :70 | "stage dispositions" | "stage decisions" (glossary decision 9 wording) |
| :71 | "iterative decision briefs" | "short written decision summaries, updated round by round" |
| :72 | "affected-site triggers" | "notes on which other code sites the change may affect" |
| :74 | "reached contracts" | "the documented rules the changed code actually touches" |
| :74 | "fresh-eyes coherence" | "a coherence review by a reviewer with no prior involvement in the change" |
| :74 | "dispatch route" | "the way the reviewer was dispatched" |
| :75 | "triggered hygiene" | "the cleanup steps this change triggers" |
| :75 | "whole-file affinity" | "which executable a whole file belongs to" (uses the affinity definition) |
| :76 | "decisive evidence" | "evidence that settles the question on its own" |
| :77 | "landing lock lease" | "the landing lock — a lease, so it expires on its own if the holder dies" |
| :91 | "Resolution ladder" | "the ordered list of preferred fixes, from best to last resort" |
| :111 | "approved deltas" | "changes the user approved after the plan" |

### Skill files

| Site | Current | Proposed rewrite |
| --- | --- | --- |
| `finalize-changes`:52 | "owner-CAS/all-worktrees-clear" | "the compare-and-swap performed by the claim owner, run only when every worktree is clean" |
| `finalize-changes`:73 | "semantic merge hazards" | "rebases that merge cleanly but change the code's meaning" |
| `compile`:151 | "transient operation claim discipline" | "the rules for taking and releasing the short-lived operation lock" |
| `create-follow-up-plans`:13 | "acceptance gap" | "an acceptance criterion the change does not meet" |
| `create-follow-up-plans`:19 | "byte-zero metadata" | "the metadata line that must be the very first bytes of the file" |
| `create-follow-up-plans`:44 | "queue tier, row, request-file" | spell each out with its role: "the queue tier, the queue row, and the request file" |
| `create-follow-up-plans`:54 | "publication transaction" | "the single commit that publishes the row" |
| `gaea2-load`:39 | "sample-structure fingerprint" | "a checksum of the sample file's structure, used to detect changes" |
| `external-refactor-clean`:68 | "phantom validation" | "validation that is claimed but never actually performed" |
| `external-skill-creator`:28 | "discovery surface" | "the text agents search when deciding whether to use the skill" |
| `codex-review`:68 | "recovery ladder" | "the ordered fallback steps to try when a run fails" |
| `next-plan-review`:112 | "inherited-context Codex turn fork" | "a Codex turn forked with the conversation context carried along" |
| `next-plan-review`:120 | "proof chain" | "the linked evidence leading from claim to conclusion" |
| `next-plan-review`:120 | "exposed cost" | "the cost made visible in the report" |
| `next-plan-review`:195 | "depth-one worker tree" | "one manager with a single level of workers below it" |
| `repo-code-review`:88 | "structural-erosion changes" | "changes that quietly weaken the code's structure" |
| `repo-code-review`:95 | "upstream-omitted row" | "a row the earlier step left out" |
| `save-plan`:13 | "cross-model handoff artifact" | "a file one model writes for another model to pick up" |
| `save-plan`:39 | "standing affirmative response" | "a yes given in advance that stays in effect" |
| `update-affected-code`:28 | "Sweep, guard, and mirror triggers" | "the triggers for sweeping callers, checking client/server guards, and updating mirrored code" |
| `verify-changes`:77 | "data-oracle verification" | "checking results against an independent source of the correct values" |
| `implement-plan`:78 | "affected-site trigger" | "a note that another code site may be affected" |
| `implement-plan`:79 | "guard-affinity, sweep, or mirror concern" | "a concern about client/server guards, missed callers, or mirrored code" |
| `agent-harness`:98 | "churning collections" | "collections whose entries are being added and removed rapidly" |
| `agent-harness`:62 | "receipt" | "the confirmation record the command returns" |
| `agent-harness`:108 | "Shared oracle" | "the shared source of expected values" |
| `analyze-diagsession`:151 | "post-landing row publication" | "adding the plan row after the change lands" |
| `code-quality-metrics`:55 | "gitlink pin" | "the recorded submodule commit" |
| `code-quality-metrics`:55 | "archive selector" | "the pattern that chooses which files go into the archive" |
| `add-collection`:119 | "frame halves" | "the two halves of the frame update" |
| `add-collection-member`:43 | "determinism contract" | "the rules that keep the simulation bit-identical across client and server" |

(`session-audit`:73 "completion-deletion" is owned by `ProcessVocabularyRenames.md` — do not double-edit.)

### Engine and tool docs

| Site | Current | Proposed rewrite |
| --- | --- | --- |
| `Engine/Source`:29 | "clock-servo ceiling" | "the cap on how fast the clock-adjustment loop may correct" |
| `Engine/Source/Frame`:26 | "drained churn window" | "the window after all pending adds and removes have been processed" |
| `Frame/Collections`:21 | "borrowed-ID removals" | "removals of entries whose IDs were borrowed from another owner" |
| `Graphics/Objects`:7 | "deferred-retirement path" | "the path that retires objects later, once the GPU is done with them" |
| `Graphics/Render`:11 | "amplitude gates" | "thresholds the amplitude must cross before the effect applies" |
| `Common/Log`:7 | "compile-time category floor" | "the minimum log level per category, fixed at compile time" |
| `Common/Log`:8 | "cross-category agent-query ring" | "one shared ring buffer, spanning all categories, that agent queries read" |
| `Common/Log`:9 | "absolute-indent scopes" | "scopes whose indent level is set directly rather than nested" |
| `Common/Threading`:17 | "forbidden services" | "OS or library calls worker threads must not make" |
| `DataPacker/Source`:15 | "output-materialization" | "writing the outputs to their real location on disk" |
| `ExportJobs`:17 | "load-bearing inputs" | "the inputs the output actually depends on" |
| `ExportJobs`:31 | "face-major/mip-minor" | "ordered by face first, then by mip level within each face" |
| `ExportJobs/Island`:7 | "bake contract" | "the agreed inputs, outputs, and ordering of the bake step" |
| `Shaders/Lighting`:9 | "backward-closed across mixed extents, gather reach" | "every earlier level is fully included even when extents differ; how far the gather step reads" |
| `Shaders/Terrain`:7 | "packed chain bounds" | "the bounds of the packed mip chain" |
| `Shaders/Water`:19 | "mip-variance handoff" | "passing the per-mip variance from one stage to the next" |
| `Documents`:7 | "worktree seeding" | "pre-copying built binaries into new worktrees" |
| `Tools/AgentHarness`:8 | "metadata envelope" | "the metadata wrapper around each message" |
| `Tools/WorktreeCli`:9 | "claimant process provenance" | "a record of which process took the claim" |
| `Tools/WorktreeCli`:10 | "selection bytes" | "the exact bytes the selector reads" |
| `Tools/WorktreeCli`:26 | "hidden regular atomic siblings" | "the hidden ordinary files stored next to it and updated atomically" |
| `Tools/WorktreeCli`:26 | "scheduler guard" | "the check that protects scheduler state" |
| `ThirdParty`:9 | "aggregation surfaces" | "the headers that gather each library's includes" |
| `Tools/ToolCommon`:8 | "tool boundary selects the coordination domain" | "which tool the code is compiled into decides which coordination mechanism it uses" |
| `Projects .../Data/Shaders`:3 | "dual-language struct additions" | "struct members that must be added in both the C++ and GLSL copies" |
| `Platforms`:22 | "rule surface" | "the set of rules that apply" |
| `Platforms`:21 | "output leaf" | "the final output directory" |
| `Projects .../Source`:5 | "unbounded coord-frame orchestration" | "coordinating cell simulation across the unbounded grid" |
| `Engine/Source/Agent`:14 | "renderer mailbox" | "the queue where messages wait for the renderer to pick them up" |
| `Engine/Source/Frame`:12,16 | "harvest" / "drain loop" | "harvest" keeps (glossary decision 20); "drain loop" → "the loop that keeps processing until the queue is empty" |
| `Frame/Collections`:13 | "magnitude tests" | "checks on the vector's length" |
| `Collections/Players`:8 | "D+2" / "mode 4/5" / "three-draw schedule" | expand each in prose: "two ticks after death (D+2)"; name what modes 4 and 5 do at first mention; "the schedule that issues three draw calls" — mode numbers that mirror code stay as numbers with a plain gloss |
| `Collections/Players`:18 | "pusher bookkeeping" | "the records tracking each pusher" |
| `Collections/Spaceships`:14 | "visible accumulator" | "the running total used for what is displayed" |
| `Collections/Spaceships`:15 | "slab reservation" | "reserving one contiguous block up front" |
| `Projects .../Graphics`:17 | "pure-current re-seed" / "prior-valid-window bounds" | "re-seeding from current values only" / "bounds taken from the last valid window" |
| `Projects .../Graphics`:18 | "publication point" | "the moment results become visible to readers" |
| `Projects .../Input`:11 | "lifetime-accumulator baseline … deferred zoom" | "the baseline for the running lifetime total … zoom applied later rather than immediately" |
| `Network/Client`:3 | "receive adoption" | plain wording aligned with glossary decision 21, e.g. "filling local slots from received state" (keep the code identifier if `Adopt` is one, per `EngineDomainTermRenames.md`) |
| `Network/Server`:11 | "relink matches" | "re-attaching the entities that match" |
| `Projects .../Save`:16 | "generation commit marker and authenticated inventory" | "the marker that commits this recording generation, plus the checksummed list of its files" |
| `Ui/Screens`:17 | "optical balance" | "spacing adjusted so it looks centered, not just measures centered" |
| `Common`:41 | "structured-fault handling" | "handling of Windows structured exceptions — crashes such as access violations" |
| `Projects .../Data`:7 | "layout-extension seam" | "the place where new layout fields can be added safely" |

## Critical files

All files named in the tables above: root `AGENTS.md`; ~30 engine/project/tool/shader AGENTS.md files; SKILL.md files for `finalize-changes`, `compile`, `create-follow-up-plans`, `gaea2-load`, `external-refactor-clean`, `external-skill-creator`, `codex-review`, `next-plan-review`, `repo-code-review`, `save-plan`, `update-affected-code`, `verify-changes`, `implement-plan`, `agent-harness`, `analyze-diagsession`, `code-quality-metrics`, `add-collection`, `add-collection-member`.

## Out of scope

- C++ and GLSL code changes; code identifier renames — identifiers inside phrases keep their names.
- Term renames owned by plans 2–4 (marked inline above where adjacent).
- Skill behavior changes — wording only; triggers, routing, contracts, and output formats unchanged.
- `Documents/Features/` tree. Third-party library code and files are out of scope; the repository-authored AGENTS.md guidance files under `ThirdParty/` ARE in scope (the `ThirdParty/AGENTS.md`:9 wording site above).

## Risk and invariants

Risk 1, but this is the judgment-heavy plan: rewrites are longer than the phrases they replace, and a wrong unpacking would silently change a documented contract. Invariants: (a) each applied rewrite is verified against its surrounding sentence and the mechanism it describes — when in doubt, the implementer reads the referenced code or doc before rewriting; (b) any meaning delta between the proposed rewrite and the verified context is applied in favor of the context and reported in the handoff; (c) every changed `.agents/skills/*/SKILL.md` triggers `/validate-skill`. The tables list discovered sites; a repo-wide grep of each phrase catches copies elsewhere.

## Acceptance criteria

- Every table row is either rewritten in place (meaning verified) or reported with the context-mismatch reason and the alternative plain wording used.
- No compressed phrase from the tables remains anywhere in tracked `*.md` files (grep per phrase).
- Changed SKILL.md files pass `/validate-skill`.
- Spot-check readability: each rewritten sentence stands alone without the reader knowing the old phrase.
