---
name: update-claude-docs
description: Updates CLAUDE.md files after code changes — syncs docs in the directories modified this session. Also has an explicit audit mode that grades every CLAUDE.md in the repo against a rubric, reports, then applies improvements.
when_to_use: Sync mode (default) — after any code change, or "update CLAUDE.md", "sync project memory", "refresh the docs". Audit mode — "audit CLAUDE.md", "grade CLAUDE.md files", "CLAUDE.md quality report", "improve CLAUDE.md across the repo".
allowed-tools: [Read, Edit, Write, Grep, Glob]
---

# Update CLAUDE.md Documentation

Two modes — infer from the invocation:

- **Sync mode** (default) — invoked after code changes or with a list of changed files. Targets only the directories touched this session; small, surgical edits.
- **Audit mode** — only when the user asks for an audit / report / grade / repo-wide improvement pass. Quality report first, then improvements after user approval.

---

## Sync Mode (default)

1. **Identify affected directories**: If invoked as a subagent, the caller should provide the list of changed files. Otherwise, enumerate files touched by Edit/Write/NotebookEdit tool calls in this session — don't use `git status`, it can't distinguish this session's edits from pre-existing changes; if session memory is insufficient, ask the user for the file list. Update CLAUDE.md only in the immediate directories containing those files (not parent directories unless their content directly changed).
   - **Hub drift**: if a hub CLAUDE.md was modified this session (root, `Engine/Source`, `Common`, any `Collections` hub), also audit its immediate descendants for newly-stale duplicated content — leaves often carry pre-trim copies of hub wording.

2. **Discover the existing CLAUDE.md tree**: Before editing, Glob `**/CLAUDE.md` (excluding `ThirdParty/` and `Documents/Plans/`) so you know what sibling docs exist. This informs cross-linking and prevents creating a new CLAUDE.md where a parent already covers the subsystem.

3. **Read existing CLAUDE.md**: Before editing, read each affected directory's CLAUDE.md (if it exists) to understand what's already documented.

4. **Create or update CLAUDE.md**:
   - **Default to inaction**: Prefer modifying existing content over adding new content. If the doc still reads true after the code change, do nothing — that is a valid outcome and the most common one. Small additions (a method, a member, a refactor, a bug fix) should typically produce zero new lines. Only add net-new content for substantial changes: a new subsystem, a new cross-cutting pattern, or a non-obvious invariant a reader would make a worse decision without.
   - **Classify hub vs. leaf**: A hub serves multiple child subsystems (root, `Engine/Source`, `Common`, any `Collections` hub); a leaf documents one subsystem. Length target follows from classification (see step 7).
   - **Hubs must claim territory**: If the directory is a hub whose children share a pattern (base class, protocol, layout, file-split convention), state ONCE at the hub that children do not re-document this pattern. This is what stops leaves from defensively re-stating the same boilerplate.
   - If a CLAUDE.md exists, update only the sections affected by the code changes.
   - Only create a new CLAUDE.md if the directory represents a distinct subsystem (not for single-file utility directories).
   - The CLAUDE.md reflects only the current state of the code — never edit history. Do not mention changes, fixes, what was previously there, or the session/plan that produced them (see Content Guidelines → No Changelogs).

5. **Trim bloat**: For every CLAUDE.md you read or wrote, if it exceeds its length target (see step 7), trim the most verbose sections before finishing.

6. **Typical structure** (use sections as they fit; omit ones that don't apply):
   ```markdown
   # [Directory Name] - [One-line Purpose]

   ## Overview
   [2-3 sentences on what this code does and why it exists]

   ## Key Classes/Systems
   - **ClassName** - What it does and its role in the system

   ## Architecture Notes
   [Design decisions, patterns used, data flow between components]

   ## See Also
   - [Subdirectory/CLAUDE.md](Subdirectory/CLAUDE.md) - Brief description
   ```

7. **Length target**: Aim for 20-50 lines per subsystem CLAUDE.md. Cross-cutting hubs (repo root, `Engine/Source`, `Common`) may reach ~100 lines because they document patterns used everywhere. If longer than that, you're too detailed.

8. **Stub handling**: If a directory is no longer a distinct subsystem (refactored away, collapsed into a parent), report the stub CLAUDE.md path to the user and recommend deletion — leave the actual removal to the user.

---

## Audit Mode (explicit invocation)

Produces a quality report across the repo's CLAUDE.md files before making any changes.

### Phase A1: Discovery

Glob `**/CLAUDE.md` (excluding `ThirdParty/` and `Documents/Plans/`); also include a root `CLAUDE.local.md` if one exists.

### Phase A2: Quality Assessment

For each file, score against this rubric (each row scored 0 to its weight, total 100):

| Criterion | Weight | Check |
|-----------|--------|-------|
| Commands/workflows | 20 | Are commands/workflows present and current? Builds are delegated to the `/compile` skill — a leaf with no commands to document scores full here. |
| Architecture clarity | 20 | Can Claude understand the subsystem's shape in one read? |
| Non-obvious patterns | 15 | Are gotchas (allocation tracking, determinism, `BT_CLIENT` guards, SOA pitfalls) documented? |
| Conciseness | 15 | No member-by-member listings; respects 20–50 line target (100 for hubs)? |
| Currency | 15 | Reflects current codebase state (no stale paths, removed APIs); no changelog / edit-history narration (see Content Guidelines → No Changelogs)? |
| Actionability | 15 | Instructions are executable — not vague? |

Grades: **A** 90+, **B** 70–89, **C** 50–69, **D** 30–49, **F** <30.

**Broken-Engine-specific checks** to apply during grading:
- No enumeration of struct members, enum values, or individual variables
- No "File Structure" / file-list section (almost always repeats the parent's splitting convention or disguises method-internal narration)
- No uniform/binding/push-constant enumeration in shader CLAUDE.mds
- Cross-references to `Documents/Architecture/` diagrams instead of duplicating their content
- No duplication with sibling CLAUDE.mds (Client/Server pairs) or parallel hierarchies (engine `Network/` vs. game `Network/`)
- SOA / `Collection<T>` conventions mentioned where relevant
- No stale filepaths (refactored-away directories)
- No changelog / edit-history narration — "this run/session", "landed", "was/used to/previously", "renamed/moved from", "replaces the old …", dated or commit-referenced notes; CLAUDE.md states current behavior only, and git history is the forensic record (see Content Guidelines → No Changelogs)
- Normal, direct tone — no ALL-CAPS emphasis, "IMPORTANT"/"CRITICAL"/"YOU MUST" markers, or rules restated for emphasis (see Tone and Emphasis below)
- `@` before a path only where a live import is intended — `@path` in CLAUDE.md inlines the target file into context at load; see-also references use plain markdown links

### Phase A3: Quality Report

Output the quality report before making any edits. Format:

```
## CLAUDE.md Quality Report

### Summary
- Files found: X
- Average score: X/100
- Files needing update: X (grade C or below)

### File-by-File Assessment

#### <path/CLAUDE.md>
**Score: XX/100 (Grade: X)**

| Criterion | Score | Notes |
|-----------|-------|-------|
| Commands/workflows | X/20 | ... |
| Architecture clarity | X/20 | ... |
| Non-obvious patterns | X/15 | ... |
| Conciseness | X/15 | ... |
| Currency | X/15 | ... |
| Actionability | X/15 | ... |

**Issues:** [list]
**Recommended additions:** [list]
```

### Phase A4: Apply Improvements

After user approval, apply edits following the sync-mode content rules (§Content Guidelines below). Show diffs per file; preserve existing structure; keep each file under its length target.

---

## Content Guidelines (both modes)

### No Changelogs — Current State Only

CLAUDE.md documents how the code is now, never how it got here — it is not a changelog, migration log, or record of a session's work. The repo's git history is the forensic record; docs carry no diff of themselves. This governs every edit in both modes and the audit rubric's Currency criterion.

Never write, and remove when you find it:
- Session/process narration: "this session", "this run", "landed", "has been removed", "dropped", "added/removed as part of X", or references to the plan/commit that produced a change.
- Before/after narration: "was X", "used to be", "previously", "changed from/to", "renamed/moved from", "replaces the old …", "the deleted/now-removed <symbol>", and "no longer" when it recounts a past edit.
- Dated or commit-referenced notes: "as of <date/version>", parenthetical dates, commit hashes.

A plain "no longer"/"never" describing a *current* runtime condition ("the thread is no longer running at that point") is fine — the test is whether the sentence states present behavior or narrates an edit. When a code change makes a sentence stale, replace it with the new present-tense fact; don't append the new fact beside the old one, and don't note that it changed.

### Vocabulary and Pattern Consistency Across the CLAUDE.md Tree

The CLAUDE.md tree (root → subsystem hubs → leaves) is this codebase's authoritative glossary; editing a leaf without checking the parents leaves it internally inconsistent. Two rules apply on every edit:

1. **Use established vocabulary.** When the doc you're writing names a concept that's already defined upstream (root `CLAUDE.md`, the nearest hub like `Engine/Source/CLAUDE.md`, or a sibling subsystem doc), use the same term. Examples that already have a fixed term in this codebase: *Collection*, *Frame*, *Members() / SharedMembers() / ClientMembers()*, *Update / PostRender / Interpolate phases*, *workbuffer*, *gp\* singleton*, *SOA*, *EWNS*, *deterministic CRC*. Don't introduce "component," "entity manager," "tick stage," "scratch buffer," or other near-synonyms — they fragment the glossary. If you find yourself inventing a term, first grep the existing CLAUDE.md tree for what the codebase already calls the thing.

2. **Flag conflicts; don't silently override.** If a code change you're documenting *contradicts* a pattern already stated in a parent or sibling CLAUDE.md, do not just overwrite the leaf with the new behaviour. Surface the contradiction in the output:

   > _Note: this change contradicts the "all engine code accesses game through `game::gpGame`" rule in `Engine/Source/CLAUDE.md`. The contradiction may be intentional (new exception) or accidental (rule still holds and the change should be revisited)._

   The user must decide which it is — a silent override leaves two docs describing the same area with conflicting rules, and the next reader has to guess which is current. When running as a subagent, report the contradiction as a residual for the caller to route to the user; when running interactively in the main session, ask the user directly.

### Tone and Emphasis

Current models follow instructions well; aggressive emphasis causes over-triggering and drowns out neighboring rules. When writing or rewriting any CLAUDE.md content:

- Use normal, direct language: "Use X when Y", not "CRITICAL: YOU MUST use X".
- No ALL-CAPS words or "IMPORTANT"/"NEVER"/"MUST" markers. A plain lowercase "never"/"always" inside a sentence is fine for genuine hard rules.
- State each rule exactly once across the whole CLAUDE.md tree — restating for emphasis is duplication, not reinforcement.
- Bold is for bullet lead-in labels, not for shouting.
- Specificity beats volume: a precise rule naming the exact API outperforms an emphatic vague one.
- Exception: the root `CLAUDE.md` C++ Code Change Process section keeps full emphasis (IMPORTANT/YOU MUST) by explicit user decision — do not flag or soften it.

### DO: Focus on Purpose and Architecture
- Document what classes/systems DO, not what members they HAVE
- Explain design patterns and relationships between components
- Describe data flow and system interactions
- Mention key algorithms or architectural decisions

**Good examples:**
- "Manages GPU buffer allocation with automatic memory pooling and defragmentation"
- "Implements observer pattern for decoupled event propagation between game systems"
- "Uses SOA layout for cache-efficient iteration over thousands of entities"

### DON'T: Document Implementation Details
- Don't list individual variables, members, constants, or parameters
- Don't enumerate every struct member, enum value, or flag
- Don't provide member-by-member breakdowns
- Don't narrate method internals or call chains (e.g., "UpdateClient() calls PollAndReconcile() which calls X then Y then Z") — that's what the code is for
- Don't add a "File Structure" / file-list section unless a file has a genuinely unusual role that can't be inferred from its name. The common "one-line-per-.cpp" pattern is either repeating the parent's splitting convention or disguised method-internal narration.
- Don't name-drop code you just added. If a change introduced a method/member/enum, describe the behavior category instead of naming the symbol — the name belongs in the code and drifts quickly in docs.
- In shader CLAUDE.mds, don't enumerate uniforms, bindings, or push-constants. Describe the technique, not the interface.
- Don't duplicate content from parent CLAUDE.mds, `Documents/Architecture/` docs, sibling CLAUDE.mds (e.g., Client/Server pairs), or parallel hierarchies (e.g., engine `Network/` vs. game `Network/`) — pick one canonical location and link from the rest (duplication wastes context and drifts from the source of truth)
- Don't prefix paths with `@` unless you intend a live import — `@path` inlines the target file into context at load. Use plain markdown links for see-also references.

**Bad examples:**
- "`bGamepad` - True if gamepad mode active"
- "`kfGamepadThreshold = 0.1f` - Thumbstick deadzone"
- "Position tracking: `pVecPositions[]`, `pVecVelocities[]`"
- "`UpdateServer()` calls `PreTickNetwork()` before physics, handles quickload/replay, calls `WaitForTick()` for the server tick timer, then runs physics..."

**Good alternative:**
- "Contains gamepad state, mouse position, and menu action flags"
- "Orchestrates the server main loop (network polling, physics ticks, broadcast). See [Architecture doc](link) for detailed flow."

### Keep it High-Level

Readers should understand system architecture and responsibilities, not be able to reconstruct class definitions. If you're listing variable names with explanations, you're too detailed.

**Removal test**: For each sentence you're about to keep, ask "would removing this cause a future reader to make a worse decision?" If no, cut it. Apply this on every pass — including sentences that weren't touched by the current code change.
