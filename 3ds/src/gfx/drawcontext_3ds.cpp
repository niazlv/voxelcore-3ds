// DrawContext implementation for the 3DS port (citro3d state).
#include "graphics/core/DrawContext.hpp"

#include <3ds.h>
#include <citro3d.h>

#include "graphics/core/Batch2D.hpp"
#include "window/Window.hpp"

static void set_blend_mode(BlendMode mode) {
    switch (mode) {
        case BlendMode::normal:
            C3D_AlphaBlend(
                GPU_BLEND_ADD, GPU_BLEND_ADD,
                GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::addition:
            C3D_AlphaBlend(
                GPU_BLEND_ADD, GPU_BLEND_ADD,
                GPU_SRC_ALPHA, GPU_ONE,
                GPU_SRC_ALPHA, GPU_ONE);
            break;
        case BlendMode::inversion:
            C3D_AlphaBlend(
                GPU_BLEND_ADD, GPU_BLEND_ADD,
                GPU_ONE_MINUS_DST_COLOR, GPU_ONE_MINUS_SRC_ALPHA,
                GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

DrawContext::DrawContext(
    const DrawContext* parent, Window& window, Batch2D* g2d
)
    : window(window),
      parent(parent),
      viewport(window.getSize()),
      g2d(g2d),
      flushable(g2d) {
}

DrawContext::~DrawContext() {
    if (flushable) {
        flushable->flush();
    }
    while (scissorsCount--) {
        window.popScissor();
    }
    if (parent == nullptr) {
        return;
    }
    // restore parent state
    set_blend_mode(parent->blendMode);
    C3D_DepthTest(
        parent->depthTest, GPU_GREATER,
        parent->depthMask ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
    C3D_CullFace(parent->cullFace ? GPU_CULL_BACK_CCW : GPU_CULL_NONE);
}

Batch2D* DrawContext::getBatch2D() const {
    return g2d;
}

const glm::uvec2& DrawContext::getViewport() const {
    return viewport;
}

DrawContext DrawContext::sub(Flushable* flushable) const {
    auto ctx = DrawContext(this, window, g2d);
    ctx.depthTest = depthTest;
    ctx.cullFace = cullFace;
    ctx.blendMode = blendMode;
    ctx.depthMask = depthMask;
    ctx.flushable = flushable ? flushable : g2d;
    return ctx;
}

void DrawContext::setViewport(const glm::uvec2& newViewport) {
    viewport = newViewport;
}

void DrawContext::setFramebuffer(Bindable* newFbo) {
    // offscreen framebuffers are not supported on 3DS (v1)
    fbo = newFbo;
}

void DrawContext::setDepthMask(bool flag) {
    depthMask = flag;
    C3D_DepthTest(
        depthTest, GPU_GREATER, flag ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
}

void DrawContext::setDepthTest(bool flag) {
    depthTest = flag;
    C3D_DepthTest(
        flag, GPU_GREATER, depthMask ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
}

void DrawContext::setCullFace(bool flag) {
    cullFace = flag;
    C3D_CullFace(flag ? GPU_CULL_BACK_CCW : GPU_CULL_NONE);
}

void DrawContext::setBlendMode(BlendMode mode) {
    blendMode = mode;
    set_blend_mode(mode);
}

void DrawContext::setScissors(const glm::vec4& area) {
    if (flushable) {
        flushable->flush();
    }
    window.pushScissor(area);
    scissorsCount++;
}

void DrawContext::setLineWidth(float width) {
    lineWidth = width;
}
