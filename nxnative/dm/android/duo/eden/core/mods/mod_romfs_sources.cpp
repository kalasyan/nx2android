// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// nx2android: the named romfs sources from the data pack (duo_platform.h). Eden built them from the game file
// (the base NCA alone, and the add-on content's data NCAs); the port has the merged RomFS and the add-on's own
// folder, so "base:" reads the merged RomFS (an update ships only the files it changes) and "aoc:" the add-on's.

#include "core/mods/mod_romfs_sources.h"
#include "duo_platform.h"

namespace Core::Mods {

FileSys::VirtualDir OpenBaseRomfs(Core::System&, u64) {
    return DuoHost::GameRomfs();
}

FileSys::VirtualDir OpenAocRomfs(Core::System&, u64) {
    return DuoHost::AocRomfs();
}

void ReleaseRomfsSources() {}

} // namespace Core::Mods
