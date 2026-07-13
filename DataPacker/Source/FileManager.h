#pragma once

#include "InputFingerprint.h"

class FileManager
{
public:
	enum class OutputRoot
	{
		kData,
		kAttribution,
	};

	enum class EnsureLocalResult
	{
		kAlreadyLocal,
		kMaterialized,
		kCancelled,
	};

	FileManager(std::span<char*> argvSpan);
	~FileManager();

	std::filesystem::path mpInputDirectories[2];
	std::filesystem::path mTempDirectory;
	std::filesystem::path mGaeaCacheDirectory;
	std::filesystem::path mOutputDirectory;
	std::filesystem::path mThirdPartyDirectory;
	std::string mProjectName;

	bool mbCleanExport = false;
	bool mbForbidExpensiveExport = false;

	std::string GetFingerprint(const std::filesystem::path& rPath);
	std::string GetSharedCacheFingerprint(const std::filesystem::path& rPath);
	EnsureLocalResult EnsureLocal(OutputRoot eRoot);
	std::filesystem::path GetAttributionDirectory() const;

private:
	enum class OutputRootState
	{
		kLocal,
		kRecognizedPrimaryLink,
		kAbsent,
		kUnvalidatedReparse,
	};

	struct OutputRootInfo
	{
		std::filesystem::path mSource;
		std::filesystem::path mDestination;
		OutputRootState meState = OutputRootState::kLocal;
	};

	void InitializeWorktreeOutputs();
	EnsureLocalResult MaterializeOutput(OutputRootInfo& rRoot);
	OutputRootInfo& GetOutputRoot(OutputRoot eRoot);

	OutputRootInfo mDataOutput;
	OutputRootInfo mAttributionOutput;

	std::unique_ptr<InputFingerprintCache> mpInputFingerprintCache;
};

inline FileManager* gpFileManager = nullptr;
