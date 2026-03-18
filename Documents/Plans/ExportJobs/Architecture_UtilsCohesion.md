# Architecture: Move Shader Logic Out of ExportJob Base Class

Source: /external-architecture-review on DataPacker/Source/ExportJobs

## Changes

### DataPacker/Source/ExportJobs/ExportJob.cpp
- Move `ShaderHeadersChanged()` (lines 7-51) and `CollectShaderIncludes()` (lines 53-90) to ExportShader.cpp — these are shader-specific functions that don't belong in the generic base class
- Remove the shader-specific `#include` dirty-checking block from `ExportJob::CheckDirty()` (lines 251-273, the `if (mChunkFlags & kShader)` block)

### DataPacker/Source/ExportJobs/ExportShader.h
- Override `CheckDirty()` in ExportShader: `virtual bool CheckDirty(const std::filesystem::path& rPackFile) override;`

### DataPacker/Source/ExportJobs/ExportShader.cpp
- Move `ShaderHeadersChanged()` and `CollectShaderIncludes()` here (from ExportJob.cpp)
- Implement `ExportShader::CheckDirty()`: call `ExportJob::CheckDirty()`, then if still clean, perform the shader header dependency check (the logic currently at ExportJob.cpp:251-273)

## Verification Notes
- All file paths exist and line numbers verified against source
- `ShaderHeadersChanged()` (lines 7-51) and `CollectShaderIncludes()` (lines 53-90) are both `static` in ExportJob.cpp and only referenced from the `if (mChunkFlags & kShader)` block at lines 251-273
- `ShaderHeadersChanged()` uses function-scoped static locals for caching; these will work identically when moved to ExportShader.cpp
- ExportScene already overrides `CheckDirty()` (ExportScene.h line 25), so the override pattern is established
- ExportShader.h already has a `CleanupOnFailure()` override (line 37), so adding another override is consistent with its design
