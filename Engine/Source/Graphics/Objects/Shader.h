#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct ShaderInfo
{
	common::ChunkHeader* pChunkHeader = nullptr;
	const VkDescriptorSetLayoutBinding* pDescriptorBindings = nullptr;
	const uint32_t* pDescriptorSetIndices = nullptr;
	const VkVertexInputAttributeDescription* pVertexAttributes = nullptr;
	int64_t iSpirvSize = 0;
};

class Shader
{
public:

	Shader() = default;
	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;
	Shader(const ShaderInfo& rInfo, const std::byte* pData);
	~Shader();

	void Create(const ShaderInfo& rInfo, const std::byte* pData);
	void Destroy() noexcept;

	ShaderInfo mInfo;

	VkShaderModule mVkShaderModule = VK_NULL_HANDLE;
};

} // namespace engine

#endif // defined(BT_CLIENT)
