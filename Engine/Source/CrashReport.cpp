#include "CrashReport.h"

#include "Game.h"

namespace engine
{

static std::string sDxDiag;

void HandleException(std::optional<const std::exception*> pException)
{
	DEBUG_BREAK();

	int iResult = MessageBox(nullptr, "Save crash report to desktop?", game::kGameName.data(), MB_YESNO | MB_SYSTEMMODAL);

	// Fixed-buffer formatting (no heap allocation): reachable from the SIGABRT handler on heap corruption, so re-entering the allocator could re-fault.
	wchar_t pcGameName[64] {};
	swprintf_s(pcGameName, std::size(pcGameName), L"%hs", game::kGameName.data());

	static wchar_t spcPath[MAX_PATH + 1] {};
	if (iResult == IDYES)
	{
		SHGetSpecialFolderPathW(HWND_DESKTOP, spcPath, CSIDL_DESKTOP, FALSE);
	}
	else
	{
		PWSTR pWideChar = nullptr;
		HRESULT hresult = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
		if (SUCCEEDED(hresult) && pWideChar != nullptr)
		{
			wcscpy_s(spcPath, std::size(spcPath), pWideChar);
		}
		else
		{
			// OS failure on the SIGABRT-reachable crash path (allocator-free): never copy from null. Fall back to the Desktop (fixed-buffer, like the IDYES branch) so the report still lands somewhere writable; if that also fails spcPath stays zero-initialized.
			LOG(kDefault, kError, "SHGetKnownFolderPath(FOLDERID_RoamingAppData) failed (hresult {}); writing crash report to Desktop", static_cast<int32_t>(hresult));
			SHGetSpecialFolderPathW(HWND_DESKTOP, spcPath, CSIDL_DESKTOP, FALSE);
		}
		CoTaskMemFree(pWideChar);

		wcscat_s(spcPath, std::size(spcPath), L"\\");
		wcscat_s(spcPath, std::size(spcPath), pcGameName);

		// Allocator-free single-level create (parent guaranteed present above): the SIGABRT-reachable crash path must not re-enter the heap, which std::filesystem::create_directories would. Ignore the result — ERROR_ALREADY_EXISTS is fine, mirroring create_directories' no-op on an existing dir.
		CreateDirectoryW(spcPath, nullptr);
	}

	wchar_t pcCrashReportFile[128] {};
	swprintf_s(pcCrashReportFile, std::size(pcCrashReportFile), L"\\%s-Crash-Report.txt", pcGameName);
	std::replace(std::begin(pcCrashReportFile), std::end(pcCrashReportFile), L' ', L'-');
	wcscat_s(spcPath, std::size(spcPath), pcCrashReportFile);

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

	common::LogDumpBuffers(ofstream);

	ofstream.close();
}

void ReadDxDiag()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	common::ThreadLocal threadLocal(1024, common::kThreadDxDiag);

	try
	{
		CHECK_HRESULT(CoInitialize(nullptr));

		Microsoft::WRL::ComPtr<IDxDiagProvider> pIdxDiagProvider;
		CHECK_HRESULT(CoCreateInstance(CLSID_DxDiagProvider, nullptr, CLSCTX_INPROC_SERVER, IID_IDxDiagProvider, reinterpret_cast<void**>(pIdxDiagProvider.GetAddressOf())));

		DXDIAG_INIT_PARAMS dxdiagInitParams
		{
			.dwSize = sizeof(DXDIAG_INIT_PARAMS),
			.dwDxDiagHeaderVersion = DXDIAG_DX9_SDK_VERSION,
			.bAllowWHQLChecks = FALSE,
			.pReserved = nullptr,
		};
		CHECK_HRESULT(pIdxDiagProvider->Initialize(&dxdiagInitParams));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pRoot;
		CHECK_HRESULT(pIdxDiagProvider->GetRootContainer(&pRoot));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pDisplayDevices;
		CHECK_HRESULT(pRoot->GetChildContainer(L"DxDiag_DisplayDevices", &pDisplayDevices));

		DWORD uiChildCount = 0;
		CHECK_HRESULT(pDisplayDevices->GetNumberOfChildContainers(&uiChildCount));
		LOG(kDefault, kDebug, "DxDiag found {} children", uiChildCount);
		for (DWORD i = 0; i < uiChildCount; ++i)
		{
			WCHAR pcChildName[256] {};
			CHECK_HRESULT(pDisplayDevices->EnumChildContainerNames(i, pcChildName, 256));
			Microsoft::WRL::ComPtr<IDxDiagContainer> pChild;
			CHECK_HRESULT(pDisplayDevices->GetChildContainer(pcChildName, &pChild));

			// Best-effort per-prop reads (GetNumberOfProps / EnumPropNames / GetProp / VariantClear): results deliberately
			// unchecked. Each output is zero-initialized and used only under the VT_BSTR guard, so a failed read skips the
			// prop safely; CHECK_HRESULT here would throw and abort the whole remaining best-effort DxDiag capture.
			DWORD uiPropCount = 0;
			pChild->GetNumberOfProps(&uiPropCount);
			LOG(kDefault, kDebug, "    {} props", uiPropCount);
			for (DWORD j = 0; j < uiPropCount; ++j)
			{
				WCHAR pcPropName[256] {};
				pChild->EnumPropNames(j, pcPropName, static_cast<DWORD>(std::size(pcPropName) - 1));

				VARIANT variant {};
				pChild->GetProp(pcPropName, &variant);
				if (variant.vt == VT_BSTR && variant.bstrVal != nullptr)
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
		LOG(kDefault, kError, "Failed to read DxDiag: {}", rException.what());
	}
	catch (...)
	{
		LOG(kDefault, kError, "Failed to read DxDiag");
	}
}

} // namespace engine
