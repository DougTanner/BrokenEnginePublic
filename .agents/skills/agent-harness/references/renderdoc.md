# RenderDoc Capture

Read this reference only for a GPU frame-capture or capture-analysis scenario — an agent needs the action tree, bound pipeline state, or a render-target dump that a `screenshot` cannot supply. It owns the `--renderdoc` launch, the `renderdoc_capture` command, and the headless `rdc_*` analysis scripts. Ordinary runs never pass `--renderdoc`.

Caveat — capture runs drop Vulkan validation. With `renderdoc.dll` loaded (which `--renderdoc` forces), `InstanceManager` omits `VK_LAYER_KHRONOS_validation` because RenderDoc does not ship it; this is existing behavior. Never combine a validation-dependent acceptance criterion with a capture run. Run validation checks on a plain launch and capture checks on a separate `--renderdoc` launch.

## Prerequisites

- RenderDoc installed and its Vulkan implicit layer registered under `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers` (verified against RenderDoc v1.44, which bundles Python 3.6).
- Installer builds ship no `pymodules`/`renderdoc.pyd` (verified on v1.44), so a standalone interpreter cannot replay captures. `qrenderdoc --python` is the analysis vehicle; the standalone fallback below applies only to installs that do ship `pymodules`.
- A client built with `kbRenderDoc = true` (the compile gate). Runtime activation still requires `--renderdoc`.

## Launch, capture, analyze

Launch with the selected project's harness-doc launch block (`Projects/<Project>/Documents/AgentHarness.md`), adding `--renderdoc` to the client arguments only. Confirm the API attached with `get_logs {"pattern":"renderDocHmodule|RenderDoc API"}` — expect a nonzero `renderDocHmodule` and a nonzero `RenderDoc API` pointer.

Capture with `renderdoc_capture` (schema in the project harness doc, `Projects/<Project>/Documents/AgentHarness.md`). Size `--timeout-ms` for the drain-count progress bound, not wall-clock. The default agent client is minimized, so the command restores it without activation, captures, and re-minimizes.

```powershell
'{"cmd":"renderdoc_capture","params":{"frames":1}}' |
	& $AgentHarness --owner $Owner --port 27101 --timeout-ms 120000 -
```

The result `paths` are absolute `.rdc` files (RenderDoc's template writes `%TEMP%\RenderDoc\agent_frameNNN.rdc`). Use the returned paths verbatim; never guess them. `Test-Path` each and sanity-check its size before analysis.

Analyze with the `rdc_*` scripts through `qrenderdoc --python`. qrenderdoc is a GUI process whose stdout is not reliably capturable, so each script writes its result to an `--out` file that is the completion signal; poll for that file, then stop the process.

The embedded interpreter has two quirks the launch must work around:

- No `sys.argv`. The script's own arguments (capture path and flags) cannot ride on the `qrenderdoc --python <script>` command line — the interpreter never sees them, and qrenderdoc rejects them as its own unknown options. Pass them through the `RDC_ARGS` environment variable instead: the launching shell sets it, the child qrenderdoc inherits it, and the scripts `shlex.split` it when `sys.argv` is absent (standalone runs still use the normal `sys.argv` path). Because `shlex` is POSIX-mode, it treats `\` as an escape; single-quote each Windows path inside `RDC_ARGS` so backslashes survive.
- No `__file__`. The scripts fall back to the *working directory* to add themselves to `sys.path` (so `import rdc_common` resolves), so qrenderdoc must be launched with `-WorkingDirectory` set to the scripts directory; otherwise the import fails before any argument is read.

```powershell
$RenderDocDir = if ($env:RENDERDOC_PATH) { $env:RENDERDOC_PATH } else { 'C:\Program Files\RenderDoc' }
$QRenderDoc = Join-Path $RenderDocDir 'qrenderdoc.exe'
$ScriptsDir = Join-Path $ROOT '.agents\skills\agent-harness\scripts'
$Script = Join-Path $ScriptsDir 'rdc_summary.py'
$Capture = '<absolute .rdc path from renderdoc_capture>'
$Out = "$Capture.summary.txt"
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
# Script args reach the embedded interpreter only via RDC_ARGS; single-quote paths for shlex.
$env:RDC_ARGS = "'$Capture' --out '$Out'"
# -WorkingDirectory = scripts dir so the no-__file__ sys.path fallback finds rdc_common.
$Proc = Start-Process -FilePath $QRenderDoc -WorkingDirectory $ScriptsDir -WindowStyle Hidden -PassThru -ArgumentList @(
	'--python', ('"' + $Script + '"'))
# Replay init scales with capture size; a multi-hundred-MB capture needs minutes, not seconds.
$Deadline = (Get-Date).AddSeconds(240)
while (-not (Test-Path -LiteralPath $Out) -and (Get-Date) -lt $Deadline) { Start-Sleep -Milliseconds 250 }
Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
if (-not (Test-Path -LiteralPath $Out)) { throw "rdc_summary produced no output: '$Out'" }
Get-Content -LiteralPath $Out
```

Other scripts differ only in `RDC_ARGS`: `rdc_pipeline.py` needs `"'$Capture' --event N --out '$Out'"`, `rdc_texture.py` needs `"'$Capture' --event N --out '$Out'"` (add `--target K` or `--depth` as needed). For `rdc_texture.py` the `--out` PNG is the completion file, so poll for the PNG rather than a `.txt`. Launch one qrenderdoc per script and stop it before the next, so a stale GUI never holds the capture.

## Scripts

All live in `.agents/skills/agent-harness/scripts/` and share `rdc_common.py` (dual-mode loader; Python 3.6 syntax). Each exits nonzero with a one-line reason on failure.

- `rdc_summary.py <rdc> [--out FILE]` — recursive action tree (eventId, kind, name, draw/dispatch params) with a per-kind totals footer. Default out `<capture>.summary.txt`. Start here to find a draw's eventId.
- `rdc_pipeline.py <rdc> --event N [--out FILE]` — pipeline state at event `N`: bound shaders, viewport/scissor, depth/blend, color/depth targets with format + dimensions, vertex/index bindings. Default out `<capture>.pipeline_evN.txt`.
- `rdc_texture.py <rdc> --event N [--target K | --depth] [--out PNG]` — saves one bound color target (`--target K`, default 0) or the depth target (`--depth`) as PNG. Here the `--out` PNG is the completion file (its absolute path also prints to stdout); default `<capture>_evN_targetK.png` (`<capture>_evN_depth.png` with `--depth`). When pixel comparison is the point, `Read` the PNG for the single target under analysis and compare it against a `screenshot` of the same session; never sweep-read every dumped target or event.

## Fallbacks and raw export

- `RENDERDOC_PATH` overrides install discovery for both the PowerShell launch above and the scripts' standalone path.
- Standalone interpreter — on an install that ships `pymodules` with an interpreter matching the bundled `python3X.dll`, the scripts run directly (`python rdc_summary.py <capture> --out <file>`) without qrenderdoc. On installer builds without `pymodules` that path exits 2 and names `qrenderdoc --python`.
- `renderdoccmd convert` — for a raw structured dump outside these scripts: `renderdoccmd convert --input <capture.rdc> --output <capture.xml>`.
