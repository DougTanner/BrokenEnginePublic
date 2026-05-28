# `/Projects/BrokenEngineSandbox/Source/Server/` - Server Display

GDI-based monitoring window for the headless server build (`BT_SERVER` only). Renders simulation stats, memory usage, and a visual grid map via `WM_PAINT` — no GPU or Vulkan dependency. Game-layer code (`game::` namespace) because it reads concrete game collections (`pPlayers`/`pSpaceships`/`pBlasters`/`pMissiles`/`pTargets`) and game profile counters (`game::kCpuCounter*`).

Per-tick stat aggregation (entity counts across `mActiveCoords`, mimalloc memory stats, smoothed CPU timers) runs in `ServerUpdateDisplayStats()` and is gated on `kbProfiling` — paint reads the resulting profile counters/timers, not the live frames, for those numbers. Memory lines come from `mi_stats_*` and compile out under `ENABLE_CRT_DEBUG_HEAP`.

## Architecture Notes

The window itself (HWND lifecycle, `WndProc`, per-tick `InvalidateRect`) lives in engine `Main.cpp`, which calls the three `game::` entry points — `ServerUpdateDisplayStats()`, `PaintServerDisplay(HWND)`, `HandleServerClick(HWND, x, y)` — directly (the composition-root engine→game call pattern). GDI double-buffering with `WM_ERASEBKGND` suppressed eliminates flicker. Layout: fixed left stats panel (tick/time read from the `kOriginCoord` frame, counts from profile counters, client count from `engine::gpServer->GetClients()`) plus a tabbed right panel (Map, Profile). Map cell fill encodes per-cell state from `mActiveCoords` and the client list: client-authorized (blue), otherwise active (green), otherwise idle (gray); cells any client has subscribed get a red interior border. The grid auto-scales to active coords with 1-cell padding and labels active cells with entity/client counts. Tab state and hit-rects are file-static — one window per process.

Profile-tab text is formatted through `gpThreadLocal->mWorkbuffer` (matched `View()`/`Pop()`) and also appended to a file-static `std::string` cache so the Copy button can push to clipboard on the next click. That `std::string` mutation plus GDI object allocations are the reason paint paths are wrapped in `ScopedSuppressAllocationTracking`.
