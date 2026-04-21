#pragma once

// Pre-pass invoked from Main.cpp between ExportScene and ExportIsland.
// Drives Gaea 2 (Gaea.Swarm.exe) to bake per-island heightmap / color / AO / normal intermediates
// from `island.json` + a resolved `.terrain` archetype, writing .r32 / .exr files that ExportIsland
// then consumes and block-compresses. Throws on any failure (caught by main()'s try/catch).
// Everything Gaea-2-specific is isolated in BakeIslandIntermediates.cpp for eventual Gaea 3 migration.
void BakeIslandIntermediates();
