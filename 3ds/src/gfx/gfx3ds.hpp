#pragma once

// citro3d graphics context for the VoxelCore 3DS port.

#include <3ds.h>
#include <citro3d.h>
#include <glm/glm.hpp>

namespace gfx3ds {
    // top screen 3D target (400x240)
    bool init();
    void shutdown();

    void beginFrame(float r, float g, float b);
    void endFrame();

    // upload model-view matrix; projection (PICA-tilted) applied on top.
    // focalLen > 0 together with stereo asks for an off-axis eye projection:
    // that plane lands on the screen, nearer geometry pops out.
    void setViewProjection(
        const glm::mat4& view, float fovDeg, float zNear, float zFar,
        float focalLen = 0.0f);
    void setModel(const glm::mat4& model);

    // bind the world vertex format (ChunkVertex)
    void useWorldPipeline();

    // stereoscopic top screen. The right-eye target only exists while enabled,
    // so gameplay keeps its VRAM. eyes() is 1 when off, 2 when on.
    void setStereo(bool enabled);
    int eyes();
    void setEye(int eye);

    // switch drawing to the bottom screen and bind the 2D UI pipeline
    void useEntityPipeline();
void beginTopUI();
void beginBottomUI(float r, float g, float b);
}
