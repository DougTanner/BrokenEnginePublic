#pragma once

#if defined(BT_CLIENT)

#include "Frame/GridCoord.h"

namespace engine
{

class Client;
class NetworkDiscoveryScanner;

enum class SessionStateFlags : uint8_t
{
	kServerDiscovered      = 1 << 0,
	kDiscoveryScanTimedOut = 1 << 1,
	kClockErrorDisconnect  = 1 << 2,
	kNoFreeSlotLogged      = 1 << 3,
};

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
	int64_t GetSimTickCeiling() const { return (miLatestServerTick < 0) ? -1 : (miLatestServerTick - miCurrentTargetBehind + kiSimCeilingSlackTicks); }

	std::unique_ptr<Client> mpClientNetwork;
	std::unique_ptr<NetworkDiscoveryScanner> mpDiscoveryScanner;

	common::Flags<SessionStateFlags> mSessionFlags;
	char mcDiscoveredAddress[16] {};

	int64_t miLatestServerTick = -1;
	int64_t miClockError = 0;
	int64_t miClockOffset = 0;
	int64_t miClockTargetBehind = 0;
	int64_t miCurrentTargetBehind = 0;
	int64_t miLastLoggedClockTargetBehind = -1;
	int64_t miLastPeriodicClockLogTick = -1;
	int64_t miConsecutiveClockErrorFrames = 0;
	int64_t miLastClockErrorLogTick = -1;
	int64_t miCoordSlots = 0;
	std::vector<GridCoord> mSubscriptionQueue;
};

} // namespace engine

#endif // BT_CLIENT
