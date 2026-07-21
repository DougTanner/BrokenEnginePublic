# TextureUploadManager Compile Firewall

> **Read first — the sibling pimpl attempt was implemented and rejected.** The
> `AgentCommandServer` half of `HeaderCompileFirewallSweep` was fully built,
> reviewed, and verified, then **rejected by the user on KISS grounds**: routing
> every member access through one `mpTransport->` indirection cost ~60 rewritten
> lines across seven functions and permanent call-site noise, to take a single
> header out of the PCH. Only the audio half landed, because it needed just 15
> ordinary member-pointer edits in one file and removed four headers.
>
> This plan proposes the same pimpl shape on a class with **higher** friction
> than the rejected one (see items 1-4 below), so the same objection very likely
> applies with more force. Do not execute it as written. Re-establish with the
> user that the churn is worth the win before starting, and treat
> "close as accept-and-document" as the leading outcome.

## Context

`Projects/BrokenEngineSandbox/Source/Pch.h` ends with `#include "Engine.h"`, so every `Engine.h`-reachable header edit forces a whole-project recompile. `HeaderCompileFirewallSweep` (completed) moved the heavy private members of `AgentCommandServer` and `AudioManager`'s voice sub-objects behind `unique_ptr` + forward declaration. `TextureUploadManager` was the sweep's third candidate and was **deferred by explicit user decision**, not dropped — its lifecycle and cross-TU surface make it materially harder than the two that landed.

Heavy members still in the PCH via `TextureUploadManager.h`:

- `TextureUploadManager.h:75-85` — `std::thread mUploadThread`, `std::mutex mWorkMutex`/`mUploadMutex`, `std::condition_variable mIdleConditionVariable`, `std::exception_ptr mException`, `std::atomic<bool>` ×2, `std::atomic<int64_t> miPendingAdoptions`, `std::priority_queue<LoadRequest> mUploadQueue`.
- `TextureUploadManager.h:45-54` — the private `ChunkDimensions` struct.
- `TextureUploadManager.h:69-73` — in-progress upload state (`mCurrentCrc`, `muiCurrentLayer`, `muiCurrentMip`, `muiCurrentMipY`, `mCurrentDataOffset`).

**Explicitly not part of the win:** the Vulkan/VMA handles at `TextureUploadManager.h:87-94` are typedef'd pointers already resident in the PCH; moving them buys nothing.

Why the sweep deferred it (this is the substance to carry forward):

1. **Lifecycle is not ctor/dtor-shaped.** `InitTransferResources` / `DestroyTransferResources` / `StartThread` re-run across a device-loss recreate, outside construction and destruction: `Graphics.cpp:389` (`InitTransferResources` when `mpDeviceManager == nullptr`), `Graphics.cpp:762` (`DestroyTransferResources` at `meDestroyType >= DestroyType::kSurface`), `Graphics.cpp:675-677` (`WaitIdle` on recreate), `TextureManager.cpp:256` (`StartThread`). A pimpl whose state is created in the ctor and destroyed in the dtor does not match this shape; the shell must survive the recreate while its transfer resources cycle.
2. **The fatal-exception mailbox is read from two separate TUs.** `RethrowException()` is called from `TextureManager.cpp:686` (boot spin loop) and `Main.cpp:405` (per-frame, immediately before `pGame->Render()`).
3. **A third subsystem depends on it outliving the recreate.** `PackChunks.cpp:729-732` calls `NotifyChunkAdoptable()` during `ResetTextureChunkStates`, with the comment asserting `gpTextureUploadManager` is always valid there because it outlives the device-loss `Graphics` recreate.
4. **Higher firewall friction than the landed candidates.** The public `std::binary_semaphore mFrameSignal` (`TextureUploadManager.h:37`) is used externally at `TextureManager.cpp:690-691`, and the three public inline accessors `NotifyChunkAdoptable` / `NotifyChunkAdopted` / `HasPendingAdoptions` (`TextureUploadManager.h:33-35`) read and write `miPendingAdoptions` inline. Moving `miPendingAdoptions` behind an opaque pointer forces those out-of-line (losing the inline atomic) or forces the counter and semaphore to stay on the shell.

## Design

Apply the landed firewall precedents to `TextureUploadManager`, keeping the shell's lifecycle contract intact.

Landed precedents to follow:

- `Engine/Source/File/FileManager.h:103` (`class PackChunks;` forward declaration), `:109-110` (out-of-line ctor/dtor), `:172` (`std::unique_ptr<PackChunks> mpPackChunks`) with the implementation in `Engine/Source/File/PackChunks.{h,cpp}` — the closest precedent, because `PackChunks` also owns loading threads.
- `AgentCommandTransport` — the shape-A pure data move described in the banner above. **Not a landed precedent: it was implemented and rejected, and does not exist in the tree.** Recorded here only so its rejection is not rediscovered as a fresh idea.
- `Engine/Source/Audio/AudioManager.h:40-41`, `:89-90` — `unique_ptr` + forward declaration for sub-objects.

Pre-staged decisions (resolve during implementation, do not assume):

- **What stays on the shell.** `mFrameSignal` and `miPendingAdoptions` are the only members with external inline reach. Either (a) keep both on `TextureUploadManager` and move only the thread/mutex/CV/exception/queue/progress state into the opaque struct — smaller win, zero call-site churn; or (b) move everything and convert the four accessors to out-of-line forwarding — larger win, loses the inline atomic on a per-chunk-adoption path. `<semaphore>`, `<atomic>`, and `<queue>` PCH residency differ between the two; measure before choosing.
- **Where the state is created and destroyed.** The opaque state must exist across `DestroyTransferResources` → `InitTransferResources` cycles (constraint 1), so it is created in the ctor and destroyed in the dtor while `InitTransferResources`/`DestroyTransferResources` mutate its contents — not a create/destroy pair mapped onto those calls.

## Critical files

- `Engine/Source/Graphics/Managers/TextureUploadManager.h` — the header to firewall; `mFrameSignal`, `NotifyChunkAdoptable`/`NotifyChunkAdopted`/`HasPendingAdoptions`, `ChunkDimensions`, the private state block.
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` — every internal reference to the moved members; `UploadThread`, `DequeueNextUpload`, `HandleUploadEarlyOut`, `CreateTransferImage`, `RecordStagingCopies`, `SubmitChunkUpload`, `ResetUploadProgress`, `WaitIdle`, `RethrowException`.
- New implementation header (e.g. `TextureUploadState.h`) — included only by `TextureUploadManager.cpp`; must not enter `Engine.h`. Run `/update-vcxproj` for the added file.
- `Engine/Source/Graphics/Graphics.cpp:389`, `:675-677`, `:762` — recreate-cycle call sites.
- `Engine/Source/Graphics/Managers/TextureManager.cpp:256`, `:686`, `:690-691` — `StartThread`, `RethrowException`, `mFrameSignal` external use.
- `Engine/Source/Main.cpp:405` — the second `RethrowException()` caller.
- `Engine/Source/File/PackChunks.cpp:729-732` — `NotifyChunkAdoptable()` across the recreate.
- `Engine/Source/Graphics/Managers/AGENTS.md` and `TextureUploadManager.AGENTS.md` — the documented `Main`-owned / spans-recreation ownership contract.

## Out of scope

- Any behavioral change to upload scheduling, priority ordering, staging budget (`kiByteBudgetPerFrame`), or adoption semantics. This is storage relocation only.
- The Vulkan/VMA handles at `TextureUploadManager.h:87-94` — already PCH-resident pointers, no win.
- Further firewall candidates beyond `TextureUploadManager`; the sweep is closed.
- The texture CPU-pool reclaim work — see `## Coordination`.

## Acceptance criteria

Compilation cannot verify this class: construction/destruction ordering of a thread-owning sub-object across a recreate is invisible to the compiler. That is precisely why the sweep deferred it, so a runtime check is required.

1. `Engine.h`-reachable headers no longer expose the moved members; a touch of the new implementation header rebuilds only `TextureUploadManager.cpp`, not the project.
2. Client and server both compile (`/compile`), and `/update-vcxproj` records the added file with client-only affinity (`TextureUploadManager.h` is whole-file `#if defined(BT_CLIENT)`).
3. **Runtime check via `/agent-harness`**, following the pattern the completed sweep used for the audio and agent-transport candidates: launch the client, exercise lazy texture load/adopt traffic (camera pan over islands), and clean-shutdown via `quit`. `~Graphics` sets `meDestroyType = DestroyType::kSurface` (`Graphics.cpp:144`) so shutdown does execute `DestroyTransferResources` at `Graphics.cpp:762`; no error logs, no hang at the upload-thread join, textures visibly resident before exit.
4. Both `RethrowException()` callers still observe a published upload-thread exception — verify the mailbox path from `TextureManager.cpp:686` and `Main.cpp:405` by inspection against the moved storage, since neither is reachable without an induced upload failure.

**Verification gap to resolve first (pre-staged decision).** There is no agent-harness command that forces a full `Graphics` recreate. `resize`, `fullscreen`, and `window_state` drive **swapchain-tier** recreates only (`Graphics.cpp:355`, tier `>= kSwapchain && < kSurface`), which never reach `InitTransferResources`/`DestroyTransferResources`. The only runtime `kSurface` escalation is a real Vulkan surface-lost/device-lost result inside `CHECK_VK` (`GraphicsUtils.cpp:31`, `:37` throwing `DeviceLostException`), caught at `Main.cpp:408-412`. So criterion 3 as written covers boot `Init` → `StartThread` → shutdown `Destroy` **once**, but not the second `Init` after a `Destroy`. Decide before implementing: (a) accept single-cycle runtime coverage plus inspection of the recreate path, or (b) add a temporary local-only device-loss trigger to exercise a full cycle and remove it before landing. Do not claim recreate-cycle coverage that criterion 3 does not actually produce.

## Coordination

`Documents/Plans/Graphics/TextureChunkCpuPoolReclaim.md` targets the same class family. **Warning-only overlap, not a never-interleave constraint** — its edit sites are `TextureManager.cpp` (the post-adoption null point) and `FileManager.{h,cpp}` (`DecommitChunkRange`/`RecommitAndReloadChunkRange`), none of which are `TextureUploadManager`'s private storage. Its only contact with this class is a reference to `TextureUploadManager::RecordStagingCopies` in prose. Whichever lands second should re-read the other's touched regions; no joint resolution or batching is required.

## Notes

- Client-only; `TextureUploadManager.h` is whole-file `#if defined(BT_CLIENT)`.
- **No determinism/CRC, `.pack`/`kiVersion`, replay, or wire-protocol exposure.** Texture upload state never feeds the sim.
- **Threading and lifetime are the exposed invariants**: the upload thread's storage must remain valid across a device-loss `Graphics` recreate, and `gpTextureUploadManager` must stay valid for `PackChunks.cpp:729-732`. `TextureUploadManager` is owned by `Main`, not `Graphics` — the documented exception in `Engine/Source/Graphics/Managers/AGENTS.md`.
- One extra pointer hop per upload-thread member access; the thread is off the sim path and byte-budgeted per frame, so this is not a measured concern. Do not add caching for it.
- Run `/update-claude-docs` — the `Managers/AGENTS.md` ownership wording may need no edit, but the affected scope must be inspected.
