#include "CrashReport.h"

#include "Memory/MemoryManager.h"

#include "Game.h"

namespace engine
{

static std::string sDxDiag;

void HandleException(std::optional<const std::exception*> pException)
{
	DEBUG_BREAK();

	int iResult = MessageBox(nullptr, "Save crash report to desktop?", game::kGameName.data(), MB_YESNO | MB_SYSTEMMODAL);

	std::wstring gameName = common::ToWstring(game::kGameName);

	static wchar_t spcPath[MAX_PATH + 1] {};
	if (iResult == IDYES)
	{
		SHGetSpecialFolderPathW(HWND_DESKTOP, spcPath, CSIDL_DESKTOP, FALSE);
	}
	else
	{
		PWSTR pWideChar = nullptr;
		SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
		wcscpy_s(spcPath, std::size(spcPath), pWideChar);
		CoTaskMemFree(pWideChar);

		wcscat_s(spcPath, std::size(spcPath), L"\\");
		wcscat_s(spcPath, std::size(spcPath), gameName.c_str());

		std::filesystem::create_directories(spcPath);
	}

	std::wstring crashReportPath(L"\\");
	crashReportPath.append(gameName);
	std::replace(crashReportPath.begin(), crashReportPath.end(), ' ', '-');
	crashReportPath.append(L"-Crash-Report.txt");
	wcscat_s(spcPath, std::size(spcPath), crashReportPath.c_str());

	std::ofstream ofstream(spcPath);

	ofstream << "\n\n\nPlease send this crash report to brokenteapotstudios@gmail.com, and if possible describe exactly what you were doing when it occurred.\n" << std::flush;
	ofstream << "Game name: " << game::kGameName << "\n" << std::flush;
	ofstream << "Game version: " << game::kiGameVersion << "\n" << std::flush;

	if (pException.has_value())
	{
		ofstream << "\n\n\n" << pException.value()->what() << "\n" << std::flush;
	}
	else
	{
		ofstream << "\n\n\nUnknown exception\n" << std::flush;
	}

	ofstream << "\n\n\n<Begin callstack>\n" << std::flush;
	common::OfstreamStackWalker ofstreamStackWalker(StackWalker::AfterCatch, &ofstream);
	ofstreamStackWalker.ShowCallstack();
	ofstream << "<End callstack>\n" << std::flush;

	ofstream << "\n\n\n<Begin DxDiag>\n" << std::flush;
	ofstream << sDxDiag;
	ofstream << "<End DxDiag>\n" << std::flush;

	ofstream << "\n\n\n<Begin Log>\n" << std::flush;
	gpFileManager->WriteLogFile(ofstream);
	ofstream << "<End Log>\n" << std::flush;

	ofstream.close();
}

void ReadDxDiag()
{
	if (IsDebuggerPresent())
	{
		return;
	}

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	common::ThreadLocal threadLocal(1024, common::kThreadDxDiag);

	try
	{
		CHECK_HRESULT(CoInitialize(nullptr));

		Microsoft::WRL::ComPtr<IDxDiagProvider> pIdxDiagProvider;
		CHECK_HRESULT(CoCreateInstance(CLSID_DxDiagProvider, nullptr, CLSCTX_INPROC_SERVER, IID_IDxDiagProvider, reinterpret_cast<void**>(pIdxDiagProvider.GetAddressOf())));

		DXDIAG_INIT_PARAMS dxdiagInitParams {.dwSize = sizeof(DXDIAG_INIT_PARAMS), .dwDxDiagHeaderVersion = DXDIAG_DX9_SDK_VERSION, .bAllowWHQLChecks = false, .pReserved = nullptr,};
		CHECK_HRESULT(pIdxDiagProvider->Initialize(&dxdiagInitParams));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pRoot;
		CHECK_HRESULT(pIdxDiagProvider->GetRootContainer(&pRoot));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pDisplayDevices;
		CHECK_HRESULT(pRoot->GetChildContainer(L"DxDiag_DisplayDevices", &pDisplayDevices));

		DWORD uiChildCount = 0;
		CHECK_HRESULT(pDisplayDevices->GetNumberOfChildContainers(&uiChildCount));
		Log("DxDiag found {} children", uiChildCount);
		for (DWORD i = 0; i < uiChildCount; ++i)
		{
			WCHAR pcChildName[256] {};
			CHECK_HRESULT(pDisplayDevices->EnumChildContainerNames(i, pcChildName, 256));
			Microsoft::WRL::ComPtr<IDxDiagContainer> pChild;
			CHECK_HRESULT(pDisplayDevices->GetChildContainer(pcChildName, &pChild));

			DWORD uiPropCount = 0;
			pChild->GetNumberOfProps(&uiPropCount);
			Log("    {} props", uiPropCount);
			for (DWORD j = 0; j < uiPropCount; ++j)
			{
				WCHAR pcPropName[256] {};
				pChild->EnumPropNames(j, pcPropName, static_cast<DWORD>(std::size(pcPropName) - 1));

				VARIANT variant {};
				pChild->GetProp(pcPropName, &variant);
				if (variant.vt == VT_BSTR)
				{
					sDxDiag += common::ToString(pcPropName);
					sDxDiag += ": ";
					sDxDiag += common::ToString(variant.bstrVal);
					sDxDiag += "\n";
				}
				VariantClear(&variant);
			}
		}
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		Log(kLogError, "Failed to read DxDiag: {}", rException.what());
	}
	catch (...)
	{
		Log(kLogError, "Failed to read DxDiag");
	}
}

} // namespace engine
