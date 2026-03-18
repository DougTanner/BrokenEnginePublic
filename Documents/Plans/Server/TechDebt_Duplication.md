# Tech Debt: Duplication & Function Size

Source: /external-tech-debt on Engine/Source/Server

## Changes

### Engine/Source/Server/ServerDisplay.cpp
- Extract a helper lambda in `PaintServerDisplay()` for the repeated snprintf+TextOutA+iTextY pattern (14 instances in the stats panel, lines 110-170). The lambda should take the format string and value, and auto-advance iTextY. Three additional snprintf+TextOutA calls in the grid loop (lines 287-297) use different coordinates and are excluded [~10m]
- Extract grid rendering logic (lines 173-303) into a separate `PaintGridMap()` function to reduce `PaintServerDisplay()` from 252 to ~120 lines. The extracted function will need parameters: hdcBuffer, pcLine buffer, map bounds (iMapLeft, iMapTop, iMapWidth, iMapHeight), and rClients reference [~15m]

## Verification Notes
- snprintf count corrected from 17 to 14 stats-panel instances (3 grid-loop instances excluded)
- TotalEntityCount helper moved to Architecture_Decoupling.md as a FrameInterpolate method (more principled approach)
