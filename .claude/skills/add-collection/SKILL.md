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

### Step 3: Update FrameBase.h (11 locations)

**Includes section:**
```cpp
#include "Frame/Collections/NewCollection.h"
```

**FrameInterpolateBase struct (5 locations):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionInterpolate newCollections {};` |
| `operator==()` | `bEqual &= common::BreakOnNotEqual(newCollections, rOther.newCollections);` |
| `Crc()` | `checksum ^= CollectionCrc(newCollections, newCollections.Members());` |
| `Write()` | `CollectionWrite(rStream, newCollections, newCollections.Members());` |
| `Read()` | `CollectionRead(rStream, newCollections, newCollections.Members());` |

**FramePostRenderBase struct (5 locations):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionPostRender newCollections {};` |
| `operator==()` | `bEqual &= common::BreakOnNotEqual(newCollections, rOther.newCollections);` |
| `Crc()` | `checksum ^= CollectionCrc(newCollections, newCollections.Members());` |
| `Write()` | `CollectionWrite(rStream, newCollections, newCollections.Members());` |
| `Read()` | `CollectionRead(rStream, newCollections, newCollections.Members());` |

### Step 4: Update FrameBase.cpp (12 phase calls)

**FrameInterpolateBase methods (5 calls):**

| Method | Call to Add |
|--------|-------------|
| `Register()` | `NewCollectionInterpolate::Register();` |
| `GraphicsResources()` | `NewCollectionInterpolate::GraphicsResources();` |
| `AllocateAndCopy()` | `NewCollectionInterpolate::AllocateAndCopy(rCurrent.newCollections, rPrevious.newCollections);` |
| `Update()` | `NewCollectionInterpolate::Update(rCurrent, rPreviousFrame);` |
| `Render()` | `NewCollectionInterpolate::Render(rCurrent, iCommandBuffer);` |

**FramePostRenderBase methods (7 calls):**

| Method | Call to Add |
|--------|-------------|
| `AllocateAndCopy()` | `NewCollectionPostRender::AllocateAndCopy(rCurrent.newCollections, rPrevious.newCollections);` |
| `Update()` | `NewCollectionPostRender::Update(rFrame, rPreviousFrame);` |
| `PreCollision()` | `NewCollectionPostRender::PreCollision(rFrame, rPreviousFrame);` |
| `PostCollision()` | `NewCollectionPostRender::PostCollision(rFrame, rPreviousFrame);` |
| `AreaDamage()` | `NewCollectionPostRender::AreaDamage(rFrame, rPreviousFrame);` |
| `Destroy()` | `NewCollectionPostRender::Destroy(rFrame);` |
| `Spawn()` | `NewCollectionPostRender::Spawn(rFrame);` |

**Total engine collection integration points: ~24 locations**

## Game Collection Pattern

### Step 1: Create Collection Header

Create `/Projects/BrokenEngineSandbox/Source/Frame/Collections/NewCollection.h`:

```cpp
#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct NewCollectionInterpolate : public engine::Collection<NewCollectionInterpolate, engine::CollectionFlags::kIdToIndex>,
                                  public engine::Renderable<NewCollectionInterpolate, "NewCollection", {engine::RenderableFlags::kGltf}>
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

### Step 3: Update Frame.h (11 locations)

**Includes section:**
```cpp
#include "Frame/Collections/NewCollection.h"
```

**FrameInterpolate struct (5 locations):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionInterpolate newCollections {};` |
| `operator==()` | `bEqual &= common::BreakOnNotEqual(newCollections, rOther.newCollections);` |
| `Crc()` | `checksum ^= engine::CollectionCrc(rCurrent.newCollections, rCurrent.newCollections.Members());` |
| `Write()` | `engine::CollectionWrite(rStream, newCollections, newCollections.Members());` |
| `Read()` | `engine::CollectionRead(rStream, newCollections, newCollections.Members());` |

**FramePostRender struct (5 locations):**

| Location | Code to Add |
|----------|-------------|
| Member declaration | `NewCollectionPostRender newCollections {};` |
| `operator==()` | `bEqual &= common::BreakOnNotEqual(newCollections, rOther.newCollections);` |
| `Crc()` | `checksum ^= engine::CollectionCrc(rCurrent.newCollections, rCurrent.newCollections.Members());` |
| `Write()` | `engine::CollectionWrite(rStream, newCollections, newCollections.Members());` |
| `Read()` | `engine::CollectionRead(rStream, newCollections, newCollections.Members());` |

### Step 4: Update Frame.cpp (12 phase calls)

**FrameInterpolate methods (5 calls):**

| Method | Call to Add |
|--------|-------------|
| `Register()` | `NewCollectionInterpolate::Register();` |
| `GraphicsResources()` | `NewCollectionInterpolate::GraphicsResources();` |
| `AllocateAndCopy()` | `NewCollectionInterpolate::AllocateAndCopy(rCurrent.newCollections, rPrevious.newCollections);` |
| `Update()` | `NewCollectionInterpolate::Update(rCurrent, rPreviousFrame);` |
| `Render()` | `NewCollectionInterpolate::Render(rFrameInterpolate, iCommandBuffer);` |

**FramePostRender methods (7 calls):**

| Method | Call to Add |
|--------|-------------|
| `AllocateAndCopy()` | `NewCollectionPostRender::AllocateAndCopy(rCurrent.newCollections, rPrevious.newCollections);` |
| `Update()` | `NewCollectionPostRender::Update(rFrame, rPreviousFrame);` |
| `PreCollision()` | `NewCollectionPostRender::PreCollision(rFrame, rPreviousFrame);` |
| `PostCollision()` | `NewCollectionPostRender::PostCollision(rFrame, rPreviousFrame);` |
| `AreaDamage()` | `NewCollectionPostRender::AreaDamage(rFrame, rPreviousFrame);` |
| `Destroy()` | `NewCollectionPostRender::Destroy(rFrame);` |
| `Spawn()` | `NewCollectionPostRender::Spawn(rFrame);` |

**Total game collection integration points: ~24 locations**

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
- `kGltf`, `kGltfShadow` - 3D model rendering
- `kLighting`, `kAxisAlignedLighting` - Light contribution to scene
- `kVisibleLights` - Visible light sprites
- `kSmoke`, `kSmokeAxisAligned` - Smoke particle rendering
- `kBillboards` - Screen-space billboards
- `kHexShields`, `kHexShieldsLighting` - Shield mesh rendering

## Checklist Summary

### Engine Collection (24 locations)
- [ ] Create `NewCollection.h` in `/Engine/Source/Frame/Collections/`
- [ ] Create `NewCollection.cpp` in `/Engine/Source/Frame/Collections/`
- [ ] Add include to `FrameBase.h`
- [ ] Add member to `FrameInterpolateBase`
- [ ] Add to `FrameInterpolateBase::operator==`
- [ ] Add to `FrameInterpolateBase::Crc()`
- [ ] Add to `FrameInterpolateBase::Write()`
- [ ] Add to `FrameInterpolateBase::Read()`
- [ ] Add member to `FramePostRenderBase`
- [ ] Add to `FramePostRenderBase::operator==`
- [ ] Add to `FramePostRenderBase::Crc()`
- [ ] Add to `FramePostRenderBase::Write()`
- [ ] Add to `FramePostRenderBase::Read()`
- [ ] Add to `FrameInterpolateBase::Register()`
- [ ] Add to `FrameInterpolateBase::GraphicsResources()`
- [ ] Add to `FrameInterpolateBase::AllocateAndCopy()`
- [ ] Add to `FrameInterpolateBase::Update()`
- [ ] Add to `FrameInterpolateBase::Render()`
- [ ] Add to `FramePostRenderBase::AllocateAndCopy()`
- [ ] Add to `FramePostRenderBase::Update()`
- [ ] Add to `FramePostRenderBase::PreCollision()`
- [ ] Add to `FramePostRenderBase::PostCollision()`
- [ ] Add to `FramePostRenderBase::AreaDamage()`
- [ ] Add to `FramePostRenderBase::Destroy()`
- [ ] Add to `FramePostRenderBase::Spawn()`

### Game Collection (24 locations)
- [ ] Create `NewCollection.h` in `/Projects/*/Source/Frame/Collections/`
- [ ] Create `NewCollection.cpp` in `/Projects/*/Source/Frame/Collections/`
- [ ] Add include to `Frame.h`
- [ ] Add member to `FrameInterpolate`
- [ ] Add to `FrameInterpolate::operator==`
- [ ] Add to `FrameInterpolate::Crc()`
- [ ] Add to `FrameInterpolate::Write()`
- [ ] Add to `FrameInterpolate::Read()`
- [ ] Add member to `FramePostRender`
- [ ] Add to `FramePostRender::operator==`
- [ ] Add to `FramePostRender::Crc()`
- [ ] Add to `FramePostRender::Write()`
- [ ] Add to `FramePostRender::Read()`
- [ ] Add to `FrameInterpolate::Register()`
- [ ] Add to `FrameInterpolate::GraphicsResources()`
- [ ] Add to `FrameInterpolate::AllocateAndCopy()`
- [ ] Add to `FrameInterpolate::Update()`
- [ ] Add to `FrameInterpolate::Render()`
- [ ] Add to `FramePostRender::AllocateAndCopy()`
- [ ] Add to `FramePostRender::Update()`
- [ ] Add to `FramePostRender::PreCollision()`
- [ ] Add to `FramePostRender::PostCollision()`
- [ ] Add to `FramePostRender::AreaDamage()`
- [ ] Add to `FramePostRender::Destroy()`
- [ ] Add to `FramePostRender::Spawn()`

## Important Notes

- **Complete all integration points**: Missing any location causes compilation errors, serialization failures, or determinism issues
- **Version numbers**: Increment `Frame::kiVersion` when adding collections for save file compatibility
- **Members() drives everything**: The `Members()` method automatically handles memory allocation, serialization, and CRC via template functions
- **Phase order matters**: Interpolate runs before PostRender; Register before GraphicsResources

## See Also

- Adding members to existing collections: [../add-collection-member/SKILL.md](../add-collection-member/SKILL.md)
- Engine collections: [/Engine/Source/Frame/Collections/CLAUDE.md](/Engine/Source/Frame/Collections/CLAUDE.md)
- Game collections: [/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md](/Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md)
- Frame system: [/Engine/Source/Frame/CLAUDE.md](/Engine/Source/Frame/CLAUDE.md)
