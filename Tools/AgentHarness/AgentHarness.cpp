// AgentHarness — loopback client/server command transport and harness lock owner.

#include <winsock2.h>
#include <ws2tcpip.h>

#include "HarnessLockCommands.h"
#include "ToolCliCommon.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace toolcli
{
	namespace
	{
		constexpr uint32_t kuiMaxRequestBytes = 1u * 1024u * 1024u;
		constexpr uint32_t kuiMaxResponseBytes = 16u * 1024u * 1024u;
		constexpr int64_t kiConnectAttemptTimeoutMilliseconds = 500; // per connect try; retried until the --timeout-ms deadline
		constexpr int64_t kiConnectRetrySleepMilliseconds = 150; // brief pause between connect tries
		constexpr int64_t kiDefaultResponseTimeoutMilliseconds = 15000;
		constexpr int64_t kiHeartbeatIntervalMilliseconds = 60'000;
		constexpr int64_t kiReadinessWaitCapMilliseconds = 30'000;

		enum class SocketOperationResult
		{
			kSuccess,
			kTransportFailure,
			kOwnershipLoss,
			kReadinessFailure,
		};

		struct SocketCommandArguments
		{
			int64_t iPort = 0;
			int64_t iTimeoutMilliseconds = kiDefaultResponseTimeoutMilliseconds;
			std::wstring owner;
			std::string inlineRequest;
			bool bReadStandardInput = false;
		};

		class ScopedWindowsSockets
		{
		public:
			ScopedWindowsSockets() = default;
			~ScopedWindowsSockets()
			{
				if (mbInitialized)
				{
					::WSACleanup();
				}
			}

			ScopedWindowsSockets(const ScopedWindowsSockets&) = delete;
			ScopedWindowsSockets& operator=(const ScopedWindowsSockets&) = delete;

			bool Initialize()
			{
				WSADATA data {};
				mbInitialized = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
				return mbInitialized;
			}

		private:
			bool mbInitialized = false;
		};

		class ScopedSocket
		{
		public:
			ScopedSocket() = default;
			~ScopedSocket()
			{
				Reset();
			}

			ScopedSocket(const ScopedSocket&) = delete;
			ScopedSocket& operator=(const ScopedSocket&) = delete;

			bool Create()
			{
				Reset();
				mSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				return mSocket != INVALID_SOCKET;
			}

			[[nodiscard]] SOCKET Get() const
			{
				return mSocket;
			}

			void Reset()
			{
				if (mSocket != INVALID_SOCKET)
				{
					::closesocket(mSocket);
					mSocket = INVALID_SOCKET;
				}
			}

		private:
			SOCKET mSocket = INVALID_SOCKET;
		};

		void PrintUsage(std::ostream& rOutput)
		{
			rOutput << "Usage: AgentHarness.exe --owner TOKEN --port N [--timeout-ms 15000] -\n";
			rOutput << "       AgentHarness.exe --owner TOKEN --port N [--timeout-ms 15000] \"<json>\"\n";
			rOutput << "       AgentHarness.exe lock <token|claim|status|release|steal|heartbeat> ...\n";
			rOutput << "       AgentHarness.exe --help\n";
		}

		bool ReadAllStandardInput(std::string& rInput)
		{
			char pBuffer[4096] {};
			size_t uiRead = 0;
			while ((uiRead = std::fread(pBuffer, 1, sizeof(pBuffer), stdin)) > 0)
			{
				if (rInput.size() > kuiMaxRequestBytes || uiRead > kuiMaxRequestBytes - rInput.size())
				{
					Fail("request exceeds 1 MiB");
					return false;
				}
				rInput.append(pBuffer, uiRead);
			}
			if (std::ferror(stdin))
			{
				Fail("standard input read failed");
				return false;
			}
			return true;
		}

		bool RefreshHeartbeatIfDue(const std::wstring& rOwner, std::chrono::steady_clock::time_point& rNextHeartbeatDue)
		{
			if (std::chrono::steady_clock::now() < rNextHeartbeatDue)
			{
				return true;
			}
			if (!RefreshHarnessHeartbeat(rOwner))
			{
				return false;
			}
			rNextHeartbeatDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(kiHeartbeatIntervalMilliseconds);
			return true;
		}

		SocketOperationResult CheckDeadlineAndRefreshHeartbeatAfterNoProgress(const std::chrono::steady_clock::time_point& rOperationDeadline, const std::wstring& rOwner, std::chrono::steady_clock::time_point& rNextHeartbeatDue)
		{
			if (std::chrono::steady_clock::now() >= rOperationDeadline)
			{
				return SocketOperationResult::kTransportFailure;
			}
			if (!RefreshHeartbeatIfDue(rOwner, rNextHeartbeatDue))
			{
				return SocketOperationResult::kOwnershipLoss;
			}
			return SocketOperationResult::kSuccess;
		}

		SocketOperationResult WaitForSocketReadiness(SOCKET socket, bool bWrite, const std::chrono::steady_clock::time_point& rOperationDeadline, const std::wstring& rOwner, std::chrono::steady_clock::time_point& rNextHeartbeatDue)
		{
			for (;;)
			{
				const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
				if (now >= rOperationDeadline)
				{
					return SocketOperationResult::kTransportFailure;
				}
				if (now >= rNextHeartbeatDue)
				{
					if (!RefreshHeartbeatIfDue(rOwner, rNextHeartbeatDue))
					{
						return SocketOperationResult::kOwnershipLoss;
					}
					continue;
				}

				std::chrono::milliseconds waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(rOperationDeadline - now);
				const std::chrono::milliseconds heartbeatDuration = std::chrono::duration_cast<std::chrono::milliseconds>(rNextHeartbeatDue - now);
				if (heartbeatDuration < waitDuration)
				{
					waitDuration = heartbeatDuration;
				}
				if (waitDuration > std::chrono::milliseconds(kiReadinessWaitCapMilliseconds))
				{
					waitDuration = std::chrono::milliseconds(kiReadinessWaitCapMilliseconds);
				}

				fd_set readSet {};
				FD_ZERO(&readSet);
				fd_set writeSet {};
				FD_ZERO(&writeSet);
				if (bWrite)
				{
					FD_SET(socket, &writeSet);
				}
				else
				{
					FD_SET(socket, &readSet);
				}
				timeval timeout {};
				timeout.tv_sec = static_cast<long>(waitDuration.count() / 1000);
				timeout.tv_usec = static_cast<long>((waitDuration.count() % 1000) * 1000);
				const int iReady = ::select(0, bWrite ? nullptr : &readSet, bWrite ? &writeSet : nullptr, nullptr, &timeout);
				if (iReady == SOCKET_ERROR)
				{
					return SocketOperationResult::kReadinessFailure;
				}
				if (iReady > 0)
				{
					const std::chrono::steady_clock::time_point readyTime = std::chrono::steady_clock::now();
					if (readyTime >= rOperationDeadline)
					{
						return SocketOperationResult::kTransportFailure;
					}
					if (readyTime >= rNextHeartbeatDue)
					{
						if (!RefreshHeartbeatIfDue(rOwner, rNextHeartbeatDue))
						{
							return SocketOperationResult::kOwnershipLoss;
						}
						continue;
					}
					return SocketOperationResult::kSuccess;
				}
			}
		}

		SocketOperationResult SendAll(SOCKET socket, const char* pData, size_t uiLength, int64_t iTimeoutMilliseconds, const std::wstring& rOwner, std::chrono::steady_clock::time_point& rNextHeartbeatDue)
		{
			size_t uiSent = 0;
			std::chrono::steady_clock::time_point operationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(iTimeoutMilliseconds);
			while (uiSent < uiLength)
			{
				SocketOperationResult eWaitResult = WaitForSocketReadiness(socket, true, operationDeadline, rOwner, rNextHeartbeatDue);
				if (eWaitResult != SocketOperationResult::kSuccess)
				{
					return eWaitResult;
				}
				const int iChunk = ::send(socket, pData + uiSent, static_cast<int>(uiLength - uiSent), 0);
				const int iSocketError = iChunk == SOCKET_ERROR ? ::WSAGetLastError() : 0;
				if (iChunk > 0)
				{
					uiSent += static_cast<size_t>(iChunk);
					operationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(iTimeoutMilliseconds);
					if (!RefreshHeartbeatIfDue(rOwner, rNextHeartbeatDue))
					{
						return SocketOperationResult::kOwnershipLoss;
					}
					continue;
				}
				SocketOperationResult ePostOperationResult = CheckDeadlineAndRefreshHeartbeatAfterNoProgress(operationDeadline, rOwner, rNextHeartbeatDue);
				if (ePostOperationResult != SocketOperationResult::kSuccess)
				{
					return ePostOperationResult;
				}
				if (iChunk == SOCKET_ERROR && iSocketError == WSAEWOULDBLOCK)
				{
					continue;
				}
				return SocketOperationResult::kTransportFailure;
			}
			return SocketOperationResult::kSuccess;
		}

		SocketOperationResult ReceiveAll(SOCKET socket, char* pData, size_t uiLength, int64_t iTimeoutMilliseconds, const std::wstring& rOwner, std::chrono::steady_clock::time_point& rNextHeartbeatDue)
		{
			size_t uiReceived = 0;
			std::chrono::steady_clock::time_point operationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(iTimeoutMilliseconds);
			while (uiReceived < uiLength)
			{
				SocketOperationResult eWaitResult = WaitForSocketReadiness(socket, false, operationDeadline, rOwner, rNextHeartbeatDue);
				if (eWaitResult != SocketOperationResult::kSuccess)
				{
					return eWaitResult;
				}
				const int iChunk = ::recv(socket, pData + uiReceived, static_cast<int>(uiLength - uiReceived), 0);
				const int iSocketError = iChunk == SOCKET_ERROR ? ::WSAGetLastError() : 0;
				if (iChunk > 0)
				{
					uiReceived += static_cast<size_t>(iChunk);
					operationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(iTimeoutMilliseconds);
					if (!RefreshHeartbeatIfDue(rOwner, rNextHeartbeatDue))
					{
						return SocketOperationResult::kOwnershipLoss;
					}
					continue;
				}
				SocketOperationResult ePostOperationResult = CheckDeadlineAndRefreshHeartbeatAfterNoProgress(operationDeadline, rOwner, rNextHeartbeatDue);
				if (ePostOperationResult != SocketOperationResult::kSuccess)
				{
					return ePostOperationResult;
				}
				if (iChunk == SOCKET_ERROR && iSocketError == WSAEWOULDBLOCK)
				{
					continue;
				}
				return SocketOperationResult::kTransportFailure;
			}
			return SocketOperationResult::kSuccess;
		}

		void FailSocketOperation(SocketOperationResult eResult, std::string_view transportFailure)
		{
			if (eResult == SocketOperationResult::kOwnershipLoss)
			{
				Fail("harness heartbeat refresh failed");
			}
			else if (eResult == SocketOperationResult::kReadinessFailure)
			{
				Fail("socket readiness wait failed");
			}
			else
			{
				Fail(transportFailure);
			}
		}

		bool ConnectWithTimeout(SOCKET socket, const sockaddr_in& rAddress, int64_t iTimeoutMilliseconds)
		{
			u_long uiNonBlocking = 1;
			if (::ioctlsocket(socket, FIONBIO, &uiNonBlocking) != 0)
			{
				return false;
			}

			int iResult = ::connect(socket, reinterpret_cast<const sockaddr*>(&rAddress), sizeof(rAddress));
			if (iResult != 0)
			{
				if (::WSAGetLastError() != WSAEWOULDBLOCK)
				{
					return false;
				}
				fd_set writeSet;
				FD_ZERO(&writeSet);
				FD_SET(socket, &writeSet);
				fd_set errorSet;
				FD_ZERO(&errorSet);
				FD_SET(socket, &errorSet);
				timeval timeout {};
				timeout.tv_sec = static_cast<long>(iTimeoutMilliseconds / 1000);
				timeout.tv_usec = static_cast<long>((iTimeoutMilliseconds % 1000) * 1000);
				int iReady = ::select(0, nullptr, &writeSet, &errorSet, &timeout);
				if (iReady <= 0 || FD_ISSET(socket, &errorSet))
				{
					return false;
				}
				int iSocketError = 0;
				int iOptionLength = sizeof(iSocketError);
				if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&iSocketError), &iOptionLength) != 0 || iSocketError != 0)
				{
					return false;
				}
			}

			u_long uiBlocking = 0;
			return ::ioctlsocket(socket, FIONBIO, &uiBlocking) == 0;
		}

		bool ParseSocketCommandArguments(int iArgumentCount, wchar_t* pArgumentValues[], SocketCommandArguments& rArguments)
		{
			bool bHaveRequestSource = false;

			for (int i = 1; i < iArgumentCount; ++i)
			{
				std::wstring_view argument = pArgumentValues[i];
				if (argument == L"--port" || argument == L"--timeout-ms" || argument == L"--owner")
				{
					if (++i >= iArgumentCount)
					{
						Fail("socket option requires a value");
						PrintUsage(std::cerr);
						return false;
					}
					if (argument == L"--port")
					{
						wchar_t* pEnd = nullptr;
						rArguments.iPort = std::wcstoll(pArgumentValues[i], &pEnd, 10);
						if (pEnd == pArgumentValues[i] || *pEnd != L'\0')
						{
							Fail("--port must be in the range 1..65535");
							PrintUsage(std::cerr);
							return false;
						}
					}
					else if (argument == L"--timeout-ms")
					{
						wchar_t* pEnd = nullptr;
						rArguments.iTimeoutMilliseconds = std::wcstoll(pArgumentValues[i], &pEnd, 10);
						if (pEnd == pArgumentValues[i] || *pEnd != L'\0')
						{
							Fail("--timeout-ms must be in the range 1..600000");
							PrintUsage(std::cerr);
							return false;
						}
					}
					else
					{
						rArguments.owner = pArgumentValues[i];
					}
				}
				else if (argument == L"-")
				{
					if (bHaveRequestSource)
					{
						Fail("exactly one request JSON source is required");
						PrintUsage(std::cerr);
						return false;
					}
					rArguments.bReadStandardInput = true;
					bHaveRequestSource = true;
				}
				else
				{
					if (bHaveRequestSource)
					{
						Fail("exactly one request JSON source is required");
						PrintUsage(std::cerr);
						return false;
					}
					rArguments.inlineRequest = WideToUtf8(argument);
					bHaveRequestSource = true;
				}
			}

			if (rArguments.iPort <= 0 || rArguments.iPort > 65535)
			{
				Fail("--port must be in the range 1..65535");
				PrintUsage(std::cerr);
				return false;
			}
			if (rArguments.iTimeoutMilliseconds <= 0 || rArguments.iTimeoutMilliseconds > 600000)
			{
				Fail("--timeout-ms must be in the range 1..600000");
				PrintUsage(std::cerr);
				return false;
			}
			if (rArguments.owner.empty())
			{
				Fail("--owner is required");
				PrintUsage(std::cerr);
				return false;
			}
			if (!bHaveRequestSource)
			{
				Fail("no request JSON provided");
				PrintUsage(std::cerr);
				return false;
			}
			return true;
		}

		std::optional<std::string> ExchangeFramedSocketCommand(ScopedSocket& rSocket, const SocketCommandArguments& rArguments, const std::string& rRequest, std::chrono::steady_clock::time_point& rNextHeartbeatDue)
		{
			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_port = ::htons(static_cast<uint16_t>(rArguments.iPort));
			::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

			// Retry the connect until the overall --timeout-ms deadline so a peer whose listen socket is not yet
			// bound (freshly launched) is tolerated. Each try gets its own short timeout, and a failed non-blocking
			// connect leaves the socket unusable, so it is closed and recreated every attempt. A dead port therefore
			// consumes the full budget by design.
			const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
			const std::chrono::steady_clock::time_point deadline = startTime + std::chrono::milliseconds(rArguments.iTimeoutMilliseconds);
			bool bConnected = false;
			bool bSocketCreateFailed = false;
			bool bHeartbeatFailed = false;
			for (;;)
			{
				if (std::chrono::steady_clock::now() >= deadline)
				{
					break;
				}
				if (!RefreshHeartbeatIfDue(rArguments.owner, rNextHeartbeatDue))
				{
					bHeartbeatFailed = true;
					break;
				}
				if (std::chrono::steady_clock::now() >= deadline)
				{
					break;
				}
				if (!rSocket.Create())
				{
					bSocketCreateFailed = true;
					break;
				}
				if (ConnectWithTimeout(rSocket.Get(), address, kiConnectAttemptTimeoutMilliseconds))
				{
					bConnected = true;
					break;
				}
				rSocket.Reset();
				if (std::chrono::steady_clock::now() >= deadline)
				{
					break;
				}
				if (!RefreshHeartbeatIfDue(rArguments.owner, rNextHeartbeatDue))
				{
					bHeartbeatFailed = true;
					break;
				}
				std::chrono::milliseconds retrySleep(kiConnectRetrySleepMilliseconds);
				const std::chrono::milliseconds heartbeatSleep = std::chrono::duration_cast<std::chrono::milliseconds>(rNextHeartbeatDue - std::chrono::steady_clock::now());
				if (heartbeatSleep < retrySleep)
				{
					retrySleep = heartbeatSleep;
				}
				if (retrySleep.count() > 0)
				{
					std::this_thread::sleep_for(retrySleep);
				}
			}
			if (!bConnected)
			{
				if (bHeartbeatFailed)
				{
					Fail("harness heartbeat refresh failed");
				}
				else if (bSocketCreateFailed)
				{
					Fail("socket creation failed");
				}
				else
				{
					int64_t iElapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
					Fail("connect to 127.0.0.1 failed or timed out after " + std::to_string(iElapsedMilliseconds) + " ms");
				}
				return std::nullopt;
			}
			u_long uiNonBlocking = 1;
			if (::ioctlsocket(rSocket.Get(), FIONBIO, &uiNonBlocking) != 0)
			{
				Fail("could not configure connected socket for non-blocking I/O");
				return std::nullopt;
			}

			uint32_t uiPayloadLength = static_cast<uint32_t>(rRequest.size());
			unsigned char pLengthPrefix[4] =
			{
				static_cast<unsigned char>(uiPayloadLength & 0xffu),
				static_cast<unsigned char>((uiPayloadLength >> 8) & 0xffu),
				static_cast<unsigned char>((uiPayloadLength >> 16) & 0xffu),
				static_cast<unsigned char>((uiPayloadLength >> 24) & 0xffu),
			};
			SocketOperationResult eSendResult = SendAll(rSocket.Get(), reinterpret_cast<const char*>(pLengthPrefix), sizeof(pLengthPrefix), rArguments.iTimeoutMilliseconds, rArguments.owner, rNextHeartbeatDue);
			if (eSendResult == SocketOperationResult::kSuccess)
			{
				eSendResult = SendAll(rSocket.Get(), rRequest.data(), rRequest.size(), rArguments.iTimeoutMilliseconds, rArguments.owner, rNextHeartbeatDue);
			}
			if (eSendResult != SocketOperationResult::kSuccess)
			{
				FailSocketOperation(eSendResult, "send failed");
				return std::nullopt;
			}

			unsigned char pResponseLengthPrefix[4] {};
			SocketOperationResult eReceiveResult = ReceiveAll(rSocket.Get(), reinterpret_cast<char*>(pResponseLengthPrefix), sizeof(pResponseLengthPrefix), rArguments.iTimeoutMilliseconds, rArguments.owner, rNextHeartbeatDue);
			if (eReceiveResult != SocketOperationResult::kSuccess)
			{
				FailSocketOperation(eReceiveResult, "no response (timed out or peer closed)");
				return std::nullopt;
			}
			uint32_t uiResponseLength = static_cast<uint32_t>(pResponseLengthPrefix[0]) |
				(static_cast<uint32_t>(pResponseLengthPrefix[1]) << 8) |
				(static_cast<uint32_t>(pResponseLengthPrefix[2]) << 16) |
				(static_cast<uint32_t>(pResponseLengthPrefix[3]) << 24);
			if (uiResponseLength == 0 || uiResponseLength > kuiMaxResponseBytes)
			{
				Fail("response length out of range");
				return std::nullopt;
			}

			std::string response(uiResponseLength, '\0');
			eReceiveResult = ReceiveAll(rSocket.Get(), response.data(), uiResponseLength, rArguments.iTimeoutMilliseconds, rArguments.owner, rNextHeartbeatDue);
			if (eReceiveResult != SocketOperationResult::kSuccess)
			{
				FailSocketOperation(eReceiveResult, "incomplete response (timed out or peer closed)");
				return std::nullopt;
			}
			return response;
		}

		int PrintResponseAndInterpretExitCode(const std::string& rResponse)
		{
			std::cout << rResponse << '\n';
			try
			{
				nlohmann::json parsed = nlohmann::json::parse(rResponse);
				if (parsed.contains("ok") && parsed["ok"].is_boolean())
				{
					return parsed["ok"].get<bool>() ? kiExitOk : kiExitStateConflict;
				}
				Fail("response missing boolean \"ok\" field");
			}
			catch (const std::exception& rException)
			{
				Fail(std::string("response is not valid JSON: ") + rException.what());
			}
			return kiExitFailure;
		}

		int RunSocketCommand(int iArgumentCount, wchar_t* pArgumentValues[])
		{
			try
			{
				SocketCommandArguments arguments {};
				if (!ParseSocketCommandArguments(iArgumentCount, pArgumentValues, arguments))
				{
					return kiExitFailure;
				}
				std::string request = arguments.inlineRequest;
				if (arguments.bReadStandardInput && !ReadAllStandardInput(request))
				{
					return kiExitFailure;
				}
				if (request.empty())
				{
					Fail("no request JSON provided");
					PrintUsage(std::cerr);
					return kiExitFailure;
				}
				if (request.size() > kuiMaxRequestBytes)
				{
					Fail("request exceeds 1 MiB");
					return kiExitFailure;
				}
				if (!RefreshHarnessHeartbeat(arguments.owner))
				{
					Fail("harness heartbeat refresh failed");
					return kiExitFailure;
				}
				std::chrono::steady_clock::time_point nextHeartbeatDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(kiHeartbeatIntervalMilliseconds);

				ScopedWindowsSockets windowsSockets;
				if (!windowsSockets.Initialize())
				{
					Fail("WSAStartup failed");
					return kiExitFailure;
				}
				ScopedSocket socket;
				std::optional<std::string> response = ExchangeFramedSocketCommand(socket, arguments, request, nextHeartbeatDue);
				if (!response)
				{
					return kiExitFailure;
				}
				if (!RefreshHarnessHeartbeat(arguments.owner))
				{
					Fail("harness heartbeat refresh failed");
					return kiExitFailure;
				}
				return PrintResponseAndInterpretExitCode(*response);
			}
			catch (const std::exception&)
			{
				Fail("socket command failed");
				return kiExitFailure;
			}
			catch (...)
			{
				Fail("socket command failed with an unknown exception");
				return kiExitFailure;
			}
		}
	}
}

int wmain(int iArgumentCount, wchar_t* pArgumentValues[])
{
	toolcli::SetToolName("AgentHarness");
	if (iArgumentCount >= 2)
	{
		std::wstring_view mode = pArgumentValues[1];
		if (mode == L"--help")
		{
			if (iArgumentCount != 2)
			{
				toolcli::Fail("--help accepts no arguments");
				return toolcli::kiExitFailure;
			}
			toolcli::PrintUsage(std::cout);
			return toolcli::kiExitOk;
		}
		if (mode == L"lock")
		{
			return toolcli::RunHarnessLockCommand(iArgumentCount, pArgumentValues);
		}
	}
	return toolcli::RunSocketCommand(iArgumentCount, pArgumentValues);
}
