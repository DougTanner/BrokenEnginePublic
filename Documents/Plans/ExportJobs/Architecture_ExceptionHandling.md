# Architecture: ExportAudio Virtual Keyword Consistency

Source: /external-architecture-review on DataPacker/Source/ExportJobs

## Changes

### DataPacker/Source/ExportJobs/ExportAudio.h
- Line 18: Add `virtual` keyword to destructor: `~ExportAudio() override = default;` → `virtual ~ExportAudio() override = default;`
- Line 20: Add `virtual` keyword to GetVersion: `int64_t GetVersion() const override` → `virtual int64_t GetVersion() const override`
- Line 24: Add `virtual` keyword to Export: `void Export() override;` → `virtual void Export() override;`

All other 7 ExportJob subclasses include `virtual` on these methods. ExportAudio is the only outlier.

## Verification Notes
- Verified all 8 ExportJob subclasses: ExportFont, ExportIsland, ExportModel, ExportRaw, ExportScene, ExportShader, ExportTexture all use `virtual` on destructor/GetVersion/Export; ExportAudio does not
- Line numbers match: destructor at line 18, GetVersion at line 20, Export at line 24
- This is a cosmetic-only change — `override` is sufficient for correctness; `virtual` is redundant on overrides. Low priority, but improves consistency
