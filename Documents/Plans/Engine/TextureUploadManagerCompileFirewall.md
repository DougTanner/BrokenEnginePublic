<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# TextureUploadManager Compile Firewall

> **Read first — execution precondition. The sibling pimpl attempt was implemented
> and rejected.** The `AgentCommandServer` half of `HeaderCompileFirewallSweep` was
> fully built, reviewed, and verified, then **rejected by the user on KISS
> grounds**: routing every member access through one `mpTransport->` indirection
> cost ~60 rewritten lines across seven functions and permanent call-site noise,
> to take a single header out of the PCH. Only the audio half landed, because it
> needed just 15 ordinary member-pointer edits in one file and removed four
> headers.
>
> This plan applies the same pimpl shape to a class with **higher** friction than
> the rejected one (constraints 1-4 below), so the same objection very likely
> applies with more force. **Do not implement without first re-establishing with
> the user that the churn is worth the win**, and treat "close as
> accept-and-document" as the leading outcome of that conversation. This
> precondition is unresolved by design; no implementer may resolve it.

## Context

`Projects/BrokenEngineSandbox/Source/Pch.h` ends with `#include "Engine.h"`, so every `Engine.h`-reachable header edit forces a whole-project recompile. `HeaderCompileFirewallSweep` (completed) moved the heavy private members of `AgentCommandServer` and `AudioManager`'s voice sub-objects behind `unique_ptr` + forward declaration. `TextureUploadManager` was the sweep's third candidate and was **deferred by explicit user decision**, not dropped — its lifecycle and cross-TU surface make it materially harder than the two that landed.

Heavy members still in the PCH via `Engine/Source/Graphics/Managers/TextureUploadManager.h`:

- `TextureUploadManager.h:75-85` — `std::thread mUploadThread`, `std::mutex mWorkMutex`/`mUploadMutex`, `std::condition_variable mIdleConditionVariable`, `bool mbDrainRequested`/`mbDrained`, `std::exception_ptr mException`, `std::atomic<bool> mbThreadExited`/`mbShutdown`, `std::atomic<int64_t> miPendingAdoptions`, `std::priority_queue<LoadRequest> mUploadQueue`.
- `TextureUploadManager.h:45-54` — the private `ChunkDimensions` struct.
- `TextureUploadManager.h:69-73` — in-progress upload state (`mCurrentCrc`, `muiCurrentLayer`, `muiCurrentMip`, `muiCurrentMipY`, `mCurrentDataOffset`).

**Explicitly not part of the win:** the Vulkan/VMA handles at `TextureUploadManager.h:87-94` are typedef'd pointers already resident in the PCH; moving them buys nothing.

Why the sweep deferred it (constraints the design must honor):

1. **Lifecycle is not ctor/dtor-shaped.** `InitTransferResources` / `DestroyTransferResources` / `StartThread` re-run across a device-loss recreate, outside construction and destruction: `Graphics.cpp:395` (`InitTransferResources` when `mpDeviceManager == nullptr`), `Graphics.cpp:768` (`DestroyTransferResources` at `meDestroyType >= DestroyType::kSurface`), `Graphics.cpp:681-683` (`WaitIdle` on recreate), `TextureManager.cpp:247` (`StartThread`). A pimpl whose state is created in the ctor and destroyed in the dtor does not match this shape; the shell must survive the recreate while its transfer resources cycle.
2. **The fatal-exception mailbox is read from two separate TUs.** `RethrowException()` is called from `TextureManager.cpp:660` (boot spin loop) and `Main.cpp:408` (per-frame, immediately before `pGame->Render()`).
3. **A third subsystem depends on it outliving the recreate.** `PackChunks.cpp:729-732` calls `NotifyChunkAdoptable()` during `ResetTextureChunkStates`, with the comment asserting `gpTextureUploadManager` is always valid there because it outlives the device-loss `Graphics` recreate.
4. **Higher firewall friction than the landed candidates.** The public `std::binary_semaphore mFrameSignal` (`TextureUploadManager.h:37`) is used externally at `TextureManager.cpp:664-665`, and the three public inline accessors `NotifyChunkAdoptable` / `NotifyChunkAdopted` / `HasPendingAdoptions` (`TextureUploadManager.h:33-35`) read and write `miPendingAdoptions` inline. Moving `miPendingAdoptions` behind an opaque pointer forces those out-of-line (losing the inline atomic) or forces the counter and semaphore to stay on the shell.

## Design

Apply the landed firewall precedents to `TextureUploadManager`: move the heavy private members listed in Context into an implementation-private state struct owned via `std::unique_ptr` + forward declaration, keeping the shell's lifecycle contract intact. This is storage relocation only — no behavioral change.

Landed precedents to follow:

- `Engine/Source/File/FileManager.h:103` (`class PackChunks;` forward declaration), `:109-110` (out-of-line ctor/dtor), `:172` (`std::unique_ptr<PackChunks> mpPackChunks`) with the implementation in `Engine/Source/File/PackChunks.{h,cpp}` — the closest precedent, because `PackChunks` also owns loading threads.
- `AgentCommandTransport` — the pure-data-move shape described in the banner above. **Not a landed precedent: it was implemented and rejected, and does not exist in the tree.** Recorded here only so its rejection is not rediscovered as a fresh idea.
- `Engine/Source/Audio/AudioManager.h:37-38`, `:86-87` — `unique_ptr` + forward declaration for sub-objects.

Fixed decision (already made; implement exactly this):

- **Where the state is created and destroyed.** The opaque state must exist across `DestroyTransferResources` → `InitTransferResources` cycles (constraint 1), so it is created in `TextureUploadManager`'s ctor and destroyed in its dtor while `InitTransferResources`/`DestroyTransferResources` mutate its contents — not a create/destroy pair mapped onto those calls.

Unresolved pre-staged decision (kept verbatim from the original plan; resolve with the user at the banner conversation, not by the implementer):

- **What stays on the shell.** `mFrameSignal` and `miPendingAdoptions` are the only members with external inline reach. Either (a) keep both on `TextureUploadManager` and move only the thread/mutex/CV/exception/queue/progress state into the opaque struct — smaller win, zero call-site churn; or (b) move everything and convert the four accessors to out-of-line forwarding — larger win, loses the inline atomic on a per-chunk-adoption path. `<semaphore>`, `<atomic>`, and `<queue>` PCH residency differ between the two; measure before choosing.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that satisfies the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, forward declarations) the named change requires.

In scope (exact regions):

- `Engine/Source/Graphics/Managers/TextureUploadManager.h` — the private member block at lines 68-85 (in-progress upload state and thread/synchronization/queue members), the private `ChunkDimensions` struct at lines 45-54, the private method declarations only as far as `ChunkDimensions` relocation forces signature-site moves (`DequeueNextUpload`, `HandleUploadEarlyOut`, `ValidateTextureDimensions`, `CreateTransferImage`, `RecordStagingCopies`, `SubmitChunkUpload`, `ResetUploadProgress`, `UploadThread`), a new forward declaration + `std::unique_ptr` member, and — only under decision (b) — `mFrameSignal` (line 37) and the three inline accessors `NotifyChunkAdoptable`/`NotifyChunkAdopted`/`HasPendingAdoptions` (lines 33-35) becoming out-of-line declarations. Public API names, `RequestUpload`/`WaitIdle`/`SignalFrame`/`RethrowException`/`InitTransferResources`/`DestroyTransferResources`/`StartThread` signatures, `kiByteBudgetPerFrame`, and the Vulkan/VMA handle block (lines 87-94) are untouched.
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` — rewrite member references to route through the opaque state in exactly these functions: ctor/dtor (state creation/destruction), `InitTransferResources`, `DestroyTransferResources`, `ResetUploadProgress`, `StartThread`, `RequestUpload`, `WaitIdle`, `SignalFrame`, `RethrowException`, `UploadThread`, `DequeueNextUpload`, `HandleUploadEarlyOut`, `ValidateTextureDimensions`, `CreateTransferImage`, `RecordStagingCopies`, `SubmitChunkUpload`. No logic, ordering, locking, or memory-order changes — reference rewrites only.
- New implementation-private header (e.g. `TextureUploadState.h`, adjacent in `Engine/Source/Graphics/Managers/`) — holds the moved state struct; included only by `TextureUploadManager.cpp`; must not enter `Engine.h`. Run `/update-vcxproj` for the added file (client-only affinity).
- `Engine/Source/Graphics/Managers/AGENTS.md` and `Engine/Source/Graphics/Managers/TextureUploadManager.AGENTS.md` — via `/update-claude-docs`, only if the storage relocation makes existing wording incorrect.

Read-only call sites the change must not break (do not edit): `Graphics.cpp:395`, `:681-683`, `:768` (recreate-cycle calls); `TextureManager.cpp:247` (`StartThread`), `:660` (`RethrowException`), `:664-665` (`mFrameSignal`); `Main.cpp:408` (`RethrowException`); `PackChunks.cpp:729-732` (`NotifyChunkAdoptable` across the recreate).

## Out of scope

- Any behavioral change to upload scheduling, priority ordering, staging budget (`kiByteBudgetPerFrame`), or adoption semantics. This is storage relocation only.
- The Vulkan/VMA handles at `TextureUploadManager.h:87-94` — already PCH-resident pointers, no win.
- Further firewall candidates beyond `TextureUploadManager`; the sweep is closed.
- The texture CPU-pool reclaim work — see `## Coordination`.
- Any edit to the read-only call sites listed in the scope contract.

## Risk tier and invariants

Tier 3 — threading and lifetime are the exposed invariants:

- The upload thread's storage must remain valid across a device-loss `Graphics` recreate.
- `gpTextureUploadManager` must stay valid for `PackChunks.cpp:729-732`; `TextureUploadManager` is owned by `Main` (`Main.cpp:815`), not `Graphics` — the documented exception in `Engine/Source/Graphics/Managers/AGENTS.md`.
- **No determinism/CRC, `.pack`/`kiVersion`, replay, or wire-protocol exposure.** Texture upload state never feeds the sim.

## Acceptance criteria

Compilation cannot verify this class: construction/destruction ordering of a thread-owning sub-object across a recreate is invisible to the compiler. That is precisely why the sweep deferred it, so a runtime check is required.

1. `Engine.h`-reachable headers no longer expose the moved members; a touch of the new implementation header rebuilds only `TextureUploadManager.cpp`, not the project.
2. Client and server both compile (`/compile`), and `/update-vcxproj` records the added file with client-only affinity (`TextureUploadManager.h` is whole-file `#if defined(BT_CLIENT)`).
3. **Runtime check via `/agent-harness`**, following the pattern the completed sweep used for the audio and agent-transport candidates: launch the client, exercise lazy texture load/adopt traffic (camera pan over islands), and clean-shutdown via `quit`. `~Graphics` sets `meDestroyType = DestroyType::kSurface` (`Graphics.cpp:144`) so shutdown does execute `DestroyTransferResources` at `Graphics.cpp:768`; no error logs, no hang at the upload-thread join, textures visibly resident before exit.
4. Both `RethrowException()` callers still observe a published upload-thread exception — verify the mailbox path from `TextureManager.cpp:660` and `Main.cpp:408` by inspection against the moved storage, since neither is reachable without an induced upload failure.

**Verification gap (unresolved pre-staged decision, kept verbatim from the original plan; resolve with the user before implementing).** There is no agent-harness command that forces a full `Graphics` recreate. `resize`, `fullscreen`, and `window_state` drive **swapchain-tier** recreates only (`Graphics.cpp:341`, tier `>= kSwapchain && < kSurface`), which never reach `InitTransferResources`/`DestroyTransferResources`. The only runtime `kSurface` escalation is a real Vulkan surface-lost/device-lost result inside `CHECK_VK` (`GraphicsUtils.cpp:31`, `:37` throwing `DeviceLostException`), caught at `Main.cpp:411`. So criterion 3 as written covers boot `Init` → `StartThread` → shutdown `Destroy` **once**, but not the second `Init` after a `Destroy`. Decide before implementing: (a) accept single-cycle runtime coverage plus inspection of the recreate path, or (b) add a temporary local-only device-loss trigger to exercise a full cycle and remove it before landing. Do not claim recreate-cycle coverage that criterion 3 does not actually produce.

## Coordination

`Documents/Plans/Graphics/TextureChunkCpuPoolReclaim.md` targets the same class family. **Warning-only overlap, not a never-interleave constraint** — its edit sites are `TextureManager.cpp` (the post-adoption null point) and `FileManager.{h,cpp}` (`DecommitChunkRange`/`RecommitAndReloadChunkRange`), none of which are `TextureUploadManager`'s private storage. Its only contact with this class is a reference to `TextureUploadManager::RecordStagingCopies` in prose. Whichever lands second should re-read the other's touched regions; no joint resolution or batching is required.

## Notes

- Client-only; `TextureUploadManager.h` is whole-file `#if defined(BT_CLIENT)`.
- One extra pointer hop per upload-thread member access; the thread is off the sim path and byte-budgeted per frame, so this is not a measured concern. Do not add caching for it.
- Run `/update-claude-docs` — the `Managers/AGENTS.md` ownership wording may need no edit, but the affected scope must be inspected.
