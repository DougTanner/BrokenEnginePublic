---
name: update-claude-docs
description: Updates CLAUDE.md documentation files in two modes. **Sync mode** (auto post-edit): scoped sync for directories modified this session — trigger when called after code changes. **Audit mode** (explicit): rubric-based repo-wide quality assessment, produces a quality report, then applies improvements. Trigger phrases for sync mode — "update CLAUDE.md", "sync project memory", "refresh the docs". Trigger phrases for audit mode — "audit CLAUDE.md", "grade CLAUDE.md files", "CLAUDE.md quality report", "improve CLAUDE.md across the repo".
allowed-tools: [Read, Edit, Write, Grep, Glob]
---

# Update CLAUDE.md Documentation

Two modes:

- **Sync mode** — default when invoked after code changes. Targets only the directories touched this session. Small, surgical edits.
- **Audit mode** — invoked explicitly by trigger phrases like "audit CLAUDE.md" or when the user wants a broad repo-wide pass. Produces a quality report first, then applies improvements after user approval.

Mode is inferred from the invocation: if the caller passes a file list (or the session clearly edited specific directories), run sync mode. If the user asks for an audit / report / grade / broad improvement, run audit mode.

---

## Sync Mode (default)

1. **Identify affected directories**: Determine which directories contain modified files. If invoked as a subagent, the caller should provide the list of changed files. Otherwise, enumerate files touched by Edit/Write/NotebookEdit tool calls in this session (DO NOT run git commands — the project forbids them; if session memory is insufficient, ask the user for the file list). Update CLAUDE.md only in the immediate directories containing those files (not parent directories unless their content directly changed). **Hub drift**: if a hub CLAUDE.md was modified this session (root, `Engine/Source`, `Common`, any `Collections` hub), also audit its immediate descendants for newly-stale duplicated content — leaves often carry pre-trim copies of hub wording.

2. **Discover the existing CLAUDE.md tree**: Before editing, Glob `**/CLAUDE.md` (excluding `ThirdParty/` and `Documents/Plans/`) so you know what sibling docs exist. This informs cross-linking and prevents creating a new CLAUDE.md where a parent already covers the subsystem.

3. **Read existing CLAUDE.md**: Before editing, read each affected directory's CLAUDE.md (if it exists) to understand what's already documented.

4. **Create or update CLAUDE.md**:
   - **Default to inaction**: Prefer modifying existing content over adding new content. If the doc still reads true after the code change, do nothing — that is a valid outcome and the most common one. Small additions (a method, a member, a refactor, a bug fix) should typically produce zero new lines. Only add net-new content for substantial changes: a new subsystem, a new cross-cutting pattern, or a non-obvious invariant a reader would make a worse decision without.
   - **Don't name-drop what you just added**: If a code change introduced a method, member, or enum, do NOT document it by name. Document the behavior category. The name belongs in the code.
   - **Classify hub vs. leaf**: A hub serves multiple child subsystems (root, `Engine/Source`, `Common`, any `Collections` hub); a leaf documents one subsystem. Length target follows from classification (see step 7).
   - **Hubs must claim territory**: If the directory is a hub whose children share a pattern (base class, protocol, layout, file-split convention), state ONCE at the hub that children do not re-document this pattern. This is what stops leaves from defensively re-stating the same boilerplate.
   - If a CLAUDE.md exists, update only the sections affected by the code changes.
   - Only create a new CLAUDE.md if the directory represents a distinct subsystem (not for single-file utility directories).
   - The CLAUDE.md file should ONLY reflect what is CURRENTLY in the code — do not mention changes, fixes, or reference what was previously there.

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

8. **Stub handling**: If a directory is no longer a distinct subsystem (refactored away, collapsed into a parent), report the stub CLAUDE.md path to the user and recommend deletion. This skill's allowed-tools list does not include filesystem delete, so leave the actual removal to the user.

---

## Audit Mode (explicit invocation)

Produces a quality report across the repo's CLAUDE.md files before making any changes.

### Phase A1: Discovery

Glob `**/CLAUDE.md` (excluding `ThirdParty/` and `Documents/Plans/`). Also check for `./.claude.local.md` and `./CLAUDE.md` at repo root.

### Phase A2: Quality Assessment

For each file, score against this rubric (each row 0–20, total 100):

| Criterion | Weight | Check |
|-----------|--------|-------|
| Commands/workflows | 20 | Are build/test/deploy commands present and current? |
| Architecture clarity | 20 | Can Claude understand the subsystem's shape in one read? |
| Non-obvious patterns | 15 | Are gotchas (allocation tracking, determinism, `BT_CLIENT` guards, SOA pitfalls) documented? |
| Conciseness | 15 | No member-by-member listings; respects 20–50 line target (100 for hubs)? |
| Currency | 15 | Reflects current codebase state (no stale paths, removed APIs)? |
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

### Phase A3: Quality Report

**Output the quality report BEFORE any edits.** Format:

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
- Don't duplicate content from `Documents/Architecture/` — link instead (duplication wastes LLM context and drifts from the source of truth over time)

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

---

## Important Notes

- **Context window awareness**: These files are added to an LLM's finite context window. Balance clarity against conciseness.
- **No duplication**: Do not repeat details from parent CLAUDE.md, `Documents/Architecture/` docs, sibling CLAUDE.mds (e.g., Client/Server pairs), or parallel hierarchies (e.g., engine `Network/` vs. game `Network/`). Pick one canonical location and link from the rest.
- **Cross-references**: Reference child directory CLAUDE.md files with links when relevant.
- **Consistency**: Maintain consistency with existing CLAUDE.md style in the codebase.
