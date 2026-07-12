#pragma once

namespace common
{

// Helper function to get content from either a file path or string
template<typename T>
inline std::pair<bool, std::string> GetFileOrStringContent(const T& rSource)
{
	if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
	{
		// Source is already a string
		return {true, rSource};
	}
	else if constexpr (std::is_same_v<std::decay_t<T>, std::filesystem::path>)
	{
		if (!std::filesystem::exists(rSource))
		{
			return {false, {}};
		}

		// Pre-allocate string based on file size
		size_t uiFileSize = std::filesystem::file_size(rSource);
		std::string fileContents;
		fileContents.resize(uiFileSize);

		// Read file into string
		std::fstream fileStream(rSource, std::ios::in | std::ios::binary);
		fileStream.read(fileContents.data(), uiFileSize);
		const bool bReadOk = static_cast<bool>(fileStream) && static_cast<size_t>(fileStream.gcount()) == uiFileSize;
		fileStream.close();

		if (!bReadOk)
		{
			return {false, {}};
		}

		return {true, std::move(fileContents)};
	}
	else
	{
		static_assert(false, "GetFileOrStringContent only supports std::string and std::filesystem::path");
	}
}

// Template function to compare file contents, supporting both file paths and std::string
template<typename T1, typename T2>
inline bool ContentsEqual(const T1& rOne, const T2& rTwo)
{
	auto [oneValid, oneContent] = GetFileOrStringContent(rOne);
	auto [twoValid, twoContent] = GetFileOrStringContent(rTwo);

	// Return false if either source does not exist or could not be read
	if (!oneValid || !twoValid)
	{
		return false;
	}

	// Return true if equal
	return oneContent == twoContent;
}

// Reads an entire file into a freshly-allocated byte vector. Offline / tool-side use only — allocates.
// Named ReadEntireFile (not ReadFile) to avoid colliding with the Win32 ReadFile API inside namespace common.
inline std::vector<std::byte> ReadEntireFile(const std::filesystem::path& rPath)
{
	std::vector<std::byte> data(std::filesystem::file_size(rPath));
	std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
	if (!fileStream)
	{
		throw std::runtime_error("ReadEntireFile failed to open file");
	}
	fileStream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
	if (static_cast<size_t>(fileStream.gcount()) != data.size())
	{
		throw std::runtime_error("ReadEntireFile failed to read complete file");
	}
	return data;
}

} // namespace common
