#pragma once

#include "Graphics/Objects/CommandBuffers.h"

namespace engine
{

struct RenderFrame;

class CommandBufferManager
{
public:

	static void RecordSecondaryBegin(VkCommandBuffer vkSecondaryCommandBuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer);
	static void RecordSecondaryEnd(VkCommandBuffer vkSecondaryCommandBuffer);

	CommandBufferManager();
	~CommandBufferManager();

	VkCommandBuffer AllocateSecondaryBuffer(int64_t iFramebuffer, const char* pcName);

	int64_t CommandBufferCount();
	int64_t CommandBufferIndex(int64_t iFramebufferIndex);

	void RecordCommandBuffers();
	void RecordCommandBuffer(int64_t iFramebuffer);
	void RecordGlobalCommandBuffer(int64_t iFramebuffer);
	void RecordImageCommandBuffer(int64_t iFramebuffer);

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
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine
