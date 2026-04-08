#pragma once

#if defined(BT_CLIENT)

namespace game
{

class ClientDataReceiver
{
public:

	void ApplyReceivedStaticData();
	void ApplyReceivedFullStates();
	void ApplyReceivedUpdates();
};

} // namespace game

#endif // BT_CLIENT
