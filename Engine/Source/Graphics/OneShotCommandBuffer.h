#pragma once

namespace engine
{

class OneShotCommandBuffer
{
public:

	OneShotCommandBuffer();
	~OneShotCommandBuffer();

	void Execute(bool bWait);

	VkCommandBuffer mVkCommandBuffer = VK_NULL_HANDLE;
};

} // namespace engine
