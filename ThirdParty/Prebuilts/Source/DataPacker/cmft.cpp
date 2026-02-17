#include <codeanalysis/warnings.h>
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

// os.h and timer.h share the same include guard (CMFT_OS_H_HEADER_GUARD), so
// pre-include both with an undef between them to ensure all functions are available
#include "cmft/src/cmft/common/os.h"
#undef CMFT_OS_H_HEADER_GUARD
#include "cmft/src/cmft/common/timer.h"

#include "cmft/src/cmft/allocator.cpp"
#include "cmft/src/cmft/clcontext.cpp"
#include "cmft/src/cmft/common/print.cpp"
#include "cmft/src/cmft/cubemapfilter.cpp"
#include "cmft/src/cmft/image.cpp"

#pragma warning(pop)
