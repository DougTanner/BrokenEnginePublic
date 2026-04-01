# `/Engine/Source/Server/` - Server Display

GDI-based monitoring window for the headless server build (`BT_SERVER` only). Renders simulation stats, memory usage, and a visual grid map directly onto the server's Win32 window using WM_PAINT handling, with no GPU or Vulkan dependency.

## Key Systems

- **ServerUpdateDisplayStats()** - Aggregates entity counts (Players, Spaceships, Blasters, Missiles, Targets, Explosions) and mimalloc memory statistics into profile counters each tick
- **PaintServerDisplay()** - Paints a fixed-width stats panel on the left (tick, time, client count, FPS, potential FPS, entity counts, memory) and a centered grid map on the right, highlighting cells with connected clients and cells where any client has an active coord subscription

## Architecture Notes

The display refreshes every tick via `InvalidateRect()` from the Main.cpp server loop. Uses GDI double buffering (`CreateCompatibleDC`/`BitBlt`) with `WM_ERASEBKGND` suppression to eliminate flicker. The grid map auto-scales cell size and centers within available space with 1-cell padding around active coordinates. GDI allocations are wrapped in `ScopedSuppressAllocationTracking`.
