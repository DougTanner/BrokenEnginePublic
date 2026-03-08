# TextManager

**Global**: `gpTextManager`

Text rendering system batching character quads into storage buffers. Manages EFIGS font character map from BMFont binary format. Named text areas for debug output and stats. Drop shadow effect rendered by computing quads once and duplicating them via memmove/memcpy, applying shadow color and offset to the copies.
