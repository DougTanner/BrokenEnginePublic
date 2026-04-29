---
name: gaea2-save
description: Save edits made to a Gaea 2 Markdown view back to a .terrain JSON file. Use this skill whenever the user is finished editing a loaded terrain and wants to write it back out — phrases like "save this terrain", "write back to .terrain", "export the gaea file", or invoking `/gaea2-save`. Reconstitutes the Newtonsoft $id/$ref object graph; pairs with /gaea2-load and /gaea2-modify.
argument-hint: <name-or-Temp/path.md> [--output <path.terrain>]
allowed-tools: [Read, Write, Bash, PowerShell]
---

# gaea2-save

Convert a `Temp/<name>.md` (plus its `.passthrough.json` sidecar) back to a Gaea 2 `.terrain` JSON file.

## Workflow

1. **Resolve the input path.** `$ARGUMENTS` holds either a bare name (`Stylized Mountain`), a `.md` filename, or a full path; optionally followed by `--output <path>`.
   - Bare name → `Temp/<name>.md`.
   - `.md` filename without dir → `Temp/<filename>`.
   - Otherwise use as given.
   - If `--output` is absent, default to `<input-base>.terrain` next to the .md file. If the user wants to overwrite the original, pull the original path from the `.passthrough.json`'s `original_file` field and offer that.

2. **Verify Python 3.10+ is available.** Same bootstrap as `/gaea2-load` (see that skill for rationale):
   ```
   powershell -ExecutionPolicy Bypass -File .claude/skills/gaea2-shared/scripts/detect-python.ps1
   ```
   Capture the `<python-exe-path>` from the `OK` line for step 3. On `MISSING ...` or `STALE ...`, ask permission to install, then run:
   ```
   winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements
   ```
   and re-run the bootstrap.

3. **Run the saver using the detected Python path.**
   ```
   "<python-exe-path>" .claude/skills/gaea2-shared/scripts/save_terrain.py "<Temp/name.md>" [--output "<path.terrain>"]
   ```
   The script:
   - Parses frontmatter, Mermaid topology, node sections, notes, and variables.
   - Reconstitutes the asset structure with Build/Automation/State/BuildProfiles from passthrough.
   - Renumbers `$id` values document-order, then rewrites all `$ref` pointers via the old-to-new remap. **The two passes are required**: Newtonsoft expects $id 1..N contiguous in document order, and every $ref must resolve to an earlier $id — collapsing the renumber and ref-fixup into one pass would break refs that point forward, so don't simplify it.
   - Sets each port's `Parent: {$ref: <node-$id>}` after the renumber pass.
   - Surfaces WARNINGs to stderr for: nodes without a passthrough entry, edges that didn't bind to any input port, synthesized type FQNs.

4. **Sanity-check the output.** Read the produced `.terrain`, confirm:
   - It's valid JSON (the Read tool will fail otherwise).
   - First key is `"$id": "1"` and the `$id` sequence is contiguous.
   - Asset/Terrain/Nodes structure looks like the original.
   Surface any WARNINGs from step 3 to the user. Report node count, edge count, and output path back.

5. **Optional diff vs original.** If the user asks "did anything change?" or you have low confidence in a tricky modification, diff the output against the original `.terrain` (path is in `.passthrough.json` → `original_file`). Strip the noise that always changes between saves before comparing — `$id` values, dates, viewport floats. The remaining diff should be exactly the user's edits.

## Round-trip caveats to surface

- **`$id` values change every save.** That's by design (Newtonsoft regenerates them). Don't flag this as a diff.
- **Dates change every save.** `DateLastSaved` is updated by Gaea; the saver leaves the field alone (your edit-time value), so a Gaea-then-tool round-trip will diverge there. Acceptable.
- **Floats may have tiny representation drift.** Python's `repr(float)` differs from C# `double.ToString("R")` in some corner cases. If you see `15.870041` becoming `15.870041000000001`, that's the round-trip — values are equal in IEEE 754 but the text differs. Note it but don't fix.
- **Unknown property keys passthrough verbatim.** If the user added `Foo: 1` to an Erosion2 node, the saver writes `Foo: 1` into the JSON. Gaea will ignore unknown keys, but they survive future loads. Warn the user if you see suspicious keys.

## Failure modes

- **`ERROR: sidecar not found`** — the `.passthrough.json` was deleted. Re-run `/gaea2-load` from the original `.terrain` to regenerate.
- **`WARNING: edge ... not wired`** — Mermaid block references a `to_port` name that doesn't exist on the target node's port catalogue. Save still produces a file; Gaea may load with that input disconnected. Most often: a node was added in `/gaea2-modify` without copying a real port catalogue.
- **`WARNING: node ... has no passthrough entry`** — same root cause; the saver synthesizes a FQN and emits no ports, which Gaea may reject outright.
