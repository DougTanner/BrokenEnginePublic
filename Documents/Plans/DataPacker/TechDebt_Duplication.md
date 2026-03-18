# Tech Debt: Duplication

Source: /external-tech-debt on DataPacker/Source

## Changes

### DataPacker/Source/Main.cpp
- Extract a "write-if-changed" helper for generated header content. The pattern of building a string, comparing with `ContentsEqual`, and writing if different is used for both DataTypes.h (lines 242-253) and Data.h (lines 272-283) [~15m]

## Verification Notes
- Path-building helper was removed during verification — the 3-line pattern (`path = dir / name + ext`) is trivially simple and only appears in one function, making extraction contrary to KISS/YAGNI.
- Write-if-changed helper is justified: 11 lines duplicated nearly verbatim across 2 occurrences. Helper would take content string, output path, and log name.
