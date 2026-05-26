# Fix ExportJob pack-ordering: total-order the RunExportJobs sort

## Context

`RunExportJobs<T>()` in `DataPacker/Source/Main.cpp` sorts the collected jobs before writing the
`.pack`/`.manifest` so chunk byte-order is stable across runs ("for more efficient Steam patching",
per the in-code comment). The comparator keys solely on `mRelativeDirectory`:

```cpp
std::sort(exportJobs.begin(), exportJobs.end(), [](const std::unique_ptr<T>& rpA, const std::unique_ptr<T>& rpB)
{ return common::ToLower(rpA->mRelativeDirectory.string()) < common::ToLower(rpB->mRelativeDirectory.string()); });
```

`ExportJob`'s ctor sets `mRelativeDirectory` to the input path with the filename stripped
(`remove_filename()`), so **any two jobs whose inputs share a directory collide on the sort key**.
`std::sort` is not stable, so their relative order in the `.pack` is unspecified and can vary
run-to-run if `recursive_directory_iterator` enumeration order ever differs. This is latent for any
asset type with multiple claimed files in one folder; the multi-route island work made it concrete —
a `2x1` route now emits two leaf chunks (`Islands/01/2x1/0`, `…/2x1/1`) that share the identical
`mRelativeDirectory` `Islands\01\2x1\` (the leaf index `0`/`1` was stripped by `remove_filename()`).

Correctness is unaffected — chunk CRCs are distinct and the manifest is a CRC→location map the
runtime resolves by CRC regardless of pack order. The only cost is defeating the stable-ordering
goal (Steam delta-patch efficiency) for same-directory jobs.

## Design

Make the comparator a total order by falling back to the unique per-job key on equal directories.
`ExportJob::mRelativeFile` (`std::string`, = `mRelativeDirectory.string() + filename`) already
includes the leaf index / filename and is unique per job, so sorting by it gives the same primary
ordering plus a deterministic tiebreaker:

```cpp
std::sort(exportJobs.begin(), exportJobs.end(), [](const std::unique_ptr<T>& rpA, const std::unique_ptr<T>& rpB)
{ return common::ToLower(rpA->mRelativeFile) < common::ToLower(rpB->mRelativeFile); });
```

For single-file-per-directory job types this is order-preserving (the directory is the dominant
prefix); for multi-file directories it adds the missing tiebreak. Verify `mRelativeFile` is
populated for every job type before the sort (it is set in the `ExportJob` ctor for all derived
types) and that `common::ToLower` accepts a `std::string` (it does — `mRelativeDirectory.string()`
is already passed through it today).

## Critical files

- `DataPacker/Source/Main.cpp` — the `std::sort` comparator in `RunExportJobs<T>()` (the lambda
  that currently compares `mRelativeDirectory.string()`).
- `DataPacker/Source/ExportJobs/ExportJob.cpp` (ctor) — confirm `mRelativeFile` derivation is the
  intended unique key (read-only check; no change expected).

## Out of scope

- Changing `mRelativeDirectory`/`mRelativeFile` derivation or the `remove_filename()` behavior.
- The chunk-CRC scheme or manifest format.
- Any island-specific logic — this is a shared `RunExportJobs` ordering fix that happens to also
  cover the new island leaves.
- Reordering or deduplicating the jobs themselves.

## Acceptance criteria

- Two consecutive DataPacker runs (no source changes) produce byte-identical `.pack` files for an
  asset type that has multiple claimed files in one directory (e.g. a `2x1` island, or any folder
  with multiple textures).
- All existing single-file-per-directory chunk orderings are unchanged.

## Notes

Found by the step-9 audit of the multi-route island change. One-line comparator change; the risk is
only that it perturbs existing pack ordering for multi-file directories (intended) — re-confirm no
asset type relies on the current unspecified order (none should).
