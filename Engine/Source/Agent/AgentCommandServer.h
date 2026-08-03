#pragma once

namespace engine
{

// Loopback-only (127.0.0.1) TCP JSON command channel embedded in both executables. Transport only — knows no
// commands; the listener thread reads length-framed request frames, parses them, and hands each to the main
// thread via Drain(), which dispatches to game::ExecuteAgentCommand. Dormant unless --agent-port is passed.
//
// Framing (both directions): 4-byte little-endian uint32 payload length, then UTF-8 JSON.
// Envelope: request {"id"?, "cmd", "params"?} -> success {"id","ok":true,"result"} / failure {"id","ok":false,"error"}.
// An unknown top-level request key (anything but id/cmd/params) is rejected before dispatch as a failure envelope.
class AgentCommandServer
{
public:

	// Thrown by the ctor when socket/bind/listen fails so Main fails fast (return 0) instead of leaving an
	// uncontrollable agent-launched process. The ctor logs the concrete WSA error before throwing. An
	// address-in-use bind (a prior listener still in TIME_WAIT after a rapid relaunch) first sets SO_REUSEADDR
	// and then blocks through a bounded retry (~2.5 s worst case) before giving up — safe because the ctor runs
	// on the startup thread (sole caller Main.cpp), not the main loop. SO_REUSEADDR also lets a duplicate launch
	// on the same port bind alongside a live listener instead of failing; the harness relaunch rule (quit, wait
	// for the exact PID) is the guard.
	struct StartupException : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	explicit AgentCommandServer(int64_t iPort);
	~AgentCommandServer();

	AgentCommandServer(const AgentCommandServer&) = delete;
	AgentCommandServer& operator=(const AgentCommandServer&) = delete;

	// Main-thread, once per frame: if a request is pending, execute it (unlocked) and publish the response.
	void Drain();

	// Called from a command handler during Drain() to complete asynchronously: Drain() withholds this request's
	// response and instead polls `poll` at the top of every later Drain(); the first non-empty result is published
	// as {"id",ok:true,"result"} echoing the deferred request's id, and a poll exception as the failure envelope.
	// Only one request is ever in flight, so there is a single deferred slot. Main-thread only.
	void DeferResponse(std::function<std::optional<nlohmann::json>()> poll);

	// Deferred-poll liveness bound (~30 s at 60 fps): a capture lost to a device-loss Graphics recreation (mailboxes
	// wiped) never resolves, so cap the wait and publish a failure rather than deadlock the channel forever.
	static constexpr int64_t kiDeferredTimeoutDrains = 1800;

private:

	// A request parsed off the listener thread and handed to the main thread. bParsed == false means the frame
	// was not valid JSON (Drain answers with an id:null error).
	struct PendingRequest
	{
		nlohmann::json request;
		bool bParsed = false;
	};

	void ListenerLoop(std::stop_token stopToken);
	void ServeConnection(SOCKET clientSocket, const std::stop_token& rStopToken);

	// Serialize, cap, and hand a response envelope to the listener thread (stores mPendingResponse + notifies).
	void PublishResponse(nlohmann::json response);

	static bool ReadExact(SOCKET clientSocket, uint8_t* pBuffer, int64_t iBytes, const std::stop_token& rStopToken);
	static bool SendExact(SOCKET clientSocket, const uint8_t* pBuffer, int64_t iBytes, const std::chrono::steady_clock::time_point& rDeadline);
	static bool SendFrame(SOCKET clientSocket, const std::string& rPayload);

	static constexpr uint32_t kuiMaxRequestBytes = 1u * 1024u * 1024u; // 1 MiB — larger request frames are rejected
	static constexpr int64_t kiMaxResponseBytes = 16ll * 1024ll * 1024ll; // 16 MiB response cap

	SOCKET mListenSocket = INVALID_SOCKET;
	SOCKET mActiveSocket = INVALID_SOCKET; // current connection; final close is owned by ListenerLoop after I/O exits

	std::mutex mMutex;
	std::condition_variable mResponseReady;
	std::optional<PendingRequest> mPendingRequest; // listener -> main
	std::optional<std::string> mPendingResponse; // main -> listener
	uint64_t muiConnectionGeneration = 0; // mMutex-guarded; bumped on each connection teardown

	// Deferred-response state — touched only on the main thread (Drain and the handlers it calls), so no lock.
	std::function<std::optional<nlohmann::json>()> mDeferredPoll; // set = a response is deferred, polled each Drain
	nlohmann::json mDeferredId; // id echoed when the deferred poll completes
	bool mbResponseDeferred = false; // set by DeferResponse within the current Drain dispatch
	uint64_t muiDeferredGeneration = 0; // muiConnectionGeneration snapshot when the response was deferred
	int64_t miDeferredDrainCount = 0; // Drains elapsed since the deferral (liveness timeout)

	std::jthread mListenerThread; // last member: its body reads every other member via `this`
};

inline AgentCommandServer* gpAgentCommandServer = nullptr;

// Requests a clean shutdown of the main loop (sets sbQuit). Defined in Main.cpp; called by the quit command.
void RequestQuit();

} // namespace engine
