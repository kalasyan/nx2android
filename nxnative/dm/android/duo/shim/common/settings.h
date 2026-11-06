// nx2android: the two settings the runtime reads: no add-on is disabled, and the system language is the port's (duo_host.cpp)
#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"
namespace Settings {
enum class Language : u32 { Japanese, EnglishAmerican, French, German, Italian, Spanish, Chinese, Korean, Dutch, Portuguese, Russian, Taiwanese,
    EnglishBritish, FrenchCanadian, SpanishLatin, ChineseSimplified, ChineseTraditional, PortugueseBrazilian, Polish, Thai };
Language DuoLanguage();
struct LanguageSetting { Language GetValue() const { return DuoLanguage(); } };
struct Values { std::map<u64, std::vector<std::string>> disabled_addons; LanguageSetting language_index; };
inline Values values;
inline bool IsNceEnabled() { return true; }
std::string_view CanonicalizeEnum(Language l);
} // namespace Settings
