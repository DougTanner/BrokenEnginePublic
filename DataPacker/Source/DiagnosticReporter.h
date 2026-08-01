#pragma once

namespace diagnostic
{

enum class Severity
{
	kError,
	kWarning,
};

enum class ButtonContract
{
	kOk,
	kOkCancel,
};

enum class ModalIcon
{
	kNone,
	kError,
	kWarning,
};

enum class ButtonResult
{
	kAcknowledged,
	kCancelled,
};

struct ExportFailure
{
	std::optional<std::filesystem::path> assetPath;
	std::string message;
};

struct Record
{
	Severity eSeverity = Severity::kError;
	std::string title;
	std::string message;
	ButtonContract eButtons = ButtonContract::kOk;
	ModalIcon eIcon = ModalIcon::kNone;
	std::vector<ExportFailure> exportFailures;
};

class AlreadyReportedError final : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

enum class DiskSpaceDecision
{
	kProceed,
	kCancelled,
	kFailed,
};

void MarkValidatedLinkedWorktree();
ButtonResult Report(const Record& rRecord);
DiskSpaceDecision ReportMaterializationDiskSpace(uint64_t uiAllocation, uint64_t uiAvailable, const std::filesystem::path& rSource, const std::filesystem::path& rDestination);

} // namespace diagnostic
