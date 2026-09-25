// scoot would - localisation (English / Japanese). English source text is the key: T("PLAY") returns the
// translation of the active language, or the text itself when there is none. The UI translates every string
// it draws, so most code never calls T() directly; composed strings (formats, trick names) do.
#pragma once

#include <string>
#include <string_view>

namespace sw {

enum class Language : int { English = 0, Japanese = 1 };

namespace i18n {
void init();                     // loads the string tables (assets/data/lang/<code>.json)
void setLanguage(Language l);
Language language();
Language systemLanguage();       // from the OS preferred locales
const char* languageName(Language l);  // in its own language ("English", "日本語")
// exact translation, or nullptr
const std::string* find(std::string_view english);
// exact translation or the text itself
std::string tr(std::string_view english);
// trick / combo names: whole string, then " + " separated parts, then the longest known phrases word by word
std::string trPhrase(std::string_view english);
}  // namespace i18n

inline std::string T(std::string_view s) { return i18n::tr(s); }

}  // namespace sw
