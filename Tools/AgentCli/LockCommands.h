#pragma once

#include <string>

namespace agentcli
{
	int RunLockCommand(int iArgumentCount, wchar_t* pArgumentValues[]);
	bool RefreshHarnessHeartbeat(const std::wstring& rOwner);
}
