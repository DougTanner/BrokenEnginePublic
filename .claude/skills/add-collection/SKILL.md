---
name: add-collection
description: Reference guide for adding new dynamically-allocated Structure-of-Arrays collections to the engine or game frame system. Use this skill when adding a new collection type, creating a new entity/object type for the frame system, or when the user asks to add something that needs SOA storage with interpolation and update phases (e.g., new projectile type, new light type, new effect system). ALSO use this skill proactively whenever your implementation plan requires creating a new struct that inherits from `Collection<T>`, even if the user didn't explicitly ask to "add a collection." Also use when the user references FrameBase.h, Frame.h collection registration, Collection base class, or ForEach helpers.
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

namespace engine { struct FrameStaticData; }

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

	// Update phases (all receive const engine::FrameStaticData& rStaticData)
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void Transfer(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Destroy(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Spawn(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);

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
- `SharedMembers()` — fields needed by both client and server (serialized in server streams, participate in CRC)
- `ClientMembers()` — client-only fields gated by `#ifdef BT_CLIENT` (visual references like area lights, sounds, wind trails)
- `Members()` — combines both via `std::tuple_cat` (client) or returns just `SharedMembers()` (server)

If a collection has no client-only fields, `ClientMembers()` returns an empty `std::tie()` — keep the method and the `std::tuple_cat` in `Members()` so the client/server pattern stays uniform across collections.

**Discipline:** keep client-only pointers OUT of `SharedMembers()`. `ServerCollectionRead()` reads only `SharedMembers()` from the server stream — a client-only pointer leaked into `SharedMembers()` will read stream bytes as garbage into a client-owned object handle.

### `extern template` is required

The `extern template struct Collection<…>` declarations in the header are **required**, not optional. The corresponding `.cpp` does `template struct engine::Collection<…>;` (explicit instantiation). Without the `extern template` in the header, callers trigger implicit instantiation, which causes link conflicts or ODR violations.

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

### Step 3: Update FrameBase.cpp — LogDifferences (manual)

Add `LogDifferences` calls for the new collection in both `FrameInterpolateBase::LogDifferences()` and `FramePostRenderBase::LogDifferences()`:
```cpp
bEqual &= newCollections.LogDifferences(rOther.newCollections);
```

All other operations (phase dispatch, serialization, CRC, AllocateAndCopy) are automatic via `Collections()`.

### Step 4: Add files to vcxproj

Add the new `.h` and `.cpp` files to the engine `.vcxproj` and `.vcxproj.filters`. The filter path must mirror the on-disk directory (e.g., `Engine/Source/Frame/Collections/NewCollection/` -> filter `Engine\Frame\Collections\NewCollection`). Create a new `<Filter>` definition with a GUID if the filter doesn't exist yet.

### Step 5: Bump `kiVersion` if the shared layout changed

If the new collection adds fields to `SharedMembers()` that participate in CRC or server streams, bump the per-collection `static constexpr int64_t kiVersion` (if the collection has one) AND any parent-frame `kiVersion`. For pure client-only additions (`ClientMembers()` only), no bump is needed — server build does not see them.

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

### Step 6: Add files to vcxproj (FOUR files for game collections)

Game projects ship as a client AND server executable from a shared source tree, so every new file must be added to **four** files:

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj`
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters`
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj`
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj.filters`

The filter path must mirror the on-disk directory (e.g., `Source/Frame/Collections/NewCollection/` -> filter `Game\Frame\Collections\NewCollection`). Create a new `<Filter>` definition with a GUID if the filter doesn't exist yet. Files fully wrapped in `#if defined(BT_CLIENT)` go in the client project only; `#if defined(BT_SERVER)`-only files go in the server project only. Shared files go in both.

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
| **Frame.cpp changes** | LogDifferences (manual) | Constructors + LogDifferences |
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

### Manual Frame.cpp Handling (Players Pattern)

Players is excluded from `GameInterpolateCollections`/`GamePostRenderCollections` and handled entirely manually in Frame.cpp (constructors, Register, GraphicsResources, AllocateAndCopy, all phases, Crcs, LogDifferences, Write, Read, ServerRead). This is needed when a collection uses `CollectionFlags::kIdToIndex`, has a custom `SharedCrcMembers()` for CRC, or has a non-standard Spawn signature (taking `FrameInput`). If a new collection needs any of these, it must also be manually wired in Frame.cpp instead of relying on the tuple dispatch.

### Client Object Hydration

Collections with client-only owned objects (area lights, wind trails, sounds) provide `ClientInit()` and `ClientInitAll()` to create visual/audio objects after receiving server state, since server streams exclude client-only fields.

## Checklist Summary

### Engine Collection
- [ ] Create `NewCollection.h` and `NewCollection.cpp` in `/Engine/Source/Frame/Collections/NewCollection/`
- [ ] Add include to `FrameBase.h` (wrap in `#ifdef BT_CLIENT` if client-only)
- [ ] Add member + `Collections()` entry + increment `kCollectionCount` in `FrameInterpolateBase`
- [ ] Add member + `Collections()` entry + increment `kCollectionCount` in `FramePostRenderBase`
- [ ] Add to `ServerCollections()` if server-relevant
- [ ] Add `LogDifferences` calls in both `FrameInterpolateBase::LogDifferences()` and `FramePostRenderBase::LogDifferences()` in FrameBase.cpp
- [ ] Add files to engine `.vcxproj`

**Automatic via Collections():** Crcs, Write, Read, ServerRead, AllocateAndCopy, all ForEach phase dispatch. **Manual:** LogDifferences (FrameBase.cpp).

**Note:** Engine collections always come in Interpolate/PostRender pairs — `static_assert` in FrameBase.h enforces matching `kCollectionCount` between the two.

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
