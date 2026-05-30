# Singleton Publish-Global Guard Sweep

## Context

A large family of owner/singleton classes publishes its global access pointer in its constructor (`gpFoo = this;`) and nulls it in the destructor (`gpFoo = nullptr;`) **without asserting the slot was empty on entry**. The publish-in-ctor / null-in-dtor idiom is fine for the one-instance-per-process lifetime these singletons actually have, but the missing entry guard hides two failure modes:

1. **Double-construct clobber.** A second instance constructed on the same lifetime silently overwrites the global. Both objects believe they own `gpFoo`; the first owner is now unreachable through the global.
2. **Teardown order-dependence.** When the inner (second) instance is destroyed, its unconditional `gpFoo = nullptr;` nulls the global even though the *outer* instance is still alive. Every later dereference of `gpFoo` is then a null deref far from the actual mistake.

The just-landed `ThreadLocal` hardening (`Common/Threading/ThreadLocal.cpp:13`) added `ASSERT(gpThreadLocal == nullptr);` at the top of its ctor — fail loud on double-construct, before the clobber. This plan extends that same one-instance-ownership guard to the rest of the codebase's publish-global singletons, matching the existing in-tree precedents:

- **Ctor entry assert** — `ThreadLocal::ThreadLocal` (`ThreadLocal.cpp:13`, just landed).
- **Dtor identity guard** — `Multithreading::~Multithreading` (`Common/Threading/Multithreading.cpp:29`): `if (gpMultithreading == this) { gpMultithreading = nullptr; }`.

The sweep spans **Common** (`DiagnosticLog`), the bulk in **Engine** (network, input, audio, graphics managers, frame), the **game layer** (`Camera`, `Game`, `ProfileManager`, `ClientSession`, `ServerSession`), and **DataPacker** (`FileManager`). It is filed under `Engine/` because most sites are Engine managers; the Common/game/DataPacker sites ride along since the fix is identical at every site.

The change is debug-`ASSERT`-only — no runtime validation, no behavior change in release, no serialized-layout / CRC / network-protocol exposure. It is verified by compile plus the asserts staying silent on a normal boot/shutdown (each singleton genuinely constructs once and tears down once today).

## Design

**Decision (this is the plan's primary design choice): apply BOTH guards at every scalar site — the ctor entry `ASSERT` *and* the dtor identity guard.**

The three idiom options considered:

- **(a) Ctor entry assert only** — `ASSERT(gpFoo == nullptr);` as the first statement of the ctor, before `gpFoo = this;`. Mirrors the landed `ThreadLocal` fix. Catches double-construct at the moment of the clobber (the most actionable stack), but leaves the dtor unconditionally nulling, so the teardown-order-dependence failure (mode 2) is unaddressed.
- **(b) Dtor identity guard only** — `if (gpFoo == this) { gpFoo = nullptr; }`. Mirrors `Multithreading.cpp:29`. Makes teardown order-independent (an inner instance's dtor no longer nulls the outer's slot), but is *silent* — it does not fail loud on the double-construct that created the situation.
- **(c) Both.** Ctor `ASSERT` for fail-loud-on-double-construct (option a's strength) plus the dtor identity guard for order-independent teardown (option b's strength).

**Recommendation: (c) both.** The two guards address the two distinct failure modes and the codebase already demonstrates both halves in-tree, so neither is novel. The ctor `ASSERT` is the load-bearing half — it is the "fail loud in debug" signal that matches the just-landed `ThreadLocal` intent and surfaces the bug at its origin. The dtor identity guard is cheap (one comparison) and makes the null-the-global step robust to teardown order, exactly as `Multithreading` already does. Per KISS this stays a fixed two-line shape stamped at every site — no helper, no template, no save/restore machinery. It remains `ASSERT`-only: no runtime branch in release, no error handling, no defensive validation (project policy forbids the latter).

**Per-site shape (scalar variant):**

```cpp
Foo::Foo()
{
    ASSERT(gpFoo == nullptr);
    gpFoo = this;
    // ...existing ctor body unchanged...
}

Foo::~Foo()
{
    // ...existing dtor body unchanged...
    if (gpFoo == this)
    {
        gpFoo = nullptr;
    }
}
```

**Array-slot variant** (`DiagnosticLog` only): the global is an array `gpDiagnosticLogs[]` indexed by `miIndex`. Apply the same guard against the slot:

```cpp
DiagnosticLog::DiagnosticLog(int iIndex, const char* pcFilename)
: /* ...init list (miIndex set here)... */
{
    ASSERT(gpDiagnosticLogs[miIndex] == nullptr);
    gpDiagnosticLogs[miIndex] = this;
}

DiagnosticLog::~DiagnosticLog()
{
    if (gpDiagnosticLogs[miIndex] == this)
    {
        gpDiagnosticLogs[miIndex] = nullptr;
    }
}
```

Note: `miIndex` is initialized in the ctor member-init list, so the `ASSERT` referencing it must come *after* the list completes (it does — it is the first statement of the ctor body). For sites whose dtor currently nulls the global at a non-final position in the body (e.g. `Server::~Server` nulls *after* its `enet_host_destroy`, `NetworkManager::~NetworkManager` after `enet_deinitialize`), keep the existing position — only wrap the existing `gpFoo = nullptr;` statement in the `if (gpFoo == this)` guard; do not relocate it.

## Critical files

Each site below is confirmed publish-in-ctor / null-in-dtor with **no existing guard**. Add the ctor `ASSERT` immediately before the `gpFoo = this;` line and wrap the dtor's existing `gpFoo = nullptr;` in `if (gpFoo == this)`. Paths are repo-root-relative; line numbers are the current ctor-publish line / dtor-null line.

**Common**

- `DiagnosticLog::DiagnosticLog` / `~DiagnosticLog` — `Common/Log/DiagnosticLog.cpp:10` / `:15` (array-slot variant; `gpDiagnosticLogs[miIndex]`).

**Engine — Network**

- `Server::Server` / `~Server` — `Engine/Source/Network/Server/Server.cpp:13` / `:43` (dtor nulls after `enet_host_destroy`; guard in place).
- `NetworkManager::NetworkManager` / `~NetworkManager` — `Engine/Source/Network/NetworkManager.cpp:10` / `:19` (dtor nulls after `enet_deinitialize`).
- `Client::Client` / `~Client` — `Engine/Source/Network/Client/Client.cpp:15` / `:70`.

**Engine — Input / Audio**

- `RawInputManager::RawInputManager` / `~RawInputManager` — `Engine/Source/Input/RawInputManager.cpp:12` / `:30`.
- `AudioManager::AudioManager` / `~AudioManager` — `Engine/Source/Audio/AudioManager.cpp:17` / `:165`.

**Engine — Frame**

- `IslandTerrain::IslandTerrain` / `~IslandTerrain` — `Engine/Source/Frame/IslandTerrain.cpp:28` / `:154`.

**Engine — File**

- `FileManager::FileManager` / `~FileManager` — `Engine/Source/File/FileManager.cpp:16` / `:71` (engine `gpFileManager`; distinct class from the DataPacker one below, same global name).

**Engine — Graphics**

- `Graphics::Graphics` / `~Graphics` — `Engine/Source/Graphics/Graphics.cpp:117` / `:146`.
- `Islands::Islands` / `~Islands` — `Engine/Source/Graphics/Islands.cpp:13` / `:85`.
- `DeviceManager::DeviceManager` / `~DeviceManager` — `Engine/Source/Graphics/Managers/DeviceManager.cpp:10` / `:413`.
- `BufferManager::BufferManager` / `~BufferManager` — `Engine/Source/Graphics/Managers/BufferManager.cpp:14` / `:224`.
- `CommandBufferManager::CommandBufferManager` / `~CommandBufferManager` — `Engine/Source/Graphics/Managers/CommandBufferManager.cpp:14` / `:38`.
- `SwapchainManager::SwapchainManager` / `~SwapchainManager` — `Engine/Source/Graphics/Managers/SwapchainManager.cpp:12` / `:418`.
- `PipelineManager::PipelineManager` / `~PipelineManager` — `Engine/Source/Graphics/Managers/PipelineManager.cpp:20` / `:146`.
- `InstanceManager::InstanceManager` / `~InstanceManager` — `Engine/Source/Graphics/Managers/InstanceManager.cpp:115` / `:694`.
- `TextureManager::TextureManager` / `~TextureManager` — `Engine/Source/Graphics/Managers/TextureManager.cpp:74` / `:361`.
- `TextureUploadManager::TextureUploadManager` / `~TextureUploadManager` — `Engine/Source/Graphics/Managers/TextureUploadManager.cpp:10` / `:15`.
- `TextManager::TextManager` / `~TextManager` — `Engine/Source/Graphics/Managers/TextManager.cpp:22` / `:50`.
- `ParticleManager::ParticleManager` / `~ParticleManager` — `Engine/Source/Graphics/Managers/ParticleManager.cpp:12` / `:19`.
- `ImGuiManager::ImGuiManager` / `~ImGuiManager` — `Engine/Source/Graphics/Managers/ImGuiManager.cpp:24` / `:155`.

**Game layer (`game::`)**

- `Camera::Camera` / `~Camera` — `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:38` / `:45`.
- `Game::Game` / `~Game` — `Projects/BrokenEngineSandbox/Source/Game.cpp:38` / `:419`.
- `ProfileManager::ProfileManager` / `~ProfileManager` — `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp:37` / `:47`.
- `ServerSession::ServerSession` / `~ServerSession` — `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:23` / `:36` (dtor nulls after the four `mp*.reset()` calls).
- `ClientSession::ClientSession` / `~ClientSession` — `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp:34` / `:43`.

**DataPacker**

- `FileManager::FileManager` / `~FileManager` — `DataPacker/Source/FileManager.cpp:5` / `:53` (DataPacker `gpFileManager`; distinct class from the engine one).

**Total: 28 sites** (27 scalar + 1 array-slot).

## Out of scope

- **`Common/Threading/ThreadLocal.cpp`** — the ctor `ASSERT(gpThreadLocal == nullptr)` already landed; this is the precedent, not a target.
- **`Common/Threading/Multithreading.cpp`** — its dtor identity guard (`:29`) already exists. (Its ctor at `:9` does *not* have the entry `ASSERT`; whether to add the ctor half there is intentionally left out — its two-overload ctor shape, where only `Multithreading(int64_t)` publishes the global while `Multithreading(Threads, ...)` does not, makes a blanket `ASSERT` non-trivial. If the user wants it, fold it in, but it is not part of this mechanical sweep.)
- **`game::gpInput`** — published externally in `Engine/Source/Main.cpp:197` (`game::gpInput = pInput.get();`) by the owning `unique_ptr`, not inside an `Input` ctor, and never nulled in a dtor. Different ownership shape; does not fit Pattern A.
- **Save/restore nesting machinery.** No mechanism to stash the previous `gpFoo` on construct and restore it on destruct (the "scoped-override" pattern). YAGNI — these are one-per-process singletons; fail-loud on double-construct is the intended behavior, not transparent nesting.
- **Runtime validation / error handling.** No `if`-then-`return`, no logging, no release-path branch on the ctor guard. `ASSERT`-only (project policy forbids defensive validation).
- **Lifetime / construction-order refactors.** This plan does not change *when* or *in what order* any singleton is constructed or destroyed, nor convert any raw `gp*` publish to a different ownership model. Pointer-publish shape is preserved; only the two guards are added.

## Acceptance criteria

- Every scalar site has `ASSERT(gpFoo == nullptr);` as the statement immediately preceding its `gpFoo = this;`, and its dtor's `gpFoo = nullptr;` is wrapped in `if (gpFoo == this) { ... }`.
- `DiagnosticLog` has the array-slot form (`ASSERT(gpDiagnosticLogs[miIndex] == nullptr);` / `if (gpDiagnosticLogs[miIndex] == this)`).
- No ctor body or dtor body is reordered beyond inserting the `ASSERT` and wrapping the existing null assignment.
- Client, server, and DataPacker all compile.
- A normal client boot + shutdown and a normal server boot + shutdown produce no new assert failures (each singleton constructs once, destructs once today).

## Notes

- Two distinct classes share the global name `gpFileManager` (`engine::FileManager` and DataPacker's `FileManager`); they live in separate builds, so the guard is independent at each. Both are in scope.
- The dtor identity guard intentionally keeps the *position* of the existing `gpFoo = nullptr;` statement — several dtors null the global after subsystem teardown (`Server` after `enet_host_destroy`, `NetworkManager` after `enet_deinitialize`, `ServerSession` after `mp*.reset()`). Wrap in place; do not hoist to the top of the dtor.
- This is a stamp-the-same-two-lines sweep; the per-site diff is the criterion. No interface, header, or `.vcxproj` change — no new files are created.
- `BT_CLIENT`-only sites (`AudioManager`, `RawInputManager`, all Graphics managers, `Camera`, `ProfileManager` client members, `ClientSession`) and `BT_SERVER`-only sites (`Server`, `ServerSession`) are already inside their respective `#if defined(BT_*)` spans; the guard inherits that gating with no extra `#ifdef`.
