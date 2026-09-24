#include "core/filesystem.h"
#include "core/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace stdfs = std::filesystem;

namespace sw::fs {

namespace {
std::string g_dataRoot;
std::string g_userDir;

std::string normalizeDir(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    if (!p.empty() && p.back() != '/') p.push_back('/');
    return p;
}
}  // namespace

bool init() {
    const char* base = SDL_GetBasePath();
    stdfs::path start = base ? stdfs::path(base) : stdfs::current_path();
    // search the executable folder and up to 4 parents for a folder with assets/ and shaders/
    stdfs::path p = start;
    for (int i = 0; i < 5; ++i) {
        std::error_code ec;
        if (stdfs::exists(p / "assets", ec) && stdfs::exists(p / "shaders", ec)) {
            g_dataRoot = normalizeDir(p.string());
            break;
        }
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    if (g_dataRoot.empty()) {
        std::error_code ec;
        stdfs::path cwd = stdfs::current_path(ec);
        g_dataRoot = normalizeDir(cwd.string());
    }

    char* pref = SDL_GetPrefPath("scoot would", "scoot would");
    if (pref) {
        g_userDir = normalizeDir(pref);
        SDL_free(pref);
    } else {
        g_userDir = g_dataRoot + "user/";
    }
    createDirectories(g_userDir);
    createDirectories(g_userDir + "logs");
    createDirectories(g_userDir + "shadercache");
    return fs::exists(g_dataRoot + "assets");
}

const std::string& dataRoot() { return g_dataRoot; }
const std::string& userDir() { return g_userDir; }
std::string resolve(const std::string& relative) {
    if (relative.empty()) return g_dataRoot;
    if (relative[0] == '/' || (relative.size() > 1 && relative[1] == ':')) return relative;
    return g_dataRoot + relative;
}
std::string userPath(const std::string& relative) { return g_userDir + relative; }

bool exists(const std::string& absPath) {
    std::error_code ec;
    return stdfs::exists(stdfs::u8path(absPath), ec);
}

std::optional<std::string> readText(const std::string& absPath) {
    std::ifstream f(stdfs::u8path(absPath), std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::optional<std::vector<uint8_t>> readBinary(const std::string& absPath) {
    std::ifstream f(stdfs::u8path(absPath), std::ios::binary | std::ios::ate);
    if (!f) return std::nullopt;
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(size_t(std::max<std::streamsize>(size, 0)));
    if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
    return data;
}

bool writeText(const std::string& absPath, const std::string& text) { return writeBinary(absPath, text.data(), text.size()); }

bool writeBinary(const std::string& absPath, const void* data, size_t size) {
    createDirectories(parentPath(absPath));
    std::ofstream f(stdfs::u8path(absPath), std::ios::binary | std::ios::trunc);
    if (!f) {
        LOG_ERROR("fs: cannot write %s", absPath.c_str());
        return false;
    }
    f.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(f);
}

bool writeTextSafe(const std::string& absPath, const std::string& text) {
    std::string tmp = absPath + ".tmp";
    if (!writeText(tmp, text)) return false;
    std::error_code ec;
    if (exists(absPath)) stdfs::copy_file(stdfs::u8path(absPath), stdfs::u8path(absPath + ".bak"), stdfs::copy_options::overwrite_existing, ec);
    stdfs::rename(stdfs::u8path(tmp), stdfs::u8path(absPath), ec);
    if (ec) {
        // rename across volumes / locked file: fall back to direct write
        stdfs::remove(stdfs::u8path(tmp), ec);
        return writeText(absPath, text);
    }
    return true;
}

int64_t modifiedTime(const std::string& absPath) {
    std::error_code ec;
    auto t = stdfs::last_write_time(stdfs::u8path(absPath), ec);
    if (ec) return 0;
    return int64_t(t.time_since_epoch().count());
}

bool createDirectories(const std::string& absPath) {
    if (absPath.empty()) return false;
    std::error_code ec;
    stdfs::create_directories(stdfs::u8path(absPath), ec);
    return !ec;
}

std::vector<std::string> listFiles(const std::string& absDir, const std::string& ext) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : stdfs::directory_iterator(stdfs::u8path(absDir), ec)) {
        if (!e.is_regular_file()) continue;
        std::string p = e.path().generic_string();
        if (ext.empty() || extension(p) == ext) out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string fileName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}
std::string stem(const std::string& path) {
    std::string f = fileName(path);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? f : f.substr(0, d);
}
std::string parentPath(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string() : path.substr(0, s);
}
std::string extension(const std::string& path) {
    std::string f = fileName(path);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? std::string() : f.substr(d);
}

uint64_t hash64(const void* data, size_t size, uint64_t seed) {
    // FNV-1a 64
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace sw::fs
