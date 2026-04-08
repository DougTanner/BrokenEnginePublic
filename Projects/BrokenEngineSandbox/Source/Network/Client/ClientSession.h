#pragma once

#if defined(BT_CLIENT)

#include "Network/Client/ClientSessionBase.h"
#include "Network/Client/ClientDataReceiver.h"
#include "Network/Client/ClientDesyncManager.h"
#include "Network/Client/ClientReconciler.h"

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
	int64_t GetDesyncTick() const { return mpDesyncManager->GetDesyncTick(); }
	bool IsStalled() const { return mpDesyncManager->IsStalled(); }
	bool CanSend() const { return mpClientNetwork != nullptr && mpClientNetwork->IsConnected() && mpClientNetwork->GetServerPeer() != nullptr; }

	// Clock correction
	std::chrono::nanoseconds ComputeClockCorrectionNs(int64_t iPreReconcileTick);

	// Game packet sends
	void SendUpdatePlayerRequest(int64_t iGlobalPlayerId, bool bUseMissiles, float fNavigationDelay);
	void SendCreateFleetRequest();
	void SendSpawnIntoFleetRequest(int64_t iFleetIndex);
	void SendRespawnInFleetRequest(int64_t iFleetIndex, int64_t iMemberIndex);

	// Subscriptions
	void UpdateDesiredCoords(std::string_view reason);
	void ClearStickySubscriptions() { mUnwantedTimestamps.clear(); }
	void ClearSubscriptionState();

	// Managers
	std::unique_ptr<ClientDataReceiver> mpDataReceiver;
	std::unique_ptr<ClientDesyncManager> mpDesyncManager;
	std::unique_ptr<ClientReconciler> mpReconciler;

private:

	// Connection helpers
	bool PollConnection();
	bool PollConnectionStatus();
	void TryEnterGame();
	void ResetForServerLoad();

	// Subscription tracking
	std::vector<engine::GridCoord> mDesiredCoords;
	std::unordered_map<engine::GridCoord, std::chrono::steady_clock::time_point> mUnwantedTimestamps;
	static constexpr std::chrono::seconds kStickySubscriptionDuration {2};

};

inline ClientSession* gpClientSession = nullptr;

} // namespace game

#endif // BT_CLIENT
