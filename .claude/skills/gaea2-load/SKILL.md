---
name: gaea2-load
description: Load a Gaea 2 .terrain file into an editable Markdown view (Mermaid topology + per-node properties) under Temp/. Pairs with /gaea2-modify and /gaea2-save for round-trip editing.
argument-hint: <path-to-.terrain>
allowed-tools: [Read, Write, Bash, PowerShell]
disable-model-invocation: true
---

# gaea2-load

Convert a Gaea 2 `.terrain` JSON into:
- `Temp/<basename>.md` — editable Markdown (frontmatter + Mermaid topology + per-node properties)
- `Temp/<basename>.passthrough.json` — sidecar with structural state needed for round-trip (do not hand-edit)

## Workflow

1. **Resolve the input path.** `$ARGUMENTS` holds the user-supplied path. If it's empty or doesn't end in `.terrain`, ask the user for the file path. If it's a bare filename (no directory), try `C:\Program Files\QuadSpinner\Gaea 2\Examples\<name>.terrain` — that's where Gaea ships its example library. The bundled `examples/` directory in this skill is intentionally empty (the example files are © QuadSpinner and aren't redistributed); see `examples/README.md` for the rationale and the 28-file working set.

2. **Verify Python 3.10+ is available.** Python may be missing or stale on a fresh dev box, and `winget install` doesn't refresh PATH in the current shell — so the bootstrap probes well-known install dirs (winget, conda, scoop, chocolatey, active venv) directly:
   ```
   powershell -ExecutionPolicy Bypass -File .claude/skills/gaea2-shared/scripts/detect-python.ps1
   ```
   - Exit 0 with `OK <python-exe-path> Python X.Y` → **capture `<python-exe-path>` for step 3** (don't assume bare `python` is on PATH; the launcher `py.exe` is intentionally not used here).
   - Exit 1 with `MISSING ...` or `STALE ...` → tell the user Python is missing/too old and ask for permission to install. Only after explicit approval, run:
     ```
     winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements
     ```
     Then re-run the bootstrap.

3. **Run the loader using the detected Python path.** Quote it (the path may contain spaces):
   ```
   "<python-exe-path>" .claude/skills/gaea2-shared/scripts/load_terrain.py "<input.terrain>"
   ```
   The script writes `Temp/<basename>.md` and `Temp/<basename>.passthrough.json`.

4. **Report what was loaded.** Read the resulting `.md` file's frontmatter and the Mermaid block, then show the user a brief summary: number of nodes, number of edges, build resolution, the Mermaid topology rendered inline. The user can now use `/gaea2-modify` to edit, then `/gaea2-save` to write back.

## Notes on the output format

The Markdown file has three editable sections:
- **Frontmatter** (between `---` lines): terrain dimensions, build settings, version metadata. Keys prefixed `build_*` map to `BuildDefinition` in the JSON.
- **`## Topology` Mermaid block**: each node is `n<ID>["<Type>"]`, each edge is either `nA --> nB` (default `Out → In`) or `nA -- "FromPort → ToPort" --> nB` for non-default ports.
- **`## Nodes` section**: one `### n<ID>: <Type>` heading per node, followed by flat `key: value` lines. `position: X, Y` is the node's canvas position. Other keys are node-specific properties (e.g. `Duration`, `Seed` on Erosion2).

The `.passthrough.json` carries Newtonsoft `$id`/`$ref` graph state, original `$type` FQNs, port catalogues, modifier arrays, viewport state, GraphTabs, BuildProfiles, Bindings, metadata templates, and anything else the Markdown view drops. **Treat it as opaque** — hand-editing it will likely corrupt round-trip; if a structural change is needed, modify the source `.terrain` and re-run `/gaea2-load`.
