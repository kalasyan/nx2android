// nx2android: the shared-font table and decryptor of Eden's pl:u service (platform_service_manager.h/.cpp), for the "sysfont:" source
#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
namespace Service::NS {
enum class FontArchives : u64 { Extension = 0x0100000000000810, Standard = 0x0100000000000811, Korean = 0x0100000000000812,
    ChineseTraditional = 0x0100000000000813, ChineseSimple = 0x0100000000000814 };
constexpr std::array<std::pair<FontArchives, const char*>, 7> SHARED_FONTS{
    std::make_pair(FontArchives::Standard, "nintendo_udsg-r_std_003.bfttf"),
    std::make_pair(FontArchives::ChineseSimple, "nintendo_udsg-r_org_zh-cn_003.bfttf"),
    std::make_pair(FontArchives::ChineseSimple, "nintendo_udsg-r_ext_zh-cn_003.bfttf"),
    std::make_pair(FontArchives::ChineseTraditional, "nintendo_udjxh-db_zh-tw_003.bfttf"),
    std::make_pair(FontArchives::Korean, "nintendo_udsg-r_ko_003.bfttf"),
    std::make_pair(FontArchives::Extension, "nintendo_ext_003.bfttf"),
    std::make_pair(FontArchives::Extension, "nintendo_ext2_003.bfttf"),
};
inline void DecryptSharedFontToTTF(const std::vector<u32>& input, std::vector<u8>& output) {
    if (input.size() < 2) return;
    const u32 key = input[0] ^ 0x7f9a0218u; // EXPECTED_RESULT
    std::vector<u32> t(input.size());
    std::transform(input.begin(), input.end(), t.begin(), [key](u32 w) { return Common::swap32(w ^ key); });
    std::memcpy(output.data(), t.data() + 2, std::min(output.size(), (t.size() - 2) * sizeof(u32)));
}
} // namespace Service::NS
