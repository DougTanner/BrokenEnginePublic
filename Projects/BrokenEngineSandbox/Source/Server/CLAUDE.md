# `/Projects/BrokenEngineSandbox/Source/Server/` - Server Display

GDI-based monitoring window for the headless server build (`BT_SERVER` only). Renders simulation stats, memory usage, and a visual grid map via `WM_PAINT` — no GPU or Vulkan dependency. Game-layer code (`game::` namespace) because it reads concrete game collections (`pPlayers`/`pSpaceships`/`pBlasters`/`pMissiles`/`pTargets`) and game profile counters (`game::kCpuCounter*`).

## Architecture Notes

The window itself (HWND lifecycle, `WndProc`, per-tick `InvalidateRect`) lives in engine `Main.cpp`, which calls the three `game::` entry points — `ServerUpdateDisplayStats()`, `PaintServerDisplay(HWND)`, `HandleServerClick(HWND, x, y)` — directly (the composition-root engine→game call pattern). GDI double-buffering with `WM_ERASEBKGND` suppressed eliminates flicker. Layout: fixed stats panel plus tabbed right panel (Map, Profile). Map cells are colored by client ownership / activity drawn from `engine::gpServer->GetClients()`; the grid auto-scales to active coords. Tab state and hit-rects are file-static — one window per process.

Profile-tab text is formatted through `gpThreadLocal->mWorkbuffer` (matched `View()`/`Pop()`) and also appended to a file-static `std::string` cache so the Copy button can push to clipboard on the next click. That `std::string` mutation plus GDI object allocations are the reason paint paths are wrapped in `ScopedSuppressAllocationTracking`.
