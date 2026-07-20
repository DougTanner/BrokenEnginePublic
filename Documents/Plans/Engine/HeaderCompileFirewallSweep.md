# Header Compile-Firewall Sweep (heavy sub-object behind unique_ptr + forward-decl)

Compile-hygiene / debt-reduction refactor. Behavior-preserving; no runtime, determinism, wire, save, or `.pack` exposure.

## Context

The `FileManager` → `PackChunks` extraction (this session) is the template for this plan: a class that lives in an `Engine.h`-aggregated header exposed its heavy implementation internals (2 `std::thread`, 2 `std::condition_variable`, `std::mutex`, `std::priority_queue`, `std::future`, `HANDLE[]`, `VirtualAlloc` pool pointers) directly as member declarations. Those internals are pure implementation detail — no consumer touches them; consumers only call the public chunk API through `gpFileManager`. Moving them into a new `engine::PackChunks` class held by `std::unique_ptr<PackChunks>` with only a **forward declaration** in `FileManager.h` (full `PackChunks.h` included by just `PackChunks.cpp` + `FileManager.cpp`) took the heavy guts out of the header that every TU parses. This plan finds the other classes in the codebase that fit the same shape and applies the same firewall.

**Honest benefit accounting (read before scoring the work).** Every standard-library and third-party header these members need is *already* in the PCH via `Common/ExternalHeaders.h` — `<mutex>`, `<thread>`, `<condition_variable>`, `<future>`, `<queue>`, `<functional>`, `<optional>`, `<unordered_map>`, `<semaphore>`, `<vector>`, plus Volk/Vulkan, VMA, `nlohmann::json` (tinygltf), XAudio2 (DirectXTK), ENet, and imgui. So this is **not** an `#include`-elimination win. The real, but modest, benefits are:

1. **Incremental-build isolation (primary).** A heavy member set declared in an `Engine.h`-aggregated header sits in the PCH. Editing that member set (adding/removing/reordering a field, changing a member type) invalidates the PCH and forces a **whole-project** recompile. Behind the firewall, the volatile internals live in a non-aggregated `*.h` included by only its owner's 1-2 TUs, so iterating on them recompiles those TUs instead of the world. This win only materializes when the internals' *header* churns (method-body edits already live in `.cpp` and never had this cost).
2. **Smaller PCH parse** — marginal (PCH amortizes header tokenization across TUs; the residue is redundant template instantiation / semantic analysis of the member declarations).
3. **Encapsulation / cohesion** — consumers can no longer reach the internals; the extracted class gets a single responsibility, exactly as `PackChunks` did for `FileManager`.

Per-class impact is therefore modest; the value is in **batching** several mechanical extractions behind one review/compile cycle and establishing the `PackChunks` precedent consistently. If the measured/expected win is judged too small, the plan may legitimately pare down to the single strongest candidate (`AgentCommandServer`) or close as accept-and-document.

## Design

For each accepted candidate, mirror the `PackChunks` mechanics:

- New sibling `<Name>Internals`-style class (or keep the existing cohesive sub-object type) holding the heavy members, defined in a **non-aggregated** header included only by its owner's TU(s).
- Owner header keeps a **forward declaration** + a `std::unique_ptr<T>` member; drop the now-unneeded `#include` of the heavy header from the owner header and from `Engine.h`.
- Out-of-line the owner's ctor/dtor into its `.cpp` (where the pointee is complete) so `unique_ptr`'s `~T()` sees a full type.
- Public methods on the owner forward to the pointee; any **inline** accessor in the owner header that touched a moved member becomes out-of-line (noted per candidate below).

Two firewall shapes exist; pick per candidate at grill:

- **A — pimpl shell** (what `PackChunks` did): keep the owner class in `Engine.h`, move heavy guts behind an owned `unique_ptr<Impl>`. Use when the owner has a real public API surface many callers use.
- **B — de-aggregate + forward-declare the global**: for a class reached only through its `gp*` global at a handful of call sites, simply remove its header from `Engine.h`, forward-declare the class and its `inline <T>* gp<T>` in a lightweight fwd location, and `#include` the real header only in the few `.cpp`s that call through it (mirrors the sanctioned non-aggregated `NetworkCursor.h`). Lighter than A when it applies. `AgentCommandServer` is the prime B candidate.

### Candidate worklist (ranked by fan-in × header-weight benefit vs churn)

**1. `AgentCommandServer` — strongest.** `Engine/Source/Agent/AgentCommandServer.h` (aggregated at `Engine.h:23`, **both** builds → every TU via PCH). Heavy members, all **private**, are the heaviest to instantiate in the set: `std::mutex`/`std::condition_variable` (`:68-69`), `std::optional<PendingRequest>` whose `PendingRequest` holds a `nlohmann::json` (`:44`, `:70`), `std::optional<std::string>` (`:71`), `std::function<std::optional<nlohmann::json>()> mDeferredPoll` (`:75`), `nlohmann::json mDeferredId` (`:76`), `std::jthread mListenerThread` (`:81`), and two `SOCKET` (`:65-66`). Transport-only class; consumers only call `Drain()` / `DeferResponse()` through `gpAgentCommandServer` (`:84`), constructed only when `--agent-port` is set. Closest analog to `PackChunks`. Shape **B** is very clean here (drain is called from ~2-3 `.cpp`s: the client frame loop + `GameBase::ServerUpdate`); shape A also works. Grill: A vs B, and where the forward-declared `gpAgentCommandServer` pointer declaration lives.

**2. `StaticVoices` + `StreamingVoices` — clean fan-in reduction.** `Engine/Source/Audio/StaticVoices.h`, `Engine/Source/Audio/StreamingVoices.h` (client-only; aggregated at `Engine.h:71-72`). Already cohesive owned-**by-value** sub-objects of `AudioManager` — its *only* consumer (`AudioManager.h:59-60` hold `StaticVoices mStaticVoices;` / `StreamingVoices mStreamingVoices;`; `AudioManager.h:5-6` include them). All heavy members are **private**: `StreamingVoices` has `std::mutex`, two `std::vector<std::unique_ptr<StreamingVoice>>` (`StreamingVoices.h:41-44`), `std::function<common::crc_t()>` (`:45`), `common::PersistentWorker` (owns a thread, `:48`); `StaticVoices` has `std::mutex` (`StaticVoices.h:97`), `common::RandomEngine` (`:98`), `std::vector<StaticVoice>` / `std::vector<PooledVoice>` (`:101-102`), `X3DAUDIO_LISTENER` (`:114`), `XMVECTOR` (`:113`). Converting `AudioManager`'s two by-value members to `std::unique_ptr<StaticVoices>` / `std::unique_ptr<StreamingVoices>` with forward decls lets **four** headers drop out of the PCH — the per-voice `StaticVoice.h`/`StreamingVoice.h` (`Engine.h:69-70`) and the aggregate `StaticVoices.h`/`StreamingVoices.h` (`Engine.h:71-72`) — since only `AudioManager.cpp` + the voices' own `.cpp`s would include them. Caveats: the one inline forwarder `AudioManager::SkipNextStaticVoiceInvalidation()` (`AudioManager.h:33`, calls `mStaticVoices.SkipNextInvalidation()`) goes out-of-line; and the 3D one-shot path (`StaticVoices::PlayOneShot3d` → `ComputeAttenuatedVolume`/`Apply3dVolume`) is reached from `Dispatch()` worker threads (mildly hot) — the extra single pointer hop through `unique_ptr` is negligible but is called out so it is a conscious choice, not an oversight. Verify no other TU references `StaticVoice`/`StreamingVoice`/`StaticVoices`/`StreamingVoices` types before removing the aggregation lines.

**3. `TextureUploadManager` — viable, more churn, lower priority.** `Engine/Source/Graphics/Managers/TextureUploadManager.h` (client-only; aggregated at `Engine.h:53`). Heavy **private** members: `std::thread` (`:74`), two `std::mutex` (`:75`, `:80`), `std::condition_variable` (`:76`), `std::priority_queue<LoadRequest>` (`:81`), atomics (`:79`, `:82-83`), the `ChunkDimensions` struct (`:44-53`), the in-progress-upload state fields (`:68-72`), and the Vulkan/VMA handles (`:85-92`, cheap to declare — typedef'd pointers already in the PCH — so these are *not* part of the win). Firewall friction is higher than #1/#2: the public `std::binary_semaphore mFrameSignal` (`:36`) and the public **inline** adoption accessors `NotifyChunkAdoptable`/`NotifyChunkAdopted`/`HasPendingAdoptions` (`:32-34`, touch `miPendingAdoptions`) would need to move out-of-line or stay on the shell, and the split `InitTransferResources`/`DestroyTransferResources`/`StartThread` lifecycle (`:18-20`) plus the fatal-exception mailbox/rethrow contract must be preserved. Refresh this candidate against `Engine/Source/Graphics/Managers/TextureUploadManager.{h,cpp}` at execution.

### Anti-candidates (explicitly excluded — do not "firewall" these)

- **Base classes with virtuals / protected members** — `Profile/ProfileManagerBase.h` (`std::unordered_map<std::thread::id, std::vector<CpuTimerThreadState>>` `:387`, `std::mutex` `:353`, `VkQueryPool`, fixed timer/counter arrays) is the base of `game::ProfileManager` reached via virtual dispatch and protected members; it **cannot** hide members behind a `unique_ptr` without breaking inheritance. Same for `GameBase.h` (`std::map`/`std::unordered_map` core game state `:71`, `:206`, `:212`), base of `game::Game`.
- **Classes whose heavy members are the public API** — `Graphics/Managers/ParticleManager.h`: `std::mutex mSpawnMutex` and the two `shaders::ParticlesSpawnLayout` members are **public** (`:21`, `:25-26`) and written/read directly by `Spawn`/`RenderGlobal`; there is nothing to hide.
- **Core render managers with wide public APIs and heavy in-flight churn** — `BufferManager` / `PipelineManager` / `TextureManager` / `TextureDescriptors` / `DynamicPipelines` hold `unordered_map`s of `Buffer`/`Texture`/`Pipeline`/`Shader`, but they are `gp*` managers with large method surfaces, their maps are `Vulkan`-wrapper-typed (whose headers are in the PCH anyway), and they are under active pipeline-cluster / bindless-lifecycle refactors — not clean, and churn-hostile.
- **`Frame/IslandTerrain.h`** (`std::unordered_map<crc_t, IslandTemplate> mIslands` `:206`) — `gp*` global under active residency-lifecycle plans; skip.
- **Single-build network cores** — `Network/Server/Server.h` / `Network/Client/Client.h` deques/maps are the heavily-used network state; server/client-only fan-in, not clean sub-objects.
- **SOA / by-value-layout / hot-path types** — the `Collection<T>` base's `idToIndexMap` (`Collection.h:203`) and all frame-collection SOA storage: layout is load-bearing for CRC/determinism and access is hot; an extra indirection is forbidden.

## Critical files

- Reference precedent: `Engine/Source/File/FileManager.h` + `Engine/Source/File/PackChunks.{h,cpp}` (the pattern to copy).
- Owner headers to edit: `Engine/Source/Agent/AgentCommandServer.h`, `Engine/Source/Audio/AudioManager.h` (+ `Audio/StaticVoices.h` / `Audio/StreamingVoices.h`), `Engine/Source/Graphics/Managers/TextureUploadManager.h`.
- Owner TUs to edit (out-of-line ctor/dtor + method forwarding): the matching `.cpp`s for each.
- Aggregation header: `Engine/Source/Engine.h` (remove firewalled includes — lines `23`, and `69-72` for the audio set).
- New `.cpp`/`.h` TUs (if shape A creates an impl unit) require `/update-vcxproj` with correct affinity: `AgentCommandServer` **both** builds; audio + `TextureUploadManager` **client-only** (`#if defined(BT_CLIENT)` whole-file wrap).

## Out of scope

- Any behavior, logic, threading-contract, or lock-ordering change — extractions are byte-for-byte behavior-preserving.
- The anti-candidate classes listed above (base classes, public-member classes, core render/network managers, SOA/hot-path types).
- `FileManager`/`PackChunks` themselves — the split that produced them already landed (this plan's precedent, cited above); this plan does not re-touch `FileManager.{h,cpp}`/`PackChunks.{h,cpp}` beyond citing the pattern.
- Vulkan/VMA handle members as a target in their own right — typedef'd pointers already in the PCH, ~zero parse cost to declare; firewalling a class *only* to hide handles buys nothing.
- Reordering or trimming `Common/ExternalHeaders.h` / the PCH contents (a different concern; see `Engine/PchProvidedHeaderReincludeSweep.md`).
- Introducing a generic pimpl helper/macro — each extraction is hand-written to match `PackChunks`.

## Acceptance criteria

- Each accepted candidate's heavy internals no longer appear as member declarations in an `Engine.h`-aggregated header; the owner header shows only a forward decl + `unique_ptr` (or the class is no longer aggregated, for shape B).
- `Engine.h` no longer includes the firewalled headers; those headers are included only by their owner's 1-2 TUs.
- Client and server both compile (`/compile`); new/renamed TUs have correct vcxproj membership and client/server affinity.
- No behavior change — no determinism/CRC, replay, wire, `kiVersion`, save, or `.pack` surface is touched; runtime is unchanged. (No agent-harness run strictly required — pure compile-time refactor — but a smoke launch of client+server is a cheap confidence check that construction/teardown of the now-heap-owned sub-objects is intact.)

## Notes

- **Invariant exposure: none.** Compile-time-only, behavior-preserving. No determinism/CRC sim path, `kiVersion`/`.pack` layout, replay, client/server guard *semantics*, or allocation-tracked main-loop path is touched (the extracted sub-objects are constructed at boot, outside the allocation tracker). The only runtime trade-off is one extra pointer indirection to reach the sub-object and one heap allocation at owner construction, per firewalled class — deliberately excluded from hot-path (`Collection` SOA) and public-by-value-layout classes.
- **Grill (single open decision):** shape **A (pimpl shell)** vs **B (de-aggregate + forward-declare the `gp*` global)** per candidate — `AgentCommandServer` is the clean B case, the audio voices are inherently A (owned sub-objects of `AudioManager`), `TextureUploadManager` is A. Secondary: whether to include candidate #3 now or defer it to a follow-up given its inline-accessor churn and File-Group overlap.
- Mechanical and compile-checked; low risk, easily reverted per candidate. Best executed as one batch behind a single review/compile cycle, but each candidate is independent and can be dropped without affecting the others.
