---
name: add-collection
description: Reference guide for adding new dynamically-allocated Structure-of-Arrays collections to the engine or game frame system.
allowed-tools: [Read, Edit]
---

# Adding New Collections

Reference guide for adding a new collection to the frame system. Collections use Structure-of-Arrays layout with dual-phase updates (Interpolate/PostRender).

## When to Use

Use this guide when creating a new collection type:
- **Engine collections**: Shared across all games (lights, effects, audio) - created in `/Engine/Source/Frame/Collections/`
- **Game collections**: Game-specific objects (entities, projectiles) - created in `/Projects/*/Source/Frame/Collections/`

## Engine Collection Pattern

### Step 1: Create Collection Header

Create `/Engine/Source/Frame/Collections/NewCollection.h`:

```cpp
#pragma once

#include "Frame/Collections/Collection.h"

namespace engine
{

struct NewCollectionInterpolate : public Collection<NewCollectionInterpolate, CollectionFlags::kIdToIndex>,
                                  public Renderable<NewCollectionInterpolate, "NewCollection", {RenderableFlags::kLighting}>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(NewCollectionInterpolate& rCurrent, const NewCollectionInterpolate& rPrevious);

	// Interpolate
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Members - add all SOA pointers here
	XMVECTOR* __restrict pVecPositions = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }

	// Render
	static void Render(const game::FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const NewCollectionInterpolate& rOther) const;
};
using new_collection_t = NewCollectionInterpolate::id_t;

struct NewCollectionPostRender : public Collection<NewCollectionPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(NewCollectionPostRender& rCurrent, const NewCollectionPostRender& rPrevious);

	// Update phases
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void PostCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void AreaDamage(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static void Destroy(game::Frame& __restrict rFrame);
	static void Spawn(game::Frame& __restrict rFrame);

	// Members - add all SOA pointers here
	new_collection_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const NewCollectionPostRender& rOther) const;
};

} // namespace engine
```

### Step 2: Create Collection Implementation

Create `/Engine/Source/Frame/Collections/NewCollection.cpp` with:
- `operator==` implementations using `CompareCountAndCapacity` and `common::BreakOnNotEqual`
- Static method implementations for all phases

### Step 3: Update FrameBase.h (3 locations)

**Includes section:**
```cpp
#include "Frame/Collections/NewCollection.h"
```

**FrameInterpolateBase struct (1 location):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionInterpolate newCollections {};` |
| `Collections()` | Add `rSelf.newCollections` to the `std::tie()` return |
| `kCollectionCount` | Increment by 1 |

The `Collections()` method automatically handles `operator==`, `Crc()`, `Write()`, and `Read()` via template helpers.

**FramePostRenderBase struct (1 location):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionPostRender newCollections {};` |
| `Collections()` | Add `rSelf.newCollections` to the `std::tie()` return |
| `kCollectionCount` | Increment by 1 |

### Step 4: Update FrameBase.cpp (0 locations - automatic!)

**No changes needed!** The `ForEach*` helpers and `AllocateAndCopyCollections()` use type lists derived from `Collections()`, so all phase methods are automatically called for any collection in the tuple.

**Total engine collection integration points: ~4 locations (header + cpp files)**

## Game Collection Pattern

### Step 1: Create Collection Header

Create `/Projects/BrokenEngineSandbox/Source/Frame/Collections/NewCollection.h`:

```cpp
#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct NewCollectionInterpolate : public engine::Collection<NewCollectionInterpolate, engine::CollectionFlags::kIdToIndex>,
                                  public engine::Renderable<NewCollectionInterpolate, "NewCollection", {engine::RenderableFlags::kModel}>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(NewCollectionInterpolate& rCurrent, const NewCollectionInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rFrameInterpolate, const Frame& __restrict rPreviousFrame);

	// Members - add all SOA pointers here
	XMVECTOR* __restrict pVecPositions = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const NewCollectionInterpolate& rOther) const;
};
using new_collection_t = NewCollectionInterpolate::id_t;

struct NewCollectionPostRender : public engine::Collection<NewCollectionPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(NewCollectionPostRender& rCurrent, const NewCollectionPostRender& rPrevious);

	// Update phases
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	// Members - add all SOA pointers here
	new_collection_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const NewCollectionPostRender& rOther) const;
};

} // namespace game
```

### Step 2: Create Collection Implementation

Create `/Projects/BrokenEngineSandbox/Source/Frame/Collections/NewCollection.cpp` with:
- `operator==` implementations
- Static method implementations for all phases
- Spawn() should include spawn request struct and implementation

### Step 3: Update Frame.h (3 locations)

**Includes section:**
```cpp
#include "Frame/Collections/NewCollection.h"
```

**FrameInterpolate struct (1 location):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionInterpolate newCollections {};` |
| `Collections()` | Add `rSelf.newCollections` to the `std::tie()` return |

The `Collections()` method automatically handles `operator==`, `Crc()`, `Write()`, and `Read()` via template helpers.

**FramePostRender struct (1 location):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionPostRender newCollections {};` |
| `Collections()` | Add `rSelf.newCollections` to the `std::tie()` return |

### Step 4: Update Frame.cpp (0 locations - automatic!)

**No changes needed!** The `ForEach*` helpers and `AllocateAndCopyCollections()` use type lists derived from `Collections()`, so all phase methods are automatically called for any collection in the tuple.

**Total game collection integration points: ~4 locations (header + cpp files)**

## Key Differences: Engine vs Game

| Aspect | Engine Collection | Game Collection |
|--------|-------------------|-----------------|
| **Location** | `/Engine/Source/Frame/Collections/` | `/Projects/*/Source/Frame/Collections/` |
| **Frame Header** | `FrameBase.h` | `Frame.h` |
| **Frame Implementation** | `FrameBase.cpp` | `Frame.cpp` |
| **Base Struct Names** | `FrameInterpolateBase`, `FramePostRenderBase` | `FrameInterpolate`, `FramePostRender` |
| **Namespace** | `engine::` | `game::` |
| **Collection Base** | `Collection<T>` | `engine::Collection<T>` |
| **Renderable Base** | `Renderable<T, ...>` | `engine::Renderable<T, ...>` |
| **Update Parameters** | `game::FrameInterpolate&`, `game::Frame&` | `FrameInterpolate&`, `Frame&` |
| **Crc/Write/Read helpers** | `CollectionCrc`, `CollectionWrite`, `CollectionRead` | `engine::CollectionCrc`, `engine::CollectionWrite`, `engine::CollectionRead` |

## Optional Features

### ID-to-Index Mapping

Add `CollectionFlags::kIdToIndex` when external systems need stable references:
```cpp
struct NewCollectionInterpolate : public Collection<NewCollectionInterpolate, CollectionFlags::kIdToIndex>
```

This enables:
- `id_t` type alias for stable external references
- ID-to-index lookup for element access after reordering
- Required when other collections reference elements by ID

### Type Registry

Add `TypeRegistry<TType>` for shared static configuration:
```cpp
struct NewCollectionType
{
	common::crc_t crc = 0;
	// Shared properties (textures, colors, etc.)
};

struct NewCollectionInterpolate : public Collection<NewCollectionInterpolate>,
                                  public TypeRegistry<NewCollectionType>
```

### Renderable Flags

Available `RenderableFlags`:
- `kModel`, `kModelShadow` - 3D model rendering
- `kLighting`, `kAxisAlignedLighting` - Light contribution to scene
- `kVisibleLights` - Visible light sprites
- `kSmoke`, `kSmokeAxisAligned` - Smoke particle rendering
- `kBillboards` - Screen-space billboards
- `kHexShields`, `kHexShieldsLighting` - Shield mesh rendering

## Checklist Summary

### Engine Collection (4 locations)
- [ ] Create `NewCollection.h` in `/Engine/Source/Frame/Collections/`
- [ ] Create `NewCollection.cpp` in `/Engine/Source/Frame/Collections/`
- [ ] Add include to `FrameBase.h`
- [ ] Add member to `FrameInterpolateBase` and add to `Collections()` tuple
- [ ] Add member to `FramePostRenderBase` and add to `Collections()` tuple
- [ ] Increment `kCollectionCount` in both `FrameInterpolateBase` and `FramePostRenderBase`

**Automatic via Collections():** `operator==`, `Crc()`, `Write()`, `Read()`, `AllocateAndCopy()`

**Automatic via ForEach helpers:** `Register()`, `GraphicsResources()`, `Update()`, `Render()`, `PreCollision()`, `PostCollision()`, `AreaDamage()`, `Destroy()`, `Spawn()`

### Game Collection (4 locations)
- [ ] Create `NewCollection.h` in `/Projects/*/Source/Frame/Collections/`
- [ ] Create `NewCollection.cpp` in `/Projects/*/Source/Frame/Collections/`
- [ ] Add include to `Frame.h`
- [ ] Add member to `FrameInterpolate` and add to `Collections()` tuple
- [ ] Add member to `FramePostRender` and add to `Collections()` tuple

**Automatic via Collections():** `operator==`, `Crc()`, `Write()`, `Read()`, `AllocateAndCopy()`

**Automatic via ForEach helpers:** `Register()`, `GraphicsResources()`, `Update()`, `Render()`, `PreCollision()`, `PostCollision()`, `AreaDamage()`, `Destroy()`, `Spawn()`

## Important Notes

- **Collections() drives automatic integration**: Adding a collection to the `Collections()` tuple automatically handles `operator==`, `Crc()`, `Write()`, `Read()`, and all phase method calls via ForEach helpers
- **Members() drives member iteration**: The `Members()` method automatically handles memory allocation, per-member serialization, and per-member CRC via template functions
- **Version numbers**: Increment `Frame::kiVersion` when adding collections for save file compatibility
- **Phase order matters**: Interpolate runs before PostRender; Register before GraphicsResources
- **Type aliases**: `GameInterpolateTypes` and `GamePostRenderTypes` are derived from `Collections()` and used by ForEach helpers

## See Also

- Adding members to existing collections: [../add-collection-member/SKILL.md](../add-collection-member/SKILL.md)
- Engine collections: [/Engine/Source/Frame/Collections/CLAUDE.md](/Engine/Source/Frame/Collections/CLAUDE.md)
- Game collections: [/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md](/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md)
- Frame system: [/Engine/Source/Frame/CLAUDE.md](/Engine/Source/Frame/CLAUDE.md)
