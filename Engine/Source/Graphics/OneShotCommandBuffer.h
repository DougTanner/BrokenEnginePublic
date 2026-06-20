#pragma once

namespace engine
{

class OneShotCommandBuffer
{
public:

	OneShotCommandBuffer();
	~OneShotCommandBuffer();

	void Execute();

	VkCommandBuffer mVkCommandBuffer = VK_NULL_HANDLE;
};

} // namespace engine
