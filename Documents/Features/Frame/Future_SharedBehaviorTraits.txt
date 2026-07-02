FUTURE: Shared Behavior Traits / SOA Mixins
============================================
Status: Save for later
Trigger: When collection count reaches 20-25 and shared logic (not just shared field names) repeats 5+ times

Context
-------
Multiple collections share field names (pVecPositions in most of the 16 collections, pVecDirections in 4, pAlignments in 5)
but the LOGIC operating on those fields is unique per collection. Field name overlap alone does not
create maintenance burden.

The one area with real shared logic — controller lifecycle (PointLights, Puffs, WindRadials) — is
being addressed by the ControllerLifecycleHelper plan with a simple template function.

Concept
-------
Define small reusable SOA structs (traits) that compose into collections via std::tuple_cat:

    struct HasHealth {
        float* __restrict pfHealths = nullptr;
        float* __restrict pfMaxHealths = nullptr;
        static void ApplyDamage(HasHealth& rTrait, int64_t iIndex, float fAmount);
    };

Collections compose traits and include their members in SharedMembers() via tuple_cat.
Generic operations use C++ concepts to operate on any collection with a matching trait.

Why Not Now
-----------
- Only 16 collections (11 engine, 5 game) — explicit is still manageable
- Game entity collections (Players, Spaceships, Missiles, Blasters) have highly specific update logic
- Duplicated one-liners (position += velocity * dt) are too short to justify traits
- pFlags uses different enum types per collection — no shared logic possible
- The existing ControllerTypeRegistry mixin already handles the one real shared pattern

Revisit When
------------
- 3+ new game collections added with shared movement/health/damage logic
- Finding yourself copy-pasting 10+ lines of identical logic between new collections
- Wanting to add a behavior (like "everything takes area damage") to many collections at once
