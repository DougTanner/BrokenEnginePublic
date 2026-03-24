#pragma once

#if defined(BT_CLIENT)

#include "Network/Client/ClientSessionBase.h"
#include "Network/ClientReconciler.h"

namespace game
{

struct Frame;

class ClientSession : public engine::ClientSessionBase
{
public:

	ClientSession();
	~ClientSession() override;

	// Connection
	void ConnectToServer(std::string_view serverAddress);
	void ConnectToDiscoveredServer();
	void DisconnectFromServer();

	// Main-loop integration
	void PollNetwork();
	void Poll();
	void Reconcile();
	void PostRender();

	// Reconciliation
	void WaitForReconcile();
	void TryKickReconcile();

	// Subscriptions
	void UpdateSubscriptions();

	// Queries
	int64_t GetDesyncTick() const { return mDesyncDebugState.iTick; }
	bool IsStalled() const { return mDesyncDebugState.iTick >= 0; }

	// Clock correction
	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick);

private:

	// Connection helpers
	bool PollConnection();
	bool PollConnectionStatus();
	void TryEnterGame();
	void PollDebugFrameResponse();

	// Subscription helpers
	void UpdateDesiredCoords(std::string_view reason);

	void ApplyReceivedFullStates();
	void ApplyReceivedUpdates();

	struct DesyncDebugState
	{
		int64_t iTick = -1;
		engine::GridCoord coord {};
		std::unique_ptr<Frame> pClientFrame;
		std::chrono::steady_clock::time_point entryTime {};
	};
	DesyncDebugState mDesyncDebugState;

	static constexpr std::chrono::seconds kDesyncDebugTimeout {5};

	// Desync frequency tracking for escalation
	int64_t miDesyncCount = 0;
	std::chrono::steady_clock::time_point mFirstDesyncTime {};
	static constexpr int64_t kiMaxDesyncsBeforeDisconnect = 3;
	static constexpr std::chrono::seconds kDesyncWindowDuration {10};

	void CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, int64_t iTick, engine::GridCoord coord);
	void RecoverFromDesync();
	void ResetCoordStatesForResync();
	void ResetForServerLoad();

	// Subscription tracking
	std::vector<engine::GridCoord> mDesiredCoords;
	std::unordered_map<engine::GridCoord, std::chrono::steady_clock::time_point> mUnwantedTimestamps;
	static constexpr std::chrono::seconds kStickySubscriptionDuration {2};

	// Reconciliation
	std::unique_ptr<ClientReconciler> mpReconciler;
};

inline ClientSession* gpClientSession = nullptr;

} // namespace game

#endif // BT_CLIENT
