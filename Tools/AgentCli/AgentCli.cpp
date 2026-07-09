// AgentCli — standalone loopback bridge to the in-process engine::AgentCommandServer.
//
// Sends one JSON request frame (4-byte little-endian uint32 payload length + UTF-8 JSON)
// to 127.0.0.1:<port>, reads one response frame, prints the raw JSON response to stdout.
//
// Usage: AgentCli.exe --port <N> [--timeout-ms 15000] -           (request JSON on stdin — primary)
//        AgentCli.exe --port <N> [--timeout-ms 15000] "<json>"    (request JSON as last argument)
//
// Exit codes: 0 = response parsed and "ok":true; 2 = response parsed and "ok":false;
//             1 = transport/usage failure (message to stderr).
//
// No PCH, no engine/Common dependency. Plain Winsock (ws2_32.lib) + tinygltf/json.hpp
// (used only to read the response's "ok" field; the request passes through verbatim).

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "tinygltf/json.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace
{
	constexpr uint32_t kuiMaxRequestBytes = 1u * 1024u * 1024u;   // 1 MiB — matches AgentCommandServer's request cap; larger requests are rejected server-side.
	constexpr uint32_t kuiMaxResponseBytes = 16u * 1024u * 1024u; // 16 MiB sanity cap.
	constexpr int64_t kiConnectTimeoutMs = 2000;
	constexpr int64_t kiDefaultResponseTimeoutMs = 15000;

	// Exit codes.
	constexpr int kiExitOk = 0;      // Response parsed, "ok":true.
	constexpr int kiExitOkFalse = 2; // Response parsed, "ok":false.
	constexpr int kiExitFailure = 1; // Transport / usage failure.

	void Fail(std::string_view message)
	{
		std::cerr << "AgentCli: " << message << '\n';
	}

	void PrintUsage()
	{
		std::cerr << "Usage: AgentCli.exe --port <N> [--timeout-ms 15000] -            (request JSON on stdin)\n";
		std::cerr << "       AgentCli.exe --port <N> [--timeout-ms 15000] \"<json>\"    (request JSON as last argument)\n";
	}

	std::string ReadAllStdin()
	{
		std::string input;
		char pBuffer[4096] = {};
		size_t uiRead = 0;
		while ((uiRead = std::fread(pBuffer, 1, sizeof(pBuffer), stdin)) > 0)
		{
			input.append(pBuffer, uiRead);
		}

		return input;
	}

	// Blocking send of the whole buffer (SO_SNDTIMEO bounds each call).
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

	// Blocking receive of exactly uiLength bytes (SO_RCVTIMEO bounds each call).
	bool RecvAll(SOCKET socket, char* pData, size_t uiLength)
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

	// Non-blocking connect with a select() timeout, then switch back to blocking mode.
	bool ConnectWithTimeout(SOCKET socket, const sockaddr_in& rAddress, int64_t iTimeoutMs)
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

			timeval timeout = {};
			timeout.tv_sec = static_cast<long>(iTimeoutMs / 1000);
			timeout.tv_usec = static_cast<long>((iTimeoutMs % 1000) * 1000);

			int iReady = ::select(0, nullptr, &writeSet, &errorSet, &timeout);
			if (iReady <= 0 || FD_ISSET(socket, &errorSet))
			{
				return false; // Timed out or connection error.
			}

			int iSocketError = 0;
			int iOptionLength = sizeof(iSocketError);
			if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&iSocketError), &iOptionLength) != 0 || iSocketError != 0)
			{
				return false;
			}
		}

		u_long uiBlocking = 0;
		if (::ioctlsocket(socket, FIONBIO, &uiBlocking) != 0)
		{
			return false;
		}

		return true;
	}
}

int main(int iArgumentCount, char* pArgumentValues[])
{
	// Trust boundary: parse and validate every command-line argument.
	int64_t iPort = 0;
	int64_t iTimeoutMs = kiDefaultResponseTimeoutMs;
	bool bReadStdin = false;
	std::string request;
	bool bHaveRequest = false;

	for (int i = 1; i < iArgumentCount; ++i)
	{
		std::string_view argument = pArgumentValues[i];
		if (argument == "--port")
		{
			if (i + 1 >= iArgumentCount)
			{
				Fail("--port requires a value");
				PrintUsage();
				return kiExitFailure;
			}

			iPort = std::atoll(pArgumentValues[++i]);
		}
		else if (argument == "--timeout-ms")
		{
			if (i + 1 >= iArgumentCount)
			{
				Fail("--timeout-ms requires a value");
				PrintUsage();
				return kiExitFailure;
			}

			iTimeoutMs = std::atoll(pArgumentValues[++i]);
		}
		else if (argument == "-")
		{
			bReadStdin = true;
		}
		else
		{
			request = argument;
			bHaveRequest = true;
		}
	}

	if (iPort <= 0 || iPort > 65535)
	{
		Fail("--port must be in the range 1..65535");
		PrintUsage();
		return kiExitFailure;
	}

	if (iTimeoutMs <= 0 || iTimeoutMs > 600000)
	{
		// Upper bound guards SO_RCVTIMEO/SO_SNDTIMEO (DWORD ms): a value that is a nonzero multiple of 2^32
		// truncates to 0, which Winsock reads as an infinite (never-timing-out) block.
		Fail("--timeout-ms must be in the range 1..600000");
		PrintUsage();
		return kiExitFailure;
	}

	if (bReadStdin)
	{
		request = ReadAllStdin();
		bHaveRequest = true;
	}

	if (!bHaveRequest || request.empty())
	{
		Fail("no request JSON provided (pass '-' for stdin or the JSON as the last argument)");
		PrintUsage();
		return kiExitFailure;
	}

	if (request.size() > kuiMaxRequestBytes)
	{
		Fail("request exceeds 1 MiB (server request cap)");
		return kiExitFailure;
	}

	WSADATA wsaData = {};
	if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
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

	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = ::htons(static_cast<uint16_t>(iPort));
	::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

	int iResult = kiExitFailure;
	do
	{
		if (!ConnectWithTimeout(socket, address, kiConnectTimeoutMs))
		{
			Fail("connect to 127.0.0.1 failed or timed out");
			break;
		}

		// Bound each blocking send/recv with the response timeout.
		DWORD uiTimeout = static_cast<DWORD>(iTimeoutMs);
		::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&uiTimeout), sizeof(uiTimeout));
		::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&uiTimeout), sizeof(uiTimeout));

		// Frame: 4-byte little-endian payload length + UTF-8 JSON.
		uint32_t uiPayloadLength = static_cast<uint32_t>(request.size());
		unsigned char pLengthPrefix[4] =
		{
			static_cast<unsigned char>(uiPayloadLength & 0xFFu),
			static_cast<unsigned char>((uiPayloadLength >> 8) & 0xFFu),
			static_cast<unsigned char>((uiPayloadLength >> 16) & 0xFFu),
			static_cast<unsigned char>((uiPayloadLength >> 24) & 0xFFu),
		};

		if (!SendAll(socket, reinterpret_cast<const char*>(pLengthPrefix), sizeof(pLengthPrefix)) || !SendAll(socket, request.data(), request.size()))
		{
			Fail("send failed");
			break;
		}

		// Read the response frame length prefix.
		unsigned char pResponseLengthPrefix[4] = {};
		if (!RecvAll(socket, reinterpret_cast<char*>(pResponseLengthPrefix), sizeof(pResponseLengthPrefix)))
		{
			Fail("no response (timed out or peer closed)");
			break;
		}

		uint32_t uiResponseLength = static_cast<uint32_t>(pResponseLengthPrefix[0]) | (static_cast<uint32_t>(pResponseLengthPrefix[1]) << 8) | (static_cast<uint32_t>(pResponseLengthPrefix[2]) << 16) | (static_cast<uint32_t>(pResponseLengthPrefix[3]) << 24);

		if (uiResponseLength == 0 || uiResponseLength > kuiMaxResponseBytes)
		{
			Fail("response length out of range");
			break;
		}

		std::string response(uiResponseLength, '\0');
		if (!RecvAll(socket, response.data(), uiResponseLength))
		{
			Fail("incomplete response (timed out or peer closed)");
			break;
		}

		// Raw response to stdout regardless of parse outcome.
		std::cout << response << '\n';

		// Parse only to read the "ok" field (trust boundary: response is opaque socket data).
		try
		{
			nlohmann::json parsed = nlohmann::json::parse(response);
			if (parsed.contains("ok") && parsed["ok"].is_boolean())
			{
				iResult = parsed["ok"].get<bool>() ? kiExitOk : kiExitOkFalse;
			}
			else
			{
				Fail("response missing boolean \"ok\" field");
				iResult = kiExitFailure;
			}
		}
		catch (const std::exception& rException)
		{
			Fail(std::string("response is not valid JSON: ") + rException.what());
			iResult = kiExitFailure;
		}
	}
	while (false);

	::closesocket(socket);
	::WSACleanup();
	return iResult;
}
