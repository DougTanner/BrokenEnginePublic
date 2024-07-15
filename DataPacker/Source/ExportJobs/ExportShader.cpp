#include "ExportShader.h"

#ifdef _CRTDBG_MAP_ALLOC
	#undef free
#endif
#include "SPIRV-Cross/spirv_cross.hpp"
#ifdef _CRTDBG_MAP_ALLOC
	#define free(p) _free_dbg(p, _NORMAL_BLOCK)
#endif

#include "FileManager.h"

#define OPTIMIZE_SHADERS

using enum common::ChunkFlags;

void WriteBinding(common::ShaderHeader& rShaderHeader, int64_t iBinding, VkDescriptorType vkDescriptorType, int64_t iDescriptorCount, common::ChunkFlags eShaderType)
{
	ASSERT(iBinding < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
	VkDescriptorSetLayoutBinding& rVkDescriptorSetLayoutBinding = rShaderHeader.pVkDescriptorSetLayoutBindings[iBinding];
	rVkDescriptorSetLayoutBinding.binding = static_cast<uint32_t>(iBinding);
	rVkDescriptorSetLayoutBinding.descriptorType = vkDescriptorType;
	rVkDescriptorSetLayoutBinding.descriptorCount = static_cast<uint32_t>(iDescriptorCount);
	rVkDescriptorSetLayoutBinding.stageFlags = eShaderType == kShaderCompute ? VK_SHADER_STAGE_COMPUTE_BIT : (eShaderType == kShaderFragment ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT);
	rVkDescriptorSetLayoutBinding.pImmutableSamplers = nullptr;
}

void ExportShader::Export()
{
	common::ChunkFlags eShaderType = mInputPath.native().find(L".comp") != std::wstring::npos ? kShaderCompute : (mInputPath.native().find(L".frag") != std::wstring::npos ? kShaderFragment : kShaderVertex);

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

	auto log = std::to_wstring(common::gpThreadLocal->miThreadId.value());
	log += L": ";
	log += glslcExecutable.native();
	log += commandLineParameters;
	log += L"\n";
	OutputDebugStringW(log.c_str());

	std::string output = common::RunExecutable(glslcExecutable, commandLineParameters);
	if (!output.empty())
	{
		LOG("glslc.exe error:\n{}", output);
		DEBUG_BREAK();
	}

	if (!std::filesystem::exists(preProcessedFile))
	{
		LOG("Shader \"{}\" failed to pre-process", mInputPath.string());
		DEBUG_BREAK();
	}

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
	commandLineParameters += L" --target-env vulkan1.1"; // Also update VK_API_VERSION_1_1 in engine
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
		LOG("glslangValidator.exe output: {}", output);
	}

	if (!std::filesystem::exists(spirvFile))
	{
		LOG("Shader \"{}\" failed to compile", mInputPath.string());
		DEBUG_BREAK();
	}

	VERIFY_SUCCESS(std::filesystem::exists(spirvFile));

	// Generate the Vulkan structures
	int64_t iSpirvFileBytes = std::filesystem::file_size(spirvFile);
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iSpirvFileBytes);
	pHeader->shaderHeader = common::ShaderHeader {};

	std::fstream fileStream(spirvFile, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(dataSpan.data()), iSpirvFileBytes);

	spirv_cross::Compiler spirvCrossCompiler(reinterpret_cast<uint32_t*>(&dataSpan.front()), iSpirvFileBytes / sizeof(uint32_t));
	spirv_cross::ShaderResources shaderResources = spirvCrossCompiler.get_shader_resources();

	for (int64_t i = 0; i < static_cast<int64_t>(shaderResources.stage_inputs.size()); ++i)
	{
		for (const spirv_cross::Resource& rResource : shaderResources.stage_inputs)
		{
			int64_t iLocation = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationLocation);

			// This is required because stage_inputs can be out of order
			if (iLocation == i)
			{
				const spirv_cross::SPIRType& spirType = spirvCrossCompiler.get_type(rResource.type_id);
				LOG("   {} {} {} size {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, spirType.vecsize);

				ASSERT(iLocation < common::ShaderHeader::kiMaxVertexInputAttributeDescriptions);
				VkVertexInputAttributeDescription& rVkVertexInputAttributeDescription = pHeader->shaderHeader.pVkVertexInputAttributeDescriptions[iLocation];
				rVkVertexInputAttributeDescription.location = static_cast<uint32_t>(iLocation);
				rVkVertexInputAttributeDescription.binding = 0;
				rVkVertexInputAttributeDescription.format = spirType.vecsize == 2 ? VK_FORMAT_R32G32_SFLOAT : (spirType.vecsize == 3 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT);
				rVkVertexInputAttributeDescription.offset = static_cast<uint32_t>(pHeader->shaderHeader.iVertexInputStride);
				++pHeader->shaderHeader.iVertexInputAttributeDescriptions;

				LOG("       location {} binding {} format {} offset {}", rVkVertexInputAttributeDescription.location, rVkVertexInputAttributeDescription.binding, static_cast<int64_t>(rVkVertexInputAttributeDescription.format), rVkVertexInputAttributeDescription.offset);

				pHeader->shaderHeader.iVertexInputStride += spirType.vecsize * sizeof(float);
			}
		}
	}
	LOG("   Descriptions: {} Input stride: {}", pHeader->shaderHeader.iVertexInputAttributeDescriptions, pHeader->shaderHeader.iVertexInputStride);

	if (shaderResources.stage_outputs.size() > 0)
	{
		LOG("Stage outputs:");
		for (const spirv_cross::Resource& rResource : shaderResources.stage_outputs)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);
		}
	}

	if (shaderResources.uniform_buffers.size() > 0)
	{
		LOG("Uniform buffers:");
		for (const spirv_cross::Resource& rResource : shaderResources.uniform_buffers)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);

			WriteBinding(pHeader->shaderHeader, iBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, eShaderType);
			pHeader->shaderHeader.iDescriptorSetLayoutBindings = std::max(iBinding + 1, pHeader->shaderHeader.iDescriptorSetLayoutBindings);
		}
	}

	if (shaderResources.storage_buffers.size() > 0)
	{
		LOG("Storage buffers:");
		for (const spirv_cross::Resource& rResource : shaderResources.storage_buffers)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);

			const spirv_cross::SPIRType& spirType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!spirType.array.empty())
			{
				LOG("   Array size: {}", spirType.array[0]);
			}

			WriteBinding(pHeader->shaderHeader, iBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spirType.array.empty() ? 1 : spirType.array[0], eShaderType);
			pHeader->shaderHeader.iDescriptorSetLayoutBindings = std::max(iBinding + 1, pHeader->shaderHeader.iDescriptorSetLayoutBindings);
		}
	}

	if (shaderResources.sampled_images.size() > 0)
	{
		LOG("Sampled images:");
		for (const spirv_cross::Resource& rResource : shaderResources.sampled_images)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);

			const spirv_cross::SPIRType& spirType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!spirType.array.empty())
			{
				LOG("   Array size: {}", spirType.array[0]);
			}

			WriteBinding(pHeader->shaderHeader, iBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, spirType.array.empty() ? 1 : spirType.array[0], eShaderType);
			pHeader->shaderHeader.iDescriptorSetLayoutBindings = std::max(iBinding + 1, pHeader->shaderHeader.iDescriptorSetLayoutBindings);
		}
	}

	if (shaderResources.storage_images.size() > 0)
	{
		LOG("Storage images:");
		for (const spirv_cross::Resource& rResource : shaderResources.storage_images)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);

			const spirv_cross::SPIRType& spirType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!spirType.array.empty())
			{
				LOG("   Array size: {}", spirType.array[0]);
			}

			WriteBinding(pHeader->shaderHeader, iBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, spirType.array.empty() ? 1 : spirType.array[0], eShaderType);
			pHeader->shaderHeader.iDescriptorSetLayoutBindings = std::max(iBinding + 1, pHeader->shaderHeader.iDescriptorSetLayoutBindings);
		}
	}

	if (shaderResources.separate_images.size() > 0)
	{
		LOG("Seperate images:");
		for (const spirv_cross::Resource& rResource : shaderResources.separate_images)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);

			const spirv_cross::SPIRType& spirType = spirvCrossCompiler.get_type(rResource.type_id);
			if (!spirType.array.empty())
			{
				LOG("   Array size: {}", spirType.array[0]);
			}

			WriteBinding(pHeader->shaderHeader, iBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, spirType.array.empty() ? 1 : spirType.array[0], eShaderType);
			pHeader->shaderHeader.iDescriptorSetLayoutBindings = std::max(iBinding + 1, pHeader->shaderHeader.iDescriptorSetLayoutBindings);
		}
	}

	if (shaderResources.separate_samplers.size() > 0)
	{
		LOG("Seperate samplers:");
		for (const spirv_cross::Resource& rResource : shaderResources.separate_samplers)
		{
			int64_t iBinding = spirvCrossCompiler.get_decoration(rResource.id, spv::DecorationBinding);
			LOG("   {} {} {} bound at {}", (uint32_t)rResource.type_id, (uint32_t)rResource.base_type_id, rResource.name, iBinding);

			WriteBinding(pHeader->shaderHeader, iBinding, VK_DESCRIPTOR_TYPE_SAMPLER, 1, eShaderType);
			pHeader->shaderHeader.iDescriptorSetLayoutBindings = std::max(iBinding + 1, pHeader->shaderHeader.iDescriptorSetLayoutBindings);
		}
	}

	ASSERT(*reinterpret_cast<uint32_t*>(dataSpan.data()) == 0x07230203u);
}
