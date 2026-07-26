#pragma once

// A single static voxel model shown on the top screen (menu decoration).
//
// The asset is produced by 3ds/tools/mkvoxmodel.py: vertices already carry the
// palette UV and baked per-face light, so the model draws through the ordinary
// world pipeline with no extra shader.

namespace model3ds {
    // Loads a .vcm3ds asset. Returns false and logs on any problem; the caller
    // is expected to keep going without the decoration.
    bool load(const char* path);
    void unload();
    bool ready();

    // Spins the model around its Y axis, auto-fitted to the top screen.
    // Requires gfx3ds::setEye() (or useWorldPipeline()) beforehand.
    void draw(float angleRad, float tiltRad = 0.34f);
}
