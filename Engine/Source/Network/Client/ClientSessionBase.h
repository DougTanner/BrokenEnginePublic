#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class Client;
class NetworkDiscoveryScanner;

class ClientSessionBase
{
public:

	ClientSessionBase() = default;
	virtual ~ClientSessionBase() = default;

	// Connection lifecycle
	void ConnectToServer(std::string_view serverAddress, uint16_t uiPort, int64_t iCoordSlots);
	void DisconnectFromServerBase();
	void StartServerDiscovery();
	void PollLANDiscovery();

	// Subscription mechanics
	void TrySubscribeNext();
	void UnsubscribeStaleCoords(std::span<const GridCoord> desiredCoords);
	void BuildSubscriptionQueue(std::span<const GridCoord> desiredCoords);

	// Update buffering
	bool ApplyReceivedUpdatesBase();

	// Clock correction
	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick, std::chrono::nanoseconds tickNs);

	// Queries
	int64_t GetConfirmedTick() const;
	int64_t GetClientConfirmedTick() const;
	int64_t GetServerUpdateBufferSize() const;
	int64_t GetTargetSimTick() const { return (miLatestServerTick < 0) ? -1 : (miLatestServerTick - miCurrentTargetBehind); }

	std::unique_ptr<Client> mpClientNetwork;
	std::unique_ptr<NetworkDiscoveryScanner> mpDiscoveryScanner;

	bool mbServerDiscovered = false;
	bool mbDiscoveryScanTimedOut = false;
	char mcDiscoveredAddress[16] {};

	int64_t miLatestServerTick = -1;
	int64_t miClockError = 0;
	int64_t miClockOffset = 0;
	int64_t miClockTargetBehind = 0;
	int64_t miCurrentTargetBehind = 0;
	int64_t miLastLoggedClockTargetBehind = -1;
	int64_t miLastPeriodicClockLogTick = -1;
	bool mbClockErrorDisconnect = false;
	int64_t miConsecutiveClockErrorFrames = 0;
	int64_t miLastClockErrorLogTick = -1;
	int64_t miCoordSlots = 0;
	std::vector<GridCoord> mSubscriptionQueue;
	bool mbNoFreeSlotLogged = false;
};

} // namespace engine

#endif // BT_CLIENT
