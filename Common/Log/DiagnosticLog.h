#pragma once

#include "AllocationTracking.h"

namespace common
{

class DiagnosticLog
{
public:

	DiagnosticLog(int iIndex, const char* pcFilename);
	~DiagnosticLog();

	DiagnosticLog(const DiagnosticLog&) = delete;
	DiagnosticLog& operator=(const DiagnosticLog&) = delete;

	template <typename... TUV>
	void Write(std::format_string<const TUV&...> format, const TUV&... parameters)
	{
		ScopedSuppressAllocationTracking suppress;

		char pcBuffer[2048] {};
		std::format_to_n_result<char*> result = std::format_to_n(pcBuffer, sizeof(pcBuffer) - 2, format, parameters...);
		if (result.size > static_cast<int64_t>(sizeof(pcBuffer) - 2))
		{
			static constexpr std::string_view kTruncationMarker = "...<trunc>";
			std::memcpy(result.out - kTruncationMarker.size(), kTruncationMarker.data(), kTruncationMarker.size());
		}
		*result.out = '\n';

		std::lock_guard lockGuard(mMutex);
		mFile.write(pcBuffer, result.out - pcBuffer + 1);
		mFile.flush();
	}

private:

	std::ofstream mFile;
	std::mutex mMutex;
	int miIndex = 0;
};

inline constexpr int64_t kDiagnosticLogCount = 4;

inline DiagnosticLog* gpDiagnosticLogs[kDiagnosticLogCount] = {};

} // namespace common

#define FILE_LOG_INIT(index, filename) static_assert((index) >= 0 && (index) < common::kDiagnosticLogCount, "diagnostic log slot out of range"); common::DiagnosticLog diagnosticLog##index(index, filename)
#define FILE_LOG(index, ...) do { static_assert((index) >= 0 && (index) < common::kDiagnosticLogCount, "diagnostic log slot out of range"); if (common::gpDiagnosticLogs[index] != nullptr) { common::gpDiagnosticLogs[index]->Write(__VA_ARGS__); } } while (false)
