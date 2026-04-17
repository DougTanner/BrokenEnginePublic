# Architecture: Include Graph Cleanup

Source: /external-architecture-review on DataPacker/Source/

Goal: shrink the public-header transitive closure so touching `ExportScene.h` (included by `Main.cpp` and `ExportTexture.cpp`) does not drag `tinygltf/tiny_gltf.h` into every TU. Also remove redundant / dead `#include` lines.

## Changes

### DataPacker/Source/ExportJobs/ExportScene.h
- Line 3: move `#include "tinygltf/tiny_gltf.h"` out of the header. Replace with a forward declaration `namespace tinygltf { class Model; }` — only private `ExportScene::PreExport` / `MainExport` / `LoadGltfModel` members reference the type (all by reference except `LoadGltfModel` which returns by value — since it's private and only called in `ExportScene.cpp`, forward-declare suffices for the header) [~10m]
- Line 6: delete `#include "ExportSceneSkeleton.h"` — `ExportScene.h` never references `SkeletonData` or any symbol from the skeleton header [~5m]

### DataPacker/Source/ExportJobs/ExportSceneAnimation.h
- Line 3: remove `#include "tinygltf/tiny_gltf.h"`. Forward-declare `namespace tinygltf { class Model; struct Animation; }` as needed; call sites pass by const-reference only [~10m]

### DataPacker/Source/ExportJobs/ExportSceneSkeleton.h
- Line 3: remove `#include "tinygltf/tiny_gltf.h"`. Forward-declare `namespace tinygltf { class Model; class Node; }` as needed (note: `Node` is a `class` in tinygltf, not a `struct`) [~10m]

### DataPacker/Source/ExportJobs/ExportSceneVertices.h
- Line 3: remove `#include "tinygltf/tiny_gltf.h"`. Forward-declare `namespace tinygltf { class Model; class Node; struct Material; struct Primitive; }` (`Node` and `Model` are classes; `Material` and `Primitive` are structs) [~10m]

### DataPacker/Source/ExportJobs/ExportScene.cpp
- Add `#include "tinygltf/tiny_gltf.h"` once near top of file so definitions are available for all four helpers (this .cpp is the only TU that links tinygltf types) [~5m]

### DataPacker/Source/ExportJobs/ExportSceneAnimation.cpp, ExportSceneSkeleton.cpp, ExportSceneVertices.cpp
- Each: add direct `#include "tinygltf/tiny_gltf.h"` at top (currently reached only transitively through their own `.h` — once the forward-declare move happens, each `.cpp` needs the definition) [~5m each]

### DataPacker/Source/Main.cpp
- Lines 330-336: delete the `#if defined(DEBUG) || defined(_DEBUG)` block that redefines `_CRTDBG_MAP_ALLOC` and includes `<crtdbg.h>`. `Common/ExternalHeaders.h:39-45` already includes `<crtdbg.h>` whenever `ENABLE_CRT_DEBUG_HEAP` is set, which `Pch.h:3` does unconditionally. Block is pure duplication. The subsequent operator new/delete overrides and `CrtBreakAllocSetter` at 338-375 do serve a real purpose and must stay [~5m]

## Expected Outcome

- Every TU that includes `ExportScene.h` (including `Main.cpp`) stops pulling in tinygltf's JSON parser + STL-heavy transitive closure
- One dead include removed from `ExportScene.h`
- One redundant debug-CRT block removed from `Main.cpp`

## Verification Notes

- All file paths and line numbers verified against source.
- Confirmed `Pch.h:3` defines `ENABLE_CRT_DEBUG_HEAP` unconditionally, so `Common/ExternalHeaders.h:43-45` always includes `<crtdbg.h>` in DataPacker. The duplicate in `Main.cpp:330-336` is unreachable-as-effect (macro already defined).
- Corrected `ExternalHeaders.h` line range (was 43-45, full block is 39-45 including the define gate).
- Corrected forward-declaration keyword (`Node` is a `class`, not a `struct` in tinygltf).
- Flagged that `LoadGltfModel()` returns `tinygltf::Model` by value — since it's private and only invoked from `ExportScene.cpp`, forward-declaring `class Model` in the header remains sufficient.
