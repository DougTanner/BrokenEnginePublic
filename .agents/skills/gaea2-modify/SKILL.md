---
name: gaea2-modify
description: Edit a Gaea 2 terrain previously loaded by /gaea2-load — add/remove/move/rewire nodes or change node properties. Operates on the Markdown file in Temp/ produced by /gaea2-load; write the result back to a .terrain with /gaea2-save.
argument-hint: <name-or-Temp/path.md>
allowed-tools: [Read, Edit, Write, Glob, Grep]
disable-model-invocation: true
---

# gaea2-modify

Edit the Markdown view of a loaded Gaea terrain. **No Python is invoked** — Claude edits `Temp/<name>.md` directly. The companion `.passthrough.json` is read-only here; if a change requires altering ports or modifiers, document the limitation and ask the user.

## Resolve the target

`$ARGUMENTS` holds either a bare name (`Stylized Mountain`), a `.md` filename, or a full path. Resolve in this order:
1. If it's a path that exists, use it.
2. Otherwise look for `Temp/<arg>.md` or `Temp/<arg>`.
3. If still ambiguous, list `Temp/*.md` and ask the user which.

## Edit operations

The Markdown is the source of truth for **topology, properties, positions, names, notes, and variables**. The Mermaid block and per-node sections must stay in sync.

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
1. Pick a free integer ID **in the range 100-998**: scan all `### n<ID>:` headings and pick `max(IDs) + 1`. Never use an ID below 100 — Gaea 2 silently deactivates it (details in "Gaea 2 conventions when adding new nodes" below).
2. Append a new node section at the bottom of `## Nodes`:
   ```
   ### n<NEW_ID>: <Type>
   position: <X>, <Y>
   <key>: <value>
   ...
   ```
3. Add `n<NEW_ID>["<Type>"]` to the Mermaid block (between `flowchart TD` and the edges).
4. **The save step needs port info for new nodes**, which lives only in `.passthrough.json`. Tell the user: "I added <Type> as node n<NEW_ID>, but its port catalogue isn't in passthrough yet — saving will use a default In/Out pair. If <Type> needs more ports (e.g. Erosion2's Flow/Wear/Deposits, Combine's secondary input), you'll need to copy the port block from another file of the same type." Offer to find a node of the same type in the shipping examples (`C:/Program Files/QuadSpinner/Gaea 2/Examples/`) and quote its port block. (`.claude/skills/gaea2-shared/examples/` is intentionally empty — see its README.)
5. **Check the per-type requirements in "Gaea 2 conventions when adding new nodes" below.** Several node types need specific fields (e.g. `Version: 2`) or valid enum values, otherwise Gaea silently loads them as deactivated. The skill doesn't validate these — you must.

### Remove a node
1. Delete the entire `### n<ID>: ...` section (heading + its property lines).
2. Delete the `n<ID>["<Type>"]` line from the Mermaid block.
3. Delete every Mermaid edge that mentions `n<ID>` (either side).

### Rewire (change edges)
Edit edge lines inside the Mermaid block.
- Default port pair (`Out` → `In`): `nA --> nB`.
- Non-default ports: `nA -- "FromPort → ToPort" --> nB`. Use the U+2192 arrow `→` (not `->`); the loader emits this exact char.
- To re-target an edge, change either side. To add an edge, append a new line. To remove, delete the line.
- A node's input port can be wired to **at most one** producer (Gaea graph rule). If you add an edge into an input port that's already wired, remove the old edge first; otherwise the saver keeps only one Record per port and Gaea may reject the file.

### Edit Notes
The `## Notes` section uses `### note<ID>` blocks with key/value lines. The `markdown:` value supports a `|` block for multi-line text — keep the 2-space indentation on continuation lines.

### Edit Variables
Plain `key: value` lines under `## Variables`. Replace `(none)` with one or more lines if adding variables.

## Gaea 2 conventions when adding new nodes

Gaea 2 silently rejects (loads as **deactivated, dashed-border node**) any node that's missing a required field, has an invalid enum value, or has an Id below 100. The .terrain file still parses as JSON, but the node won't compute. Things to check whenever you add a node type:

### Id must be ≥ 100

The single most common cause of "node loads but is deactivated." Verified across the full shipping Examples library (659 nodes in 59 files at the Gaea 2.3.0.1 survey) — every Id is in 100-998. Never assign an Id of 0-99. If translating from a legacy format (e.g. Gaea 1) whose IDs start at 0, offset all of them (e.g. +100) before writing the markdown.

### Types that require `Version: 2`

Surveyed across the full shipping Examples library (59 files at the Gaea 2.3.0.1 survey) — these types **always** have `Version: 2` and are deactivated without it:

- `Mixer` (7/7 examples) — also requires `PortCount` and at least `Layer1` JSON object
- `TextureBase` (24/24)
- `Trees` (6/6)
- `Cellular3D` (4/4), `ThermalShaper` (2/2) — less common, same rule
- `Erosion2` and `Debris` — Version is optional in the example data (Erosion2 7/69; Debris 4/8 — `Complex Scene - Debris.terrain` and `Glacier - Complex Setup.terrain` ship Debris without it) but emit it for safety

Counts above are from the survey; re-verify with `inspect_samples.py --type <Type>` when in doubt.

Emit as a top-level property on the node, e.g. `Version: 2` in the markdown.

### Required input ports must be wired

Most non-primitive nodes have a port whose `Type` is `PrimaryIn, Required` (visible in passthrough.json). If that `In` port isn't wired in the Mermaid block, the node loads as deactivated even if all parameters are valid. (The shipping examples bear this out: at the 59-file survey, every node with a Required `In` is wired except one stray Combine in `Erosion - Mineral Deposits and ColorErosion.terrain` — exactly the deactivated case; don't copy it.)

Primitives without a required input (their `In` is plain `PrimaryIn`): `Mountain`, `Ridge`, `Constant`, `Noise`, `RadialGradient`, `LinearGradient`, `Perlin`, `MultiFractal`, `Canyon`, `Crater`, and most under the `Primitive` category. **`Sandstone` is NOT one of them** — despite its generator look, its `In` port is `PrimaryIn, Required` (all 38 shipping instances are wired). `Dunes` doesn't appear in the current examples — check its passthrough port catalogue before assuming either way.

Filter/processing nodes that **do** require an input (verified in current examples): `Slope`, `Height`, `SatMap`, `TextureBase`, `HSL`, `Tint`, `Curve`, `Clamp`, `Autolevel`, `Adjust`, `Combine`, `Mixer`, `Transform`, `Erosion2`, `Thermal2`, `Sea`, `Snowfield`, `Snow`, `Warp`, `LightX`, `Sandstone`, etc. (`Coast`, `Normals`, `AO`, `Export` no longer appear in the shipping examples — verify via passthrough or `inspect_samples.py --ports` against your own terrain when wiring them.)

### Nodes must reach an Export (or be Marked) to bake

A node that has no downstream path to an `Export` node (or a node explicitly marked for save) doesn't get computed during `F5`/Build, and the UI shows it dashed/inactive. Dead-end mini-chains (e.g. `Constant → Noise → Autolevel` with nothing consuming the Autolevel) won't bake. Either wire them to something or drop them.

"Marked for save" in the file format = the node carries a `SaveDefinition` JSON block (keys `Node`, `Filename`, `Format`, `IsEnabled`). That's how the current shipping examples mark outputs — they contain **no `Export` nodes at all** (59-file survey; `Cartography` and `Shade` nodes carry `SaveDefinition` with `Format: PNG16`). Export nodes remain valid for user graphs (the Broken Engine island archetype uses them); they just can't be cross-checked against shipping examples anymore.

### Per-type enum constraints

Invalid enum string values cause silent deactivation. Verified valid values (from shipping examples):

- `Mountain.Style`: `Strata`, `Alpine`, `Eroded` (case-sensitive; common defaults like `Basic` are rejected). **Absent is also valid** — 8 of 18 shipping Mountains omit `Style` and use the default.
- `Combine.Mode`: `Add`, `Subtract`, `Multiply`, `Max`, `Min`, `Screen`, `Difference`, `GrainMerge`, `Overlay`, `HardLight`. When `Mode` is **absent** and `PortCount=2` with `Mask` wired, the node performs canonical mask-driven linear blend `lerp(In, Input2, Mask)` — that's the pattern shipping example `Project Arenal.terrain` uses for SatMap layer compositing.
- `Snowfield.Direction`: compass directions — shipping examples exercise only `N` and `E` (one each); the remaining compass values (`NE`, `SE`, `S`, `SW`, `W`, `NW`) follow the same enum pattern but are not example-verified — confirm via a Gaea save/load before relying on one.
- `Export.Format`: `UshortRaw16`, `FloatRaw32`, `PNG8`, `PNG16`, `Exr`, `Tiff16`, `GLTF` (case-sensitive — the current shipping examples contain no `Export` nodes, but their `SaveDefinition.Format: PNG16` blocks confirm the uppercase casing; the Broken Engine island archetype uses `PNG8` for color, `FloatRaw32` for elevation, `UshortRaw16` for AO, and `GLTF` for mesh). Others exist — copy from a known-good terrain when unsure.
- `RenderIntentOverride` (not Export-specific — appears on many node types: Combine, Adjust, Stratify, ...): `Mask`, `Color`, `Heightfield` — all three observed in shipping examples.
- `SatMap.Library` (the `CLUTLibrary` enum): `Green`, `Sand`, `Blue`, `Color` — and **absent = Rocky** (the default). The names `Sandy`/`Rocky`/`Colorful` you'll see in QuadSpinner's UI library browser are display labels; the JSON enum values are `Sand`/(absent)/`Color`. Writing `Library: Sandy` throws `JsonSerializationException: Error converting value "Sandy" to type 'QuadSpinner.Gaea.Nodes.CLUTLibrary'` and Gaea refuses to load the file. (`Blue` ships only in `Glacier - Complex Setup.terrain`, which `inspect_samples.py` currently skips — its strict JSON parser rejects that file's trailing commas — so `--enum SatMap.Library` won't list it.)

When unsure, grep the shipping `.terrain` files in `C:/Program Files/QuadSpinner/Gaea 2/Examples/` for `"<Type>"` and inspect the enum value used.

### `PortCount` is required on dynamic-port nodes

- `Combine`: `PortCount: N` controls how many input ports exist. `PortCount: 2` → `[In, Out, Input2, Mask]`. `PortCount: 3` → adds `Input3`. `PortCount: 4` → adds `Input4` (one shipping instance, `Structure - Controlled Mountains.terrain`). Higher values are untested in shipping examples. Every shipping Combine (67/67) carries `PortCount`.
- `Mixer`: `PortCount: N` (shipping values: 2, 4, 6, 8) along with `Layer1`, `Layer2`, ... JSON objects. Shipping Mixers carry all of `Layer1`-`Layer15` regardless of `PortCount`; at minimum emit `Layer1`.

Without `PortCount`, dynamic-port nodes default to a minimal port set and any wired extra inputs silently drop.

### Mountain (and other primitives): always emit `Seed`

Every shipping Mountain (18/18 at the 59-file survey) has `Seed` (int). `Height` (float, observed 2.0..3.0) appears on 13/18 and `Style` on 10/18 — several shipping Mountains omit them and fall back to defaults, so neither is strictly required. When adding a Mountain, emit `Seed` always, and `Height` too if you care about vertical scale (the default may shift between binary versions).

## Sanity checks before stopping

- Every `n<ID>` referenced in the Mermaid block has a matching `### n<ID>:` section, and vice versa.
- No duplicate IDs.
- Every edge endpoint refers to a node that exists.
- Property values parse as their original type (don't put a string where a float was).
- For any newly-added node, surface the port-catalogue limitation to the user (the saver will use a default In/Out pair until passthrough is updated) so they can decide whether to copy a port block from a sibling example.
- For any newly-added node, check the "Gaea 2 conventions when adding new nodes" section above: required `Version: 2`, valid enum values, required In wiring, reaches an Export.

If any check fails, fix it before reporting done.

## What this skill does NOT do

- Doesn't change ports/modifiers — those are in passthrough JSON.
- Doesn't change `$type` / type FQNs — pinned by passthrough.
- Doesn't validate that a property name is real for the given node type — the parser is permissive; if you invent `Foo: 1` on an Erosion2 it'll round-trip through but Gaea will likely ignore it.
