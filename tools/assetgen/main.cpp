// scoot would asset generator
//   scoot_assetgen [--rider] [--audio] [--root <project dir>]
// With no selection flags every generator runs. Output goes into <root>/assets.
#include "assetgen.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    bool rider = false, audio = false;
    std::string root = ".";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rider")) rider = true;
        else if (!std::strcmp(argv[i], "--audio")) audio = true;
        else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else {
            std::printf("usage: %s [--rider] [--audio] [--root <project dir>]\n", argv[0]);
            return 1;
        }
    }
    if (!rider && !audio) rider = audio = true;
    namespace fs = std::filesystem;
    if (!fs::exists(fs::path(root) / "assets")) {
        std::printf("assetgen: %s has no assets/ directory (use --root)\n", root.c_str());
        return 1;
    }
    bool ok = true;
    if (rider) {
        fs::create_directories(fs::path(root) / "assets/models");
        ok &= sw::tools::generateRider((fs::path(root) / "assets/models/rider.glb").string());
    }
    if (audio) {
        fs::create_directories(fs::path(root) / "assets/audio");
        ok &= sw::tools::generateAudio((fs::path(root) / "assets/audio").string());
    }
    return ok ? 0 : 1;
}
