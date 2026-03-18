---
name: add-collection
description: Reference guide for adding new dynamically-allocated Structure-of-Arrays collections to the engine or game frame system. Use this skill when adding a new collection type, creating a new entity/object type for the frame system, or when the user asks to add something that needs SOA storage with interpolation and update phases (e.g., new projectile type, new light type, new effect system). Also use when the user references FrameBase.h, Frame.h collection registration, Collection base class, or ForEach helpers.
allowed-tools: [Read, Edit]
---

# Adding New Collections

Reference guide for adding a new collection to the frame system. Collections use Structure-of-Arrays layout with dual-phase updates (Interpolate for rendering, PostRender for logic). Each collection has seven PostRender sub-phases: Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn.

## When to Use

- **Engine collections**: Shared across all games (lights, effects, audio, physics) — created in `/Engine/Source/Frame/Collections/`
- **Game collections**: Game-specific objects (entities, projectiles) — created in `/Projects/*/Source/Frame/Collections/`

## Collection Header Pattern

Both engine and game collections follow the same struct layout. The example below shows a game collection; engine collections differ only in namespace and base class qualification (see Key Differences table).

```cpp
#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct NewCollectionInterpolate : public engine::Collection<NewCollectionInterpolate>
{
	static constexpr char kName[] = "NewCollection";
	static constexpr common::crc_t kCrc = common::CrcConsteval(kName);

	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(NewCollectionInterpolate& rCurrent, const NewCollectionInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

#if defined(BT_CLIENT)
	// Render
	static void BeginRender(int64_t, const std::unordered_map<engine::GridCoord, FrameInterpolate>&, const std::vector<engine::GridCoord>&) {}
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t) {}
#endif

	// SOA members
	XMVECTOR* __restrict pVecPositions = nullptr;
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
#if defined(BT_CLIENT)
	// Client-only visual references (area lights, sounds, wind trails, etc.)
	auto ClientMembers(this auto&& rSelf) { return std::tie(); }
#endif
	auto Members(this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}

	// Utility
	bool LogDifferences(const NewCollectionInterpolate& rOther) const;
};

struct NewCollectionPostRender : public engine::Collection<NewCollectionPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(NewCollectionPostRender& rCurrent, const NewCollectionPostRender& rPrevious);

	// Update phases
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	// SOA members
	auto SharedMembers(this auto&& rSelf) { return std::tie(); }
	auto Members([[maybe_unused]] this auto&& rSelf) { return std::tie(); }

	// Utility
	bool LogDifferences(const NewCollectionPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		// Add spawn parameters here
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::NewCollectionInterpolate>;
extern template struct Collection<game::NewCollectionPostRender>;
}
```

### Members Pattern (Client/Server)

Collections always use the three-method pattern for client/server support:
- `SharedMembers()` — fields needed by both client and server (serialized in server streams)
- `ClientMembers()` — client-only fields gated by `#ifdef BT_CLIENT` (visual references like area lights, sounds, wind trails)
- `Members()` — combines both via `std::tuple_cat` (client) or returns just `SharedMembers()` (server)

If a collection has no client-only fields, `ClientMembers()` can return an empty `std::tie()` or be omitted (with `Members()` just returning `SharedMembers()`).

## Engine Collection Steps

### Step 1: Create Header and Implementation

Create `NewCollection.h` and `NewCollection.cpp` (or split into multiple `.cpp` files if large) in `/Engine/Source/Frame/Collections/NewCollection/`.

The `.cpp` file needs:
- Explicit template instantiations: `template struct Collection<NewCollectionInterpolate>;`
- `AllocateAndCopy()` implementations for both structs
- `LogDifferences()` using `Collection::LogDifferences()` base call
- Static method implementations for all phases

### Step 2: Update FrameBase.h

**Include** (wrap in `#ifdef BT_CLIENT` if client-only):
```cpp
#include "Frame/Collections/NewCollection/NewCollection.h"
```

**FrameInterpolateBase** — add member, add to `Collections()` tuple, increment `kCollectionCount`:

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionInterpolate newCollections {};` |
| `Collections()` | Add `rSelf.newCollections` to the `std::tie()` return |
| `kCollectionCount` | Increment by 1 (both `BT_CLIENT` and `BT_SERVER` counts) |

**FramePostRenderBase** — same three changes:

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionPostRender newCollections {};` |
| `Collections()` | Add `rSelf.newCollections` to the `std::tie()` return |
| `kCollectionCount` | Increment by 1 (both `BT_CLIENT` and `BT_SERVER` counts) |

**ServerCollections()** — if the collection is relevant to server CRC validation, add it to `ServerCollections()` in both structs. Client-only collections (lights, sounds, billboards) skip this.

**Client-only wrapping**: Most engine collections are client-only. Wrap the include, members, and `Collections()` entries in `#ifdef BT_CLIENT`. Only server-relevant collections (like Explosions, Pushers) are always compiled.

### Step 3: No changes needed in FrameBase.cpp

The `ForEach*` helpers and `AllocateAndCopyCollections()` use type lists derived from `Collections()`, so all phase methods, serialization, and CRC are called automatically.

### Step 4: Add files to vcxproj

Add the new `.h` and `.cpp` files to the appropriate filter in the engine `.vcxproj`.

## Game Collection Steps

### Step 1: Create Header and Implementation

Create in `/Projects/BrokenEngineSandbox/Source/Frame/Collections/NewCollection/`:
- `NewCollection.h` — struct definitions (see template above)
- `NewCollection.cpp` — lifecycle (AllocateAndCopy, Spawn, Destroy, Transfer, LogDifferences)
- Split into `NewCollectionUpdate.cpp` and `NewCollectionRender.cpp` if the file gets large

The `.cpp` file needs:
- Explicit template instantiations: `template struct engine::Collection<NewCollectionInterpolate>;`
- `AllocateAndCopy()` implementations for both structs
- `LogDifferences()` using `Collection::LogDifferences()` base call
- Static method implementations for all phases

### Step 2: Update Frame.h (3 locations)

**Forward declarations** (at top of namespace, with existing forward decls):
```cpp
struct NewCollectionInterpolate;
struct NewCollectionPostRender;
```

**FrameInterpolate** — add `std::unique_ptr` member:
```cpp
std::unique_ptr<NewCollectionInterpolate> pNewCollections;
```

**FramePostRender** — add `std::unique_ptr` member:
```cpp
std::unique_ptr<NewCollectionPostRender> pNewCollections;
```

### Step 3: Update FrameCollections.h (3 locations)

**Include**:
```cpp
#include "Frame/Collections/NewCollection/NewCollection.h"
```

**GameInterpolateCollections()** — add `*rSelf.pNewCollections` to `std::tie()`:
```cpp
return std::tie(*rSelf.pBlasters, ..., *rSelf.pNewCollections);
```

**GamePostRenderCollections()** — add `*rSelf.pNewCollections` to `std::tie()`:
```cpp
return std::tie(*rSelf.pBlasters, ..., *rSelf.pNewCollections);
```

### Step 4: Update Frame.cpp (4 locations)

**Constructors** — add `std::make_unique` in both `FrameInterpolate` and `FramePostRender` constructors:
```cpp
, pNewCollections(std::make_unique<NewCollectionInterpolate>())
```

**LogDifferences** — add to both `FrameInterpolate::LogDifferences()` and `FramePostRender::LogDifferences()`:
```cpp
bEqual &= pNewCollections->LogDifferences(*rOther.pNewCollections);
```

### Step 5: Increment Frame::kiVersion

In `Frame.h`, increment `Frame::kiVersion` for save file compatibility.

### Step 6: Add files to vcxproj

Add the new `.h` and `.cpp` files to the appropriate filter in the game `.vcxproj`.

### What's Automatic for Game Collections

Once the collection is in the `GameInterpolateCollections()`/`GamePostRenderCollections()` tuples:
- **Phase dispatch**: Register, GraphicsResources, AllocateAndCopy, Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn, BeginRender, Render, EndRender
- **Serialization**: Crcs, Write, Read, ServerRead

**Not automatic** (requires manual additions in Frame.cpp):
- LogDifferences — each collection listed by name
- Constructor initialization — `std::make_unique<>()` calls

## Key Differences: Engine vs Game

| Aspect | Engine Collection | Game Collection |
|--------|-------------------|-----------------|
| **Location** | `/Engine/Source/Frame/Collections/` | `/Projects/*/Source/Frame/Collections/` |
| **Namespace** | `engine::` | `game::` |
| **Collection Base** | `Collection<T>` | `engine::Collection<T>` |
| **Frame Header** | `FrameBase.h` | `Frame.h` + `FrameCollections.h` |
| **Storage** | Direct members in FrameBase structs | `std::unique_ptr` with forward declarations |
| **Collection tuple** | `Collections()` method on FrameBase structs | `GameInterpolateCollections()` / `GamePostRenderCollections()` free functions |
| **kCollectionCount** | Must increment (has `static_assert`) | Not applicable |
| **Frame.cpp changes** | None (fully automatic) | Constructors + LogDifferences |
| **Update Parameters** | `game::FrameInterpolate&`, `game::Frame&` | `FrameInterpolate&`, `Frame&` |

## Optional Features

### ID-to-Index Mapping

Add `CollectionFlags::kIdToIndex` when external systems need stable references:
```cpp
struct NewCollectionInterpolate : public engine::Collection<NewCollectionInterpolate, engine::CollectionFlags::kIdToIndex>
```

This provides:
- `id_t` type alias for stable external references (add `using new_collection_t = NewCollectionInterpolate::id_t;`)
- `idToIndexMap` for lookup after reordering
- Required when other collections reference elements by ID

### Type Registry

Add `TypeRegistry<TType>` for shared static configuration:
```cpp
struct NewCollectionType
{
	common::crc_t crc = 0;
	// Shared properties
};

struct NewCollectionInterpolate : public engine::Collection<NewCollectionInterpolate>,
                                  public engine::TypeRegistry<NewCollectionType>
```

### Client Object Hydration

Collections with client-only owned objects (area lights, wind trails, sounds) provide `ClientInit()` and `ClientInitAll()` to create visual/audio objects after receiving server state, since server streams exclude client-only fields.

## Checklist Summary

### Engine Collection
- [ ] Create `NewCollection.h` and `NewCollection.cpp` in `/Engine/Source/Frame/Collections/NewCollection/`
- [ ] Add include to `FrameBase.h` (wrap in `#ifdef BT_CLIENT` if client-only)
- [ ] Add member + `Collections()` entry + increment `kCollectionCount` in `FrameInterpolateBase`
- [ ] Add member + `Collections()` entry + increment `kCollectionCount` in `FramePostRenderBase`
- [ ] Add to `ServerCollections()` if server-relevant
- [ ] Add files to engine `.vcxproj`

**Automatic via Collections():** LogDifferences, Crcs, Write, Read, ServerRead, AllocateAndCopy, all ForEach phase dispatch

### Game Collection
- [ ] Create `NewCollection.h` and `NewCollection.cpp` in `/Projects/*/Source/Frame/Collections/NewCollection/`
- [ ] Add forward declarations in `Frame.h`
- [ ] Add `std::unique_ptr` members in `FrameInterpolate` and `FramePostRender` (in `Frame.h`)
- [ ] Add include + tuple entries in `FrameCollections.h`
- [ ] Add `std::make_unique` in both constructors in `Frame.cpp`
- [ ] Add `LogDifferences()` calls in both `FrameInterpolate::LogDifferences()` and `FramePostRender::LogDifferences()` in `Frame.cpp`
- [ ] Increment `Frame::kiVersion` in `Frame.h`
- [ ] Add files to game `.vcxproj`

**Automatic via tuple accessors:** Crcs, Write, Read, ServerRead, AllocateAndCopy, all ForEach phase dispatch (Register, GraphicsResources, Update, Render, etc.)

## See Also

- Adding members to existing collections: [../add-collection-member/SKILL.md](../add-collection-member/SKILL.md)
- Engine collections: [/Engine/Source/Frame/Collections/CLAUDE.md](/Engine/Source/Frame/Collections/CLAUDE.md)
- Game collections: [/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md](/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md)
- Frame system: [/Engine/Source/Frame/CLAUDE.md](/Engine/Source/Frame/CLAUDE.md)
