# Tech Debt: Duplication

Source: /external-tech-debt on Engine/Source/File

## Changes

### Engine/Source/File/FileManager.h
- Replace `GetEagerMemoryBytes()`, `GetLazyMemoryBytes()`, `GetEagerAllocationCount()`, `GetLazyAllocationCount()` (lines 148-151) with two methods: `GetEagerStats()` and `GetLazyStats()`, each returning `MemoryStats` (which already has `iBytes` and `iCount` fields) [~15m]

### Engine/Source/File/FileManager.cpp
- Replace `GetEagerMemoryBytes()` + `GetEagerAllocationCount()` (lines 609-639) with single `GetEagerStats()` returning `MemoryStats` [~10m]
- Replace `GetLazyMemoryBytes()` + `GetLazyAllocationCount()` (lines 622-653) with single `GetLazyStats()` returning `MemoryStats` [~10m]

### Engine/Source/Profile/ProfileManagerBase.cpp
- Update caller at lines 517-521 to use `GetEagerStats()` and `GetLazyStats()` instead of four separate calls [~5m]

## Verification Notes
- Caller displays eager and lazy stats separately, so the eager/lazy split must be preserved (a single aggregate would break the profile display)
