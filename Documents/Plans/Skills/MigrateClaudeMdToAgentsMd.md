# Migrate CLAUDE.md → AGENTS.md (stub imports)

## Context

The repo's agent memory lives in 70 `CLAUDE.md` files, but the emerging cross-tool standard is `AGENTS.md`. Claude Code does not read AGENTS.md natively; it does mechanically expand `@`-imports in CLAUDE.md at load time (resolved relative to the containing file, no model action — https://code.claude.com/docs/en/memory.md#import-additional-files). Goal: single source of truth in AGENTS.md, readable by other agent tools, with one-line CLAUDE.md stubs keeping Claude Code loading identical content. Symlinks rejected: on Windows clones without Developer Mode, git checks a committed symlink out as a plain text file containing only the target path — the import syntax silently breaks.

## Design

For each of the 70 directories containing a `CLAUDE.md`:
1. Rename `CLAUDE.md` → `AGENTS.md` (plain `Rename-Item`; git detects renames at commit — repo rule allows agents only read-only git commands).
2. Create a new `CLAUDE.md` beside it containing exactly one line: `@AGENTS.md` (imports resolve relative to the containing file, so each stub picks up its sibling).

Also rename the 11 linked per-manager docs `Engine/Source/Graphics/Managers/<Name>.CLAUDE.md` → `<Name>.AGENTS.md`. These were never auto-loaded memory (only files named exactly `CLAUDE.md` are), so they get **no stubs** — rename and fix inbound links only.

### Reference updates (repo-wide)

~430 textual `CLAUDE.md` references across ~150 files. Replace `CLAUDE.md` → `AGENTS.md` (and `<Name>.CLAUDE.md` → `<Name>.AGENTS.md`) in:
- The renamed memory files — cross-links like `[CLAUDE.md](Common/CLAUDE.md)` become `[AGENTS.md](Common/AGENTS.md)`.
- `.claude/skills/**` — `update-claude-docs`, `repo-code-review`, `update-vcxproj`, `add-collection`, `next-plan`, `session-audit`, etc. The `update-claude-docs` skill directory/name is NOT renamed; only its content changes to operate on AGENTS.md files.
- `Documents/Plans/**`, `Documents/Features/**` including both `Order.md` indexes (decided: historical docs included).
- C++/GLSL source comments (~20 files, e.g. `Common/Workbuffer.cpp`, `Common/ExternalHeaders.h`, `Engine/Source/Network/NetworkSerialization.h`, `Engine/Data/Shaders/Water/WaterDisplacement.comp`). Verify each hit is a comment, not a string literal, before replacing.

**Judgment exceptions — not blind replacement:**
- Sentences describing Claude Code *mechanics* (e.g. how memory files load) must stay true — where a sentence is about the loader rather than the repo files, keep `CLAUDE.md` or rephrase. Expected mainly in `.claude/skills/update-claude-docs/SKILL.md`.
- Add one Directives bullet to the root `AGENTS.md` (next to the existing "kept CONCISE" bullet): each `CLAUDE.md` is a one-line `@AGENTS.md` stub for Claude Code loading — edit AGENTS.md, never the stubs.

## Critical files

- All 70 `CLAUDE.md` files (root plus subdirectories — full list via `Glob **/CLAUDE.md`)
- `Engine/Source/Graphics/Managers/*.CLAUDE.md` (11 files)
- `.claude/skills/**/SKILL.md` reference sweep
- `Documents/Plans/**`, `Documents/Features/**` reference sweep (incl. both `Order.md`)
- ~20 source files with comment references (grep `CLAUDE\.md` at execution for the current list)

## Out of scope

- `RDXmin/` — untracked separate project, already has its own AGENTS.md; untouched.
- The user's global `~/.claude/CLAUDE.md` — not part of the repo.
- Renaming the `update-claude-docs` skill directory or other skill names.
- `ThirdParty/` library code (but `ThirdParty/CLAUDE.md` and `ThirdParty/Prebuilts/.../CLAUDE.md` are our docs and ARE migrated).
- Any behavior/code change beyond comment text — no CRC, wire, `kiVersion`, or vcxproj exposure (markdown is not project-membered).

## Acceptance criteria

1. `Glob **/CLAUDE.md` → exactly the original 70 paths, each exactly one line `@AGENTS.md`; `Glob **/AGENTS.md` → 70 siblings + 11 `Managers/*.AGENTS.md` (+ untouched `RDXmin/AGENTS.md`).
2. Repo-wide grep for `CLAUDE.md` (excluding `RDXmin/`) → remaining hits only in the deliberate stub-mechanism sentences; zero stale `[CLAUDE.md](...)` links.
3. Selective `/compile` of the touched `.cpp` files passes (confirms no string literal was hit).
4. Fresh Claude Code session in the repo has root memory content in context (import verified).

## Notes

- Decisions already made with the user: manager docs rename YES; reference scope = repo-wide replace.
- Mechanical rename + stub creation is scriptable in one PowerShell loop; reference replacement splits across Sonnet subagents by area with the judgment exceptions called out above.
- This plan textually touches nearly every live plan file and SKILL.md — execute alone (no concurrent sessions), or refresh other sessions' citations after; conflicts are textual only.
