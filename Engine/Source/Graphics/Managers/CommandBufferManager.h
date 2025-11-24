#pragma once

#include "Graphics/Objects/CommandBuffers.h"
#include <functional>

namespace engine
{

struct RenderFrame;

struct SecondaryBufferSpec
{
	VkRenderPass vkRenderPass;
	VkFramebuffer vkFramebuffer;
	VkCommandBuffer* pSecondaryBuffers;
	std::function<void(int64_t, VkCommandBuffer)> recordCallback;
};

class CommandBufferManager
{
public:

	CommandBufferManager();
	~CommandBufferManager();

	int64_t CommandBufferCount();
	int64_t CommandBufferIndex(int64_t iFramebufferIndex);

	void RecordCommandBuffers();

	void SubmitGlobalCommandBuffer();
	void SubmitImageCommandBuffer();

	std::vector<CommandBuffers> mPerFramebufferCommandBuffers;

#if defined(ENABLE_RENDER_THREAD)
	std::future<void> mSubmitGlobal;
	std::future<void> mSubmitImage;
#endif

#if defined(ENABLE_SCREENSHOTS)
	bool mbSaveScreenshot = false;
#endif

private:

	void RecordCommandBuffer(int64_t iFramebuffer);
	void RecordSecondary(int64_t iFramebuffer, int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, const std::function<void(int64_t, VkCommandBuffer)>& recordCallback);

	std::vector<SecondaryBufferSpec> mLightingSecondarySpecs;
	std::vector<SecondaryBufferSpec> mSceneSecondarySpecs;
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine
