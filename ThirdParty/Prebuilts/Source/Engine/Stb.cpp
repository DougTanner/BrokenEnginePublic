#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

// Filenames passed to stbi_write_* are interpreted as UTF-8 and opened via _wfopen
#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../ThirdParty/stb/stb_image_write.h"

#pragma warning(pop)
