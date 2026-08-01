---
name: gaea2-modify
description: Edit a Gaea 2 terrain previously loaded by /gaea2-load — add/remove/move/rewire nodes or change node properties. Operates on the Markdown file in Temp/ produced by /gaea2-load; write the result back to a .terrain with /gaea2-save.
argument-hint: <name-or-Temp/path.md>
allowed-tools: [Read, Edit, Write, Glob, Grep, PowerShell]
disable-model-invocation: true
---

# gaea2-modify

Edit the Markdown view of a loaded Gaea terrain. Nothing but Claude edits `Temp/<name>.md`; the only script this skill runs is the read-only validator in "Sanity checks before stopping". The companion `.passthrough.json` is read-only here; if a change requires altering ports or modifiers, document the limitation and ask the user.

## Resolve the target

`$ARGUMENTS` holds either a bare name (`Stylized Mountain`), a `.md` filename, or a full path. Resolve in this order:
1. If it's a path that exists, use it.
2. Otherwise look for `Temp/<arg>.md` or `Temp/<arg>`.
3. If still ambiguous, list `Temp/*.md` and ask the user which.

## Edit operations

The Markdown is the source of truth for topology, properties, positions, names, notes, and variables. The Mermaid block and per-node sections must stay in sync.

### Edit a property
Find `### n<ID>: <Type>`, edit the `key: value` line beneath it.
- Floats like `Duration: 15.870041` — change the number, keep float syntax (a trailing `.0` if integer-valued).
- Ints like `Seed: 40828` — bare integer, no decimal.
- Strings — quote with `"..."` only if the value contains `:`, `#`, leading/trailing whitespace, or starts with `[`/`{`.
- Booleans — `true` / `false` (lowercase).

### Move a node
Edit the `position:` line: `position: 27225.0, 26250.0`. Two floats, comma-separated. The X axis grows right; Y grows down (canvas space). Adjacent grid spacing in the examples is typically ~150–300 units.

### Rename a node
Edit the `name: ...` line under the node's heading. If no `name:` line exists (the loader omits it when name == type), add one.

### Add a node
1. Pick a free integer ID in the range 100-998: scan all `### n<ID>:` headings and pick `max(IDs) + 1`. Never use an ID below 100 — Gaea 2 silently deactivates it (details under "Id must be ≥ 100" in `../gaea2-shared/references/node-conventions.md`).
2. Append a new node section at the bottom of `## Nodes`:
   ```
   ### n<NEW_ID>: <Type>
   position: <X>, <Y>
   <key>: <value>
   ...
   ```
3. Add `n<NEW_ID>["<Type>"]` to the Mermaid block (between `flowchart TD` and the edges).
4. The save step needs port info for new nodes, which lives only in `.passthrough.json`. Tell the user: "I added <Type> as node n<NEW_ID>, but its port catalogue isn't in passthrough yet — saving will use a default In/Out pair. If <Type> needs more ports (e.g. Erosion2's Flow/Wear/Deposits, Combine's secondary input), you'll need to copy the port block from another file of the same type." Offer to find a node of the same type in the shipping examples (`C:/Program Files/QuadSpinner/Gaea 2/Examples/`) and quote its port block. (`.claude/skills/gaea2-shared/examples/` is intentionally empty — see its README.)
5. Check the per-type requirements in `../gaea2-shared/references/node-conventions.md`. Several node types need specific fields (e.g. `Version: 2`) or valid enum values, otherwise Gaea silently loads them as deactivated. The validator in "Sanity checks before stopping" catches the field-level ones; whether an enum value is legal it cannot judge — you must.

### Remove a node
1. Delete the entire `### n<ID>: ...` section (heading + its property lines).
2. Delete the `n<ID>["<Type>"]` line from the Mermaid block.
3. Delete every Mermaid edge that mentions `n<ID>` (either side).

### Rewire (change edges)
Edit edge lines inside the Mermaid block.
- Default port pair (`Out` → `In`): `nA --> nB`.
- Non-default ports: `nA -- "FromPort → ToPort" --> nB`. Use the U+2192 arrow `→` (not `->`); the loader emits this exact char.
- To re-target an edge, change either side. To add an edge, append a new line. To remove, delete the line.
- A node's input port can be wired to at most one producer (Gaea graph rule). If you add an edge into an input port that's already wired, remove the old edge first; otherwise the saver keeps only one Record per port and Gaea may reject the file.

### Edit Notes
The `## Notes` section uses `### note<ID>` blocks with key/value lines. The `markdown:` value supports a `|` block for multi-line text — keep the 2-space indentation on continuation lines.

### Edit Variables
Plain `key: value` lines under `## Variables`. Replace `(none)` with one or more lines if adding variables.

## Gaea 2 conventions when adding new nodes

Before adding or retyping any node, read `../gaea2-shared/references/node-conventions.md` — Id floors, types that require `Version: 2`, required input ports, per-type enum values, and `PortCount` rules all live there.

## Sanity checks before stopping

Run the validator on the edited file before reporting done. It is required, not optional, and it reads only — it never edits the Markdown or the sidecar:

```powershell
$Wrapper = Join-Path (git rev-parse --show-toplevel) '.agents/skills/gaea2-shared/scripts/Invoke-Gaea2Python.ps1'
pwsh -NoProfile -ExecutionPolicy Bypass -Command "& '$Wrapper' -Script validate_markdown.py -Arguments '<Temp/name.md>'"
```

Use `-Command`, not `-File`: that is the form that passes the `-Arguments` list through intact. The wrapper resolves relative paths and `Temp/` against the repository root whatever the current directory is, and prints one `broken-engine-gaea2-python/v1` envelope. On `python.missing` / `python.stale` (exit 2) nothing ran: tell the user Python is missing or too old and never install it yourself — for `python.missing` pass on the envelope's `installCommand` as the suggested command for them to run, and for `python.stale` quote its `message`/`probeLine` instead (`installCommand` is null there; the user resolves it by upgrading). Otherwise the validator's own `broken-engine-gaea2-markdown/v1` envelope is the string in the wrapper's `stdout` field — read its `code`, not the exit code. That holds for `python.script-failed` too: the validator exits 2 on its own blocked codes, so parse the inner envelope out of `stdout` for the diagnostic code first, and fall back to the wrapper's `stderr` only when `stdout` carries no envelope. The inner codes:

- `ok` — no entries; the mechanical checks pass.
- `markdown.entries-reported` — `payload.entries` lists `{nodeId, rule, message}` findings. Fix every one before reporting done. The rules cover Mermaid-versus-section agreement in both directions, duplicate IDs, added nodes with an ID below 100, edges naming a node that doesn't exist, types missing `Version: 2`, dynamic-port nodes missing `PortCount`, unwired required In ports, an input port fed by more than one producer, and nodes with no path to an output.
- `input.invalid`, `markdown.missing`, `markdown.unreadable`, `passthrough.missing`, `passthrough.unreadable`, `markdown.structure-unparsed` — the validator couldn't read or parse the file at all, so it proved nothing. Resolve that before reporting done.

Never reconstruct this inline: don't run the shared `.agents/scripts/Detect-Python.ps1` probe the wrapper already calls, and don't invoke `validate_markdown.py` with a bare `python` or a captured interpreter path.

One limit is worth knowing: the reaches-output rule is skipped entirely when the graph has no output anchor at all — no `Export` node and no node carrying `SaveDefinition`. That is common in the shipping examples (`Mesa` is one), so a dead-end chain in such a file goes unreported; examples that do carry a `SaveDefinition` anchor are checked normally.

Then do the judgment the validator can't:

- Check that property values still parse as their original type — don't put a string where a float was, and see the type rules under "Edit a property".
- For any newly-added node, surface the port-catalogue limitation to the user (the saver will use a default In/Out pair until passthrough is updated) so they can decide whether to copy a port block from a sibling example.
- For any newly-added node, check the enum values against "Per-type enum constraints" in `../gaea2-shared/references/node-conventions.md`; an invalid one silently deactivates the node and the validator won't see it.

## What this skill does NOT do

- Doesn't change ports/modifiers — those are in passthrough JSON.
- Doesn't change `$type` / type FQNs — pinned by passthrough.
- Doesn't validate that a property name is real for the given node type — the parser is permissive; if you invent `Foo: 1` on an Erosion2 it'll round-trip through but Gaea will likely ignore it.
