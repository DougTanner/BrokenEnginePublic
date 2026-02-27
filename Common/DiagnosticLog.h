#pragma once

namespace common
{

class DiagnosticLog
{
public:

	DiagnosticLog(const char* pcFilename);
	~DiagnosticLog();

	DiagnosticLog(const DiagnosticLog&) = delete;
	DiagnosticLog& operator=(const DiagnosticLog&) = delete;

	template <typename... TUV>
	void Write(std::format_string<const TUV&...> format, const TUV&... parameters)
	{
		ScopedSuppressAllocationTracking suppressAllocationTracking;

		char pcBuffer[2048] {};
		auto result = std::format_to_n(pcBuffer, sizeof(pcBuffer) - 2, format, parameters...);
		*result.out = '\n';

		std::lock_guard lockGuard(mMutex);
		mFile.write(pcBuffer, result.out - pcBuffer + 1);
		mFile.flush();
	}

private:

	std::ofstream mFile;
	std::mutex mMutex;
};

inline DiagnosticLog* gpDiagnosticLog = nullptr;

} // namespace common

#define FILE_LOG_INIT(filename) common::DiagnosticLog diagnosticLog(filename)
#define FILE_LOG(...) do { if (common::gpDiagnosticLog != nullptr) { common::gpDiagnosticLog->Write(__VA_ARGS__); } } while (false)
