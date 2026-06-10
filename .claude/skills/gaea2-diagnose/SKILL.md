---
name: gaea2-diagnose
description: Diagnose why Gaea 2 rejected a .terrain file or failed a build. Use when the user reports an error like "File is corrupt or missing additional data", "Swarm failed to load", a node loading as deactivated (dashed border), or unexpected behavior after a /gaea2-save round-trip. Finds the latest Gaea 2 session log (under %APPDATA%\QuadSpinner\Gaea\2.0\Logs — NOT Gaea 1's location), decodes its base64+gzip ERR payloads to extract the underlying Newtonsoft exception with JSON path, then cross-references the shipping Examples library to suggest the valid value or shape.
argument-hint: [--latest-swarm | --blob <base64> | --log <path>]
allowed-tools: [Read, Edit, Bash, PowerShell, Glob, Grep]
disable-model-invocation: true
---

# gaea2-diagnose

The Gaea 2 UI reports failures with a vague modal ("File is corrupt or missing additional data"); the real Newtonsoft exception is in a log. Find the right log, decode the exception, cross-reference the shipping samples for the valid value, and tell the user what to change.

## Log streams

Pick by how Gaea was invoked:

| Source of error | Log | How to read |
|---|---|---|
| File-open / file-save failure (UI) | `%APPDATA%\QuadSpinner\Gaea\2.0\Logs\YYYY-MM-DD_HH-MM-SS.txt` | `decode_gaea_err.py --latest` |
| Build / "Swarm" failure inside the UI | same dir: `*-SWARM.txt`, `CRASH_SWARM_*.txt` | `decode_gaea_err.py --latest-swarm` |
| DataPacker invoking headless Swarm | `%LOCALAPPDATA%\Temp\DataPacker\<Project>\<island>.gaea.log` — plain stdout/stderr from `Gaea.Swarm.exe`; one file per island, overwritten each build | Plain text, exception already in the clear — read directly, no decoder |
| Caller pasted a specific log path | that path | `decode_gaea_err.py --log <path>` |
| Caller pasted a base64 `ERR` payload | inline | `decode_gaea_err.py --blob <text>` |

UI logs wrap each `ERR` line as base64: `<4-byte LE uncompressed length><gzip stream>`. The gzip footer (CRC32/ISIZE) is occasionally truncated, so the bundled decoder inflates with raw deflate (negative wbits) and skips the trailing checksum — never hand-roll a decoder inline.

Confusable locations:
- `%APPDATA%\QuadSpinner\Gaea\Logs\` (without `2.0\`) is **Gaea 1**'s log path. Different format, irrelevant to Gaea 2 errors.
- DataPacker's per-island log is in `Local\Temp\`, not `Roaming\`. If you only look in the QuadSpinner directory you'll miss the headless-Swarm errors entirely.
- If `2.0\Logs\` does not exist, Gaea 2 was never opened on this machine; if `DataPacker\<Project>\` does not exist, no build has been attempted via the DataPacker pipeline.

## Workflow

### Step 1: Verify Python is available

Same probe as the other gaea2-* skills:

```
powershell -ExecutionPolicy Bypass -File "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/detect-python.ps1"
```

Capture the `<python-exe-path>` for the next steps.

### Step 2: Decode (or read) the log

For the two compressed-format streams:

```
"<python-exe-path>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/decode_gaea_err.py" --latest
```

The decoder prints each ERR with its timestamp, trimmed to the exception line by default. Pass `--full` only when the stack trace would actually help — Newtonsoft stack traces are noisy and the first two lines almost always name the problem. If `--latest` reports no ERR lines, the error may have come from an earlier session — list `*.txt` in the log dir by mtime and try the previous one.

The DataPacker plain-text log is small (typically 5-20 lines):
```
Preparing Gaea Build Swarm <version>...
Loading devices...
    Activated <device>...
Opening <basename>.terrain...
<exception or build progress>
```
Read it directly with the Read tool — no script needed.

### Step 3: Parse the exception

The decoded first line follows a predictable pattern:

```
Newtonsoft.Json.JsonSerializationException:
  Error converting value "<VALUE>" to type 'QuadSpinner.Gaea.Nodes.<EnumType>'.
  Path 'Assets.$values[0].Terrain.Nodes.<ID>.<Property>', line N, position M.
 ---> System.ArgumentException: Requested value '<VALUE>' was not found.
```

Extract three things:
- **JSON path** — names the node Id and the offending property
- **Type** — the .NET enum/class that rejected the value
- **Value** — what was written that Gaea didn't accept

These three together tell you which sample query to run.

### Step 4: Find the valid value via shipping samples

The shipping examples under `C:\Program Files\QuadSpinner\Gaea 2\Examples\` are the ground truth. **Trust them over UI labels and over documentation** — the Gaea UI library browser shows "Sandy", "Rocky", "Colorful", but the JSON enum the serializer expects is `Sand` / (absent for Rocky) / `Color`. Discrepancies like this recur.

Use `inspect_samples.py` to ask three kinds of questions:

```
# What values does this enum actually accept?
"<python>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/inspect_samples.py" --enum SatMap.Library

# What does a working node of this type look like?
"<python>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/inspect_samples.py" --type Erosion2

# What ports does this node have? (for wiring questions)
"<python>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/inspect_samples.py" --ports Combine
```

Other useful queries when the error is less specific:

```
# Where does this property name appear and with what values?
"<python>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/inspect_samples.py" --field-grep Library

# What node types exist at all?
"<python>" "${CLAUDE_SKILL_DIR}/../gaea2-shared/scripts/inspect_samples.py" --list-types
```

### Step 5: Apply the fix and re-save

Once you know the valid value, edit `Temp/<basename>.md` and re-run `/gaea2-save`. Then have the user retry the load in Gaea 2.

If a second error appears, repeat from Step 2 — Gaea fails fast on the first error, so subsequent issues only surface after the first is fixed.

### Step 6: Promote the finding

If the discovered constraint is durable (a stable enum value, a required field, a node-deactivation rule), add a one-liner to `gaea2-modify/SKILL.md` under "Per-type enum constraints" or "Gaea 2 conventions when adding new nodes" — that's where future invocations look first, so a fix that escapes into docs avoids the next round-trip.

## Common error patterns

UI / compressed-log errors (decoded form):

| Decoded message starts with… | Likely cause | Fix |
|---|---|---|
| `JsonSerializationException: Error converting value "X" to type 'Enum...'` | Invalid enum value (often UI-label vs JSON-name mismatch) | `--enum Type.Property` then write the JSON name |
| `JsonSerializationException: ... missing required member 'X'` | Required field absent | `--type <NodeType>` and copy the field from a sample |
| `JsonReaderException: Unexpected end of content` or "File is corrupt or missing additional data" (no decoded error) | Root-level field missing (e.g., `Macros`) | Compare root keys with a known-good sample |
| **No ERR line at all, but a node loads dashed/deactivated** | Id < 100, missing `Version: 2`, required input port not wired, or graph-rejected enum | See `gaea2-modify/SKILL.md` → "Gaea 2 conventions when adding new nodes" |
| ERR mentions `CompositeFailure` / a Swarm log | Build-time evaluation error, not a load error | Re-run with `--latest-swarm` |

Headless / DataPacker plain-text errors:

| Plain-text message | Likely cause | What this means |
|---|---|---|
| `IOException: The handle is invalid` immediately after `Opening <name>.terrain...` | `Gaea.Swarm.exe` console-handle setup fails when run with redirected stdio. **Not a .terrain content issue.** | Try running the build with a real console attached, or test the .terrain by opening it in the Gaea UI to confirm the file itself is intact. If the UI loads it but Swarm via DataPacker still fails, the issue is in the Swarm process environment (caller/version/redirection), not the data. |
| `FileNotFoundException: Could not find file ...` | Wrong path, or DataPacker passing a stale path | Check the DataPacker invocation arguments. |
| `Unhandled exception. <Type>: <message>` followed by obfuscated stack frames like `at .(...)` | Generic .NET unhandled exception. The obfuscated names (rendered as `.` or as Unicode glyphs in some terminals) are intentional QuadSpinner code obfuscation — they convey no diagnostic value. | Look only at the exception type + message on the first line. The frames after it are noise. |
| Exit code `3762504530` (`0xE0434352`) | Standard CLR unhandled-exception SEH code — set by Windows whenever .NET propagates an unhandled exception, regardless of which exception. | Carries no diagnostic value beyond "the .NET process crashed." Use the captured log text, not the exit code. |

## Scope

- This skill diagnoses; it does not edit the .terrain. Apply fixes via `Temp/<basename>.md` + `/gaea2-save` (Step 5); structural graph changes chain to `/gaea2-modify`.
- Suspected Gaea-version drift: run the bundled `check_gaea_version.py` (also auto-runs from `/gaea2-load`).
- Runtime crashes in the Gaea binary itself go to QuadSpinner support, not this skill.
