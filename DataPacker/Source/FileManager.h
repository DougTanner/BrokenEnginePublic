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

	enum class InitializationMode
	{
		kFull,
		kDataOnly,
	};

	FileManager(std::span<char*> argvSpan, InitializationMode eMode = InitializationMode::kFull);
	~FileManager();

	std::filesystem::path mpInputDirectories[2];
	std::filesystem::path mCacheDirectory;
	std::filesystem::path mGaeaCacheDirectory;
	std::filesystem::path mOutputDirectory;
	std::filesystem::path mThirdPartyDirectory;
	std::string mProjectName;

	bool mbCleanExport = false;
	bool mbForbidExpensiveExport = false;
	bool mbForbidGaeaExport = false;

	std::string GetFingerprint(const std::filesystem::path& rPath, InputFingerprintMode eMode = InputFingerprintMode::kRaw);
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

	void InitializeWorktreeOutputs(InitializationMode eMode);
	EnsureLocalResult MaterializeOutput(OutputRootInfo& rRoot);
	OutputRootInfo& GetOutputRoot(OutputRoot eRoot);

	OutputRootInfo mDataOutput;
	OutputRootInfo mAttributionOutput;

	std::unique_ptr<InputFingerprintCache> mpInputFingerprintCache;
};

inline FileManager* gpFileManager = nullptr;
