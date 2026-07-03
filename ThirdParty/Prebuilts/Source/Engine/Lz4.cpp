#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

#include "lz4/lib/lz4.c"

// lz4hc.c provides LZ4_compress_HC (LZ4HC), used offline by DataPacker for texture .pack chunks. It
// self-includes lz4.c guarded by LZ4_SRC_INCLUDED (which lz4.c defines above), so this include reuses
// the already-compiled lz4.c in this unity unit rather than pulling a second copy.
#include "lz4/lib/lz4hc.c"

#pragma warning(pop)
