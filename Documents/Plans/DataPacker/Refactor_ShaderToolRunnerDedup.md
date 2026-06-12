# Refactor: ExportShader Vulkan-Tool Runner Dedup

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive). `PreprocessShader` / `CompileShader` / `OptimizeShader` triplicate a ~50-line skeleton: build wide command line → hand-rolled `OutputDebugStringW` thread-id log → `common::RunExecutable` → throw-on-error → `mIntermediateFiles.push_back`.

## Design

### Extract `RunVulkanTool`
- Extract one private helper (e.g. `RunVulkanTool(const std::filesystem::path& rExecutable, const std::wstring& rParams, const std::filesystem::path& rOutputFile)`) from the triplicated skeleton in `ExportShader.cpp:196-338`, leaving each of the three callers as command-line assembly + one call [~30m]

### Replace the hand-rolled debug logging
- The `OutputDebugStringW` thread-id string-assembly block is identical thrice (`ExportShader.cpp:229-234, 279-284, 318-323`). Inside the new helper, replace with one `LOG` call — `Common/Log/LogFormatters.h` handles wide strings allocation-free [~15m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportShader.{h,cpp}`

## Out of scope
- Replacing the subprocess approach with shaderc/glslang libraries — evaluated and rejected (`Architecture_LibraryReplacement.md` notes); the `.d` depfile dirty-tracking depends on the CLI tools.
- The binding-table/attribute ASSERTs — `Architecture_ShaderReflectionGuards.md`.
- The chunk payload offset math — `Architecture_PayloadLayoutSingleSource.md`.

## Notes
- No pack-byte/version exposure — identical tool invocations, identical outputs; only the orchestration code is deduplicated.
- File-group overlap: `ExportShader.cpp` shared with `Architecture_ShaderReflectionGuards.md` and `Refactor_ExportJobsQuickWins.md` — co-schedule.

## Verification Notes
- Triplication confirmed at :196-251 / :253-299 / :301-338; the three `OutputDebugStringW` thread-id blocks are byte-identical at exactly :229-234 / :279-284 / :318-323.
- One asymmetry the `RunVulkanTool` signature must carry: the error policies differ. glslc (preprocess) treats *any* stdout as fatal (:237-240) because it fails silently on compile errors; glslangValidator and spirv-opt key on `miExitCode != 0` / missing output file and only `LOG(kWarning)` non-empty output (:287-294, :326-333). Either parameterize the policy (e.g. a `bThrowOnAnyOutput` flag) or keep the preprocess caller's throw outside the helper — do not unify to one policy, which would either mask glslc failures or fail builds on benign validator warnings.
- The `LOG` replacement claim checks out: `Common/Log/LogFormatters.h` formats wide strings/paths allocation-free per `Common/CLAUDE.md`, and DataPacker's `keLogLevelDefault = kVerbose` means the line still reaches the debugger output as today.
