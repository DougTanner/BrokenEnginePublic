#pragma once

#if defined(BT_CLIENT)

#include "Frame/GridCoord.h"

#include "Network/Client/ClientDesyncManager.h"
#include "Network/Client/ClientReconciler.h"

namespace engine
{

class Client;
class ClientSessionRuntime;
struct ReceivedDebugFrame;

} // namespace engine

namespace game
{

struct FleetGuid;
struct Frame;
struct ReceivedPlayerEvent;
enum class GamePacketType : uint8_t;

enum class SubscriptionChangeReason : uint8_t
{
	kAssigned,
	kSpawned,
	kChangedFrame,
	kDied,
	kFleetSync,
	kPollTick,
	kFocusNextFleet,
	kFocusPrevFleet,
	kSelectPlayer,
};

const char* ToString(SubscriptionChangeReason eReason);

class ClientSession
{
public:

	ClientSession();
	~ClientSession();

	// Connection
	void ConnectToServer(std::string_view serverAddress);

	// Main-loop integration
	void Poll();
	void Reconcile();

	// Subscriptions
	void UpdateSubscriptions();

	// Update buffering
	void ApplyReceivedStaticData();
	void ApplyReceivedFullStates();
	bool ApplyReceivedUpdates();

	// Queries
	int64_t GetConfirmedTick() const;
	int64_t GetClientConfirmedTick() const;
	int64_t GetServerUpdateBufferSize() const;

	// Clock correction
	// Game packet sends
	void SendUpdatePlayerRequest(int64_t iGlobalPlayerId, bool bUseMissiles, float fNavigationDelay);
	void SendCreateFleetRequest();
	void SendSpawnIntoFleetRequest(const FleetGuid& rFleetGuid);
	void SendRespawnInFleetRequest(const FleetGuid& rFleetGuid, int64_t iMemberIndex);
	void SendDeleteFleetRequest(const FleetGuid& rFleetGuid);
	void SendFleetNavigationDelayRequest(const FleetGuid& rFleetGuid, float fDelay);

	// Subscriptions
	void UpdateDesiredCoords(SubscriptionChangeReason eReason);

	// Managers
	std::unique_ptr<ClientDesyncManager> mpDesyncManager;
	std::unique_ptr<ClientReconciler> mpReconciler;
	std::unique_ptr<engine::ClientSessionRuntime> mpRuntime;

private:
	friend class engine::ClientSessionRuntime;

	void OnConnectionRejected(const char* pcReason);
	void OnConnectionFailed();
	void OnConnectionAccepted();
	void PollDesyncState();
	void OnConnectionLost();
	void OnServerLoad();
	void OnRuntimeDisconnected();
	void ProcessReceivedGamePackets();
	void OnCoordReleased(engine::GridCoord coord);

	// Game packet helpers
	void ApplyPlayerEvent(const ReceivedPlayerEvent& rEvent);
	void UpdatePlayerCoord(engine::global_id_t globalPlayerId, engine::GridCoord coord);

	template <typename TLogFunction, typename... TArgs>
	void SendGameRequest(GamePacketType ePacketType, const TLogFunction& rLogFunction, const TArgs&... rArgs);

};

inline ClientSession* gpClientSession = nullptr;

} // namespace game

#endif // BT_CLIENT
