// scoot would - gameplay events (consumed by audio, particles, HUD, camera, rumble, challenges)
#pragma once

#include "core/math.h"

#include <string>
#include <vector>

namespace sw {

enum class GameEventType {
    Push,
    Pop,
    Land,
    TrickStart,
    TrickLanded,
    GrindStart,
    GrindEnd,
    ManualStart,
    ManualEnd,
    Revert,
    Bail,
    Respawn,
    Checkpoint,
    ComboBanked,
    ComboFailed,
    Impact,
    Gap,
};

struct GameEvent {
    GameEventType type = GameEventType::Land;
    Vec3 position;
    Vec3 velocity;
    float magnitude = 0.0f;
    int surface = 0;
    int score = 0;
    int landing = 0;  // LandingResult
    std::string text;
};

}  // namespace sw
