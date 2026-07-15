#include "DiagnosticReporter.h"

namespace diagnostic
{

namespace
{

inline constexpr size_t kuiPayloadFragmentBytes = 4 * 1024;
std::atomic<bool> sbValidatedLinkedWorktree = false;
std::atomic<uint64_t> suiNextRecordIdentifier = 1;

static_assert(kuiPayloadFragmentBytes * 6 + 1024 < common::kiLogBufferSize);

const char* OperationName(Operation eOperation)
{
	switch (eOperation)
	{
		case Operation::kExportJobs:
			return "export_jobs";
		case Operation::kTopLevelStandardException:
			return "top_level_std_exception";
		case Operation::kTopLevelUnknownException:
			return "top_level_unknown_exception";
		case Operation::kMaterializeOutput:
			return "materialize_output";
	}
	std::unreachable();
}

const char* SeverityName(Severity eSeverity)
{
	return eSeverity == Severity::kError ? "error" : "warning";
}

const char* ButtonContractName(ButtonContract eButtons)
{
	return eButtons == ButtonContract::kOk ? "ok" : "ok_cancel";
}

const char* ModalIconName(ModalIcon eIcon)
{
	switch (eIcon)
	{
		case ModalIcon::kNone:
			return "none";
		case ModalIcon::kError:
			return "error";
		case ModalIcon::kWarning:
			return "warning";
	}
	std::unreachable();
}

const char* ButtonResultName(ButtonResult eResult)
{
	return eResult == ButtonResult::kAcknowledged ? "acknowledged" : "cancelled";
}

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

uint64_t AddChecked(uint64_t uiLeft, uint64_t uiRight)
{
	if (uiLeft > std::numeric_limits<uint64_t>::max() - uiRight)
	{
		throw std::runtime_error("Output materialization size overflow");
	}
	return uiLeft + uiRight;
}

uint64_t MultiplyChecked(uint64_t uiLeft, uint64_t uiRight)
{
	if (uiRight != 0 && uiLeft > std::numeric_limits<uint64_t>::max() / uiRight)
	{
		throw std::runtime_error("Output materialization size overflow");
	}
	return uiLeft * uiRight;
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

std::vector<std::string_view> SplitPayload(const std::string& rPayload)
{
	std::vector<std::string_view> parts;
	parts.reserve(rPayload.size() / kuiPayloadFragmentBytes + (rPayload.size() % kuiPayloadFragmentBytes != 0));
	for (size_t i = 0; i < rPayload.size();)
	{
		size_t uiEnd = (std::min)(i + kuiPayloadFragmentBytes, rPayload.size());
		while (uiEnd < rPayload.size() && uiEnd > i && (static_cast<unsigned char>(rPayload[uiEnd]) & 0xc0) == 0x80)
		{
			--uiEnd;
		}
		parts.emplace_back(rPayload.data() + i, uiEnd - i);
		i = uiEnd;
	}
	return parts;
}

void LogLine(Severity eSeverity, const std::string& rLine)
{
	if (eSeverity == Severity::kError)
	{
		LOG(kDefault, kError, "{}", rLine);
	}
	else
	{
		LOG(kDefault, kWarning, "{}", rLine);
	}
}

void EmitRecord(const Record& rRecord, uint64_t uiRecordIdentifier, std::string_view outcome)
{
	nlohmann::json record
	{
		{"version", 1},
		{"record_id", uiRecordIdentifier},
		{"category", "DataPacker"},
		{"severity", SeverityName(rRecord.eSeverity)},
		{"log_level", SeverityName(rRecord.eSeverity)},
		{"operation", OperationName(rRecord.eOperation)},
		{"title", NormalizeUtf8(rRecord.title)},
		{"message", NormalizeUtf8(rRecord.message)},
		{"buttons", ButtonContractName(rRecord.eButtons)},
		{"icon", ModalIconName(rRecord.eIcon)},
		{"outcome", outcome},
	};
	if (rRecord.sourcePath)
	{
		record["source_path"] = PathToUtf8(*rRecord.sourcePath);
	}
	if (rRecord.destinationPath)
	{
		record["destination_path"] = PathToUtf8(*rRecord.destinationPath);
	}
	if (rRecord.uiRequiredBytes)
	{
		record["required_bytes"] = *rRecord.uiRequiredBytes;
	}
	if (rRecord.uiAvailableBytes)
	{
		record["available_bytes"] = *rRecord.uiAvailableBytes;
	}
	if (rRecord.uiTotalBytes)
	{
		record["total_bytes"] = *rRecord.uiTotalBytes;
	}
	if (rRecord.uiProjectedBytes)
	{
		record["projected_bytes"] = *rRecord.uiProjectedBytes;
	}
	if (rRecord.uiWin32Error)
	{
		record["win32_error"] = *rRecord.uiWin32Error;
	}
	if (!rRecord.exportFailures.empty())
	{
		record["export_failures"] = nlohmann::json::array();
		for (const ExportFailure& rFailure : rRecord.exportFailures)
		{
			nlohmann::json failure {{"message", NormalizeUtf8(rFailure.message)}};
			if (rFailure.assetPath)
			{
				failure["asset_path"] = PathToUtf8(*rFailure.assetPath);
			}
			record["export_failures"].push_back(std::move(failure));
		}
	}

	const std::string payload = record.dump();
	const std::vector<std::string_view> parts = SplitPayload(payload);
	for (size_t i = 0; i < parts.size(); ++i)
	{
		nlohmann::json envelope
		{
			{"record_id", uiRecordIdentifier},
			{"part", i + 1},
			{"parts", parts.size()},
			{"payload", std::string(parts.at(i))},
		};
		LogLine(rRecord.eSeverity, std::format("DATAPACKER_DIAGNOSTIC_V1 {}", envelope.dump()));
	}
}

void EmitResult(const Record& rRecord, uint64_t uiRecordIdentifier, ButtonResult eResult)
{
	nlohmann::json result
	{
		{"version", 1},
		{"record_id", uiRecordIdentifier},
		{"operation", OperationName(rRecord.eOperation)},
		{"outcome", ButtonResultName(eResult)},
	};
	LogLine(rRecord.eSeverity, std::format("DATAPACKER_DIAGNOSTIC_RESULT_V1 {}", result.dump()));
}

} // namespace

void MarkValidatedLinkedWorktree()
{
	sbValidatedLinkedWorktree.store(true, std::memory_order_release);
}

ButtonResult Report(const Record& rRecord)
{
	const bool bNoninteractive = sbValidatedLinkedWorktree.load(std::memory_order_acquire);
	const ButtonResult eForcedResult = rRecord.eButtons == ButtonContract::kOk ? ButtonResult::kAcknowledged : ButtonResult::kCancelled;
	const uint64_t uiRecordIdentifier = suiNextRecordIdentifier.fetch_add(1, std::memory_order_relaxed);
	EmitRecord(rRecord, uiRecordIdentifier, bNoninteractive ? ButtonResultName(eForcedResult) : "pending");
	if (bNoninteractive)
	{
		return eForcedResult;
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
	const std::wstring message = Utf8ToWide(BuildModalText(rRecord));
	const int iResult = MessageBoxW(nullptr, message.c_str(), title.c_str(), uiFlags);
	const ButtonResult eResult = rRecord.eButtons == ButtonContract::kOk || iResult == IDOK ? ButtonResult::kAcknowledged : ButtonResult::kCancelled;
	EmitResult(rRecord, uiRecordIdentifier, eResult);
	return eResult;
}

DiskSpaceDecision ReportMaterializationDiskSpace(uint64_t uiAllocation, uint64_t uiAvailable, uint64_t uiTotal, const std::filesystem::path& rSource, const std::filesystem::path& rDestination)
{
	const uint64_t uiReserve = (std::max)(1ull << 30, AddChecked(MultiplyChecked(uiAllocation, 5), 99) / 100);
	const uint64_t uiRequired = AddChecked(uiAllocation, uiReserve);
	if (uiRequired > uiAvailable)
	{
		Record record
		{
			.eSeverity = Severity::kError,
			.eOperation = Operation::kMaterializeOutput,
			.title = "DataPacker - Insufficient Disk Space",
			.message = std::format("Insufficient disk space. Required: {} bytes. Available: {} bytes.", uiRequired, uiAvailable),
			.eButtons = ButtonContract::kOk,
			.eIcon = ModalIcon::kError,
			.sourcePath = rSource,
			.destinationPath = rDestination,
			.uiRequiredBytes = uiRequired,
			.uiAvailableBytes = uiAvailable,
			.uiTotalBytes = uiTotal,
		};
		Report(record);
		return DiskSpaceDecision::kFailed;
	}

	const uint64_t uiProjected = uiAvailable - uiAllocation;
	const uint64_t uiWarning = (std::max)(10ull << 30, AddChecked(MultiplyChecked(uiTotal, 10), 99) / 100);
	if (uiProjected < uiWarning)
	{
		Record record
		{
			.eSeverity = Severity::kWarning,
			.eOperation = Operation::kMaterializeOutput,
			.title = "DataPacker - Low Disk Space",
			.message = std::format("Copy-on-write needs approximately {} bytes. Available: {} bytes. Projected remaining: {} bytes.", uiAllocation, uiAvailable, uiProjected),
			.eButtons = ButtonContract::kOkCancel,
			.eIcon = ModalIcon::kWarning,
			.sourcePath = rSource,
			.destinationPath = rDestination,
			.uiRequiredBytes = uiAllocation,
			.uiAvailableBytes = uiAvailable,
			.uiTotalBytes = uiTotal,
			.uiProjectedBytes = uiProjected,
		};
		return Report(record) == ButtonResult::kAcknowledged ? DiskSpaceDecision::kProceed : DiskSpaceDecision::kCancelled;
	}
	return DiskSpaceDecision::kProceed;
}

} // namespace diagnostic
