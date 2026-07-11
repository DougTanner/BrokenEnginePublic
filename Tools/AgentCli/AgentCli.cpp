// AgentCli — standalone engine command client and local workflow coordinator.
// Socket mode preserves the existing length-prefixed loopback protocol.

#include <winsock2.h>
#include <ws2tcpip.h>

#include "AgentCliCommon.h"
#include "BuildCommand.h"
#include "InstallCommand.h"
#include "LockCommands.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "tinygltf/json.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace agentcli
{
	namespace
	{
		constexpr uint32_t kuiMaxRequestBytes = 1u * 1024u * 1024u;
		constexpr uint32_t kuiMaxResponseBytes = 16u * 1024u * 1024u;
		constexpr int64_t kiConnectTimeoutMilliseconds = 2000;
		constexpr int64_t kiDefaultResponseTimeoutMilliseconds = 15000;

		void PrintUsage()
		{
			std::cerr << "Usage: AgentCli.exe [--owner TOKEN] --port N [--timeout-ms 15000] -\n";
			std::cerr << "       AgentCli.exe [--owner TOKEN] --port N [--timeout-ms 15000] \"<json>\"\n";
			std::cerr << "       AgentCli.exe lock <token|claim|status|release|steal> ...\n";
			std::cerr << "       AgentCli.exe build [--files <cpp...> --] <project-or-solution> <MSBuild args...>\n";
			std::cerr << "       AgentCli.exe install\n";
			std::cerr << "       AgentCli.exe --version\n";
		}

		std::string ReadAllStandardInput()
		{
			std::string input;
			char pBuffer[4096] {};
			size_t uiRead = 0;
			while ((uiRead = std::fread(pBuffer, 1, sizeof(pBuffer), stdin)) > 0)
			{
				input.append(pBuffer, uiRead);
			}
			return input;
		}

		bool SendAll(SOCKET socket, const char* pData, size_t uiLength)
		{
			size_t uiSent = 0;
			while (uiSent < uiLength)
			{
				int iChunk = ::send(socket, pData + uiSent, static_cast<int>(uiLength - uiSent), 0);
				if (iChunk <= 0)
				{
					return false;
				}
				uiSent += static_cast<size_t>(iChunk);
			}
			return true;
		}

		bool ReceiveAll(SOCKET socket, char* pData, size_t uiLength)
		{
			size_t uiReceived = 0;
			while (uiReceived < uiLength)
			{
				int iChunk = ::recv(socket, pData + uiReceived, static_cast<int>(uiLength - uiReceived), 0);
				if (iChunk <= 0)
				{
					return false;
				}
				uiReceived += static_cast<size_t>(iChunk);
			}
			return true;
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

		int RunSocketCommand(int iArgumentCount, wchar_t* pArgumentValues[])
		{
			int64_t iPort = 0;
			int64_t iTimeoutMilliseconds = kiDefaultResponseTimeoutMilliseconds;
			bool bReadStandardInput = false;
			std::wstring owner;
			std::string request;
			bool bHaveRequest = false;

			for (int i = 1; i < iArgumentCount; ++i)
			{
				std::wstring_view argument = pArgumentValues[i];
				if (argument == L"--port" || argument == L"--timeout-ms" || argument == L"--owner")
				{
					if (++i >= iArgumentCount)
					{
						Fail("socket option requires a value");
						PrintUsage();
						return kiExitFailure;
					}
					if (argument == L"--port")
					{
						iPort = std::wcstoll(pArgumentValues[i], nullptr, 10);
					}
					else if (argument == L"--timeout-ms")
					{
						iTimeoutMilliseconds = std::wcstoll(pArgumentValues[i], nullptr, 10);
					}
					else
					{
						owner = pArgumentValues[i];
					}
				}
				else if (argument == L"-")
				{
					bReadStandardInput = true;
				}
				else
				{
					request = WideToUtf8(argument);
					bHaveRequest = true;
				}
			}

			if (iPort <= 0 || iPort > 65535)
			{
				Fail("--port must be in the range 1..65535");
				PrintUsage();
				return kiExitFailure;
			}
			if (iTimeoutMilliseconds <= 0 || iTimeoutMilliseconds > 600000)
			{
				Fail("--timeout-ms must be in the range 1..600000");
				PrintUsage();
				return kiExitFailure;
			}
			if (bReadStandardInput)
			{
				request = ReadAllStandardInput();
				bHaveRequest = true;
			}
			if (!bHaveRequest || request.empty())
			{
				Fail("no request JSON provided");
				PrintUsage();
				return kiExitFailure;
			}
			if (request.size() > kuiMaxRequestBytes)
			{
				Fail("request exceeds 1 MiB");
				return kiExitFailure;
			}

			if (!owner.empty() && !RefreshHarnessHeartbeat(owner))
			{
				Fail("harness heartbeat refresh failed");
				return kiExitFailure;
			}
			WSADATA windowsSocketsData {};
			if (::WSAStartup(MAKEWORD(2, 2), &windowsSocketsData) != 0)
			{
				Fail("WSAStartup failed");
				return kiExitFailure;
			}
			SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (socket == INVALID_SOCKET)
			{
				Fail("socket creation failed");
				::WSACleanup();
				return kiExitFailure;
			}

			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_port = ::htons(static_cast<uint16_t>(iPort));
			::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
			int iResult = kiExitFailure;
			do
			{
				if (!ConnectWithTimeout(socket, address, kiConnectTimeoutMilliseconds))
				{
					Fail("connect to 127.0.0.1 failed or timed out");
					break;
				}
				DWORD uiTimeout = static_cast<DWORD>(iTimeoutMilliseconds);
				::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&uiTimeout), sizeof(uiTimeout));
				::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&uiTimeout), sizeof(uiTimeout));

				uint32_t uiPayloadLength = static_cast<uint32_t>(request.size());
				unsigned char pLengthPrefix[4] =
				{
					static_cast<unsigned char>(uiPayloadLength & 0xffu),
					static_cast<unsigned char>((uiPayloadLength >> 8) & 0xffu),
					static_cast<unsigned char>((uiPayloadLength >> 16) & 0xffu),
					static_cast<unsigned char>((uiPayloadLength >> 24) & 0xffu),
				};
				if (!SendAll(socket, reinterpret_cast<const char*>(pLengthPrefix), sizeof(pLengthPrefix)) || !SendAll(socket, request.data(), request.size()))
				{
					Fail("send failed");
					break;
				}

				unsigned char pResponseLengthPrefix[4] {};
				if (!ReceiveAll(socket, reinterpret_cast<char*>(pResponseLengthPrefix), sizeof(pResponseLengthPrefix)))
				{
					Fail("no response (timed out or peer closed)");
					break;
				}
				uint32_t uiResponseLength = static_cast<uint32_t>(pResponseLengthPrefix[0]) |
					(static_cast<uint32_t>(pResponseLengthPrefix[1]) << 8) |
					(static_cast<uint32_t>(pResponseLengthPrefix[2]) << 16) |
					(static_cast<uint32_t>(pResponseLengthPrefix[3]) << 24);
				if (uiResponseLength == 0 || uiResponseLength > kuiMaxResponseBytes)
				{
					Fail("response length out of range");
					break;
				}

				std::string response(uiResponseLength, '\0');
				if (!ReceiveAll(socket, response.data(), uiResponseLength))
				{
					Fail("incomplete response (timed out or peer closed)");
					break;
				}
				std::cout << response << '\n';
				try
				{
					nlohmann::json parsed = nlohmann::json::parse(response);
					if (parsed.contains("ok") && parsed["ok"].is_boolean())
					{
						iResult = parsed["ok"].get<bool>() ? kiExitOk : kiExitStateConflict;
					}
					else
					{
						Fail("response missing boolean \"ok\" field");
					}
				}
				catch (const std::exception& rException)
				{
					Fail(std::string("response is not valid JSON: ") + rException.what());
				}
			}
			while (false);

			::closesocket(socket);
			::WSACleanup();
			return iResult;
		}
	}
}

int wmain(int iArgumentCount, wchar_t* pArgumentValues[])
{
	if (iArgumentCount >= 2)
	{
		std::wstring_view mode = pArgumentValues[1];
		if (mode == L"--version")
		{
			if (iArgumentCount != 2)
			{
				agentcli::Fail("--version accepts no arguments");
				return agentcli::kiExitFailure;
			}
			std::cout << "2\n";
			return agentcli::kiExitOk;
		}
		if (mode == L"lock")
		{
			return agentcli::RunLockCommand(iArgumentCount, pArgumentValues);
		}
		if (mode == L"build")
		{
			return agentcli::RunBuildCommand(iArgumentCount, pArgumentValues);
		}
		if (mode == L"install")
		{
			return agentcli::RunInstallCommand(iArgumentCount);
		}
	}
	return agentcli::RunSocketCommand(iArgumentCount, pArgumentValues);
}
