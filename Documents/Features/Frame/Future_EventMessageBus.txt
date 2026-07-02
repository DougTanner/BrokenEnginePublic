FUTURE: Event/Message Bus for Cross-Collection Communication
=============================================================
Status: Save for later
Trigger: When collections accumulate knowledge about each other that they shouldn't need

Context
-------
Collections currently communicate through direct function calls. One collection includes another's
header and calls its static methods (Add, Remove, Sync, Spawn). This creates explicit coupling:
- Players.cpp includes Blasters.h, Explosions.h (after the Players.cpp split, the Missiles.h and
  Spaceships.h cross-collection includes moved to PlayersCombat.cpp; Spaceships.h also appears in
  PlayersNavigation.cpp and PlayersRender.cpp — the coupling set is unchanged, only which file holds it)
- Spaceships.cpp includes Blasters.h, Targets.h, Players.h, Explosions.h
- Missiles.cpp includes Targets.h, Explosions.h

The destroy/cleanup cascades are the main coupling point — each collection removes 3-6 owned objects
from other collections when an entity dies or transfers.

Concept
-------
A frame-scoped event queue that collections publish to and consume from:

    rEvents.Push<EntityDestroyed>(spaceship_id, vecPosition);

    rEvents.ForEach<EntityDestroyed>([&](auto& event) {
        ExplosionsPostRender::Add(rFrame, event.vecPosition, ...);
    });

The event queue is another SOA array, cleared each frame, fitting the deterministic model.

Why Not Now
-----------
- Owned-object relationships are 1:1 parent-child, not many-to-many subscriptions
- The sync pattern (parent writes child data every frame) requires direct access, not events
- The one cross-collection notification (spaceship death -> missile loses target) is handled
  elegantly via the subscriber pattern + existence check
- Only 5 game collections — the N*M coupling argument does not apply at this scale
- An event bus adds indirection that harms debugging and determinism validation
- The OwnedObjectCleanupHelper plan addresses the worst of the destroy/transfer duplication

Lighter Alternative
-------------------
Before a full event bus, consider an "owned objects" abstraction that tracks which child IDs a parent
owns and provides a single RemoveAll() call. This eliminates Transfer/Destroy duplication without
the indirection of a message bus.

Revisit When
------------
- Adding a new "on death" behavior requires modifying 4+ collection files
- A new system needs to react to events from collections it shouldn't depend on
- The fan-out from entity lifecycle events exceeds 3-4 consumers
