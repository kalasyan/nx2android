// nx2android: what the dual-screen runtime asks the port for instead of Eden's emulator (duo_host.cpp). Folders are the data pack's.
#pragma once
#include <filesystem>
#include <string>
#include "common/common_types.h"
#include <vector>
#include "core/file_sys/vfs/vfs_types.h"

namespace DuoHost {
/// <data pack>/RomFS: the game's RomFS as the port runs it (base + update merged by porter)
FileSys::VirtualDir GameRomfs();
/// <data pack>/aoc/<first add-on>: the add-on content's data RomFS, or null without DLC
FileSys::VirtualDir AocRomfs();
/// the companion packages, as Eden's load/<title> folder has them: <package>/dualscreen/manifest.json and its files
FileSys::VirtualDir PackageDir();
/// a private, writable folder of the companion's (persisted flags, the player's own files, save-free state)
std::filesystem::path StateDir();
/// the title's native module, built into the APK's native library folder
std::string ModulePath(u64 title_id);
/// the main module's build id (hex, 32 or 64 digits) and where main's .text starts and ends
std::string MainBuildId();
void MainRegion(u64& begin, u64& size);
/// the language the port gives the game (GetDesiredLanguage), as Eden names an ApplicationLanguage: "AmericanEnglish", "Spanish", ...
std::string GameLanguage();
/// the game's own icon (the NACP icon JPEG porter stores in the data pack), empty if none
std::vector<u8> GameIconJpeg();
} // namespace DuoHost
