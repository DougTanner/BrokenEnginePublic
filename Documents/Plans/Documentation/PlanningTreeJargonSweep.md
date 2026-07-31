<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-31T14:20:30.338Z","dependsOn":[]} -->
# Plain-Language Word Sweep for the Planning, Investigation, and Feature Trees

## Context

`FormalWordSwaps.md` (now completed and deleted) replaced formal words with plain ones across every agent-facing guidance file: root `AGENTS.md`, every subsystem `AGENTS.md`, and every `.agents/skills/**` file. That sweep deliberately skipped the three write-up trees — `Documents/Plans/**`, `Documents/Investigations/**`, and `Documents/Features/**` — and the user directed that they be handled by a follow-up.

The question of whether those trees are worth sweeping is now decided: **yes, sweep all three.** Agents read Plans aloud to the user and quote them in handoffs, so the same formal vocabulary reaches the user through a different door. Leaving two vocabularies in the repository also weakens the "one term per concept" directive.

This plan is the mechanical execution of that decision. It is a wording change only: no Plan's instructions, acceptance criteria, dependency edges, metadata, or file names change meaning.

## Design

### Words to swap

Only the words that actually occur in these trees, using the same replacements the completed guidance sweep used:

| Word (targeted sense) | Replacement |
| --- | --- |
| adjudicate / adjudicates / adjudication | decide / decides / decision |
| mutate / mutates / mutation / mis-mutated | change / changes / change / wrongly changed |
| canonical (authoritative sense) | authoritative |
| canonical (normalized-path or normalized-output sense) | normalized |
| material / materially (significance sense) | meaningful / meaningfully |
| liveness | "whether it is still running" (fitted to the sentence) |
| cardinality | count |
| in-flight | still in progress (or, for the framing property, "single-request-at-a-time") |
| blast radius | spread of breakage |
| gold-plating | unnecessary extra work |

Each replacement is fitted to its sentence's grammar, never pasted blindly.

### Senses that are NOT swapped

The executor classifies each hit's sense before editing and leaves these alone:

- "material" as a graphics/asset term (material mask, per-material draws, material ranges) and as a mass noun ("reference material", "removed material").
- "materialization" as a code concept (`FileManager` materialization).
- "terminal" meaning a console or command window ("human-terminal"), and "terminally constrained" in `IslandPlacementSubscriptionCapacity.md`.
- "mutate/mutates" inside a code identifier, a quoted code comment, or a sentence describing what a named function does to memory — this is the precise engineering claim being recorded, and the plain word would blur it. Prose about changing a branch, document, or manager state IS swapped.
- Any word inside a verbatim quotation of another file that carries a `path:line` citation. Rewording a quotation makes its citation wrong. `ManagerBoundaryRootStatement.md:24-29` is the clearest case.

### Documents excluded from the sweep

- `Documents/Plans/Documentation/DeJargonGuardrails.md` — its line 24 rule text ("decide, not adjudicate; change, not mutate; final, not terminal; count, not cardinality") quotes every formal word on purpose, and the file is a live unexecuted Plan whose deliverable is that exact wording.
- `Documents/Plans/Documentation/WaterSamplingPactTermAlignment.md` and the four `Documents/Features/Graphics/Water*.md` "pact" sites it owns — that live Plan is the decided fix for that word. Do not double-edit.
- `AGENTS.md` and `CLAUDE.md` at every level of the three trees — already covered by the completed guidance sweep.
- Non-`.md` files in these trees (`Documents/Features/Graphics/*.txt`) — the completed sweep's scope was tracked `*.md`, and their only hits are the graphics "material" term, which is not swapped anyway.

### Re-derive the site list at execution time

Plans are deleted from the tree when they complete, so the list below decays. Before editing, re-run a word-boundary, case-insensitive grep for every word in the table across tracked `*.md` files under the three trees, and work from that result. A listed site that no longer exists is a completed Plan, not a miss.

## Critical files

Confirmed sites at the time of writing (re-derive, line numbers move):

`Documents/Plans/`

- `DataPacker/ManifestPackChunkIdentityValidation.md:18` — "cardinality".
- `Documents/PlanClosureSweepInvestigationsCoverage.md:52` — "canonical-path" (normalized sense).
- `Documents/ManagerBoundaryRootStatement.md:36,62` — "materially shorter". Lines `:24-29` are cited quotations and stay.
- `Documents/AgentHarnessAutoConnectWindowRestore.md:6,14,44` — "canonical" (authoritative sense).
- `Graphics/IslandPlacementSubscriptionCapacity.md:16` — "canonical compile-time interface" (authoritative). `:60` "terminally constrained" stays.
- `Network/HandshakeGatedClientRelink.md:14,33` — "client-manager mutation".
- `Network/QueuedSpawnDroppedFleet.md:99` — "mis-mutated".
- `Network/AgentActiveSocketTeardownSynchronization.md:19` — "single-in-flight framing".
- `Frame/NavInflateMarginUvRelative.md:61` — "materially different cost".

`Documents/Investigations/`

- `Documents/ReviewRubricReferences.md:21` — "blast radius"; `:25` — "materially the same wording".
- `Documents/BlindSpotAndInterviewGuidance.md:16` — "gold-plating".
- `Documents/AgentsMdRewriteAudit.md` — many hits (around `:165`, `:1238`, `:1290`, `:1368`, `:1422`, `:1430`, `:1543`). This is a findings record built largely from cited quotations and descriptions of what code does; most hits fall under the not-swapped rules above. Classify each one; expect few actual edits.

`Documents/Features/`

- `Agent/AgentModeCrashReportHandoff.md:14,28` — "liveness" ×2.
- `Agent/AgentQueryGlobalState.md:24` — "Any mutation of manager state".
- `Agent/AgentTweaksUiAutomation.md:18` — "mutating the production UI state machine".
- `Agent/CodeQualityMetrics.md:42` — "canonical JSON" (normalized sense).
- `Frame/FrameRelativePositions.md:127` — "blast radius". `:34` "Mutates only the render copy" describes code behavior and stays.

## In scope

- Word replacements from the table above, in the targeted sense only, in tracked `*.md` files under `Documents/Plans/`, `Documents/Investigations/`, and `Documents/Features/`, excluding the documents named under "Documents excluded from the sweep".
- Fitting each replacement to the surrounding sentence's grammar, including nearby article or verb agreement the swap forces.
- Recording, in the handoff, every site where the plain word would have changed a rule and the original word was kept instead (keep-and-define).

## Out of scope

- The already-swept guidance files: root `AGENTS.md`, every subsystem `AGENTS.md`, every `.agents/skills/**` file, and the `AGENTS.md`/`CLAUDE.md` files inside the three trees.
- `Documents/Plans/Documentation/DeJargonGuardrails.md` and `Documents/Plans/Documentation/WaterSamplingPactTermAlignment.md`, plus the "pact" sites that second plan owns.
- C++, GLSL, script, and `.txt` files; code identifier renames; anything under `ThirdParty/`.
- Plan metadata lines, `dependsOn` edges, file names, directory placement, and heading structure.
- Any change to what a Plan instructs, what it accepts, or its scope boundaries. Rewriting content, fixing stale facts, or improving a write-up is not part of this work.
- The compressed-phrase and term-collision rewrites owned by other completed Documentation plans.

## Risk and invariants

Risk 1 — wording only, but these documents are work orders that future implementers execute.

Invariants:

1. Exact meaning preservation. Every swap must read as the same instruction, claim, or boundary after the edit. A swap that would weaken, broaden, or shift a Plan's instruction becomes keep-and-define and is reported instead.
2. Cited quotations stay byte-identical, so every `path:line` citation in these trees still matches the text it quotes.
3. No file in these trees is created, deleted, renamed, or moved, and no byte-zero metadata line changes.
4. Code identifiers, JSON keys, command names, and log strings quoted inside these documents are never edited.

## Acceptance criteria

- A word-boundary, case-insensitive grep of tracked `*.md` under the three trees returns zero remaining uses of adjudicate/adjudication, cardinality, blast radius, and gold-plating outside the excluded documents.
- Remaining hits for canonical, material(ly), mutat-, terminal, and in-flight are each confirmed to be a not-swapped sense, an excluded document, or a cited quotation — listed with file and line in the handoff.
- `git diff --stat` shows changes only in `.md` files under the three trees, with no excluded document among them.
- Every changed line is a same-meaning restatement: the diff contains no added or removed instruction, criterion, scope clause, citation, metadata line, or heading.
- `WorktreeCli plan validate` returns exit 0, `status: valid`, with no diagnostics, proving no metadata line was disturbed.
