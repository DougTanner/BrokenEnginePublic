# Migrate CLAUDE.md → AGENTS.md (stub imports)

## Summary

**What this plan does:** Renames all 70 repo `CLAUDE.md` memory files (root + subdirectories) to `AGENTS.md`, leaving beside each a one-line `CLAUDE.md` stub containing only `@AGENTS.md` so Claude Code's native import expansion loads identical content. Also renames the 11 `Engine/Source/Graphics/Managers/*.CLAUDE.md` linked docs to `*.AGENTS.md` (no stubs — these were never auto-loaded memory). Then sweeps the ~470 textual `CLAUDE.md` references across ~163 files (memory-file cross-links, `.claude/skills/**` SKILL.md, `Documents/Plans/**` + `Documents/Features/**` incl. both `Order.md` indexes, top-level `Documents/*.txt` reference docs, and ~24 C++/GLSL source comments) to `AGENTS.md`, keeping loader-mechanics sentences (which describe how Claude Code loads memory) true rather than blindly replaced. Finally, codifies the stub relationship as an enforced repo pattern in the `update-claude-docs` skill so future directory memory docs keep their `CLAUDE.md`→`AGENTS.md` stub pairing.

**Why it's good for the codebase:** Makes the single source of truth for agent memory readable by the cross-tool `AGENTS.md` standard (Cursor, Codex, and other agent tools that read `AGENTS.md` natively) while keeping Claude Code loading byte-identical via the stub imports — a concrete dev-experience/interoperability win with zero runtime, determinism, or build exposure (markdown is not project-membered).

## Context

- Source: `Documents/Plans/Skills/MigrateClaudeMdToAgentsMd.md` (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier Small / Effort 2 / Impact 2 / Risks 0 / Score 0 (marked `[CLAIMED]` in Step 2)
- Notes: Migrate agent memory to the cross-tool AGENTS.md standard: rename all 70 CLAUDE.md → AGENTS.md plus the 11 `Managers/*.CLAUDE.md` linked docs, leave a one-line `@AGENTS.md` import stub CLAUDE.md beside each of the 70 (Claude Code expands imports mechanically at load), repo-wide reference sweep (~430 hits: memory cross-links, skills, Documents/ plans, source comments) with loader-mechanics sentences kept true. Docs/comments only — no CRC/wire/`kiVersion`/vcxproj exposure. Execute alone: textually touches nearly every plan/SKILL.md file
- Relevance: **Fully** — all 70 `CLAUDE.md` files and all 11 `Managers/*.CLAUDE.md` docs confirmed present; no `AGENTS.md` exists yet (migration not started); every cited example source file resolves.
- Dependency resolution: none — user-specified target (`MigrateClaudeMdToAgentsMd.md`), no unmet prerequisites (its only Dependencies bullet is a coordination note: execute alone, no concurrent sessions).
- Changes since the plan was written:
  - Reference count refreshed: **~470 `CLAUDE.md` occurrences across ~163 files** (plan estimated ~430 across ~150) — same order of magnitude, no scope explosion.
  - File/doc counts confirmed exact: **70** `CLAUDE.md`, **11** `Managers/*.CLAUDE.md`.
  - `RDXmin/AGENTS.md` is no longer present on disk (RDXmin remains untracked + out of scope); acceptance criterion 1's "untouched RDXmin/AGENTS.md" parenthetical is moot.
  - New untracked `.agents/` directory holds only a `skills` symlink → `.claude/skills` (no memory file) — out of scope; does not add or alias any `CLAUDE.md`.
  - Working tree currently has root `CLAUDE.md` modified — the rename captures the current working-tree content.

## Execution steps

1. **Rename the 70 memory files + create stubs** (one PowerShell loop). For each directory containing a `CLAUDE.md` (full list via `Glob **/CLAUDE.md` — confirmed 70, root plus subdirectories):
   - `Rename-Item CLAUDE.md → AGENTS.md` (git detects the rename at commit; agents use read-only git only).
   - Create a new `CLAUDE.md` beside it containing exactly one line: `@AGENTS.md` (imports resolve relative to the containing file, so each stub picks up its sibling `AGENTS.md`).
2. **Rename the 11 manager docs** `Engine/Source/Graphics/Managers/<Name>.CLAUDE.md` → `<Name>.AGENTS.md`. These are **not** auto-loaded memory (only files named exactly `CLAUDE.md` load), so they get **no stubs** — rename only, then fix inbound links (step 3).
3. **Repo-wide reference sweep** (`CLAUDE.md` → `AGENTS.md`, and `<Name>.CLAUDE.md` → `<Name>.AGENTS.md`). Split across Sonnet subagents by area. **Drive the sweep off a live `grep CLAUDE\.md` at execution** (the plan's acceptance methodology) rather than a fixed directory list, so nothing outside the enumerated dirs is missed. Categories:
   - **Renamed memory files** — cross-links like `[CLAUDE.md](Common/CLAUDE.md)` → `[AGENTS.md](Common/AGENTS.md)` (heaviest: root, `Engine/Source/CLAUDE.md`, `Engine/Data/Shaders/CLAUDE.md`, `Projects/.../Source/CLAUDE.md`, the collection hub docs).
   - **`.claude/skills/**` SKILL.md** — `update-claude-docs` (34 hits — most), `repo-code-review` (9), `next-plan` (5), `update-vcxproj`, `update-affected-code`, `session-audit`, `add-collection`, `add-collection-member`, `code-style-review`, `glsl-review`, `generate-architecture-diagram`, `external-*`, `save-plan`, `analyze-diagsession`, etc. The `update-claude-docs` skill **directory/name is NOT renamed** — only its content changes to operate on `AGENTS.md`.
   - **`Documents/Plans/**` + `Documents/Features/**`** — including both `Order.md` indexes (decided: historical docs included). Covers the `.txt` feature plans (`SkyboxRenderPass.txt`, `FlipbookSpriteAnimations.txt`).
   - **Top-level `Documents/*.txt` reference docs** — `Documents/UserInterfaceDesign.txt` (2 hits) references `Screens/CLAUDE.md` memory files [auto-folded sibling — see Additional candidate locations].
   - **C++/GLSL source comments (~24 files)** — e.g. `Common/ExternalHeaders.h`, `Common/Workbuffer.cpp`, `Engine/Source/Network/NetworkSerialization.h`, `Projects/.../Network/NetworkSerialization.cpp`, `Engine/Data/Shaders/Water/WaterDisplacement.comp`, `Engine/Data/Shaders/Smoke/SmokeSpreadTwo.comp`, `Engine/Source/Profile/ProfileManagerBase.h`, `Engine/Source/Graphics/AnimationData.h`, `Engine/Source/Audio/StaticVoices.{h,cpp}`, `Engine/Source/Ui/*WrappersBase.h`, `Projects/.../Save/GameSaveLoad.cpp`, `Projects/.../Agent/AgentCommands*.{h,cpp}`, `DataPacker/Source/ExportJobs/**`, etc. **Verify each hit is a comment, not a string literal, before replacing** (selective `/compile` in acceptance backstops this).
4. **Judgment exceptions — not blind replacement:**
   - Sentences describing Claude Code **mechanics** (how memory files load, that only files named exactly `CLAUDE.md` are auto-loaded, import expansion) must stay **true** — where a sentence is about the loader rather than the repo files, keep `CLAUDE.md` or rephrase. Expected mainly in `.claude/skills/update-claude-docs/SKILL.md`.
   - Add one Directives bullet to the **root `AGENTS.md`** (next to the existing "kept CONCISE" bullet): each `CLAUDE.md` is a one-line `@AGENTS.md` stub for Claude Code loading — edit `AGENTS.md`, never the stubs. The existing "…and CLAUDE.md files are used only by AI agents and must be kept CONCISE" bullet describes the repo's memory files → becomes "AGENTS.md files", reconciled with the new stub note.
5. **Codify the stub pattern as an enforced repo convention in the `update-claude-docs` skill** (`.claude/skills/update-claude-docs/SKILL.md`) — chosen because it is the only skill that both *creates* memory docs (so the create-time rule is actionable there) and has an *audit/review* mode (so the invariant is checked repo-wide). Add:
   - **Pattern statement**: Directory agent-memory lives in `AGENTS.md`; each directory `AGENTS.md` has a sibling `CLAUDE.md` whose entire content is the single line `@AGENTS.md` — the Claude Code import stub. (`Managers/<Name>.AGENTS.md` linked docs are *not* directory memory and get no stub.)
   - **Rule 1 (creation)**: When adding a new directory `AGENTS.md`, create the sibling one-line `@AGENTS.md` `CLAUDE.md` stub in the same edit — never an `AGENTS.md` without its stub. Fold this into the skill's create/"Stub handling" steps.
   - **Rule 2 (stub purity)**: Every `CLAUDE.md` contains exactly `@AGENTS.md` and nothing else — put content in the `AGENTS.md`, never in the stub.
   - **Audit-mode check**: flag any directory `AGENTS.md` missing its `CLAUDE.md` stub, and any `CLAUDE.md` whose content is anything other than the single `@AGENTS.md` line; adjust the mode's `Glob **/CLAUDE.md` discovery to `Glob **/AGENTS.md` (excluding the `Managers/*.AGENTS.md` linked docs and `ThirdParty/`/`RDXmin/`).

## Out of scope

- `RDXmin/` — untracked separate project; untouched (no `RDXmin/AGENTS.md` currently on disk).
- The user's global `~/.claude/CLAUDE.md` — not part of the repo.
- The new `.agents/` directory (holds only a `skills` symlink) — no memory file, nothing to migrate.
- Renaming the `update-claude-docs` skill directory or any other skill name.
- `ThirdParty/` library code (but `ThirdParty/CLAUDE.md` and `ThirdParty/Prebuilts/.../CLAUDE.md` are our docs and **are** migrated). `ThirdParty/implot/.gitignore`'s `CLAUDE.md` line is not ours — leave it.
- Any behavior/code change beyond comment text — no CRC, wire, `kiVersion`, or vcxproj exposure (markdown is not project-membered).

## Acceptance criteria

1. `Glob **/CLAUDE.md` → exactly the original 70 paths, each exactly one line `@AGENTS.md`; `Glob **/AGENTS.md` → 70 siblings + 11 `Managers/*.AGENTS.md`.
2. Repo-wide grep for `CLAUDE.md` (excluding `RDXmin/` and `ThirdParty/implot/.gitignore`) → remaining hits only in the deliberate stub-mechanism / loader-mechanics sentences; zero stale `[CLAUDE.md](...)` links.
3. Selective `/compile` of the touched `.cpp` files passes (confirms no string literal was hit).
4. Fresh Claude Code session in the repo has root memory content in context (import verified).
5. `update-claude-docs/SKILL.md` documents the stub pattern (statement + creation rule + stub-purity rule) and its audit mode flags a missing stub / a non-`@AGENTS.md` `CLAUDE.md`.

## Additional candidate locations

- `Documents/UserInterfaceDesign.txt:82,229` — references `Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md` (a renamed memory file). **Oversight** · **Identical** · high confidence · **Folded** (into step 3 as the "Top-level `Documents/*.txt` reference docs" category). Justification: a top-level `Documents/*.txt` doc, outside the plan's originally-enumerated `Documents/Plans/**` / `Documents/Features/**` / source-comment categories; the link points at a memory file being renamed, so it goes stale if missed. Same mechanical `CLAUDE.md`→`AGENTS.md` path-reference replace, markdown-only, zero invariant exposure.
- `Documents/Features/Graphics/FlipbookSpriteAnimations.txt:164` cites `Engine/Source/Frame/Collections/Flipbooks/CLAUDE.md` — a **not-yet-created** path (future Flipbooks collection). Already covered by the `Documents/Features/**` sweep; blind `→AGENTS.md` is correct and consistent (the feature, when built, creates `AGENTS.md`). No separate action.

## Notes

- Decisions already made with the user: manager docs rename YES; reference scope = repo-wide replace (incl. historical docs).
- Grill-confirmed premise (load-bearing): Claude Code loads `CLAUDE.md` only, **not** `AGENTS.md` natively → the one-line `@AGENTS.md` stubs are required and load each memory file exactly once (no double-load). Do not drop the stubs. Verified separately: no `.vcxproj`/`.filters` or `.json` config references any `CLAUDE.md`/`AGENTS.md` path, so the rename has zero build/config exposure.
- Mechanical rename + stub creation is scriptable in one PowerShell loop; reference replacement splits across Sonnet subagents by area with the judgment exceptions above.
- This plan textually touches nearly every live plan file and SKILL.md — **execute alone** (no concurrent sessions), or refresh other sessions' citations after; conflicts are textual only (no code overlap).
- Verification (Acceptance criterion 4) requires a fresh Claude Code session to confirm the `@AGENTS.md` import resolves — this is the only non-grep check.
