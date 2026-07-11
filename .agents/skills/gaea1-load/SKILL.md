---
name: gaea1-load
description: Load a Gaea 1 .tor file into a read-only Markdown view (Mermaid topology + per-node properties) under Temp/. Inspection-only — Gaea 1 files are legacy and we do not re-save them.
argument-hint: <path-to-.tor>
allowed-tools: [Read, Bash, PowerShell]
disable-model-invocation: true
---

# gaea1-load

Convert a Gaea 1 `.tor` into `Temp/<basename>.md` — frontmatter + Mermaid topology + per-node properties. **Read-only**: there is no `/gaea1-modify` or `/gaea1-save` because Gaea 1 is legacy and we don't round-trip these files.

## .tor wire format (reverse-engineered, no public spec)

1. The file is ASCII text, single line, base64-encoded.
2. After base64 decode: a 4-byte little-endian uint32 (== decompressed payload size, matches gzip's ISIZE field) followed by a complete gzip stream.
3. The gzip stream decompresses to UTF-8 XML rooted at `<Terrain>`. Nodes live under `<Layers>`, positions and connections under `<GraphData>` (`<Shapes>` as `ID|X|Y|0` strings, `<Connections>` as `FROM|TO|FROM-PORT|TO-PORT||0` strings).

## Workflow

1. **Resolve the input path.** `$ARGUMENTS` holds the user-supplied path. If it's empty or doesn't end in `.tor`, ask the user for the file path.

2. **Verify Python 3.10+ is available** (the loader only needs stdlib — base64, gzip, xml.etree). Reuse the gaea2 family's detector:
   ```
   powershell -ExecutionPolicy Bypass -File "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/detect-python.ps1"
   ```
   - Exit 0 with `OK <python-exe-path> Python X.Y` → capture `<python-exe-path>` for step 3.
   - Exit 1 with `MISSING ...` or `STALE ...` → tell the user Python is missing/too old and ask permission to install. Only after explicit approval:
     ```
     winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements
     ```
     Then re-run the detector.

3. **Execute the loader with the detected Python path.** Quote it (path may contain spaces). Run the script as-is — don't read or reimplement it; the wire-format section above covers what it does:
   ```
   "<python-exe-path>" "${CLAUDE_SKILL_DIR}/scripts/load_tor.py" "<input.tor>"
   ```
   The script writes `Temp/<basename>.md` (pass `--output-dir` to change the destination).

4. **Report what was loaded.** Read the resulting `.md` file's frontmatter and the Mermaid block, then show the user a brief summary: number of nodes, number of edges, resolution, and the Mermaid topology rendered inline.

## Notes on the output format

The Markdown file has two sections:

- **Frontmatter** (between `---` lines): terrain `Id`/`Name`/`Workflow`, `resolution_working`/`resolution_final`, and `def_*` keys for each `<Definition>/<Parameter>` (e.g. `def_height`, `def_scale`).
- **`## Topology` Mermaid block**: each node is `n<index>["<DisplayName><br/><i>Type</i>"]` when the display name differs from the node type, otherwise just `n<index>["<Type>"]`. Bypassed nodes are marked `:::bypassed` (dashed, faded). Edges with the default `Output → Input` port pair render as plain `nA --> nB`; non-default ports show `nA -- "FromPort → ToPort" --> nB`.
- **`## Nodes` section**: one `### <DisplayName>  \`<GUID>\`` heading per node, followed by `type`, `category`, `position`, `ports`, then a `**Parameters:**` bullet list. A `**Mask / Post-process (non-default only):**` list is emitted only when the node has non-default mask post-process values — `PBoolean=false` and zero numerics are suppressed to keep the output readable.

The `.tor` file itself is never modified.
