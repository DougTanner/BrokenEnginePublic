#include "ExportIsland.h"

#include "Texture.h"

using enum common::ChunkFlags;

void ExportIsland::PreExport()
{
	std::filesystem::path ambientOcclusionFloat32File(mInputPath);
	ambientOcclusionFloat32File /= "AmbientOcclusion.r32";
	if (std::filesystem::exists(ambientOcclusionFloat32File))
	{
		Texture texture(ambientOcclusionFloat32File, FileType::kFloat32, true, kiIslandSize, kiIslandSize);
		texture.MakeMipmaps(VK_FORMAT_BC4_UNORM_BLOCK, 2);
		texture.mData.erase(texture.mData.begin());
		texture.miWidth /= kiAmbientOcclusionDivisor;
		texture.miHeight /= kiAmbientOcclusionDivisor;

		std::filesystem::path path(mInputPath);
		path /= kpcIslandAmbientOcclusion;
		texture.Save(path, VK_FORMAT_BC4_UNORM_BLOCK, false);

		std::filesystem::remove(ambientOcclusionFloat32File);
	}

	std::filesystem::path colorExrFile(mInputPath);
	colorExrFile /= "Color.exr";
	if (std::filesystem::exists(colorExrFile))
	{
		Texture texture(colorExrFile, FileType::kExr, true);
		std::filesystem::path path(mInputPath);
		path /= kpcIslandColor;
		texture.Save(path, VK_FORMAT_BC7_UNORM_BLOCK, true);

		std::filesystem::remove(colorExrFile);
	}

	std::filesystem::path elevationFloat32File(mInputPath);
	elevationFloat32File /= "Elevation.r32";
	if (std::filesystem::exists(elevationFloat32File))
	{
		Texture texture(elevationFloat32File, FileType::kFloat32, false, kiIslandSize, kiIslandSize);
		texture.MakeMipmaps(VK_FORMAT_R16_UNORM, 3);
		texture.mData.erase(texture.mData.begin());
		texture.mData.erase(texture.mData.begin());
		texture.miWidth /= kiElevationDivisor;
		texture.miHeight /= kiElevationDivisor;

		std::filesystem::path path(mInputPath);
		path /= kpcIslandElevation;
		texture.Save(path, VK_FORMAT_R16_UNORM, false);

		std::filesystem::remove(elevationFloat32File);
	}

	std::filesystem::path normalsExrFile(mInputPath);
	normalsExrFile /= "Normals.exr";
	if (std::filesystem::exists(normalsExrFile))
	{
		Texture texture(normalsExrFile, FileType::kExr, true);

		std::filesystem::path path(mInputPath);
		path /= kpcIslandNormals;
		texture.Save(path, VK_FORMAT_BC7_UNORM_BLOCK, false);

		std::filesystem::remove(normalsExrFile);
	}
}

void ExportIsland::Export()
{
	// Beach elevation
	std::filesystem::path elevationU16File(mInputPath);
	elevationU16File /= kpcIslandElevation;
	std::fstream fileStreamU16(elevationU16File, std::ios::in | std::ios::binary);
	std::vector<byte> dataU16(std::filesystem::file_size(elevationU16File));
	fileStreamU16.read(reinterpret_cast<char*>(dataU16.data()), dataU16.size());
	fileStreamU16.close();

	uint16_t* puiPixels = reinterpret_cast<uint16_t*>(dataU16.data());
	std::unordered_map<uint16_t, int64_t> map;
	int64_t iElevationSize = kiIslandSize / kiElevationDivisor;
	for (int64_t i = 0; i < iElevationSize * iElevationSize; ++i)
	{
		map[puiPixels[i]]++;
	}

	int64_t iMaxCount = 0;
	uint16_t uiBeachElevation = 0;
	for (const auto& rElement : map)
	{
		if (rElement.first != 0 && rElement.second > iMaxCount)
		{
			iMaxCount = rElement.second;
			uiBeachElevation = rElement.first;
		}
	}
	LOG("Beach elevation: {} {} ({} times)", uiBeachElevation, common::UnormToFloat(uiBeachElevation), iMaxCount);

	// Save crcs and height
	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	auto [pHeader, dataSpan] = AllocateHeaderAndData(1);

	std::filesystem::path ambientOcclusionFile(relativeFile);
	ambientOcclusionFile /= kpcIslandAmbientOcclusion;
	pHeader->islandHeader.ambientOcclusionCrc = common::Crc(ambientOcclusionFile.string());

	std::filesystem::path colorsFile(relativeFile);
	colorsFile /= kpcIslandColor;
	pHeader->islandHeader.colorsCrc = common::Crc(colorsFile.string());

	std::filesystem::path elevationFile(relativeFile);
	elevationFile /= kpcIslandElevation;
	pHeader->islandHeader.elevationCrc = common::Crc(elevationFile.string());

	std::filesystem::path normalsFile(relativeFile);
	normalsFile /= kpcIslandNormals;
	pHeader->islandHeader.normalsCrc = common::Crc(normalsFile.string());

	pHeader->islandHeader.uiBeachElevation = uiBeachElevation;
}
