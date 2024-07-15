#pragma warning(push, 0)
#pragma warning(disable : 4146 4702 4706 6001 6011 6262 6308 6330 6387 26051 26408 26409 26429 26432 26433 26434 26435 26438 26440 26443 26444 26447 26448 26451 26455 26456 26459 26460 26461 26466 26472 26475 26477 26481 26482 26485 26488 26498 26490 26493 26494 26495 26496 26497 26812 26814 26818 26819 28182)

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
