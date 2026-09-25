// scoot would asset generator
//   scoot_assetgen [--rider] [--audio] [--textures] [--root <project dir>]
// With no selection flags every generator runs. Output goes into <root>/assets.
#include "assetgen.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    bool rider = false, audio = false, textures = false;
    std::string root = ".";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rider")) rider = true;
        else if (!std::strcmp(argv[i], "--audio")) audio = true;
        else if (!std::strcmp(argv[i], "--textures")) textures = true;
        else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else {
            std::printf("usage: %s [--rider] [--audio] [--textures] [--root <project dir>]\n", argv[0]);
            return 1;
        }
    }
    if (!rider && !audio && !textures) rider = audio = textures = true;
    namespace fs = std::filesystem;
    if (!fs::exists(fs::path(root) / "assets")) {
        std::printf("assetgen: %s has no assets/ directory (use --root)\n", root.c_str());
        return 1;
    }
    bool ok = true;
    if (rider) {
        fs::create_directories(fs::path(root) / "assets/models");
        std::string out = (fs::path(root) / "assets/models/rider.glb").string();
        if (fs::exists(fs::path(root) / "third_party/makehuman/base.obj"))
            ok &= sw::tools::generateHumanRider(root, out);
        else
            ok &= sw::tools::generateRider(out);
    }
    if (audio) {
        fs::create_directories(fs::path(root) / "assets/audio");
        ok &= sw::tools::generateAudio((fs::path(root) / "assets/audio").string());
    }
    if (textures) {
        fs::create_directories(fs::path(root) / "assets/textures/gen");
        ok &= sw::tools::generateTextures(root);
    }
    return ok ? 0 : 1;
}
