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

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <3ds.h>
#include "error.h"
#include "fs.h"
#include "misc.h"
#include "title.h"
#include "hashes.h"
#include "sha256.h"

#define _FILE_ "main.cpp" // Replacement for __FILE__ without the path

typedef struct
{
	std::u16string name;
	AM_TitleEntry entry;
	bool requiresDelete;
} TitleInstallInfo;

// Ordered from highest to lowest priority.
static const u32 titleTypes[7] = {
		0x00040138, // System Firmware
		0x00040130, // System Modules
		0x00040030, // Applets
		0x00040010, // System Applications
		0x0004001B, // System Data Archives
		0x0004009B, // System Data Archives (Shared Archives)
		0x000400DB, // System Data Archives
};

u32 getTitlePriority(u64 id) {
	u32 type = (u32) (id >> 32);
	for(u32 i = 0; i < 7; i++) {
		if(type == titleTypes[i]) {
			return i;
		}
	}

	return 0;
}

bool sortTitlesHighToLow(const TitleInstallInfo &a, const TitleInstallInfo &b) {
	bool aSafe = (a.entry.titleID & 0xFF) == 0x03;
	bool bSafe = (b.entry.titleID & 0xFF) == 0x03;
	if(aSafe != bSafe) {
		return aSafe;
	}

	return getTitlePriority(a.entry.titleID) < getTitlePriority(b.entry.titleID);
}

bool sortTitlesLowToHigh(const TitleInstallInfo &a, const TitleInstallInfo &b) {
        bool aSafe = (a.entry.titleID & 0xFF) == 0x03;
        bool bSafe = (b.entry.titleID & 0xFF) == 0x03;
        if(aSafe != bSafe) {
                return aSafe;
        }

	return getTitlePriority(a.entry.titleID) > getTitlePriority(b.entry.titleID);
}

// Fix compile error. This should be properly initialized if you fiddle with the title stuff!
u8 sysLang = 0;

// Override the default service exit functions
extern "C"
{
	void __appExit()
	{
		// Exit services
		amExit();
		sdmcArchiveExit();
		fsExit();
		hidExit();
		gfxExit();
		aptExit();
		srvExit();
	}
}

// Find title and compare versions. Returns CIA file version - installed title version
int versionCmp(std::vector<TitleInfo>& installedTitles, u64& titleID, u16 version)
{
	for(auto it : installedTitles)
	{
		if(it.titleID == titleID)
		{
			return (version - it.version);
		}
	}

	return 1; // The title is not installed
}


// If downgrade is true we don't care about versions (except equal versions) and uninstall newer versions
void installUpdates(bool downgrade)
{
	std::vector<fs::DirEntry> filesDirs = fs::listDirContents(u"/updates", u".cia;"); // Filter for .cia files
	std::vector<TitleInfo> installedTitles = getTitleInfos(MEDIATYPE_NAND);
	std::vector<TitleInstallInfo> titles;

	u8 is_n3ds = 0;
	APT_CheckNew3DS(&is_n3ds);

	Buffer<char> tmpStr(256);
	Result res;
	TitleInstallInfo installInfo;
	AM_TitleEntry ciaFileInfo;
	fs::File f;

	printf("正在获取固件文件信息...\n\n");

	for(auto it : filesDirs)
	{
		if(!it.isDir)
		{

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle())))
				throw titleException(_FILE_, __LINE__, res, "获取CIA文件信息失败!");

			if(ciaFileInfo.titleID != 0x0004013800000002LL && ciaFileInfo.titleID != 0x0004013820000002L)
				continue;

			if(ciaFileInfo.titleID == 0x0004013820000002LL && is_n3ds == 0)
				throw titleException(_FILE_, __LINE__, res, "在老3上安装N3D的包及易变砖!");
			if(ciaFileInfo.titleID == 0x0004013800000002LL && is_n3ds == 1 && ciaFileInfo.version > 11872)
				throw titleException(_FILE_, __LINE__, res, "在N3DS上安装>6.0的老3包及易变砖!");

			if(ciaFileInfo.titleID == 0x0004013800000002LL && is_n3ds == 1 && ciaFileInfo.version < 11872){
				printf("在N3DS上安装老3包会变砖，除非你换了NCSD和加密!\n");
				printf("!! 别继续了 !!\n!! 除非你是A9LH和REDNAND!!\n\n");
				printf("(A) 继续\n(a) 取消\n\n");
				while(aptMainLoop())
				{
					hidScanInput();

					if(hidKeysDown() & KEY_A)
						break;

					if(hidKeysDown() & KEY_B)
						throw titleException(_FILE_, __LINE__, res, "Canceled!");
				}
			}

			printf("获取固件文件版本...\n\n");
			printf("NATIVE_FIRM (");

			tmpStr.clear();
			utf16_to_utf8((u8*) &tmpStr, (u16*) it.name.c_str(), 255);
			printf("%s", &tmpStr);

			printf(") is v");
			printf("%i\n\n", ciaFileInfo.version);

			printf("验证固件文件...\n\n");

			for(auto const &firmVersionMap : firmVersions) {

				if(firmVersionMap.first == ciaFileInfo.version) {

					for(auto it2 : filesDirs) {
						for(auto const &devicesVersionMap : firmVersionMap.second) {

							tmpStr.clear();
							utf16_to_utf8((u8*) &tmpStr, (u16*) it2.name.c_str(), 255);

							if(&tmpStr == devicesVersionMap.first) {

								for(auto it3 : filesDirs) {
									for(auto const &regionVersionMap : devicesVersionMap.second) {

										tmpStr.clear();
										utf16_to_utf8((u8*) &tmpStr, (u16*) it3.name.c_str(), 255);

										if(&tmpStr == regionVersionMap.first) {

											if(filesDirs.size() > regionVersionMap.second.size()) throw titleException(_FILE_, __LINE__, res, "/updates/中发现太多的title!\n");
											if(filesDirs.size() < regionVersionMap.second.size()) throw titleException(_FILE_, __LINE__, res, "/updates/的title太少!\n");

											for(auto it4 : filesDirs) {

												tmpStr.clear();
												utf16_to_utf8((u8*) &tmpStr, (u16*) it4.name.c_str(), 255);

                        									fs::File ciaFile(u"/updates/" + it4.name, FS_OPEN_READ);
                      										Buffer<u8> shaBuffer(MAX_BUF_SIZE, false);
												u32 blockSize;
                      										u64 ciaSize, offset = 0;
                      										ciaSize = ciaFile.size();
												SHA256 sha256stream;

												sha256stream.reset();

												for(u32 i=0; i<=ciaSize / MAX_BUF_SIZE; i++)
												{
													blockSize = ((ciaSize - offset<MAX_BUF_SIZE) ? ciaSize - offset : MAX_BUF_SIZE);

													if(blockSize>0)
													{
														try
														{
															ciaFile.read(&shaBuffer, blockSize);
														} catch(fsException& e)
														{
															throw titleException(_FILE_, __LINE__, res, "无法读取文件!");
														}

														sha256stream.add(&shaBuffer, blockSize);
														offset += blockSize;
													}
												}

												printf("%s", &tmpStr);

												if(sha256stream.getHash() != regionVersionMap.second.find(&tmpStr)->second) {
													throw titleException(_FILE_, __LINE__, res, "\x1b[31m校对不匹配! 文件损害或错误!\x1b[0m\n\n");
												} else {
													printf("\x1b[32m 验证\x1b[0m\n");
												}

											}

										}

									}
								}

							}

						}
					}

		 		}
			}
			printf("\n\n\x1b[32m验证固件文件成功!\n\n\x1b[0m\n\n");
			printf("安装固件文件中...\n");
		}

	}

	for(auto it : filesDirs)
	{
		if(!it.isDir)
		{
			// Quick and dirty hack to detect these pesky
			// attribute files OSX creates.
			// This should rather be added to the
			// filter rules later.
			if(it.name[0] == u'.') continue;

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle()))) throw titleException(_FILE_, __LINE__, res, "获取CIA文件信息失败!");

			int cmpResult = versionCmp(installedTitles, ciaFileInfo.titleID, ciaFileInfo.version);
			if((downgrade && cmpResult != 0) || (cmpResult > 0))
			{
				installInfo.name = it.name;
				installInfo.entry = ciaFileInfo;
				installInfo.requiresDelete = downgrade && cmpResult < 0;

				titles.push_back(installInfo);
			}
		}
	}

	std::sort(titles.begin(), titles.end(), downgrade ? sortTitlesLowToHigh : sortTitlesHighToLow);

	for(auto it : titles)
	{
		bool nativeFirm = it.entry.titleID == 0x0004013800000002LL || it.entry.titleID == 0x0004013820000002LL;
		if(nativeFirm)
		{
			printf("NATIVE_FIRM         ");
		} else {
			tmpStr.clear();
			utf16_to_utf8((u8*) &tmpStr, (u16*) it.name.c_str(), 255);

			printf("%s", &tmpStr);
		}

		if(it.requiresDelete) deleteTitle(MEDIATYPE_NAND, it.entry.titleID);
		installCia(u"/updates/" + it.name, MEDIATYPE_NAND);
		if(nativeFirm && (res = AM_InstallFirm(it.entry.titleID))) throw titleException(_FILE_, __LINE__, res, "安装NATIVE_FIRM失败!");
		printf("\x1b[32m  已安装\x1b[0m\n");
	}
}

int main()
{
	gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);

	bool once = false;
	int mode;

	consoleInit(GFX_TOP, NULL);

	printf("sysDowngraderCN\n");
	printf("更多3DS汉化软件请访问youxijihe.com\n");
	printf("(A) 升级\n(Y) 降级\n(X) 测试svchax\n(B) 退出\n\n");
	printf("使用(HOME)键退出CIA版本.\n");
	printf("一旦开始安装无法取消!\n\n");
	printf("贡献名单:\n");
	printf(" + profi200\n");
	printf(" + aliaspider\n");
	printf(" + AngelSL\n");
	printf(" + Plailect\n");
	printf(" + youxijihe.com\n");


	while(aptMainLoop())
	{
		hidScanInput();


		if(hidKeysDown() & KEY_B)
			break;
		if(!once)
		{
			if(hidKeysDown() & (KEY_A | KEY_Y | KEY_X))
			{
				try
				{

					if ((bool)(hidKeysDown() & KEY_Y)) {
						mode = 0;
					} else if ((bool)(hidKeysDown() & KEY_A)) {
						mode = 1;
					} else {
						mode = 2;
					}

					consoleClear();

					if (getAMu() != 0) {
						printf("\x1b[31m无法获取 am:u 服务, 请重启\x1b[0m\n\n");
						while (aptMainLoop()) {
							svcSleepThread(10000000000LL);
						}
      		}

					if (mode == 0) {
						printf("开始降级...\n\n");
						installUpdates(true);
						printf("\n\n安装成功; 将在10后重启...\n");
					} else if (mode == 1) {
						printf("开始升级...\n\n");
						installUpdates(false);
						printf("\n\n安装成功; 将在10后重启......\n");
					} else {
						printf("测试svchax; 将在10后重启...\n");
					}

					svcSleepThread(10000000000LL);

					aptOpenSession();
					APT_HardwareResetAsync();
					aptCloseSession();

					once = true;
				}
				catch(fsException& e)
				{
					printf("\n%s\n", e.what());
					printf("是否已在'/updates'目录放置了升级文件?\n");
					printf("请重启.");
					once = true;
				}
				catch(titleException& e)
				{
					printf("\n%s\n", e.what());
					printf("请重启.");
					once = true;
				}
			}
		}
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	return 0;
}
