#include "core/i18n.h"
#include "core/filesystem.h"
#include "core/json.h"
#include "core/log.h"

#include <SDL3/SDL.h>

#include <unordered_map>
#include <vector>

namespace sw::i18n {

namespace {
Language g_lang = Language::English;
std::unordered_map<std::string, std::string> g_ja;
bool g_loaded = false;

const std::unordered_map<std::string, std::string>* table() {
    if (g_lang == Language::Japanese) return &g_ja;
    return nullptr;
}
}  // namespace

void init() {
    if (g_loaded) return;
    g_loaded = true;
    auto j = loadJsonFile(fs::resolve("assets/data/lang/ja.json"));
    if (!j || !j->is_object()) {
        LOG_WARN("i18n: no Japanese string table");
        return;
    }
    for (auto& [k, v] : j->items())
        if (v.is_string() && k.rfind("_", 0) != 0) g_ja[k] = v.get<std::string>();
    LOG_INFO("i18n: %zu Japanese strings", g_ja.size());
}

void setLanguage(Language l) {
    init();
    g_lang = l;
}

Language language() { return g_lang; }

Language systemLanguage() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    Language l = Language::English;
    if (locales) {
        for (int i = 0; i < count && locales[i]; ++i) {
            const char* lang = locales[i]->language;
            if (lang && std::string(lang) == "ja") {
                l = Language::Japanese;
                break;
            }
            if (lang && std::string(lang) == "en") break;
        }
        SDL_free(locales);
    }
    return l;
}

const char* languageName(Language l) { return l == Language::Japanese ? "日本語" : "English"; }

const std::string* find(std::string_view english) {
    auto t = table();
    if (!t || english.empty()) return nullptr;
    auto it = t->find(std::string(english));
    return it == t->end() ? nullptr : &it->second;
}

std::string tr(std::string_view english) {
    const std::string* s = find(english);
    return s ? *s : std::string(english);
}

std::string trPhrase(std::string_view english) {
    if (!table()) return std::string(english);
    if (const std::string* s = find(english)) return *s;
    // combo chains
    std::string src(english);
    size_t plus = src.find(" + ");
    if (plus != std::string::npos) {
        std::string out;
        size_t start = 0;
        while (true) {
            size_t p = src.find(" + ", start);
            out += trPhrase(src.substr(start, p == std::string::npos ? std::string::npos : p - start));
            if (p == std::string::npos) break;
            out += " + ";
            start = p + 3;
        }
        return out;
    }
    // words: take the longest run of words that has a translation ("Double Tailwhip" before "Double")
    std::vector<std::string> words;
    for (size_t i = 0; i < src.size();) {
        size_t sp = src.find(' ', i);
        if (sp == std::string::npos) sp = src.size();
        if (sp > i) words.push_back(src.substr(i, sp - i));
        i = sp + 1;
    }
    std::string out;
    for (size_t i = 0; i < words.size();) {
        size_t best = 0;
        std::string bestText;
        std::string run;
        for (size_t n = 1; n <= 4 && i + n <= words.size(); ++n) {
            run += (n > 1 ? " " : "") + words[i + n - 1];
            if (const std::string* s = find(run)) {
                best = n;
                bestText = *s;
            }
        }
        if (!out.empty()) out += " ";
        if (best == 0) {
            out += words[i];
            ++i;
        } else {
            out += bestText;
            i += best;
        }
    }
    return out;
}

}  // namespace sw::i18n
