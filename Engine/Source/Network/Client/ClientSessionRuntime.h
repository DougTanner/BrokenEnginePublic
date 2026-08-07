#pragma once

#if defined(BT_CLIENT)

#include "Frame/GridCoord.h"
#include "Network/Client/Client.h"

namespace game
{

class ClientSession;

} // namespace game

namespace engine
{

class NetworkDiscoveryScanner;

enum class ClientSessionStateFlags : uint8_t
{
	kServerDiscovered      = 1 << 0,
	kDiscoveryScanTimedOut = 1 << 1,
	kNoFreeSlotLogged      = 1 << 2,
};

class ClientSessionRuntime
{
public:

	explicit ClientSessionRuntime(game::ClientSession& rSession);
	~ClientSessionRuntime();

	void Connect(std::string_view serverAddress, uint16_t uiPort, int64_t iCoordSlots);
	void ConnectToDiscoveredServer(uint16_t uiPort, int64_t iCoordSlots);
	void Disconnect();
	void StartDiscovery();
	void PollDiscovery();
	void PollAndDrain(const NetworkTimeState& rTimeState);
	// Flush is transport-private and runtime-owned; this surfaces it so the game session can make a rare
	// user command depart immediately instead of waiting for the next tick-cadence ack flush.
	void FlushOutgoing();

	void SetDesiredCoords(const GridCoord* pDesiredCoords, int64_t iDesiredCount, std::string_view reason, int64_t iTick);
	void SynchronizeSubscriptions();
	void ClearSubscriptionState();

	std::chrono::nanoseconds EvaluateClock(int64_t iPreReconcileTick);
	void ResetClock();

	game::ClientSession& mrSession;
	std::unique_ptr<Client> mpClient;
	std::unique_ptr<NetworkDiscoveryScanner> mpDiscoveryScanner;
	common::Flags<ClientSessionStateFlags> mStateFlags;
	char mcDiscoveredAddress[16] {};
	int64_t miLatestServerTick = -1;
	int64_t miClockError = 0;
	int64_t miClockOffset = 0;
	int64_t miClockTargetBehind = 0;
	int64_t miCurrentTargetBehind = 0;
	std::vector<GridCoord> mDesiredCoords;
	std::vector<GridCoord> mSubscriptionQueue;
	std::unordered_map<GridCoord, std::chrono::steady_clock::time_point> mUnwantedTimestamps;

private:

	void ResetForConnect();
	void ResetForServerLoad();
	void UnsubscribeStaleCoords(const GridCoord* pDesiredCoords, int64_t iDesiredCount);
	void BuildSubscriptionQueue(const GridCoord* pDesiredCoords, int64_t iDesiredCount);
	void TrySubscribeNext();
	void SendAckAndFlush();

	int64_t miLastLoggedClockTargetBehind = -1;
	int64_t miLastPeriodicClockLogTick = -1;
	int64_t miLastClockErrorLogTick = -1;
	// Sim tick the current "computed target is lower" streak started on; -1 when no streak is active.
	int64_t miLowerTargetBehindStreakStartTick = -1;
	// Sim tick the previous streak evaluation observed; -1 when no tick has been observed yet.
	int64_t miLastEvaluateClockTick = -1;
	static constexpr std::chrono::seconds kStickySubscriptionDuration {2};
};

} // namespace engine

#endif // BT_CLIENT
