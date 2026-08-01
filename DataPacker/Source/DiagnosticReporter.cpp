#include "DiagnosticReporter.h"

namespace diagnostic
{

namespace
{

std::atomic<bool> sbValidatedLinkedWorktree = false;

std::string PathToUtf8(const std::filesystem::path& rPath)
{
	const std::u8string value = rPath.u8string();
	return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::wstring Utf8ToWide(std::string_view value)
{
	if (value.empty())
	{
		return {};
	}
	int iCharacters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (iCharacters == 0)
	{
		iCharacters = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
		std::wstring wideValue(iCharacters, L'\0');
		MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), wideValue.data(), iCharacters);
		return wideValue;
	}
	std::wstring wideValue(iCharacters, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wideValue.data(), iCharacters);
	return wideValue;
}

std::string NormalizeUtf8(std::string_view value)
{
	const std::wstring wideValue = Utf8ToWide(value);
	if (wideValue.empty())
	{
		return {};
	}
	int iBytes = WideCharToMultiByte(CP_UTF8, 0, wideValue.data(), static_cast<int>(wideValue.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8Value(iBytes, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wideValue.data(), static_cast<int>(wideValue.size()), utf8Value.data(), iBytes, nullptr, nullptr);
	return utf8Value;
}

std::string BuildModalText(const Record& rRecord)
{
	if (rRecord.exportFailures.empty())
	{
		return NormalizeUtf8(rRecord.message);
	}

	std::string text;
	for (size_t i = 0; i < rRecord.exportFailures.size(); ++i)
	{
		if (i > 0)
		{
			text.append("\n\n");
		}
		const ExportFailure& rFailure = rRecord.exportFailures.at(i);
		if (rFailure.assetPath)
		{
			text.append("Asset: ");
			text.append(PathToUtf8(*rFailure.assetPath));
			text.append("\n\n");
		}
		text.append(NormalizeUtf8(rFailure.message));
	}
	return text;
}

} // namespace

void MarkValidatedLinkedWorktree()
{
	sbValidatedLinkedWorktree.store(true, std::memory_order_release);
}

ButtonResult Report(const Record& rRecord)
{
	const std::string text = BuildModalText(rRecord);
	if (rRecord.eSeverity == Severity::kError)
	{
		LOG(kDefault, kError, "{}: {}", rRecord.title, text);
	}
	else
	{
		LOG(kDefault, kWarning, "{}: {}", rRecord.title, text);
	}
	if (sbValidatedLinkedWorktree.load(std::memory_order_acquire))
	{
		return rRecord.eButtons == ButtonContract::kOk ? ButtonResult::kAcknowledged : ButtonResult::kCancelled;
	}

	fflush(stdout);
	UINT uiFlags = MB_SYSTEMMODAL;
	uiFlags |= rRecord.eButtons == ButtonContract::kOk ? MB_OK : MB_OKCANCEL;
	switch (rRecord.eIcon)
	{
		case ModalIcon::kNone:
			break;
		case ModalIcon::kError:
			uiFlags |= MB_ICONERROR;
			break;
		case ModalIcon::kWarning:
			uiFlags |= MB_ICONWARNING;
			break;
	}
	const std::wstring title = Utf8ToWide(NormalizeUtf8(rRecord.title));
	const std::wstring message = Utf8ToWide(text);
	const int iResult = MessageBoxW(nullptr, message.c_str(), title.c_str(), uiFlags);
	return rRecord.eButtons == ButtonContract::kOk || iResult == IDOK ? ButtonResult::kAcknowledged : ButtonResult::kCancelled;
}

DiskSpaceDecision ReportMaterializationDiskSpace(uint64_t uiAllocation, uint64_t uiAvailable, const std::filesystem::path& rSource, const std::filesystem::path& rDestination)
{
	const uint64_t uiReserve = (std::max)(1ull << 30, (uiAllocation * 5 + 99) / 100);
	const uint64_t uiRequired = uiAllocation + uiReserve;
	if (uiRequired > uiAvailable)
	{
		Record record
		{
			.eSeverity = Severity::kError,
			.title = "DataPacker - Insufficient Disk Space",
			.message = std::format("Insufficient disk space copying \"{}\" to \"{}\". Required: {} bytes. Available: {} bytes.", rSource.string(), rDestination.string(), uiRequired, uiAvailable),
			.eButtons = ButtonContract::kOk,
			.eIcon = ModalIcon::kError,
		};
		Report(record);
		return DiskSpaceDecision::kFailed;
	}

	const uint64_t uiProjected = uiAvailable - uiAllocation;
	const uint64_t uiWarning = 10ull << 30;
	if (uiProjected < uiWarning)
	{
		Record record
		{
			.eSeverity = Severity::kWarning,
			.title = "DataPacker - Low Disk Space",
			.message = std::format("Copy-on-write of \"{}\" to \"{}\" needs approximately {} bytes. Available: {} bytes. Projected remaining: {} bytes.", rSource.string(), rDestination.string(), uiAllocation, uiAvailable, uiProjected),
			.eButtons = ButtonContract::kOkCancel,
			.eIcon = ModalIcon::kWarning,
		};
		return Report(record) == ButtonResult::kAcknowledged ? DiskSpaceDecision::kProceed : DiskSpaceDecision::kCancelled;
	}
	return DiskSpaceDecision::kProceed;
}

} // namespace diagnostic
