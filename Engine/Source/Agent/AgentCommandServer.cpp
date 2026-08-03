#include "Pch.h"

#include "Agent/AgentCommandServer.h"

#include "Agent/AgentCommands.h"

namespace engine
{

// Bounds dormant agent-command connection latency to 20 wakes per second; dtor notification does not depend on this cadence.
static constexpr std::chrono::milliseconds kListenerRetryInterval = 50ms;
// A response that has been moved out of the shared slot is flushed as one frame during shutdown, but never longer
// than this deadline. The same 50 ms readiness cadence keeps stop/join bounded while a peer is not reading.
static constexpr std::chrono::seconds kResponseFlushTimeout = 3s;

AgentCommandServer::AgentCommandServer(int64_t iPort)
{
	// WSAStartup is guaranteed by NetworkManager (enet_initialize), constructed before this.

	// Loopback only — never bind a routable interface.
	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(static_cast<uint16_t>(iPort));

	// A rapid agent relaunch can find the previous listen port still in TIME_WAIT (Windows default ~120 s), which
	// fails bind() with WSAEADDRINUSE. SO_REUSEADDR lets the new listener rebind the recycling port immediately; it
	// is safe on this loopback channel because it already trusts every local process, so there is no foreign socket
	// that could steal the address. Accepted tradeoff: a duplicate launch on the same port also binds successfully
	// (dual listeners, nondeterministic connection routing) instead of failing fast with WSAEADDRINUSE — the harness
	// quit-and-wait-for-exact-PID relaunch rule is the guard against that misuse. If the port is still momentarily
	// held, fall back to a bounded blocking retry (startup thread, off the main loop) before failing fast.
	static constexpr int64_t kiMaxBindAttempts = 10;
	static constexpr DWORD kuiBindRetryMilliseconds = 250; // ~2.5 s worst case across the attempts
	for (int64_t iAttempt = 0; ; ++iAttempt)
	{
		mListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mListenSocket == INVALID_SOCKET)
		{
			LOG(kNetwork, kError, "AgentCommandServer socket creation failed: {}", WSAGetLastError());
			throw StartupException("agent socket creation failed");
		}

		BOOL bReuseAddress = TRUE;
		setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&bReuseAddress), sizeof(bReuseAddress));

		if (bind(mListenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR)
		{
			break;
		}

		int iBindError = WSAGetLastError();
		closesocket(mListenSocket); // a failed bind leaves the socket unusable; recreate (and re-arm SO_REUSEADDR) next attempt
		mListenSocket = INVALID_SOCKET;

		// Only a TIME_WAIT address collision is retryable; every other bind failure is a hard startup error.
		if (iBindError != WSAEADDRINUSE || iAttempt + 1 >= kiMaxBindAttempts)
		{
			LOG(kNetwork, kError, "AgentCommandServer bind to 127.0.0.1:{} failed: {}", iPort, iBindError);
			throw StartupException("agent bind failed");
		}

		if (iAttempt == 0)
		{
			LOG(kNetwork, kWarning, "AgentCommandServer bind to 127.0.0.1:{} in use (WSAEADDRINUSE), retrying up to {} attempts", iPort, kiMaxBindAttempts);
		}
		Sleep(kuiBindRetryMilliseconds);
	}

	if (listen(mListenSocket, 1) == SOCKET_ERROR) // backlog 1 — a single connection at a time
	{
		LOG(kNetwork, kError, "AgentCommandServer listen on 127.0.0.1:{} failed: {}", iPort, WSAGetLastError());
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
		throw StartupException("agent listen failed");
	}

	u_long uiNonBlocking = 1;
	if (ioctlsocket(mListenSocket, FIONBIO, &uiNonBlocking) == SOCKET_ERROR)
	{
		LOG(kNetwork, kError, "AgentCommandServer listener non-blocking configuration failed: {}", WSAGetLastError());
		closesocket(mListenSocket);
		mListenSocket = INVALID_SOCKET;
		throw StartupException("agent listener non-blocking configuration failed");
	}

	LOG(kNetwork, kInfo, "AgentCommandServer listening on 127.0.0.1:{}", iPort);

	mListenerThread = std::jthread([this](std::stop_token stopToken)
	{
		ListenerLoop(std::move(stopToken));
	});
}

AgentCommandServer::~AgentCommandServer()
{
	// Request stop, close only the listener under the lock, and wake a pending response wait. The listener owns the
	// active connection until ServeConnection exits, then performs its one final close before the jthread joins.
	{
		std::unique_lock lock(mMutex);
		mListenerThread.request_stop();
		if (mListenSocket != INVALID_SOCKET)
		{
			closesocket(mListenSocket);
			mListenSocket = INVALID_SOCKET;
		}
	}
	mResponseReady.notify_all();
}

void AgentCommandServer::ListenerLoop(std::stop_token stopToken)
{
	// Heap: agent listener thread — socket buffers and JSON parse, off the sim path. Established before the
	// ThreadLocal ctor so its buffer allocations are covered too, whatever the main-loop tracking state.
	ScopedSuppressAllocationTracking suppress;

	// Own ThreadLocal: MXCSR / exception handlers / log buffer for this thread. Small workbuffer — the JSON
	// parse and socket buffers live on the heap, off the sim path.
	common::ThreadLocal threadLocal(64 * 1024);

	while (true)
	{
		SOCKET clientSocket = INVALID_SOCKET;
		{
			std::unique_lock lock(mMutex);
			if (stopToken.stop_requested() || mListenSocket == INVALID_SOCKET)
			{
				break;
			}

			clientSocket = accept(mListenSocket, nullptr, nullptr);
			if (clientSocket == INVALID_SOCKET)
			{
				int iAcceptError = WSAGetLastError();
				if (iAcceptError == WSAEWOULDBLOCK)
				{
					mResponseReady.wait_for(lock, kListenerRetryInterval, [&stopToken]()
					{
						return stopToken.stop_requested();
					});
					continue;
				}
				if (stopToken.stop_requested())
				{
					break;
				}

				LOG(kNetwork, kError, "AgentCommandServer accept failed: {}", iAcceptError);
				break;
			}
		}

		u_long uiNonBlocking = 1;
		if (ioctlsocket(clientSocket, FIONBIO, &uiNonBlocking) == SOCKET_ERROR)
		{
			LOG(kNetwork, kError, "AgentCommandServer accepted socket non-blocking configuration failed: {}", WSAGetLastError());
			closesocket(clientSocket);
			continue;
		}

		{
			std::unique_lock lock(mMutex);
			if (stopToken.stop_requested())
			{
				closesocket(clientSocket);
				break;
			}
			mActiveSocket = clientSocket;
		}

		ServeConnection(clientSocket, stopToken);

		// ServeConnection has returned, so the listener exclusively owns the final active-socket close. Bump the
		// generation so any response still deferred from this connection is discarded
		// by Drain instead of landing in the next connection's stream (id desync / wedged deferred branch), and drop
		// any response the main thread published after the peer stopped waiting so it can't satisfy the next
		// connection's first request. mDeferredPoll itself is main-thread-only — never bare-written here.
		{
			std::unique_lock lock(mMutex);
			++muiConnectionGeneration;
			mPendingResponse.reset();
			if (mActiveSocket != INVALID_SOCKET)
			{
				closesocket(mActiveSocket);
				mActiveSocket = INVALID_SOCKET;
			}
		}
	}
}

void AgentCommandServer::ServeConnection(SOCKET clientSocket, const std::stop_token& rStopToken)
{
	while (!rStopToken.stop_requested())
	{
		// Read the 4-byte little-endian length prefix (x64 host is little-endian — use the bytes directly).
		uint32_t uiLength = 0;
		if (!ReadExact(clientSocket, reinterpret_cast<uint8_t*>(&uiLength), sizeof(uiLength), rStopToken))
		{
			return; // peer closed or socket error
		}

		if (uiLength > kuiMaxRequestBytes)
		{
			LOG(kNetwork, kWarning, "AgentCommandServer request frame too large ({} bytes), closing connection", uiLength);
			return;
		}

		std::string payload;
		payload.resize(uiLength);
		if (uiLength > 0 && !ReadExact(clientSocket, reinterpret_cast<uint8_t*>(payload.data()), static_cast<int64_t>(uiLength), rStopToken))
		{
			return;
		}

		PendingRequest request;
		try
		{
			request.request = nlohmann::json::parse(payload);
			request.bParsed = true;
		}
		catch (const std::exception&)
		{
			request.bParsed = false;
		}

		{
			std::unique_lock lock(mMutex);
			if (rStopToken.stop_requested())
			{
				return; // stop can race the completed read; do not publish a request after shutdown begins
			}
			mPendingRequest = std::move(request);
		}

		// Wait for the main thread's Drain() to publish the serialized response.
		std::string response;
		{
			std::unique_lock lock(mMutex);
			mResponseReady.wait(lock, [this, &rStopToken]()
			{
				return mPendingResponse.has_value() || rStopToken.stop_requested();
			});
			if (!mPendingResponse.has_value())
			{
				return; // stop requested during shutdown
			}
			response = std::move(*mPendingResponse);
			mPendingResponse.reset();
		}

		if (!SendFrame(clientSocket, response))
		{
			return;
		}
	}
}

void AgentCommandServer::Drain()
{
	// Heap: JSON request/response construction runs inside the tracked main loop
	ScopedSuppressAllocationTracking suppress;

	// An in-flight deferred response (async screenshot / dump capture) takes precedence: poll it before accepting a
	// new request. The originating request stays in flight (its listener thread still waits on mResponseReady) until
	// the poll yields a result, so no new request can arrive meanwhile (single connection, one request at a time).
	if (mDeferredPoll)
	{
		// Belt-and-suspenders shutdown guard: discard a deferred response whose connection generation no longer
		// matches the live connection. In practice the listener thread stays blocked in the mResponseReady cv wait
		// while its request is deferred, so connection teardown only runs after this deferral publishes or times out
		// — a mid-capture disconnect can't be observed here today. This branch would only fire on shutdown (or if
		// active disconnect detection is added later); publishing a stale response would land in the next
		// connection's stream (id desync), so drop it silently since no peer is waiting.
		bool bStaleConnection = false;
		{
			std::unique_lock lock(mMutex);
			bStaleConnection = muiDeferredGeneration != muiConnectionGeneration;
		}
		if (bStaleConnection)
		{
			mDeferredPoll = nullptr;
			mbResponseDeferred = false;
			return;
		}

		nlohmann::json response;
		response["id"] = mDeferredId;

		// Liveness timeout: a capture request lost to device-loss Graphics recreation (mailboxes wiped) never
		// resolves — bound the wait and publish a failure so the channel isn't deadlocked forever.
		if (++miDeferredDrainCount > kiDeferredTimeoutDrains)
		{
			mDeferredPoll = nullptr;
			response["ok"] = false;
			response["error"] = "capture timed out";
			PublishResponse(std::move(response));
			return;
		}

		try
		{
			common::ScopedExpectedThrows scopedExpectedThrows; // validation throws here are a designed error path — keep them off the VEH crash-diagnostic walk
			std::optional<nlohmann::json> result = mDeferredPoll();
			if (!result.has_value())
			{
				return; // still pending — do not accept a new request
			}
			response["ok"] = true;
			response["result"] = std::move(*result);
		}
		catch (const std::exception& rException)
		{
			response["ok"] = false;
			response["error"] = rException.what();
		}
		mDeferredPoll = nullptr;
		PublishResponse(std::move(response));
		return;
	}

	PendingRequest request;
	{
		std::unique_lock lock(mMutex);
		if (!mPendingRequest.has_value())
		{
			return;
		}
		request = std::move(*mPendingRequest);
		mPendingRequest.reset();
	}

	// Build the response envelope. Malformed JSON or a missing/invalid cmd answers with id:null; a handler
	// exception echoes the request id. The lock is never held across game-state work.
	nlohmann::json response;
	if (!request.bParsed)
	{
		response["id"] = nullptr;
		response["ok"] = false;
		response["error"] = "malformed JSON";
	}
	else if (!request.request.contains("cmd") || !request.request["cmd"].is_string())
	{
		response["id"] = nullptr;
		response["ok"] = false;
		response["error"] = "missing cmd";
	}
	else
	{
		response["id"] = request.request.contains("id") ? request.request["id"] : nlohmann::json(nullptr);

		// Reject an unknown top-level envelope key before dispatch (before mDeferredId is armed) so a typo'd field
		// can never be silently ignored. Only id/cmd/params are legitimate; the first offending key is reported.
		bool bUnknownKey = false;
		for (auto& [rKey, rValue] : request.request.items())
		{
			if (rKey != "cmd" && rKey != "params" && rKey != "id")
			{
				response["ok"] = false;
				response["error"] = "unknown envelope key '" + rKey + "'";
				bUnknownKey = true;
				break;
			}
		}
		if (!bUnknownKey)
		{
			std::string cmd = request.request["cmd"].get<std::string>();
			const nlohmann::json& rParams = request.request.contains("params") ? request.request["params"] : nlohmann::json::object();

			// A handler may call DeferResponse() to complete asynchronously; record the id it must echo first.
			mDeferredId = response["id"];
			mbResponseDeferred = false;
			try
			{
				common::ScopedExpectedThrows scopedExpectedThrows; // validation throws here are a designed error path — keep them off the VEH crash-diagnostic walk
				nlohmann::json result;
				game::ExecuteAgentCommand(cmd, rParams, result);
				if (mbResponseDeferred)
				{
					return; // response published later by the deferred-poll path above
				}
				response["ok"] = true;
				response["result"] = std::move(result);
			}
			catch (const std::exception& rException)
			{
				mDeferredPoll = nullptr; // a handler that deferred then threw does not leave a stale poll
				mbResponseDeferred = false;
				response["ok"] = false;
				response["error"] = rException.what();
			}
		}
	}

	PublishResponse(std::move(response));
}

void AgentCommandServer::DeferResponse(std::function<std::optional<nlohmann::json>()> poll)
{
	mDeferredPoll = std::move(poll);
	mbResponseDeferred = true;
	miDeferredDrainCount = 0;

	// Snapshot the current connection generation so a later disconnect (which bumps it) makes Drain discard this
	// deferred response instead of publishing it into a subsequent connection's stream.
	{
		std::unique_lock lock(mMutex);
		muiDeferredGeneration = muiConnectionGeneration;
	}
}

void AgentCommandServer::PublishResponse(nlohmann::json response)
{
	std::string responseString = response.dump();
	if (static_cast<int64_t>(responseString.size()) > kiMaxResponseBytes)
	{
		LOG(kNetwork, kError, "AgentCommandServer response exceeds cap ({} bytes), replacing with error", responseString.size());
		nlohmann::json capped;
		capped["id"] = response["id"];
		capped["ok"] = false;
		capped["error"] = "response too large";
		responseString = capped.dump();
	}

	{
		std::unique_lock lock(mMutex);
		mPendingResponse = std::move(responseString);
	}
	mResponseReady.notify_one();
}

bool AgentCommandServer::ReadExact(SOCKET clientSocket, uint8_t* pBuffer, int64_t iBytes, const std::stop_token& rStopToken)
{
	int64_t iTotal = 0;
	while (iTotal < iBytes)
	{
		if (rStopToken.stop_requested())
		{
			return false;
		}

		fd_set readSet {};
		FD_ZERO(&readSet);
		FD_SET(clientSocket, &readSet);
		timeval timeout {};
		timeout.tv_sec = static_cast<long>(kListenerRetryInterval.count() / 1000);
		timeout.tv_usec = static_cast<long>((kListenerRetryInterval.count() % 1000) * 1000);
		const int iReady = select(0, &readSet, nullptr, nullptr, &timeout);
		if (iReady == SOCKET_ERROR)
		{
			return false;
		}
		if (iReady == 0)
		{
			continue;
		}
		if (rStopToken.stop_requested())
		{
			return false;
		}

		const int iReceived = recv(clientSocket, reinterpret_cast<char*>(pBuffer + iTotal), static_cast<int>(iBytes - iTotal), 0);
		if (iReceived > 0)
		{
			iTotal += iReceived;
			continue;
		}
		if (iReceived == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		{
			continue;
		}
		return false; // 0 = peer closed; other errors end the connection
	}
	return true;
}

// ServeConnection moved the response out of the shared slot before calling SendFrame, so this frame must flush
// despite stop. rDeadline bounds the detached flush.
bool AgentCommandServer::SendExact(SOCKET clientSocket, const uint8_t* pBuffer, int64_t iBytes, const std::chrono::steady_clock::time_point& rDeadline)
{
	int64_t iTotal = 0;
	while (iTotal < iBytes)
	{
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if (now >= rDeadline)
		{
			return false;
		}

		std::chrono::microseconds waitDuration = std::chrono::duration_cast<std::chrono::microseconds>(rDeadline - now);
		const std::chrono::microseconds wakeDuration = std::chrono::duration_cast<std::chrono::microseconds>(kListenerRetryInterval);
		if (waitDuration > wakeDuration)
		{
			waitDuration = wakeDuration;
		}
		waitDuration = (std::max)(waitDuration, 1us);

		fd_set writeSet {};
		FD_ZERO(&writeSet);
		FD_SET(clientSocket, &writeSet);
		timeval timeout {};
		timeout.tv_sec = static_cast<long>(waitDuration.count() / 1'000'000);
		timeout.tv_usec = static_cast<long>(waitDuration.count() % 1'000'000);
		const int iReady = select(0, nullptr, &writeSet, nullptr, &timeout);
		if (iReady == SOCKET_ERROR)
		{
			return false;
		}
		if (iReady == 0)
		{
			continue;
		}

		const int iSent = send(clientSocket, reinterpret_cast<const char*>(pBuffer + iTotal), static_cast<int>(iBytes - iTotal), 0);
		if (iSent > 0)
		{
			iTotal += iSent;
			continue;
		}
		if (iSent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
		{
			continue;
		}
		return false;
	}
	return true;
}

bool AgentCommandServer::SendFrame(SOCKET clientSocket, const std::string& rPayload)
{
	uint32_t uiLength = static_cast<uint32_t>(rPayload.size());
	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kResponseFlushTimeout;
	if (!SendExact(clientSocket, reinterpret_cast<const uint8_t*>(&uiLength), sizeof(uiLength), deadline))
	{
		return false;
	}
	return SendExact(clientSocket, reinterpret_cast<const uint8_t*>(rPayload.data()), static_cast<int64_t>(rPayload.size()), deadline);
}

} // namespace engine
