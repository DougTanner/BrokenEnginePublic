#pragma once

#include "Graphics/Objects/CommandBuffers.h"

namespace engine
{

struct RenderFrame;

class CommandBufferManager
{
public:

	CommandBufferManager();
	~CommandBufferManager();

	int64_t CommandBufferCount();
	int64_t CommandBufferIndex(int64_t iFramebufferIndex);

	void RecordCommandBuffer(int64_t iFramebuffer);
	void RecordAllCommandBuffers();

	void SubmitGlobalCommandBuffer();
	void SubmitMainCommandBuffer();
	void SubmitImageCommandBuffer();

	std::vector<CommandBuffers> mPerFramebufferCommandBuffers;

#if defined(ENABLE_RENDER_THREAD)
	std::future<void> mSubmitGlobal;
	std::future<void> mSubmitMain;
	std::future<void> mSubmitImage;
#endif

#if defined(ENABLE_SCREENSHOTS)
	common::Timer mScreenshotTimer;
	bool mbSaveScreenshots = false;
#endif
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine
