#pragma once

#if defined(BT_CLIENT)

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

#endif // defined(BT_CLIENT)
