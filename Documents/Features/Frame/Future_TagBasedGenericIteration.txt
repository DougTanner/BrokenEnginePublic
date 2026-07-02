FUTURE: Tag-Based Generic Iteration via C++ Concepts
=====================================================
Status: Save for later
Trigger: When adding 3+ more game collections makes the explicit Players + ForEach pattern unwieldy

Context
-------
The codebase already has compile-time generic iteration via TypeList + fold expressions
(ForEachUpdate, ForEachDestroy, etc. in FrameUtils.h). This works well.

The main boilerplate is in Frame.cpp where every phase method (13 of them) follows:
  1. Call Base class method (dispatches engine collections)
  2. Call Players explicitly
  3. Call ForEach with GameInterpolateTypes/GamePostRenderTypes

This is ~39 lines of repetitive 3-line blocks. Players is excluded from the ForEach because
it has different signatures (e.g., Spawn takes FrameInput).

Concept
-------
Use C++ concepts to enable generic operations across any collection matching a trait:

    template <typename TCollection>
    concept Damageable = requires(TCollection c) { c.health; };

    template <Damageable TCollection>
    void ApplyAreaDamage(TCollection& rCollection, FXMVECTOR vecCenter, float fRadius, float fDamage);

Why Not Now
-----------
- The existing TypeList + fold expression system already provides this capability
- Only 5 game collections — explicit listing has minimal overhead
- Only ~4 first-party C++ concepts exist in the codebase (NotStringLike in Common/Crc.h:98,
  HasSharedMembers in Engine/Source/Frame/Collections/Collection.h:368, IsExportJob in
  DataPacker/Source/ExportJobs/ExportJob.h:53) — not an established pattern yet
- No operations are actually duplicated across collections
- Cross-collection operations (AreaDamage, Collision) are already mediated through shared infrastructure
- The ~39 lines of boilerplate in Frame.cpp exist for a reason (Players has unique signatures)

Revisit When
------------
- Adding 3+ more game-level collections
- Needing a genuinely generic cross-collection operation (e.g., "find nearest entity of any type")
- Wanting to make the engine more pluggable for different game projects
