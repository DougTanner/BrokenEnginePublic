# `DataPacker/Source/ThirdParty`

Third-party library integration files for DataPacker. Each file wraps external library code with Visual Studio warning suppressions to maintain clean builds while using external code.

## Library Integrations

**DirectXTK.cpp** - DirectX Tool Kit WAV file reader for audio loading and processing

**Mimalloc.cpp** - mimalloc general-purpose allocator with per-thread heaps, replacing CRT malloc/new. Undefs CRT debug allocation macros before including mimalloc source

**StackWalker.cpp** - Stack trace generation for crash reports

**SPIRV-Cross.cpp** - SPIR-V shader reflection and cross-compilation library. Used to analyze compiled SPIR-V shaders and extract descriptor bindings and vertex input layouts. Includes special handling to temporarily undefine `free` macro for debug memory tracking compatibility

**bc7enc_rdo.cpp** - BC7 and BC4 texture compression encoder with rate-distortion optimization for high-quality GPU texture compression (bc7enc.cpp, rgbcx.cpp)

**stb.cpp** - STB image loading library (stb_image.h) for reading common image formats (PNG, JPG, TGA, BMP, etc.)

**tinygltf.cpp** - TinyGLTF loader for parsing glTF 2.0 model files including embedded STB image writer

**tinyobjloader.cpp** - TinyObjLoader for parsing Wavefront OBJ model files

**openexr/** - OpenEXR Core library for loading high dynamic range (HDR) image files. Includes configuration headers and the main implementation that compiles all OpenEXRCore source files

## Pattern

All integration files use the same pattern:
1. `#pragma warning(push, 0)` - disable all warnings
2. Extensive warning suppression list for Visual Studio code analysis
3. Library-specific defines (e.g., `IMPLEMENTATION` macros)
4. Include external library source files
5. `#pragma warning(pop)` - restore warnings

This allows building third-party code "as-is" without modifying source files or cluttering build output with warnings from external code.
