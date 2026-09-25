// scoot would - detailed pro freestyle scooter geometry (body space: origin between the axles at
// axle height, forward -Z, up +Y). Separate parts so tricks can move them independently:
// deck (with headtube, neck, dropouts), griptape, brake, fork, bars, grips, clamp, wheel (tyre + core).
#pragma once

#include "game/player/rider_blueprint.h"
#include "render/mesh.h"

namespace sw {

struct ScooterModelOptions {
    int deck = 0;    // 0 street 4.8", 1 wide 5.3", 2 park 4.5" (narrow, rounder nose), 3 truss neck 5.0" boxed
    int bars = 0;    // 0 standard 25" x 22", 1 tall 27" x 22", 2 wide 25" x 24", 3 Y-bar 25" x 22"
    int wheels = 0;  // 0 six spoke hollow core, 1 twelve spoke, 2 solid core
    int clamp = 0;   // 0 IHC double clamp (4 bolts, slit bar), 1 SCS (one piece over the headset, 4 bolts)
};

// grip height above the deck top / bar width for the bar option (the rider's hands follow these)
float scooterBarHeight(const ScooterDims& d, int bars);
float scooterBarWidth(const ScooterDims& d, int bars);

struct ScooterMeshSet {
    // material slots per part (see PlayerVisual::create):
    //   deck: 0 deck alu, 1 hardware, 2 headset | fork: 0 paint, 1 hardware | bars: 0 paint, 1 bar end
    //   clamp: 0 alu, 1 hardware | wheel core: 0 alu, 1 bearing, 2 hardware
    MeshData deck, grip, brake, fork, bars, grips, clamp, tyre, core;
    float barHeight = 0.79f, barWidth = 0.56f;  // resolved from the options
};

ScooterMeshSet buildScooterModel(const ScooterDims& dims, const ScooterModelOptions& opt);

}  // namespace sw
