// scoot would - file system helpers
// All game data paths are relative to the data root (folder that contains
// assets/, shaders/ and config/). User data (save, settings, logs, shader cache)
// lives in the per-user preference folder.
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace sw::fs {

// locate data root (searches exe dir and its parents for assets/)
bool init();
const std::string& dataRoot();
const std::string& userDir();

// resolve "assets/..." or "shaders/..." to an absolute path
std::string resolve(const std::string& relative);
std::string userPath(const std::string& relative);

bool exists(const std::string& absPath);
std::optional<std::string> readText(const std::string& absPath);
std::optional<std::vector<uint8_t>> readBinary(const std::string& absPath);
bool writeText(const std::string& absPath, const std::string& text);
bool writeBinary(const std::string& absPath, const void* data, size_t size);
// atomic write (temp file + rename) with a .bak backup of the previous version
bool writeTextSafe(const std::string& absPath, const std::string& text);
int64_t modifiedTime(const std::string& absPath);
bool createDirectories(const std::string& absPath);
std::vector<std::string> listFiles(const std::string& absDir, const std::string& extension = "");
std::string fileName(const std::string& path);
std::string stem(const std::string& path);
std::string parentPath(const std::string& path);
std::string extension(const std::string& path);

uint64_t hash64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ULL);
inline uint64_t hash64(const std::string& s, uint64_t seed = 0xcbf29ce484222325ULL) { return hash64(s.data(), s.size(), seed); }

}  // namespace sw::fs
