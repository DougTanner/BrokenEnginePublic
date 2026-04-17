# Architecture: Main.cpp Orchestration

Source: /external-architecture-review on DataPacker/Source/

Goal: `Main.cpp:MainThread` currently mixes pipeline orchestration with two large inline header generators and a blocking MessageBox inside a parallel loop. Extract the generators and fix the failure-collection pattern.

## Changes

### DataPacker/Source/Main.cpp
- Lines 208-250: extract the inline `DataTypes.h` generator into a static helper `GenerateDataTypesHeader(const std::filesystem::path& outPath)` [~20m]
- Lines 252-274: extract the inline `Data.h` generator into a static helper `GenerateDataHeader(const std::filesystem::path& outPath)` [~20m]
- Lines 219-226 + 233-240 + 260-267: THREE parallel lists of 8 items each (enum entries, names array, Data.h includes) — high drift risk. Drive all three from a single `static constexpr std::array<std::tuple<std::string_view, std::string_view, std::string_view>, kDataTypeCount> kDataTypes = {{...}};` fed into both generators [~20m]
- Lines 115-141 (`RunExportJobs<T>` failure loop): replace the inline `Quit()` call (`Main.cpp:138`) which shows a MessageBox inside the future loop. Collect failure descriptions into a `std::vector<std::string>` and show a single MessageBox after the loop ends — fewer stacked dialogs on multi-failure runs [~20m]
- Lines 198-199 (IBL cubemap generation): the IBL phase currently runs as two free functions defined in `ExportTexture.cpp` (`GenerateIrradianceCubemaps` at line 51, `GeneratePreFilteredCubemaps` at line 125) and declared in `ExportTexture.h:29-30`. Consider moving to a dedicated `ExportJobs/ExportCubemapIbl.{h,cpp}` so the pipeline phase is explicit rather than hidden inside texture code [~45m]

## Expected Outcome

- `MainThread` body shrinks to readable pipeline phases (pre-export → IBL → main → aggregation headers → attribution)
- Single source of truth for data-type enum + names + include list
- At most one failure MessageBox per run
- IBL phase becomes discoverable via directory listing

## Verification Notes

- Verified the DataTypes.h generator spans 208-250 (adjusted from 209-250 to include the blank-line boundary at 208).
- Verified the Data.h generator spans 252-274 (adjusted from 253-274 similarly).
- Upgraded the parallel-list bullet from TWO lists to THREE — the `Data.h` include list (lines 260-267) is also 8 items kept parallel by hand.
- Verified the IBL functions' actual names (`GenerateIrradianceCubemaps`, `GeneratePreFilteredCubemaps`) and file locations; updated from "or similar" to exact names.
- All other line numbers verified.
