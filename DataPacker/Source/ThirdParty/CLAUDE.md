# `DataPacker/Source/ThirdParty`

Third-party library integration files for DataPacker. Each file wraps external library code with Visual Studio warning suppressions to maintain clean builds while using external code.

## Library Integrations

**DirectXTK.cpp** - DirectX Tool Kit WAV file reader for audio loading and processing

**SPIRV-Cross.cpp** - SPIR-V shader reflection and cross-compilation library. Used to analyze compiled SPIR-V shaders and extract metadata

**bc7enc_rdo.cpp** - BC7 texture compression encoder with rate-distortion optimization for high-quality GPU texture compression

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
