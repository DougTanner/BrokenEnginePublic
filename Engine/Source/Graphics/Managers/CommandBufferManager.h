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

	void RecordCommandBuffers();
	void RecordCommandBuffer(int64_t iFramebuffer);
	void RecordGlobalCommandBuffer(int64_t iFramebuffer);
	void RecordMainCommandBuffer(int64_t iFramebuffer);

	void SubmitGlobalCommandBuffer(int64_t iFramebufferIndex);
	void SubmitMainCommandBuffer(int64_t iFramebufferIndex, bool bSignalFence);
	void SubmitUiCommandBuffer(int64_t iFramebufferIndex);

	std::vector<CommandBuffers> mPerFramebufferCommandBuffers;

	std::future<void> mSubmitGlobal;
	std::future<void> mSubmitMain;

	bool mbSaveScreenshot = false;
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine
