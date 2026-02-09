#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

// Undo CRT debug macros that redirect malloc/free to _malloc_dbg/_free_dbg
// (injected via force-included Pch.h -> ExternalHeaders.h -> crtdbg.h)
#ifdef _CRTDBG_MAP_ALLOC
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef _msize
#endif

#include "mimalloc/src/static.c"

#pragma warning(pop)
