# `/Engine/Source/Server/` - Server Display

## Overview

GDI-based monitoring window for the headless server build (`BT_SERVER` only). Renders simulation stats, memory usage, and a visual grid map directly onto the server's Win32 window using WM_PAINT handling, with no GPU or Vulkan dependency.

## Key Systems

- **UpdateServerDisplayStats()** - Aggregates entity counts across all active frames (players, spaceships, blasters, missiles, targets, explosions) into profile counters, and collects mimalloc memory statistics (guarded by `!ENABLE_CRT_DEBUG_HEAP`). Called every tick from the Main.cpp server loop before repainting.
- **PaintServerDisplay()** - Paints the server window with a fixed-width stats panel on the left and a centered grid map on the right. The stats panel shows tick counter, simulation time, active cell count, connected client count, per-collection entity totals (via profile counters), and mimalloc memory statistics (committed, peak committed, heap used, peak heap). The grid map renders active cells with entity density labels; cells containing connected clients are highlighted with a distinct color and border, with a client count label. Queries `NetworkServer::GetClients()` to determine client positions by matching `humanGridCoord` and valid `humanPlayerId` per cell.

## Architecture Notes

The display refreshes every tick, triggered by `InvalidateRect()` in the Main.cpp server loop. Uses GDI double buffering (off-screen bitmap via `CreateCompatibleDC`/`BitBlt`) paired with a `WM_ERASEBKGND` handler that suppresses default background erasing to eliminate flicker. All rendering uses Win32 GDI calls with no external dependencies. GDI allocations are wrapped in `ScopedSuppressAllocationTracking` at both the paint and invalidation sites. The layout uses a fixed-width stats panel with the grid map occupying the remaining space, auto-scaling cell size and centering the grid within its area. A 1-cell padding is added around the bounding box of active coordinates.
