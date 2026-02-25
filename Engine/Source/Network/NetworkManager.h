#pragma once

namespace engine
{

class NetworkManager
{
public:

	NetworkManager();
	~NetworkManager();

	static constexpr uint8_t kuiChannelReliable = 0;
	static constexpr uint8_t kuiChannelUnreliable = 1;
	static constexpr uint8_t kuiChannelCount = 2;
};

inline NetworkManager* gpNetworkManager = nullptr;

} // namespace engine
