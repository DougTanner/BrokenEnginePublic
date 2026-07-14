#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "AgentCliCommon.h"
#include "tinygltf/json.hpp"

namespace agentcli::coordination
{
	inline constexpr int kiSchemaVersion = 2;

	struct Locator
	{
		std::wstring domain;
		std::wstring logicalKey;
		std::filesystem::path path;
	};

	class Guard
	{
	public:
		explicit Guard(const std::filesystem::path& rPath);

		[[nodiscard]] bool IsValid() const;

	private:
		Handle mhFile;
	};

	std::string CurrentUtcTimestamp();
	bool ParseUtcTimestamp(const std::string& rValue, uint64_t& rTicks);
	uint64_t CurrentUtcTicks();
	std::string FormatUtcTimestamp(uint64_t uiTicks);
	std::optional<std::string> HashSha256(std::string_view value);
	std::optional<std::wstring> CanonicalizeDirectoryPath(const std::wstring& rValue);
	std::optional<std::wstring> NormalizeRelativeKey(const std::wstring& rValue);
	std::optional<std::wstring> NormalizeRepositoryRelativeKey(const std::wstring& rValue);
	std::optional<Locator> MakeLocator(const std::wstring& rDomain, const std::wstring& rLogicalKey);
	bool EnsureParentDirectory(const std::filesystem::path& rPath);
	bool ReadMetadata(const std::filesystem::path& rPath, nlohmann::json& rMetadata);
	bool WriteMetadataAtomic(const std::filesystem::path& rPath, const nlohmann::json& rMetadata);
	void PrintMetadata(const nlohmann::json& rMetadata);
	bool HasOwner(const nlohmann::json& rMetadata, const std::wstring& rOwner);
	bool JsonIntegerEquals(const nlohmann::json& rValue, int64_t iExpected);
	std::optional<int64_t> JsonInt64(const nlohmann::json& rValue);
	bool ValidateMetadataEnvelope(const nlohmann::json& rMetadata, const Locator& rLocator);
	nlohmann::json NewMetadata(const Locator& rLocator, const std::wstring& rOwner, const std::wstring& rSession, const std::wstring& rWorktree);
}
