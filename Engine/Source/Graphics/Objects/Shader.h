#pragma once

namespace engine
{

struct ShaderInfo
{
	common::ChunkHeader* pChunkHeader = nullptr;
	const VkDescriptorSetLayoutBinding* pDescriptorBindings = nullptr;
	const VkVertexInputAttributeDescription* pVertexAttributes = nullptr;
	int64_t iSpirvSize = 0;
};

class Shader
{
public:

	Shader() = default;
	Shader(const ShaderInfo& rInfo, std::byte* pData);
	~Shader();

	void Create(const ShaderInfo& rInfo, std::byte* pData);
	void Destroy() noexcept;

	ShaderInfo mInfo;

	VkShaderModule mVkShaderModule = VK_NULL_HANDLE;
};

} // namespace engine
