#pragma once

namespace engine
{

inline constexpr float kfMinDepth = 0.0f;
inline constexpr float kfMaxDepth = 1.0f;

enum class TextureFlags : uint64_t
{
	kMultisampling = 0x01,
	kRenderPass    = 0x02,
	kDepth         = 0x04,
	kHostVisible   = 0x08,
};
using TextureFlags_t = common::Flags<TextureFlags>;

enum class TextureLayout
{
	kUndefined,

	kColorAttachment,
	kComputeRead,
	kComputeWrite,
	kGeneral,
	kShaderReadOnly,
	kTransferDestination,
};

struct TextureInfo
{
	TextureFlags_t textureFlags;
	std::string_view pcName;

	// VkImageCreateInfo
	VkImageCreateFlags flags = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent3D extent {};
	uint32_t mipLevels = 1;
	uint32_t arrayLayers = 1;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageUsageFlags usage = 0;

	// VkImageViewCreateInfo
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	VkImageAspectFlags aspectMask = 0;

	// vkCreateRenderPass
	VkAttachmentLoadOp renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	VkImageLayout renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// vkCmdBeginRenderPass
	VkClearColorValue renderPassVkClearColorValue = {};

	// TransitionImageLayout
	TextureLayout eTextureLayout = TextureLayout::kShaderReadOnly;
};

class Texture
{
public:

	static void RecordBeginRenderPass(VkCommandBuffer vkCommandBuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, VkExtent2D vkExtent2D, VkClearColorValue vkClearColorValue, bool bDepth, bool bMultisampling, bool bClear);
	static void RecordEndRenderPass(VkCommandBuffer vkCommandBuffer, VkRenderPass vkRenderPass);

	Texture() = default;
	Texture(const Texture&) = default;
	Texture(Texture&&) noexcept = default;
	Texture(const TextureInfo& rInfo, std::function<void(void*,int64_t,int64_t)> dataFunction = nullptr);
	~Texture();

	void Create(const TextureInfo& rInfo, std::function<void(void*, int64_t, int64_t)> dataFunction = nullptr);
	void ReCreate();
	void Destroy() noexcept;

	void TransitionImageLayout(VkCommandBuffer vkCommandBuffer, TextureLayout eOldLayout, TextureLayout eNewLayout);
	void RecordBeginRenderPass(VkCommandBuffer vkCommandBuffer);
	void RecordEndRenderPass(VkCommandBuffer vkCommandBuffer);

	TextureInfo mInfo {};

	// Image
	VkDeviceMemory mVkDeviceMemory = VK_NULL_HANDLE;
	VkImage mVkImage = VK_NULL_HANDLE;
	VkImageView mVkImageView = VK_NULL_HANDLE;

	// Render target
	std::unique_ptr<Texture> mpDepthTexture;
	VkRenderPass mVkRenderPass = VK_NULL_HANDLE;
	VkFramebuffer mVkFramebuffer = VK_NULL_HANDLE;
};

} // namespace engine
