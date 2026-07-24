---
name: gaea2-load
description: Load a Gaea 2 .terrain file into an editable Markdown view (Mermaid topology + per-node properties) under Temp/. Pairs with /gaea2-modify and /gaea2-save for round-trip editing.
argument-hint: <path-to-.terrain>
allowed-tools: [Read, Bash, PowerShell]
disable-model-invocation: true
---

# gaea2-load

Convert a Gaea 2 `.terrain` JSON into:
- `Temp/<basename>.md` — editable Markdown (frontmatter + Mermaid topology + per-node properties)
- `Temp/<basename>.passthrough.json` — sidecar with structural state needed for round-trip (do not hand-edit)

## Workflow

1. Resolve the input path. `$ARGUMENTS` holds the user-supplied path. If it's empty or doesn't end in `.terrain`, ask the user for the file path. If it's a bare filename (no directory), try `C:/Program Files/QuadSpinner/Gaea 2/Examples/<name>.terrain` — that's where Gaea ships its example library. The shared `.claude/skills/gaea2-shared/examples/` directory is intentionally empty (the example files are © QuadSpinner and aren't redistributed); see its `README.md` for the rationale and the 28-file working set.

2. Verify Python 3.10+ is available. Python may be missing or stale on a fresh dev box, and `winget install` doesn't refresh PATH in the current shell — so the bootstrap probes well-known install dirs (winget, conda, scoop, chocolatey, active venv) directly:
   ```
   pwsh -ExecutionPolicy Bypass -File "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/detect-python.ps1"
   ```
   - Exit 0 with `OK <python-exe-path> Python X.Y` → capture `<python-exe-path>` for step 3 (don't assume bare `python` is on PATH; the launcher `py.exe` is intentionally not used here).
   - Exit 1 with `MISSING ...` or `STALE ...` → tell the user Python is missing/too old and ask for permission to install. Only after explicit approval, run:
     ```
     winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements
     ```
     Then re-run the bootstrap.

3. Probe Gaea 2 version drift. Gaea's accepted enum values, required fields, and port catalogues can shift between releases. Catch this before it bites by running:
   ```
   "<python-exe-path>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/check_gaea_version.py"
   ```
   The script reads the current Gaea version from the latest session log (or, as a fallback, from `Gaea.exe`'s `ProductVersion`) and compares it against the PC-local baseline at `%LOCALAPPDATA%/BrokenEngine/AgentCache/Gaea2/gaea-version.txt`. Every worktree reuses this cache; the script safely copies an older checkout-local baseline when present and retains the legacy source for compatibility with already-running older tools.
   - Exit 0 — version unchanged, proceed silently.
   - Exit 1 — version changed. The script prints a structural diff (new node types, new enum values, dropped properties) to stderr. Surface this to the user in your final report as a one- or two-line note ("Gaea moved 2.3.0.0 → 2.4.0.0; SatMap.Library gained 'Volcanic'"). Suggest they check `gaea2-modify/SKILL.md`'s per-type constraints aren't stale.
   - Exit 2 — no PC-local shared baseline exists yet; baseline cached silently, proceed.
   - Exit 3 — could not detect a Gaea install or access the shared cache. Note it in the report but continue — this is just an information probe, not a gate.
   The shared cache also stores a sample-structure fingerprint so the diff includes *what* changed, not just the version string. A fresh checkout can return exit 0 by reusing the PC-local baseline.

4. Run the loader using the detected Python path. Quote it (the path may contain spaces):
   ```
   "<python-exe-path>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/load_terrain.py" "<input.terrain>"
   ```
   The script writes `Temp/<basename>.md` and `Temp/<basename>.passthrough.json`.

5. Report what was loaded. Read the resulting `.md` file's frontmatter and the Mermaid block, then show the user a brief summary: number of nodes, number of edges, build resolution, the Mermaid topology rendered inline. Include any version-drift note from Step 3. The user can now use `/gaea2-modify` to edit, then `/gaea2-save` to write back. If something fails when they open the result in Gaea 2, point them at `/gaea2-diagnose`.

## Notes on the output format

The Markdown file has three editable sections:
- Frontmatter (between `---` lines): terrain dimensions, build settings, version metadata. Keys prefixed `build_*` map to `BuildDefinition` in the JSON.
- `## Topology` Mermaid block: each node is `n<ID>["<Type>"]`, each edge is either `nA --> nB` (default `Out → In`) or `nA -- "FromPort → ToPort" --> nB` for non-default ports.
- `## Nodes` section: one `### n<ID>: <Type>` heading per node, followed by flat `key: value` lines. `position: X, Y` is the node's canvas position. Other keys are node-specific properties (e.g. `Duration`, `Seed` on Erosion2).

The `.passthrough.json` carries Newtonsoft `$id`/`$ref` graph state, original `$type` FQNs, port catalogues, modifier arrays, viewport state, GraphTabs, BuildProfiles, Bindings, metadata templates, and anything else the Markdown view drops. Treat it as opaque — hand-editing it will likely corrupt round-trip; if a structural change is needed, modify the source `.terrain` and re-run `/gaea2-load`.
