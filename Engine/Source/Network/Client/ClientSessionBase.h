#pragma once

#if defined(BT_CLIENT)

namespace game
{
struct Frame;
}

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
	bool PollLANDiscovery();

	// Subscription mechanics
	void TrySubscribeNext();
	void UnsubscribeStaleCoords(const std::vector<GridCoord>& rDesiredCoords);
	void BuildSubscriptionQueue(const std::vector<GridCoord>& rDesiredCoords);

	// Update buffering
	bool ApplyReceivedUpdatesBase();

	// Clock correction
	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick, std::chrono::nanoseconds tickNs);

	// Extrapolation
	bool IsExtrapolating() const;
	void PrepareExtrapolationTick(const std::vector<GridCoord>& rActiveCoords);
	void BuildExtrapolationFrameRef(const GridCoord& rCoord, game::Frame*& rpNext, game::Frame*& rpCurrent);
	void RecordExtrapolationSnapshot(const std::vector<GridCoord>& rActiveCoords, int64_t iTick);
	game::Frame* GetSnapshotFrame(GridCoord coord) const;

	// Queries
	int64_t GetConfirmedTick() const;
	int64_t GetServerUpdateBufferSize() const;

	std::unique_ptr<Client> mpClientNetwork;
	std::unique_ptr<NetworkDiscoveryScanner> mpDiscoveryScanner;

	int64_t miLatestServerTick = -1;
	int64_t miClockError = 0;
	int64_t miClockOffset = 0;
	int64_t miClockTargetBehind = 0;
	bool mbClockErrorDisconnect = false;
	int64_t miCoordSlots = 0;
	std::vector<GridCoord> mSubscriptionQueue;
};

} // namespace engine

#endif // BT_CLIENT
