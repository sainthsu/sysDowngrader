/*
 *  sysUpdater is an update app for the Nintendo 3DS.
 *  Copyright (C) 2015 profi200
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/
 */


#include <string>
#include <vector>
#include <cstring>
#include <3ds.h>
#include "fs.h"
#include "misc.h"
#include "title.h"

#define _FILE_ "title.cpp" // Replacement for __FILE__ without the path



std::vector<TitleInfo> getTitleInfos(FS_MediaType mediaType)
{
	char tmpStr[16];
	extern u8 sysLang; // We got this in main.c
	u32 count, bytesRead;
	Result res;
	Handle fileHandle;
	TitleInfo tmpTitleInfo;

	u32 archiveLowPath[4] = {0, 0, mediaType, 0};
	//const FS_Archive iconArchive = {0x2345678A, {PATH_BINARY, 0x10, (u8*)archiveLowPath}};
	const u32 fileLowPath[5] = {0, 0, 2, 0x6E6F6369, 0};
	const FS_Path filePath = {PATH_BINARY, 0x14, (const u8*)fileLowPath};


	if((res = AM_GetTitleCount(mediaType, &count))) throw titleException(_FILE_, __LINE__, res, "Failed to get title count!");


	std::vector<TitleInfo> titleInfos; titleInfos.reserve(count);
	Buffer<u64> titleIdList(count, false);
	Buffer<AM_TitleEntry> titleList(count, false);
	Buffer<Icon> icon(1, false);


	u32 throwaway;
	if((res = AM_GetTitleList(&throwaway, mediaType, count, &titleIdList))) throw titleException(_FILE_, __LINE__, res, "Failed to get title ID list!");
	if((res = AM_GetTitleInfo(mediaType, count, &titleIdList, &titleList))) throw titleException(_FILE_, __LINE__, res, "Failed to get title list!");
	for(u32 i=0; i<count; i++)
	{
		// Copy title ID, size and version directly
		memcpy(&tmpTitleInfo.titleID, &titleList[i].titleID, 18);
		if(AM_GetTitleProductCode(mediaType, titleIdList[i], tmpStr)) memset(tmpStr, 0, 16);
		tmpTitleInfo.productCode = tmpStr;

		// Copy the title ID into our archive low path
		memcpy(archiveLowPath, &titleIdList[i], 8);
		icon.clear();
		if(!FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SAVEDATA_AND_CONTENT, (FS_Path){PATH_BINARY, 0x10, (u8*)archiveLowPath}, filePath, FS_OPEN_READ, 0))
		{
			// Nintendo decided to release a title with an icon entry but with size 0 so this will fail.
			// Ignoring errors because of this here.
			FSFILE_Read(fileHandle, &bytesRead, 0, &icon, sizeof(Icon));
			FSFILE_Close(fileHandle);
		}

		tmpTitleInfo.title = icon[0].appTitles[sysLang].longDesc;
		tmpTitleInfo.publisher = icon[0].appTitles[sysLang].publisher;
		memcpy(tmpTitleInfo.icon, icon[0].icon48, 0x1200);

		titleInfos.push_back(tmpTitleInfo);
	}

	return titleInfos;
}


void installCia(const std::u16string& path, FS_MediaType mediaType, std::function<void (const std::u16string& file, u32 percent)> callback)
{
	fs::File ciaFile(path, FS_OPEN_READ), cia;
	Buffer<u8> buffer(MAX_BUF_SIZE, false);
	Handle ciaHandle;
	u32 blockSize;
	u64 ciaSize, offset = 0;
	Result res;



	ciaSize = ciaFile.size();
	if((res = AM_StartCiaInstall(mediaType, &ciaHandle))) throw titleException(_FILE_, __LINE__, res, "Failed to start CIA installation!");
	cia.setFileHandle(ciaHandle); // Use the handle returned by AM


	for(u32 i=0; i<=ciaSize / MAX_BUF_SIZE; i++)
	{
		blockSize = ((ciaSize - offset<MAX_BUF_SIZE) ? ciaSize - offset : MAX_BUF_SIZE);

		if(blockSize>0)
		{
			try
			{
				ciaFile.read(&buffer, blockSize);
				cia.write(&buffer, blockSize);
			} catch(fsException& e)
			{
				AM_CancelCIAInstall(ciaHandle); // Abort installation
				cia.setFileHandle(0); // Reset the handle so it doesn't get closed twice
				throw;
			}

			offset += blockSize;
			if(callback) callback(path, offset * 100 / ciaSize);
		}
	}

	if((res = AM_FinishCiaInstall(ciaHandle))) throw titleException(_FILE_, __LINE__, res, "Failed to finish CIA installation!");
}


void deleteTitle(FS_MediaType mediaType, u64 titleID)
{
	Result res;

	// System app
	if(titleID>>32 & 0xFFFF) {if((res = AM_DeleteTitle(mediaType, titleID))) throw titleException(_FILE_, __LINE__, res, "Failed to delete system title!");} // Who likes ambiguous else?
	// Normal app
	else if((res = AM_DeleteAppTitle(mediaType, titleID))) throw titleException(_FILE_, __LINE__, res, "Failed to delete app title!");
}
