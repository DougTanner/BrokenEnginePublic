#include "ExportShader.h"

#if defined(_CRTDBG_MAP_ALLOC)
	#undef free
#endif
#include "SPIRV-Cross/spirv_cross.hpp"
#if defined(_CRTDBG_MAP_ALLOC)
	#define free(p) _free_dbg(p, _NORMAL_BLOCK)
#endif

#include "FileManager.h"

#define OPTIMIZE_SHADERS

using enum common::ChunkFlags;

static bool ShaderHeadersChanged()
{
	static bool sbComputed = false;
	static bool sbChanged = false;
	if (sbComputed)
		return sbChanged;
	sbComputed = true;

	// Find most recent modification time across all shader files
	std::filesystem::file_time_type maxWriteTime;
	for (int64_t i = 0; i < 2; ++i)
	{
		std::filesystem::path shadersDir = gpFileManager->mpInputDirectories[i] / "Shaders";
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(shadersDir))
		{
			if (!rEntry.is_regular_file())
				continue;
			std::filesystem::file_time_type writeTime = rEntry.last_write_time();
			if (writeTime > maxWriteTime)
				maxWriteTime = writeTime;
		}
	}

	// Compare with stored time
	std::filesystem::path storedTimePath = gpFileManager->mTempDirectory / "ShaderHeadersModifiedTime.bin";
	if (std::filesystem::exists(storedTimePath))
	{
		int64_t iStoredTime = 0;
		std::fstream fileStream(storedTimePath, std::ios::in | std::ios::binary);
		fileStream.read(reinterpret_cast<char*>(&iStoredTime), sizeof(iStoredTime));
		if (maxWriteTime.time_since_epoch().count() <= iStoredTime)
		{
			sbChanged = false;
			return sbChanged;
		}
	}

	// Something changed — update stored time
	int64_t iMaxTime = maxWriteTime.time_since_epoch().count();
	std::fstream fileStream(storedTimePath, std::ios::out | std::ios::binary);
	fileStream.write(reinterpret_cast<char*>(&iMaxTime), sizeof(iMaxTime));

	sbChanged = true;
	return sbChanged;
}

static void CollectShaderIncludes(const std::filesystem::path& rFile, const std::filesystem::path& rIncludeDir0, const std::filesystem::path& rIncludeDir1, std::vector<std::filesystem::path>& rResolvedIncludes)
{
	std::fstream fileStream(rFile, std::ios::in);
	std::string line;
	while (std::getline(fileStream, line))
	{
		size_t uiIncludePos = line.find("#include");
		if (uiIncludePos == std::string::npos)
			continue;

		size_t uiFirstQuote = line.find('"', uiIncludePos);
		if (uiFirstQuote == std::string::npos)
			continue;

		size_t uiSecondQuote = line.find('"', uiFirstQuote + 1);
		if (uiSecondQuote == std::string::npos)
			continue;

		std::string includePath = line.substr(uiFirstQuote + 1, uiSecondQuote - uiFirstQuote - 1);

		// Resolve: relative to file, then includeDir0, then includeDir1
		std::filesystem::path resolved;
		if (std::filesystem::path candidate0 = rFile.parent_path() / includePath; std::filesystem::exists(candidate0))
			resolved = std::filesystem::canonical(candidate0);
		else if (std::filesystem::path candidate1 = rIncludeDir0 / includePath; std::filesystem::exists(candidate1))
			resolved = std::filesystem::canonical(candidate1);
		else if (std::filesystem::path candidate2 = rIncludeDir1 / includePath; std::filesystem::exists(candidate2))
			resolved = std::filesystem::canonical(candidate2);
		else
			continue;

		if (std::find(rResolvedIncludes.begin(), rResolvedIncludes.end(), resolved) != rResolvedIncludes.end())
			continue;

		rResolvedIncludes.push_back(resolved);
		CollectShaderIncludes(resolved, rIncludeDir0, rIncludeDir1, rResolvedIncludes);
	}
}

std::optional<common::ChunkFlags_t> ExportShader::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".comp" || rDirectoryEntry.path().extension() == ".frag" || rDirectoryEntry.path().extension() == ".vert" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kShader) : std::nullopt;
}

bool ExportShader::CheckDirty(const std::filesystem::path& rPackFile)
{
	if (ExportJob::CheckDirty(rPackFile))
		return true;

	if (ShaderHeadersChanged())
	{
		std::filesystem::path includeDir0 = gpFileManager->mpInputDirectories[0] / "Shaders";
		std::filesystem::path includeDir1 = gpFileManager->mpInputDirectories[1] / "Shaders";

		std::vector<std::filesystem::path> resolvedIncludes;
		CollectShaderIncludes(mInputPath, includeDir0, includeDir1, resolvedIncludes);

		std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
		for (const std::filesystem::path& rHeaderFile : resolvedIncludes)
		{
			std::filesystem::file_time_type headerFileLastWriteTime = std::filesystem::last_write_time(rHeaderFile);
			if (headerFileLastWriteTime > chunkFileLastWriteTime)
			{
				auto [date, time] = common::FileTimeString(chunkFileLastWriteTime);
				Log("Chunk file \"{}\" is out of date (header modified): {} {}", mChunkFile.string(), date, time);
				mbDirty = true;
				return mbDirty;
			}
		}
	}

	return mbDirty;
}

void WriteBinding(VkDescriptorSetLayoutBinding* pBindings, uint32_t* pSetIndices, int64_t iBinding, uint32_t uiSet, VkDescriptorType vkDescriptorType, int64_t iDescriptorCount, common::ChunkFlags_t chunkFlags)
{
	ASSERT(iBinding < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	VkDescriptorSetLayoutBinding& rVkDescriptorSetLayoutBinding = pBindings[iBinding];
	rVkDescriptorSetLayoutBinding.binding = static_cast<uint32_t>(iBinding);
	rVkDescriptorSetLayoutBinding.descriptorType = vkDescriptorType;
	rVkDescriptorSetLayoutBinding.descriptorCount = static_cast<uint32_t>(iDescriptorCount);
	rVkDescriptorSetLayoutBinding.stageFlags = chunkFlags & kCompute ? VK_SHADER_STAGE_COMPUTE_BIT : (chunkFlags & kFragment ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT);
	rVkDescriptorSetLayoutBinding.pImmutableSamplers = nullptr;
	pSetIndices[iBinding] = uiSet;
}

void ExportShader::Export()
{
	// glslc.exe is glslangValidator.exe but with support for #include
	// We're only going to use it to pre-process the shader to bake in include files
	// We'll use glslangValidator.exe to actually compile it because glslc.exe often fails silently on compile errors
	std::filesystem::path glslcExecutable(gpFileManager->mVulkanSdkBinariesDirectory);
	glslcExecutable.append("glslc.exe");

	std::filesystem::path preProcessedFile(gpFileManager->mTempDirectory);
	preProcessedFile /= mRelativeDirectory;
	preProcessedFile /= mInputPath.filename();
	std::filesystem::remove(preProcessedFile);

	std::wstring commandLineParameters(L"");
#if defined(OPTIMIZE_SHADERS)
	commandLineParameters += L" -O";      // Enable optimization
#else
	commandLineParameters += L" -O0";     // Disable optimization
	commandLineParameters += L" -g";      // Add debug info
#endif
	commandLineParameters += L" -E";      // Pre-process only
	commandLineParameters += L" -Werror"; // Treat warnings as errors
	commandLineParameters += L" -I \"" + gpFileManager->mpInputDirectories[0].native() + L"/Shaders\"";
	commandLineParameters += L" -I \"" + gpFileManager->mpInputDirectories[1].native() + L"/Shaders\"";
	commandLineParameters += L" -o \"" + preProcessedFile.native() + L"\"";
	commandLineParameters += L" \"" + mInputPath.native() + L"\"";

	std::wstring log = std::to_wstring(common::gpThreadLocal->miThreadId.value());
	log += L": ";
	log += glslcExecutable.native();
	log += commandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	std::string output = common::RunExecutable(glslcExecutable, commandLineParameters);
	if (!output.empty())
	{
		throw std::runtime_error(std::format("glslc.exe error: {}", output));
	}

	if (!std::filesystem::exists(preProcessedFile))
	{
		throw std::runtime_error(std::format("Shader '{}' failed to pre-process", mInputPath.string()));
	}

	mIntermediateFiles.push_back(preProcessedFile);
	VERIFY_SUCCESS(std::filesystem::exists(preProcessedFile));

	// Compile the pre-processed file to Spirv
	std::filesystem::path glslangValidatorExecutable(gpFileManager->mVulkanSdkBinariesDirectory);
	glslangValidatorExecutable.append("glslangValidator.exe");

	std::filesystem::path spirvFile(preProcessedFile);
	spirvFile += ".spv";
	std::filesystem::remove(spirvFile);

	commandLineParameters = L"";
#if defined(OPTIMIZE_SHADERS)
	// Optimization is enabled by default
	commandLineParameters += L" -g0"; // Strip debug info
#else
	commandLineParameters += L" -Od"; // Disable optimization
	commandLineParameters += L" -g";  // Add debug info
#endif
	commandLineParameters += L" -V";      // Generate binary
	commandLineParameters += L" --target-env vulkan1.2"; // Also update VK_API_VERSION_1_2 in engine
	// commandLineParameters += L" -t";   // Multi-threaded
	commandLineParameters += L" -o \"" + spirvFile.native() + L"\"";
	commandLineParameters += L" \"" + preProcessedFile.native() + L"\"";

	log = std::to_wstring(common::gpThreadLocal->miThreadId.value());
	log += L": ";
	log += glslangValidatorExecutable.native();
	log += commandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	output = common::RunExecutable(glslangValidatorExecutable, commandLineParameters);
	if (!output.empty())
	{
		Log("glslangValidator.exe output: {}", output);
	}

	if (!std::filesystem::exists(spirvFile))
	{
		throw std::runtime_error(std::format("Shader '{}' failed to compile", mInputPath.string()));
	}

	mIntermediateFiles.push_back(spirvFile);
	VERIFY_SUCCESS(std::filesystem::exists(spirvFile));

	// Read SPIR-V into temporary buffer for reflection
	int64_t iSpirvFileBytes = std::filesystem::file_size(spirvFile);
	std::vector<std::byte> spirvData(iSpirvFileBytes);
	std::fstream spirvFileStream(spirvFile, std::ios::in | std::ios::binary);
	spirvFileStream.read(reinterpret_cast<char*>(spirvData.data()), iSpirvFileBytes);
	spirvFileStream.close();

	// Reflect into local stack arrays
	VkDescriptorSetLayoutBinding tempBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	uint32_t tempSetIndices[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	VkVertexInputAttributeDescription tempAttrs[common::ShaderHeader::kiMaxVertexInputAttributeDescriptions] {};
	int64_t iBindingCount = 0;
	int64_t iAttrCount = 0;
	int64_t iVertexInputStride = 0;

	spirv_cross::Compiler spirvCrossCompiler(reinterpret_cast<uint32_t*>(spirvData.data()), iSpirvFileBytes / sizeof(uint32_t));
	spirv_cross::ShaderResources shaderResources = spirvCrossCompiler.get_shader_resources();

	for (int64_t i = 0; i < static_cast<int64_t>(shaderResources.stage_inputs.size()); ++i)
	{
		for (const spirv_cross::Resource& rResource : shaderResources.stage_inputs)
		{
			int64_t iLocation = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationLocation);

			// This is required because stage_inputs can be out of order
			if (iLocation == i)
			{
				const spirv_cross::SPIRType& rSpirvType = spirvCrossCompiler.get_type(rResource.type_id);
				Log("   {} {} {} size {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, rSpirvType.vecsize);

				ASSERT(iLocation < common::ShaderHeader::kiMaxVertexInputAttributeDescriptions);
				VkVertexInputAttributeDescription& rVkVertexInputAttributeDescription = tempAttrs[iLocation];
				rVkVertexInputAttributeDescription.location = static_cast<uint32_t>(iLocation);
				rVkVertexInputAttributeDescription.binding = 0;
				rVkVertexInputAttributeDescription.format = rSpirvType.vecsize == 2 ? VK_FORMAT_R32G32_SFLOAT : (rSpirvType.vecsize == 3 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT);
				rVkVertexInputAttributeDescription.offset = static_cast<uint32_t>(iVertexInputStride);
				++iAttrCount;

				Log("       location {} binding {} format {} offset {}", rVkVertexInputAttributeDescription.location, rVkVertexInputAttributeDescription.binding, static_cast<int64_t>(rVkVertexInputAttributeDescription.format), rVkVertexInputAttributeDescription.offset);

				iVertexInputStride += rSpirvType.vecsize * sizeof(float);
			}
		}
	}
	Log("   Descriptions: {} Input stride: {}", iAttrCount, iVertexInputStride);

	if (shaderResources.stage_outputs.size() > 0)
	{
		Log("Stage outputs:");
		for (const spirv_cross::Resource& rResource : shaderResources.stage_outputs)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			Log("   {} {} {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, iBinding);
		}
	}

	if (shaderResources.uniform_buffers.size() > 0)
	{
		Log("Uniform buffers:");
		for (const spirv_cross::Resource& rResource : shaderResources.uniform_buffers)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			uint32_t uiSet = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
			Log("   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

			WriteBinding(tempBindings, tempSetIndices, iBinding, uiSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, mChunkFlags);
			iBindingCount = std::max(iBinding + 1, iBindingCount);
		}
	}

	if (shaderResources.storage_buffers.size() > 0)
	{
		Log("Storage buffers:");
		for (const spirv_cross::Resource& rResource : shaderResources.storage_buffers)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			uint32_t uiSet = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
			Log("   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

			const spirv_cross::SPIRType& rSpirvType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!rSpirvType.array.empty())
			{
				Log("   Array size: {}", rSpirvType.array[0]);
			}

			WriteBinding(tempBindings, tempSetIndices, iBinding, uiSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rSpirvType.array.empty() ? 1 : rSpirvType.array[0], mChunkFlags);
			iBindingCount = std::max(iBinding + 1, iBindingCount);
		}
	}

	if (shaderResources.sampled_images.size() > 0)
	{
		Log("Sampled images:");
		for (const spirv_cross::Resource& rResource : shaderResources.sampled_images)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			uint32_t uiSet = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
			Log("   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

			const spirv_cross::SPIRType& rSpirvType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!rSpirvType.array.empty())
			{
				Log("   Array size: {}", rSpirvType.array[0]);
			}

			WriteBinding(tempBindings, tempSetIndices, iBinding, uiSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rSpirvType.array.empty() ? 1 : rSpirvType.array[0], mChunkFlags);
			iBindingCount = std::max(iBinding + 1, iBindingCount);
		}
	}

	if (shaderResources.storage_images.size() > 0)
	{
		Log("Storage images:");
		for (const spirv_cross::Resource& rResource : shaderResources.storage_images)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			uint32_t uiSet = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
			Log("   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

			const spirv_cross::SPIRType& rSpirvType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!rSpirvType.array.empty())
			{
				Log("   Array size: {}", rSpirvType.array[0]);
			}

			WriteBinding(tempBindings, tempSetIndices, iBinding, uiSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rSpirvType.array.empty() ? 1 : rSpirvType.array[0], mChunkFlags);
			iBindingCount = std::max(iBinding + 1, iBindingCount);
		}
	}

	if (shaderResources.separate_images.size() > 0)
	{
		Log("Seperate images:");
		for (const spirv_cross::Resource& rResource : shaderResources.separate_images)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			uint32_t uiSet = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
			Log("   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

			const spirv_cross::SPIRType& rSpirvType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!rSpirvType.array.empty())
			{
				Log("   Array size: {}", rSpirvType.array[0]);
			}

			// Runtime-sized arrays (unsized) report array[0] == 0; use UINT32_MAX sentinel for pipeline to resolve
			int64_t iArraySize = rSpirvType.array.empty() ? 1 : (rSpirvType.array[0] == 0 ? UINT32_MAX : rSpirvType.array[0]);
			WriteBinding(tempBindings, tempSetIndices, iBinding, uiSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, iArraySize, mChunkFlags);
			iBindingCount = std::max(iBinding + 1, iBindingCount);
		}
	}

	if (shaderResources.separate_samplers.size() > 0)
	{
		Log("Seperate samplers:");
		for (const spirv_cross::Resource& rResource : shaderResources.separate_samplers)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			uint32_t uiSet = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
			Log("   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

			WriteBinding(tempBindings, tempSetIndices, iBinding, uiSet, VK_DESCRIPTOR_TYPE_SAMPLER, 1, mChunkFlags);
			iBindingCount = std::max(iBinding + 1, iBindingCount);
		}
	}

	// Allocate data span: [bindings ALIGN16] [setIndices ALIGN16] [attrs ALIGN16] [SPIR-V]
	int64_t iBindingsBytes = common::RoundUp<int64_t, common::kiAlignmentBytes>(iBindingCount * static_cast<int64_t>(sizeof(VkDescriptorSetLayoutBinding)));
	int64_t iSetIndicesBytes = common::RoundUp<int64_t, common::kiAlignmentBytes>(iBindingCount * static_cast<int64_t>(sizeof(uint32_t)));
	int64_t iAttrsBytes = common::RoundUp<int64_t, common::kiAlignmentBytes>(iAttrCount * static_cast<int64_t>(sizeof(VkVertexInputAttributeDescription)));
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iBindingsBytes + iSetIndicesBytes + iAttrsBytes + iSpirvFileBytes);
	pHeader->shaderHeader = common::ShaderHeader {};
	pHeader->shaderHeader.iDescriptorSetLayoutBindings = iBindingCount;
	pHeader->shaderHeader.iVertexInputAttributeDescriptions = iAttrCount;
	pHeader->shaderHeader.iVertexInputStride = iVertexInputStride;

	// Copy arrays to data span
	std::memcpy(dataSpan.data(), tempBindings, iBindingCount * sizeof(VkDescriptorSetLayoutBinding));
	std::memcpy(dataSpan.data() + iBindingsBytes, tempSetIndices, iBindingCount * sizeof(uint32_t));
	std::memcpy(dataSpan.data() + iBindingsBytes + iSetIndicesBytes, tempAttrs, iAttrCount * sizeof(VkVertexInputAttributeDescription));

	// Copy SPIR-V after arrays
	std::memcpy(dataSpan.data() + iBindingsBytes + iSetIndicesBytes + iAttrsBytes, spirvData.data(), iSpirvFileBytes);

	ASSERT(*reinterpret_cast<uint32_t*>(dataSpan.data() + iBindingsBytes + iSetIndicesBytes + iAttrsBytes) == 0x07230203u);
}

void ExportShader::CleanupOnFailure()
{
	for (const std::filesystem::path& rPath : mIntermediateFiles)
	{
		std::filesystem::remove(rPath);
	}
	mIntermediateFiles.clear();
}
