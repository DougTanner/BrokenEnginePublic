# Tech Debt: Cross-Collection Duplication

Source: /external-tech-debt on Engine/Source/Frame/Collections/SmokeTrails

Note: These items are cross-collection concerns shared with WindTrails (and potentially other collections). Implementation should be coordinated across both collections.

## Changes

### Engine/Source/Frame/Collections/SmokeTrails/SmokeTrailsRender.cpp + WindTrails/WindTrailsRender.cpp
- Extract shared BeginRender stale-position cleanup into a template helper in Collection.h or a new CollectionRenderUtils.h [~30m]
  - SmokeTrailsRender.cpp:52-67 and WindTrailsRender.cpp equivalent share identical std::erase_if logic iterating rActiveCoords and checking idToIndexMap.contains()
  - Template would be parameterized on collection type and render state map type
  - Both call sites would reduce to a single function call

### Engine/Source/Frame/Collections/ (all collections)
- Extract AllocateAndCopy memcpy boilerplate into a template utility that iterates Members() tuple [~45m]
  - SmokeTrails.cpp:15-26 (4 memcpy calls), WindTrails.cpp:15-26 (4 memcpy calls), Billboards.cpp:15-27 (5 memcpy calls) all follow identical pattern
  - Utility would call Allocate() then memcpy each member using std::apply over Members()
  - Would eliminate per-collection boilerplate entirely
  - SwapElement in CollectionMemory.h (lines 401-422) provides a precedent using std::apply with fold expressions

## Verification Notes
- BeginRender cleanup duplication verified: SmokeTrailsRender.cpp:52-67 and WindTrailsRender.cpp:49-62 contain near-identical std::erase_if logic differing only in collection member accessor
- AllocateAndCopy duplication verified across multiple collections; no existing utility in CollectionMemory.h
- Both items are cross-collection changes affecting multiple directories; coordinate implementation
