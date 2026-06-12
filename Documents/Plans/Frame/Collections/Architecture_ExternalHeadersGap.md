# Architecture: ExternalHeaders Gap — `<cstring>` / `<cmath>`

## Context

Source: /external-architecture-review on `Engine/Source/Frame/Collections` (non-recursive). The collection headers use `std::memcpy` (`CollectionMemory.h:87, :102, :272`), `std::memset` (`Collection.h:673`), and `std::lerp` (`Collection.h:313-317`), but neither `<cstring>` nor `<cmath>` appears in `Common/ExternalHeaders.h` (verified against its full contents) or anywhere else first-party. The symbols currently resolve only via transitive includes inside the MSVC STL — fragile across toolset updates. Per the repo rule, standard-library headers belong in `Common/ExternalHeaders.h`, not in individual files, so the fix is upstream.

## Design

### Common/ExternalHeaders.h
- Add `#include <cstring>` and `#include <cmath>` at their sorted positions in the std-header block [~5m]

## Critical files
- `Common/ExternalHeaders.h`

## Out of scope
- Adding includes to `Collection.h`/`CollectionMemory.h` themselves (the PCH/aggregation model deliberately keeps subsystem headers include-free)
- A repo-wide audit for other std symbols riding transitive STL includes (worth a one-off grep if cheap at execution, but not required by this plan)

## Notes
- No determinism/serialization/guard exposure; compile-checked, affects every TU's PCH (full rebuild, no behavior change).

## Verification Notes (2026-06-11)
- Absence confirmed by grep: neither `<cstring>` nor `<cmath>` appears in `Common/ExternalHeaders.h` or any first-party file — every repo hit is under `ThirdParty/`.
- Use sites confirmed: `std::memcpy` (`CollectionMemory.h:87`, `:102`, `:272`), `std::memset` (`Collection.h:673`), `std::lerp` (`Collection.h:313-317`). Symbols currently resolve via transitive MSVC STL includes only, as claimed.
- Fix location matches the root CLAUDE.md standard-library-headers rule (additions go in `Common/ExternalHeaders.h`). No overlap with `Frame/Architecture_IncludeHygiene.md` or `Network/Architecture_IncludeHygiene.md` (neither touches std headers). No corrections needed.
