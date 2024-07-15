#include "FileManager.h"

#include "ExportJobs/ExportJob.h"
#include "Texture.h"

using enum common::ChunkFlags;

constexpr int64_t kiDataPackerVersion = 1;

void MainThread(int argc, char* argv[])
{
	static_assert(VK_HEADER_VERSION >= 198, "Update the Vulkan SDK"); // Also update in Engine

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	SetProcessDPIAware();

	common::ThreadLocal threadLocal(1024);

	LOG("\nData Packer");
	LOG_INDENT(1);

	Texture::StaticInit();

	auto pFileManager = std::make_unique<FileManager>(std::span(argv, argc));

	VERIFY_SUCCESS(DirectX::XMVerifyCPUSupport());

	std::vector<std::unique_ptr<ExportJob>> preExportJobs;
	std::vector<std::unique_ptr<ExportJob>> exportJobs;

	for (int64_t i = 0; i < 2; ++i)
	{
		std::filesystem::path gltfDirectory(gpFileManager->mpInputDirectories[i]);
		gltfDirectory /= "Gltf";
		if (!std::filesystem::exists(gltfDirectory))
		{
			continue;
		}

		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::directory_iterator(gltfDirectory))
		{
			if (!rDirectoryEntry.is_directory())
			{
				continue;
			}

			for (const std::filesystem::directory_entry& rDirectoryEntryFile : std::filesystem::directory_iterator(rDirectoryEntry))
			{
				if (rDirectoryEntryFile.path().extension() != ".gltf")
				{
					continue;
				}

				preExportJobs.emplace_back(std::make_unique<ExportGltf>(common::ChunkFlags_t({kGltf}), rDirectoryEntryFile.path()));
				exportJobs.emplace_back(std::make_unique<ExportGltf>(common::ChunkFlags_t({kGltf}), rDirectoryEntryFile.path()));
			}
		}

		std::filesystem::path islandsDirectory(gpFileManager->mpInputDirectories[i]);
		islandsDirectory /= "Islands";
		if (std::filesystem::exists(islandsDirectory))
		{
			for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::directory_iterator(islandsDirectory))
			{
				if (!rDirectoryEntry.is_directory())
				{
					continue;
				}

				preExportJobs.emplace_back(std::make_unique<ExportIsland>(common::ChunkFlags_t({kIsland}), rDirectoryEntry.path()));
				exportJobs.emplace_back(std::make_unique<ExportIsland>(common::ChunkFlags_t({kIsland}), rDirectoryEntry.path()));
			}
		}
	}

	/* LOG("Running {} pre-export jobs:", preExportJobs.size());
	int64_t iPreExportJob = 1;
	for (std::unique_ptr<ExportJob>& rpExportJob : preExportJobs)
	{
		LOG("  {}: {}{} Flags: {:#018x}", iPreExportJob++, rpExportJob->mbDirty ? "" : "(Not dirty) ", rpExportJob->mInputPath.string(), rpExportJob->mChunkFlags.muiUnderlying);
	} */

	std::vector<std::future<void>> preExportFutures;
	preExportFutures.reserve(preExportJobs.size());
	for (std::unique_ptr<ExportJob>& rpExportJob : preExportJobs)
	{
		[[maybe_unused]] auto& future = preExportFutures.emplace_back(std::async(std::launch::async, &ExportJob::RunPreExport, rpExportJob.get()));
		// future.get();
	}
	for (auto& rFuture : preExportFutures)
	{
		try
		{
			rFuture.get();
		}
		catch (std::exception& rException)
		{
			LOG("ERROR FAILED Exception thrown from PRE-export future: \"{}\"", rException.what());
			DEBUG_BREAK();
			throw std::exception(rException.what());
			return;
		}
	}

	std::unordered_map<std::string, common::ChunkFlags> extensionToDataFlagsMap =
	{
		{".wav",             kAudio},
		{".fnt",             kFont},
		{".obj",             kModel},
		{".GLTF_MODEL",      kModel},
		{".frag",            kShaderFragment},
		{".vert",            kShaderVertex},
		{".comp",            kShaderCompute},
		{".png",             kTexture},
		{".tga",             kTexture},
		{".jpg",             kTexture},
		{".ktx",             kTexture},
		{".BC4_UNORM_BLOCK", kRawTexture},
		{".BC7_UNORM_BLOCK", kRawTexture},
		{".R8_UNORM",        kRawTexture},
		{".R8G8B8A8_UNORM",  kRawTexture},
		{".R16_UNORM",       kRawTexture},
		{".R16G16_UNORM",    kRawTexture},
		{".R32_SFLOAT",      kRawTexture},
	};

	for (int64_t i = 0; i < 2; ++i)
	{
		if (!std::filesystem::exists(gpFileManager->mpInputDirectories[i]))
		{
			continue;
		}

		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(gpFileManager->mpInputDirectories[i]))
		{
			if (rDirectoryEntry.is_directory())
			{
				if (rDirectoryEntry.path().native().find(L"[C]") != std::wstring::npos)
				{
					exportJobs.emplace_back(std::make_unique<ExportTexture>(common::ChunkFlags_t({kTexture, kCubemap}), rDirectoryEntry.path()));
				}

				continue;
			}

			auto it = extensionToDataFlagsMap.find(rDirectoryEntry.path().extension().string());
			if (it != extensionToDataFlagsMap.end())
			{
				if (it->second == kAudio)
				{
					exportJobs.emplace_back(std::make_unique<ExportAudio>(common::ChunkFlags_t(kAudio), rDirectoryEntry.path()));
				}
				else if (it->second == kFont)
				{
					exportJobs.emplace_back(std::make_unique<ExportFont>(common::ChunkFlags_t(kFont), rDirectoryEntry.path()));
				}
				else if (it->second == kModel)
				{
					exportJobs.emplace_back(std::make_unique<ExportModel>(rDirectoryEntry.path().native().find(L".GLTF_MODEL") == std::wstring::npos ? common::ChunkFlags_t(kModel) : common::ChunkFlags_t({kModel, kGltfModel}), rDirectoryEntry.path()));
				}
				else if (it->second == kShaderCompute || it->second == kShaderFragment || it->second == kShaderVertex)
				{
					exportJobs.emplace_back(std::make_unique<ExportShader>(common::ChunkFlags_t({it->second}), rDirectoryEntry.path()));
				}
				else if (it->second == kTexture)
				{
					if (rDirectoryEntry.path().native().find(L"Gltf") == std::wstring::npos)
					{
						if (rDirectoryEntry.path().native().find(L".ktx") != std::wstring::npos)
						{
							exportJobs.emplace_back(std::make_unique<ExportTexture>(common::ChunkFlags_t({it->second, kCubemap}), rDirectoryEntry.path()));
						}
						else if (rDirectoryEntry.path().native().find(L"[C]") == std::wstring::npos)
						{
							exportJobs.emplace_back(std::make_unique<ExportTexture>(common::ChunkFlags_t({it->second}), rDirectoryEntry.path()));
						}
					}
				}
				else if (it->second == kRawTexture)
				{
					exportJobs.emplace_back(std::make_unique<ExportTexture>(common::ChunkFlags_t({kTexture, kRawTexture}), rDirectoryEntry.path()));
				}
				else
				{
					throw std::exception("Unknown export job flags");
				}
			}
		}
	}

	bool bCleanExport = false; // IsDebuggerPresent();
	if (gpFileManager->mbCleanExport)
	{
		LOG("Clean export");
		bCleanExport = true;
	}
	else
	{
		{
			common::DataHeader dataHeader {};
			std::fstream fileStream(gpFileManager->mDataFile, std::ios::in | std::ios::binary);
			fileStream.read(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
			if (dataHeader.iVersion != common::DataHeader::kiVersion)
			{
				LOG("Data version mismatch {} -> {}, clean export", dataHeader.iVersion, common::DataHeader::kiVersion);
				bCleanExport = true;
			}
		}

		{
			common::DataHeader dataHeader {};
			std::fstream fileStream(gpFileManager->mTexturesFile, std::ios::in | std::ios::binary);
			fileStream.read(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
			if (dataHeader.iVersion != common::DataHeader::kiVersion)
			{
				LOG("Textures version mismatch {} -> {}, clean export", dataHeader.iVersion, common::DataHeader::kiVersion);
				bCleanExport = true;
			}
		}
	}

	if (bCleanExport)
	{
		std::filesystem::remove(gpFileManager->mDataHeader);
		std::filesystem::remove(gpFileManager->mDataFile);
		std::filesystem::remove(gpFileManager->mTexturesFile);
	}

	bool bOutofDate = bCleanExport;
	int64_t iExportJobs = exportJobs.size();
	for (std::unique_ptr<ExportJob>& rpExportJob : exportJobs)
	{
		if (rpExportJob->CheckDirty(bCleanExport))
		{
			bOutofDate = true;
		}

	#if 0
		if (rpExportJob->mInputPath.string().find("[C]") != std::string::npos)
		{
			rpExportJob->mbDirty = true;
			bOutofDate = true;
		}
	#endif
	}

	if (!bOutofDate)
	{
		LOG_INDENT(-1);
		LOG("Data Packer had nothing to export\n");
		return;
	}

	LOG("Running {} export jobs:", exportJobs.size());
	int64_t iExportJob = 1;
	for (std::unique_ptr<ExportJob>& rpExportJob : exportJobs)
	{
		LOG("  {}: {}{} Flags: {:#018x}", iExportJob++, rpExportJob->mbDirty ? "" : "(Not dirty) ", rpExportJob->mInputPath.string(), rpExportJob->mChunkFlags.muiUnderlying);
	}

	std::vector<std::future<std::vector<byte>&>> exportFutures;
	exportFutures.reserve(exportJobs.size());
	for (std::unique_ptr<ExportJob>& rpExportJob : exportJobs)
	{
		[[maybe_unused]] auto& future = exportFutures.emplace_back(std::async(std::launch::async, &ExportJob::RunExport, rpExportJob.get()));
		// future.get();
	}

	int64_t iDataChunks = 0;
	int64_t iTexturesChunks = 0;
	for (std::unique_ptr<ExportJob>& rpExportJob : exportJobs)
	{
		rpExportJob->mChunkFlags & kTexture ? ++iTexturesChunks : ++iDataChunks;
	}

	{
		std::fstream fileStreamForHeader(gpFileManager->mDataFileTemp, std::ios::out | std::ios::binary);
		common::DataHeader dataHeader {};
		dataHeader.iMagic = common::DataHeader::kiMagic;
		dataHeader.iVersion = common::DataHeader::kiVersion;
		dataHeader.iChunkCount = iDataChunks;
		fileStreamForHeader.write(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
		common::AlignOutputStream(fileStreamForHeader);
		fileStreamForHeader.close();
	}

	{
		std::fstream fileStreamForHeader(gpFileManager->mTexturesFileTemp, std::ios::out | std::ios::binary);
		common::DataHeader dataHeader {};
		dataHeader.iMagic = common::DataHeader::kiMagic;
		dataHeader.iVersion = common::DataHeader::kiVersion;
		dataHeader.iChunkCount = iTexturesChunks;
		fileStreamForHeader.write(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
		common::AlignOutputStream(fileStreamForHeader);
		fileStreamForHeader.close();
	}

	std::filesystem::remove(gpFileManager->mDataHeaderTemp);
	std::fstream dataHeaderTempFileStream(gpFileManager->mDataHeaderTemp, std::ios::out);
	dataHeaderTempFileStream << "#pragma once" << std::endl;
	dataHeaderTempFileStream << std::endl;
	dataHeaderTempFileStream << "// This file is automatically generated by the Data Packer" << std::endl;
	dataHeaderTempFileStream << "// To add a Crc to this file place your data file in: BrokenEngine/Engine/Data" << std::endl;
	dataHeaderTempFileStream << "// or BrokenEngine/Projects/YOUR_PROJECT/Data" << std::endl;
	dataHeaderTempFileStream << "// Then re-build and the Data Packer will run automatically as a pre-build event" << std::endl;
	dataHeaderTempFileStream << std::endl;
	dataHeaderTempFileStream << "namespace data" << std::endl;
	dataHeaderTempFileStream << "{" << std::endl;
	dataHeaderTempFileStream << std::endl;

	bool bFailed = false;
	std::vector<common::crc_t> textureCrcs;
	std::vector<common::crc_t> textureCrcsUi;
	for (int64_t i = 0; i < iExportJobs; ++i)
	{
		auto& rpExportJob = exportJobs[i];
		auto& rFuture = exportFutures[i];

		try
		{
			std::vector<byte>& rData = rFuture.get();

			std::filesystem::path& rFile = rpExportJob->mChunkFlags & kTexture ? gpFileManager->mTexturesFileTemp : gpFileManager->mDataFileTemp;
			std::fstream fileStream(rFile, std::ios::out | std::ios::binary | std::fstream::app);
			fileStream.write(reinterpret_cast<char*>(rData.data()), rData.size());
			common::AlignOutputStream(fileStream);

			std::string relativeFileOriginal = rpExportJob->mRelativeDirectory.string();
			relativeFileOriginal += rpExportJob->mInputPath.filename().string();
			common::crc_t crc = common::Crc(relativeFileOriginal);

			std::string relativeFile;
			for (const char& rChar : relativeFileOriginal)
			{
				relativeFile.push_back(rChar);
				if (rChar == '\\')
				{
					relativeFile.push_back('\\');
				}
			}

			std::string crcConstant = relativeFile;
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), '\\'), crcConstant.end());
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), '.'), crcConstant.end());
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), ' '), crcConstant.end());
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), '['), crcConstant.end());
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), ']'), crcConstant.end());
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), '-'), crcConstant.end());
			crcConstant.erase(std::remove(crcConstant.begin(), crcConstant.end(), ','), crcConstant.end());

			dataHeaderTempFileStream << "inline constexpr common::crc_t k" << crcConstant << "Crc = " << crc << ";" << std::endl;

			if (rpExportJob->mChunkFlags & kTexture && !(rpExportJob->mChunkFlags & kCubemap) && !(rpExportJob->mChunkFlags & kElevation) && rpExportJob->mInputPath.native().find(L"Gltf") == std::wstring::npos)
			{
				if (rpExportJob->mInputPath.native().find(L"Textures\\Ui") != std::wstring::npos)
				{
					textureCrcsUi.emplace_back(crc);
				}
				else
				{
					textureCrcs.emplace_back(crc);
				}
			}
		}
		catch (std::exception& rException)
		{
			LOG("ERROR FAILED Exception thrown from export future {}: \"{}\"", i + 1, rException.what());
			DEBUG_BREAK();
			bFailed = true;
		}
	}

	dataHeaderTempFileStream << std::endl;
	dataHeaderTempFileStream << "inline constexpr int64_t kiTextureCount = " << textureCrcs.size() << ";" << std::endl;
	dataHeaderTempFileStream << "inline constexpr common::crc_t kpTextureCrcs[] = " << std::endl;
	dataHeaderTempFileStream << "{" << std::endl;
	for (const common::crc_t& rCrc : textureCrcs)
	{
		dataHeaderTempFileStream << rCrc;
		dataHeaderTempFileStream << ", ";
	}
	dataHeaderTempFileStream << std::endl << "};" << std::endl;

	dataHeaderTempFileStream << std::endl;
	dataHeaderTempFileStream << "inline constexpr int64_t kiUiTextureCount = " << textureCrcsUi.size() << ";" << std::endl;
	dataHeaderTempFileStream << "inline constexpr common::crc_t kpUiTextureCrcs[] = " << std::endl;
	dataHeaderTempFileStream << "{" << std::endl;
	for (const common::crc_t& rCrc : textureCrcsUi)
	{
		dataHeaderTempFileStream << rCrc;
		dataHeaderTempFileStream << ", ";
	}
	dataHeaderTempFileStream << std::endl << "};" << std::endl;

	if (bFailed)
	{
		LOG("\n\n\nFAILED\n\n\n");

		dataHeaderTempFileStream.close();

		std::filesystem::remove(gpFileManager->mDataHeaderTemp);
		std::filesystem::remove(gpFileManager->mDataFileTemp);
		std::filesystem::remove(gpFileManager->mTexturesFileTemp);
	}
	else
	{
		dataHeaderTempFileStream << std::endl;
		dataHeaderTempFileStream << "} // namespace data" << std::endl;
		dataHeaderTempFileStream.close();

		// Only copy header if it has changed
		bool bCopy = true;
		if (std::filesystem::exists(gpFileManager->mDataHeader))
		{
			dataHeaderTempFileStream = std::fstream(gpFileManager->mDataHeaderTemp, std::ios::in);
			std::vector<char> dataHeaderTemp(std::filesystem::file_size(gpFileManager->mDataHeaderTemp));
			dataHeaderTempFileStream.read(dataHeaderTemp.data(), dataHeaderTemp.size());
			dataHeaderTempFileStream.close();
			std::fstream dataHeaderFileStream(gpFileManager->mDataHeader, std::ios::in);
			std::vector<char> dataHeader(std::filesystem::file_size(gpFileManager->mDataHeader));
			dataHeaderFileStream.read(dataHeader.data(), dataHeader.size());
			dataHeaderFileStream.close();

			if (dataHeaderTemp.size() == dataHeader.size())
			{
				bCopy = false;
				for (int64_t i = 0; i < static_cast<int64_t>(dataHeaderTemp.size()); ++i)
				{
					if (dataHeaderTemp[i] != dataHeader[i])
					{
						bCopy = true;
					}
				}
			}
		}

		if (bCopy)
		{
			std::filesystem::remove(gpFileManager->mDataHeader);
			std::filesystem::copy_file(gpFileManager->mDataHeaderTemp, gpFileManager->mDataHeader);
		}

		std::filesystem::remove(gpFileManager->mDataFile);
		std::filesystem::copy_file(gpFileManager->mDataFileTemp, gpFileManager->mDataFile);

		std::filesystem::remove(gpFileManager->mTexturesFile);
		std::filesystem::copy_file(gpFileManager->mTexturesFileTemp, gpFileManager->mTexturesFile);
	}

	LOG_INDENT(-1);
	LOG("Finished running Data Packer\n");
}

void Quit(const char* pcMessage, const char* pcTitle)
{
	fflush(stdout);
	MessageBox(nullptr, pcMessage, pcTitle, MB_OK | MB_SYSTEMMODAL);
}

int main(int argc, char* argv[])
{
#if defined(_CRTDBG_MAP_ALLOC)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	if (IsDebuggerPresent() == TRUE)
	{
		MainThread(argc, argv);
	}
	else
	{
		try
		{
			MainThread(argc, argv);
		}
		catch (std::exception& rException)
		{
			Quit(rException.what(), "Data Packer - std::exception");
		}
		catch (...)
		{
			Quit("", "Data Packer - Unknown exception");
		}
	}

	fflush(stdout);
	return 0;
}

#if defined(_CRTDBG_MAP_ALLOC)

_Ret_notnull_ _Post_writable_byte_size_(_Size) _VCRT_ALLOCATOR void* __CRTDECL operator new(size_t _Size)
{
	return malloc(_Size);
}

void __CRTDECL operator delete(void* _Block) noexcept
{
	return free(_Block);
}

_Ret_notnull_ _Post_writable_byte_size_(_Size) _VCRT_ALLOCATOR void* __CRTDECL operator new[](size_t _Size)
{
	return malloc(_Size);
}

void __CRTDECL operator delete[](void* _Block) noexcept
{
	return free(_Block);
}

#endif
