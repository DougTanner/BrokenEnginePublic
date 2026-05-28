#include "ExportShader.h"

#if defined(_CRTDBG_MAP_ALLOC)
	#undef free
#endif
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "SPIRV-Cross/spirv_cross.hpp"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)
#if defined(_CRTDBG_MAP_ALLOC)
	#define free(p) _free_dbg(p, _NORMAL_BLOCK)
#endif

#include "FileManager.h"

constexpr bool kbOptimizeShaders = true;

using enum common::ChunkFlags;

namespace
{

const std::filesystem::path& GetVulkanSdkBinariesDirectory()
{
	static const std::filesystem::path sPath = []()
	{
		char pcDirectory[MAX_PATH] {};
		DWORD uiResult = GetEnvironmentVariable("VK_SDK_PATH", pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
		if (uiResult == 0)
		{
			throw std::runtime_error("VK_SDK_PATH environment variable not found");
		}
		std::filesystem::path path(pcDirectory);
		VERIFY_SUCCESS(std::filesystem::exists(path));
		path.append("Bin");
		LOG(kDefault, kDebug, "Vulkan binaries directory: \"{}\"", path.string());
		return path;
	}();
	return sPath;
}

}

static std::vector<std::filesystem::path> ParseDependencyFile(const std::filesystem::path& rDependencyFilePath)
{
	std::vector<std::filesystem::path> dependencies;
	std::fstream fileStream(rDependencyFilePath, std::ios::in);
	std::string content((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());

	// Skip target (everything before first ':')
	size_t uiColon = content.find(':');
	if (uiColon == std::string::npos)
	{
		return dependencies;
	}
	content = content.substr(uiColon + 1);

	// Remove backslash-newline continuations and carriage returns
	std::string cleaned;
	for (size_t i = 0; i < content.size(); ++i)
	{
		if (content[i] == '\\' && i + 1 < content.size() && (content[i + 1] == '\n' || content[i + 1] == '\r'))
		{
			++i;
			if (content[i] == '\r' && i + 1 < content.size() && content[i + 1] == '\n')
			{
				++i;
			}
		}
		else if (content[i] != '\r')
		{
			cleaned += content[i];
		}
	}

	// Split on whitespace to get dependency paths
	std::istringstream stream(cleaned);
	std::string token;
	while (stream >> token)
	{
		dependencies.emplace_back(token);
	}

	return dependencies;
}

std::optional<common::ChunkFlags_t> ExportShader::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".comp" || rDirectoryEntry.path().extension() == ".frag" || rDirectoryEntry.path().extension() == ".vert" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kShader) : std::nullopt;
}

bool ExportShader::CheckDirty(const std::filesystem::path& rPackFile)
{
	if (ExportJob::CheckDirty(rPackFile))
	{
		return true;
	}

	// Use dependency file from previous export to check if any included header changed
	std::filesystem::path dependencyFile = gpFileManager->mTempDirectory / mRelativeDirectory / (mInputPath.filename().native() + L".d");
	if (!std::filesystem::exists(dependencyFile))
	{
		mbDirty = true;
		return true;
	}

	std::vector<std::filesystem::path> dependencies = ParseDependencyFile(dependencyFile);
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	for (const std::filesystem::path& rDependency : dependencies)
	{
		if (!std::filesystem::exists(rDependency))
		{
			mbDirty = true;
			return true;
		}
		if (std::filesystem::last_write_time(rDependency) > chunkFileLastWriteTime)
		{
			auto [date, time] = common::FileTimeString(chunkFileLastWriteTime);
			LOG(kDefault, kDebug, "Chunk file \"{}\" is out of date (dependency modified): {} {}", mChunkFile.string(), date, time);
			mbDirty = true;
			return mbDirty;
		}
	}

	return mbDirty;
}

// In-progress binding-table state shared by every CollectBindings call for one shader.
// All four members refer to caller-owned storage; riBindingCount is mutated as bindings land.
struct BindingTable
{
	VkDescriptorSetLayoutBinding* pBindings;
	uint32_t* pSetIndices;
	int64_t& riBindingCount;
	common::ChunkFlags_t chunkFlags;
};

void WriteBinding(BindingTable& rTable, int64_t iBinding, uint32_t uiSet, VkDescriptorType vkDescriptorType, int64_t iDescriptorCount)
{
	ASSERT(iBinding < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	VkDescriptorSetLayoutBinding& rVkDescriptorSetLayoutBinding = rTable.pBindings[iBinding];
	rVkDescriptorSetLayoutBinding.binding = static_cast<uint32_t>(iBinding);
	rVkDescriptorSetLayoutBinding.descriptorType = vkDescriptorType;
	rVkDescriptorSetLayoutBinding.descriptorCount = static_cast<uint32_t>(iDescriptorCount);
	rVkDescriptorSetLayoutBinding.stageFlags = rTable.chunkFlags & kCompute ? VK_SHADER_STAGE_COMPUTE_BIT : (rTable.chunkFlags & kFragment ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT);
	rVkDescriptorSetLayoutBinding.pImmutableSamplers = nullptr;
	rTable.pSetIndices[iBinding] = uiSet;
}

// Enumerates one SPIRV-Cross resource category, writing a descriptor binding per resource.
// countFn maps the reflected type to a descriptor count (constant for buffers/samplers, array-derived for images).
template <typename COUNT_FN>
void CollectBindings(const spirv_cross::SmallVector<spirv_cross::Resource>& rResources, const char* pcLabel, VkDescriptorType vkDescriptorType, spirv_cross::Compiler& rCompiler, BindingTable& rTable, COUNT_FN&& countFn)
{
	if (rResources.empty())
	{
		return;
	}

	LOG(kDefault, kVerbose, "{}", pcLabel);
	for (const spirv_cross::Resource& rResource : rResources)
	{
		int64_t iBinding = rCompiler.get_decoration(rResource.id, spv::DecorationBinding);
		uint32_t uiSet = rCompiler.get_decoration(rResource.id, spv::DecorationDescriptorSet);
		LOG(kDefault, kVerbose,"   {} {} {} set {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, uiSet, iBinding);

		const spirv_cross::SPIRType& rSpirvType = rCompiler.get_type(rResource.type_id);
		if (!rSpirvType.array.empty())
		{
			LOG(kDefault, kVerbose,"   Array size: {}", rSpirvType.array[0]);
		}

		WriteBinding(rTable, iBinding, uiSet, vkDescriptorType, countFn(rSpirvType));
		rTable.riBindingCount = std::max(iBinding + 1, rTable.riBindingCount);
	}
}

void ExportShader::Export()
{
	std::filesystem::path preProcessedFile = PreprocessShader();
	std::filesystem::path spirvFile = CompileShader(preProcessedFile);
	if constexpr (kbOptimizeShaders)
	{
		spirvFile = OptimizeShader(spirvFile);
	}
	ReflectAndWriteShader(spirvFile);
}

std::filesystem::path ExportShader::PreprocessShader()
{
	// glslc.exe is glslangValidator.exe but with support for #include
	// We're only going to use it to pre-process the shader to bake in include files
	// We'll use glslangValidator.exe to actually compile it because glslc.exe often fails silently on compile errors
	std::filesystem::path glslcExecutable(GetVulkanSdkBinariesDirectory());
	glslcExecutable.append("glslc.exe");

	std::filesystem::path preProcessedFile(gpFileManager->mTempDirectory);
	preProcessedFile /= mRelativeDirectory;
	preProcessedFile /= mInputPath.filename();
	std::filesystem::remove(preProcessedFile);

	std::wstring commandLineParameters(L"");
	if constexpr (kbOptimizeShaders)
	{
		commandLineParameters += L" -O";      // Enable optimization
	}
	else
	{
		commandLineParameters += L" -O0";     // Disable optimization
		commandLineParameters += L" -g";      // Add debug info
	}
	commandLineParameters += L" -E";      // Pre-process only
	commandLineParameters += L" -Werror"; // Treat warnings as errors
	std::filesystem::path dependencyFile = gpFileManager->mTempDirectory / mRelativeDirectory / (mInputPath.filename().native() + L".d");
	commandLineParameters += L" -MD";
	commandLineParameters += L" -MF \"" + dependencyFile.native() + L"\"";
	commandLineParameters += L" -I \"" + gpFileManager->mpInputDirectories[0].native() + L"/Shaders\"";
	commandLineParameters += L" -I \"" + gpFileManager->mpInputDirectories[1].native() + L"/Shaders\"";
	commandLineParameters += L" -o \"" + preProcessedFile.native() + L"\"";
	commandLineParameters += L" \"" + mInputPath.native() + L"\"";

	std::wstring log = std::to_wstring(common::gpThreadLocal->miThreadId.value_or(0));
	log += L": ";
	log += glslcExecutable.native();
	log += commandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	common::ExecutableResult result = common::RunExecutable(glslcExecutable, commandLineParameters);
	if (!result.mOutput.empty())
	{
		throw std::runtime_error(std::format("glslc.exe error: {}", result.mOutput));
	}

	if (!std::filesystem::exists(preProcessedFile))
	{
		throw std::runtime_error(std::format("Shader '{}' failed to pre-process", mInputPath.string()));
	}

	mIntermediateFiles.push_back(preProcessedFile);
	VERIFY_SUCCESS(std::filesystem::exists(preProcessedFile));

	return preProcessedFile;
}

std::filesystem::path ExportShader::CompileShader(const std::filesystem::path& rPreProcessedFile)
{
	std::filesystem::path glslangValidatorExecutable(GetVulkanSdkBinariesDirectory());
	glslangValidatorExecutable.append("glslangValidator.exe");

	std::filesystem::path spirvFile(rPreProcessedFile);
	spirvFile += ".spv";
	std::filesystem::remove(spirvFile);

	std::wstring commandLineParameters = L"";
	if constexpr (kbOptimizeShaders)
	{
		// Optimization is enabled by default
		commandLineParameters += L" -g0"; // Strip debug info
	}
	else
	{
		commandLineParameters += L" -Od"; // Disable optimization
		commandLineParameters += L" -g";  // Add debug info
	}
	commandLineParameters += L" -V";      // Generate binary
	commandLineParameters += L" --target-env vulkan1.2"; // Also update VK_API_VERSION_1_2 in engine
	// commandLineParameters += L" -t";   // Multi-threaded
	commandLineParameters += L" -o \"" + spirvFile.native() + L"\"";
	commandLineParameters += L" \"" + rPreProcessedFile.native() + L"\"";

	std::wstring log = std::to_wstring(common::gpThreadLocal->miThreadId.value_or(0));
	log += L": ";
	log += glslangValidatorExecutable.native();
	log += commandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	common::ExecutableResult result = common::RunExecutable(glslangValidatorExecutable, commandLineParameters);
	if (result.miExitCode != 0 || !std::filesystem::exists(spirvFile))
	{
		throw std::runtime_error(std::format("glslangValidator.exe error: {}", result.mOutput));
	}
	if (!result.mOutput.empty())
	{
		LOG(kDefault, kWarning, "glslangValidator.exe output: {}", result.mOutput);
	}

	mIntermediateFiles.push_back(spirvFile);

	return spirvFile;
}

std::filesystem::path ExportShader::OptimizeShader(const std::filesystem::path& rSpirvFile)
{
	// Run spirv-opt on the compiled SPIR-V
	std::filesystem::path spirvOptExecutable(GetVulkanSdkBinariesDirectory());
	spirvOptExecutable.append("spirv-opt.exe");

	std::filesystem::path optimizedSpirvFile(rSpirvFile);
	optimizedSpirvFile += ".opt.spv";
	std::filesystem::remove(optimizedSpirvFile);

	std::wstring spirvOptCommandLineParameters = L"";
	spirvOptCommandLineParameters += L" -O";
	spirvOptCommandLineParameters += L" --target-env=vulkan1.2";
	spirvOptCommandLineParameters += L" --scalar-block-layout";
	spirvOptCommandLineParameters += L" -o \"" + optimizedSpirvFile.native() + L"\"";
	spirvOptCommandLineParameters += L" \"" + rSpirvFile.native() + L"\"";

	std::wstring log = std::to_wstring(common::gpThreadLocal->miThreadId.value_or(0));
	log += L": ";
	log += spirvOptExecutable.native();
	log += spirvOptCommandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	common::ExecutableResult result = common::RunExecutable(spirvOptExecutable, spirvOptCommandLineParameters);
	if (result.miExitCode != 0 || !std::filesystem::exists(optimizedSpirvFile))
	{
		throw std::runtime_error(std::format("spirv-opt.exe error: {}", result.mOutput));
	}
	if (!result.mOutput.empty())
	{
		LOG(kDefault, kWarning, "spirv-opt.exe output: {}", result.mOutput);
	}

	mIntermediateFiles.push_back(optimizedSpirvFile);

	return optimizedSpirvFile;
}

void ExportShader::ReflectAndWriteShader(const std::filesystem::path& rSpirvFile)
{
	// Read SPIR-V into temporary buffer for reflection
	std::vector<std::byte> spirvData = common::ReadEntireFile(rSpirvFile);
	int64_t iSpirvFileBytes = static_cast<int64_t>(spirvData.size());

	// Reflect into local stack arrays
	VkDescriptorSetLayoutBinding tempBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	uint32_t tempSetIndices[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	VkVertexInputAttributeDescription tempAttrs[common::ShaderHeader::kiMaxVertexInputAttributeDescriptions] {};
	int64_t iBindingCount = 0;
	int64_t iAttrCount = 0;
	int64_t iVertexInputStride = 0;

	spirv_cross::Compiler spirvCrossCompiler(reinterpret_cast<uint32_t*>(spirvData.data()), iSpirvFileBytes / sizeof(uint32_t));
	spirv_cross::ShaderResources shaderResources = spirvCrossCompiler.get_shader_resources();

	// stage_inputs can be reported out of order; sort by location once, then process ascending
	std::vector<std::pair<int64_t, const spirv_cross::Resource*>> sortedStageInputs;
	sortedStageInputs.reserve(shaderResources.stage_inputs.size());
	for (const spirv_cross::Resource& rResource : shaderResources.stage_inputs)
	{
		sortedStageInputs.emplace_back(spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationLocation), &rResource);
	}
	std::sort(sortedStageInputs.begin(), sortedStageInputs.end(), [](const auto& rLhs, const auto& rRhs) { return rLhs.first < rRhs.first; });

	for (const auto& [iLocation, pResource] : sortedStageInputs)
	{
		const spirv_cross::SPIRType& rSpirvType = spirvCrossCompiler.get_type(pResource->type_id);
		LOG(kDefault, kVerbose,"   {} {} {} size {}", static_cast<uint32_t>(pResource->type_id), static_cast<uint32_t>(pResource->base_type_id), pResource->name, rSpirvType.vecsize);

		ASSERT(iLocation < common::ShaderHeader::kiMaxVertexInputAttributeDescriptions);
		VkVertexInputAttributeDescription& rVkVertexInputAttributeDescription = tempAttrs[iLocation];
		rVkVertexInputAttributeDescription.location = static_cast<uint32_t>(iLocation);
		rVkVertexInputAttributeDescription.binding = 0;
		rVkVertexInputAttributeDescription.format = rSpirvType.vecsize == 2 ? VK_FORMAT_R32G32_SFLOAT : (rSpirvType.vecsize == 3 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT);
		rVkVertexInputAttributeDescription.offset = static_cast<uint32_t>(iVertexInputStride);
		++iAttrCount;

		LOG(kDefault, kVerbose,"       location {} binding {} format {} offset {}", rVkVertexInputAttributeDescription.location, rVkVertexInputAttributeDescription.binding, static_cast<int64_t>(rVkVertexInputAttributeDescription.format), rVkVertexInputAttributeDescription.offset);

		iVertexInputStride += rSpirvType.vecsize * sizeof(float);
	}
	LOG(kDefault, kVerbose, "   Descriptions: {} Input stride: {}", iAttrCount, iVertexInputStride);

	if (shaderResources.stage_outputs.size() > 0)
	{
		LOG(kDefault, kVerbose,"Stage outputs:");
		for (const spirv_cross::Resource& rResource : shaderResources.stage_outputs)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG(kDefault, kVerbose,"   {} {} {} bound at {}", static_cast<uint32_t>(rResource.type_id), static_cast<uint32_t>(rResource.base_type_id), rResource.name, iBinding);
		}
	}

	auto constantOne = [](const spirv_cross::SPIRType&) -> int64_t { return 1; };
	auto arrayCount = [](const spirv_cross::SPIRType& rType) -> int64_t { return rType.array.empty() ? 1 : rType.array[0]; };
	// Runtime-sized arrays (unsized) report array[0] == 0; use UINT32_MAX sentinel for pipeline to resolve
	auto runtimeArrayCount = [](const spirv_cross::SPIRType& rType) -> int64_t { return rType.array.empty() ? 1 : (rType.array[0] == 0 ? std::numeric_limits<uint32_t>::max() : rType.array[0]); };

	BindingTable bindingTable {.pBindings = tempBindings, .pSetIndices = tempSetIndices, .riBindingCount = iBindingCount, .chunkFlags = mChunkFlags};
	CollectBindings(shaderResources.uniform_buffers, "Uniform buffers:", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, spirvCrossCompiler, bindingTable, constantOne);
	CollectBindings(shaderResources.storage_buffers, "Storage buffers:", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spirvCrossCompiler, bindingTable, arrayCount);
	CollectBindings(shaderResources.sampled_images, "Sampled images:", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, spirvCrossCompiler, bindingTable, arrayCount);
	CollectBindings(shaderResources.storage_images, "Storage images:", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, spirvCrossCompiler, bindingTable, arrayCount);
	CollectBindings(shaderResources.separate_images, "Separate images:", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, spirvCrossCompiler, bindingTable, runtimeArrayCount);
	CollectBindings(shaderResources.separate_samplers, "Separate samplers:", VK_DESCRIPTOR_TYPE_SAMPLER, spirvCrossCompiler, bindingTable, constantOne);

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
