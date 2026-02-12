#include "Shader.h"

#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/DeviceManager.h"

namespace engine
{

Shader::Shader(const ShaderInfo& rInfo, std::byte* pData)
{
	Create(rInfo, pData);
}

Shader::~Shader()
{
	Destroy();
}

#pragma warning(push, 0)
#pragma warning(disable : 26461)

void Shader::Create(const ShaderInfo& rInfo, std::byte* pData)
{
	mInfo = rInfo;

	VkShaderModuleCreateInfo vkShaderModuleCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = static_cast<size_t>(mInfo.iSpirvSize),
		.pCode = reinterpret_cast<uint32_t*>(pData),
	};

	ASSERT(*reinterpret_cast<uint32_t*>(pData) == 0x07230203u);

	CHECK_VK(vkCreateShaderModule(gpDeviceManager->mVkDevice, &vkShaderModuleCreateInfo, nullptr, &mVkShaderModule));
	VkName(VK_OBJECT_TYPE_SHADER_MODULE, mVkShaderModule, mInfo.pChunkHeader->pcPath);
}

#pragma warning(pop)

void Shader::Destroy() noexcept
{
	if (mVkShaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(gpDeviceManager->mVkDevice, mVkShaderModule, nullptr);
		mVkShaderModule = VK_NULL_HANDLE;
	}
}

} // namespace engine
