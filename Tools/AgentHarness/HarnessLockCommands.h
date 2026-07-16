#pragma once

#include <string>

namespace toolcli
{
	int RunHarnessLockCommand(int iArgumentCount, wchar_t* pArgumentValues[]);
	bool RefreshHarnessHeartbeat(const std::wstring& rOwner);
}
