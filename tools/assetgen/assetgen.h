// scoot would asset tools - generators for original game content
#pragma once

#include <string>

namespace sw::tools {

// skinned rider with clothing variants and animation clips (glTF binary)
bool generateRider(const std::string& outPath);
// sound effects (WAV files) into a directory
bool generateAudio(const std::string& dir);
// decals, signs, shop fronts, detail normals (PNG) into <root>/assets/textures/gen
bool generateTextures(const std::string& root);

}  // namespace sw::tools
