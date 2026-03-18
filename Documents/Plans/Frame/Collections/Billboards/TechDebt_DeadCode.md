# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source/Frame/Collections/Billboards

## Changes

### Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h
- Remove stale comments referencing billboard integration (lines ~49, ~87) [~5m]

### Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp
- Remove or update stale comment at line ~496 mentioning billboard creation [~5m]

## Notes

The entire Billboards collection (Billboards.h, Billboards.cpp, BillboardsUpdate.cpp, BillboardsRender.cpp — ~270 lines total) appears to be unused dead code:
- `BillboardsInterpolate::Register()` is empty — no types registered
- `BillboardsPostRender::Add()` and `Remove()` have zero call sites
- `BillboardsInterpolate::Sync()` has zero call sites
- `billboard_t` is only referenced within Billboards files themselves
- Seven phase methods are empty stubs

**Decision required**: If billboards are not planned for near-term use, the entire collection can be removed (~1h effort, low risk). This requires human input.

## Verification Notes

Verified against source. Targets.h lines 49 and 87 contain stale parenthetical comments referencing billboard sync/registration. Spaceships.cpp line 496 contains a stale parenthetical comment referencing billboard creation. All three are confirmed stale — the referenced billboard operations do not exist in the called functions. Dead code status of the entire collection confirmed: Register() empty, Add/Remove/Sync have zero external call sites.
