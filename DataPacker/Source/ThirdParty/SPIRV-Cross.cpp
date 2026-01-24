#include <codeanalysis/warnings.h>
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

// SPIRV-Cross
#ifdef _CRTDBG_MAP_ALLOC
	#undef free(p)
#endif

#include "SPIRV-Cross/spirv_cross.hpp"

#include "SPIRV-Cross/spirv_cfg.cpp"
#include "SPIRV-Cross/spirv_cpp.cpp"
#include "SPIRV-Cross/spirv_cross.cpp"
#include "SPIRV-Cross/spirv_cross_parsed_ir.cpp"
#include "SPIRV-Cross/spirv_cross_util.cpp"
#include "SPIRV-Cross/spirv_glsl.cpp"
#include "SPIRV-Cross/spirv_hlsl.cpp"
#include "SPIRV-Cross/spirv_msl.cpp"
#include "SPIRV-Cross/spirv_parser.cpp"
#include "SPIRV-Cross/spirv_reflect.cpp"

#ifdef _CRTDBG_MAP_ALLOC
	#define free(p) _free_dbg(p, _NORMAL_BLOCK)
#endif

#pragma warning(pop)
