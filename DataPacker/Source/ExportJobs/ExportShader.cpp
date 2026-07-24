#include "ExportShader.h"

#include "FileManager.h"

constexpr bool kbOptimizeShaders = true;

using enum common::ChunkFlags;

namespace
{

const std::filesystem::path& GetVulkanSdkBinariesDirectory()
{
	static const std::filesystem::path sPath = []()
	{
		wchar_t pcDirectory[MAX_PATH] {};
		DWORD uiResult = GetEnvironmentVariableW(L"VK_SDK_PATH", pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
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

std::optional<common::ChunkFlags_t> ExportShader::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	std::filesystem::path extension = rDirectoryEntry.path().extension();
	return extension == ".comp" || extension == ".frag" || extension == ".vert" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kShader) : std::nullopt;
}

namespace
{

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
	// Table is indexed by binding number alone — a second write means two sets reuse one binding number, which would silently overwrite the first entry
	ASSERT(rTable.pBindings[iBinding].descriptorCount == 0);
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

} // namespace

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

std::filesystem::path ExportShader::RunVulkanTool(const std::filesystem::path& rExecutable, std::wstring& rParameters, const std::filesystem::path& rOutputFile, bool bThrowOnAnyOutput)
{
	LOG(kDefault, kVerbose, "{}: {}{}", common::gpThreadLocal->miThreadId.value_or(0), rExecutable, rParameters);

	common::ExecutableResult result = common::RunExecutable(rExecutable, rParameters);

	std::string toolName = rExecutable.filename().string();

	// glslc fails silently on compile errors (empty stdout + success exit code), so the preprocess caller treats any
	// stdout as fatal; glslangValidator / spirv-opt key on the exit code and only warn on non-empty stdout.
	if (bThrowOnAnyOutput)
	{
		if (!result.mOutput.empty())
		{
			throw std::runtime_error(std::format("{} error: {}", toolName, result.mOutput));
		}
	}
	else if (result.miExitCode != 0)
	{
		throw std::runtime_error(std::format("{} error: {}", toolName, result.mOutput));
	}

	if (!std::filesystem::exists(rOutputFile))
	{
		throw std::runtime_error(std::format("{} did not produce \"{}\"", toolName, rOutputFile.string()));
	}

	if (!bThrowOnAnyOutput && !result.mOutput.empty())
	{
		LOG(kDefault, kWarning, "{} output: {}", toolName, result.mOutput);
	}

	mIntermediateFiles.push_back(rOutputFile);

	return rOutputFile;
}

std::filesystem::path ExportShader::PreprocessShader()
{
	// glslc.exe is glslangValidator.exe but with support for #include
	// We're only going to use it to pre-process the shader to bake in include files
	// We'll use glslangValidator.exe to actually compile it because glslc.exe often fails silently on compile errors
	std::filesystem::path glslcExecutable(GetVulkanSdkBinariesDirectory());
	glslcExecutable.append("glslc.exe");

	std::filesystem::path preProcessedFile(gpFileManager->mCacheDirectory);
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
	commandLineParameters += L" -MD";
	commandLineParameters += L" -MF \"" + mDependencyFile.native() + L"\"";
	commandLineParameters += L" -I \"" + gpFileManager->mpInputDirectories[0].native() + L"/Shaders\"";
	commandLineParameters += L" -I \"" + gpFileManager->mpInputDirectories[1].native() + L"/Shaders\"";
	commandLineParameters += L" -o \"" + preProcessedFile.native() + L"\"";
	commandLineParameters += L" \"" + mInputPath.native() + L"\"";

	std::filesystem::path result = RunVulkanTool(glslcExecutable, commandLineParameters, preProcessedFile, /*bThrowOnAnyOutput*/ true);
	CaptureDependencies();
	return result;
}

ExportShader::ExportShader(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
: ExportJob(rChunkFlags, rFile)
{
	mDependencyFile = gpFileManager->mCacheDirectory / mRelativeDirectory / (mInputPath.filename().native() + L".d");
	mDependencyMetadataFile = gpFileManager->mCacheDirectory / mRelativeDirectory / (mInputPath.filename().native() + L".deps.meta");

	if (rFile.extension() == ".comp")
	{
		mChunkFlags.Set(common::ChunkFlags::kCompute);
	}
	else if (rFile.extension() == ".frag")
	{
		mChunkFlags.Set(common::ChunkFlags::kFragment);
	}
	else if (rFile.extension() == ".vert")
	{
		mChunkFlags.Set(common::ChunkFlags::kVertex);
	}
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

	return RunVulkanTool(glslangValidatorExecutable, commandLineParameters, spirvFile, /*bThrowOnAnyOutput*/ false);
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

	return RunVulkanTool(spirvOptExecutable, spirvOptCommandLineParameters, optimizedSpirvFile, /*bThrowOnAnyOutput*/ false);
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
		// Format mapping below assumes 32-bit float inputs (SPIRV-Cross Half/Double are distinct basetypes) — an int attribute would be silently mis-typed.
		// Vertex stage only: fragment interpolants are legitimately flat int/uint and never feed VkVertexInputAttributeDescription
		ASSERT(rSpirvType.basetype == spirv_cross::SPIRType::Float || !(mChunkFlags & kVertex));
		VkVertexInputAttributeDescription& rVkVertexInputAttributeDescription = tempAttrs[iLocation];
		rVkVertexInputAttributeDescription.location = static_cast<uint32_t>(iLocation);
		rVkVertexInputAttributeDescription.binding = 0;
		rVkVertexInputAttributeDescription.format = rSpirvType.vecsize == 1 ? VK_FORMAT_R32_SFLOAT : (rSpirvType.vecsize == 2 ? VK_FORMAT_R32G32_SFLOAT : (rSpirvType.vecsize == 3 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT));
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
	// Unsized arrays report array[0] == 0 in these categories; a 0 descriptorCount disarms the WriteBinding double-write guard, so fail loud (no current shader hits this)
	auto arrayCount = [](const spirv_cross::SPIRType& rType) -> int64_t { ASSERT(rType.array.empty() || rType.array[0] != 0); return rType.array.empty() ? 1 : rType.array[0]; };
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
	auto [pHeader, dataSpan] = AllocateHeaderAndData(common::ShaderHeader::SpirvOffset(iBindingCount, iAttrCount) + iSpirvFileBytes);
	pHeader->shaderHeader = common::ShaderHeader {};
	pHeader->shaderHeader.iDescriptorSetLayoutBindings = iBindingCount;
	pHeader->shaderHeader.iVertexInputAttributeDescriptions = iAttrCount;
	pHeader->shaderHeader.iVertexInputStride = iVertexInputStride;

	// Copy arrays to data span
	std::memcpy(dataSpan.data(), tempBindings, iBindingCount * sizeof(VkDescriptorSetLayoutBinding));
	std::memcpy(dataSpan.data() + common::ShaderHeader::SetIndicesOffset(iBindingCount), tempSetIndices, iBindingCount * sizeof(uint32_t));
	std::memcpy(dataSpan.data() + common::ShaderHeader::AttributesOffset(iBindingCount), tempAttrs, iAttrCount * sizeof(VkVertexInputAttributeDescription));

	// Copy SPIR-V after arrays
	std::memcpy(dataSpan.data() + common::ShaderHeader::SpirvOffset(iBindingCount, iAttrCount), spirvData.data(), iSpirvFileBytes);

	ASSERT(*reinterpret_cast<uint32_t*>(dataSpan.data() + common::ShaderHeader::SpirvOffset(iBindingCount, iAttrCount)) == common::ShaderHeader::kuiSpirvMagic);
}

void ExportShader::CleanupOnFailure()
{
	for (const std::filesystem::path& rPath : mIntermediateFiles)
	{
		std::filesystem::remove(rPath);
	}
	mIntermediateFiles.clear();
}
