#pragma once

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
		ScopedSuppressAllocationTracking suppressAllocationTracking;

		char pcBuffer[2048] {};
		std::format_to_n_result<char*> result = std::format_to_n(pcBuffer, sizeof(pcBuffer) - 2, format, parameters...);
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

inline DiagnosticLog* gpDiagnosticLogs[4] = {};

} // namespace common

#define FILE_LOG_INIT(index, filename) common::DiagnosticLog diagnosticLog##index(index, filename)
#define FILE_LOG(index, ...) do { if (common::gpDiagnosticLogs[index] != nullptr) { common::gpDiagnosticLogs[index]->Write(__VA_ARGS__); } } while (false)
