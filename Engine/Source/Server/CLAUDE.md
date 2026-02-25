# `/Engine/Source/Server/` - Server Display

## Overview

GDI-based monitoring window for the headless server build (`BT_SERVER` only). Renders simulation stats and a visual grid map directly onto the server's Win32 window using WM_PAINT handling, with no GPU or Vulkan dependency.

## Key Systems

- **PaintServerDisplay()** - Paints the server window in two halves: left side shows text stats (frame counter, simulation time, active cell count, connected client count, per-collection entity totals across all active frames), right side renders a grid map of active cells with entity density labels. Cells containing connected clients are highlighted with a distinct color and border, with a client count label. Queries `NetworkServer::GetClients()` to determine client positions by matching `humanGridCoord` and valid `humanPlayerId` per cell.

## Architecture Notes

The display refreshes every tick, triggered by `InvalidateRect()` in the Main.cpp server loop. Uses GDI double buffering (off-screen bitmap via `CreateCompatibleDC`/`BitBlt`) paired with a `WM_ERASEBKGND` handler that suppresses default background erasing to eliminate flicker. All rendering uses Win32 GDI calls (CreateFont, TextOutA, FillRect, CreatePen) with no external dependencies. GDI allocations are wrapped in `ScopedSuppressAllocationTracking` at both the paint and invalidation sites. The grid map auto-scales cell size to fit the window and adds a 1-cell padding around the bounding box of active coordinates.
