#pragma once

namespace engine
{

struct ShaderInfo
{
	common::ChunkHeader* pChunkHeader = nullptr;
};

class Shader
{
public:

	Shader() = default;
	Shader(const ShaderInfo& rInfo, byte* pData);
	~Shader();

	void Create(const ShaderInfo& rInfo, byte* pData);
	void Destroy() noexcept;

	ShaderInfo mInfo;

	VkShaderModule mVkShaderModule = VK_NULL_HANDLE;
};

} // namespace engine
