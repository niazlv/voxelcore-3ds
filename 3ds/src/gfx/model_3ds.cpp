#include "model_3ds.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gfx3ds.hpp"
#include "debug/Logger.hpp"
#include "graphics/core/ImageData.hpp"
#include "graphics/core/Mesh.hpp"
#include "graphics/core/Texture.hpp"
#include "graphics/render/commons.hpp"

static debug::Logger logger("model-3ds");

namespace {
    // 3ds/tools/mkvoxmodel.py writes exactly this
    struct Header {
        char magic[4];        // "VCM3"
        uint32_t version;     // 1
        uint32_t vertexCount;
        uint32_t indexCount;
        uint32_t paletteSide;
        float bounds[6];      // min xyz, max xyz
    };
    static_assert(sizeof(ChunkVertex) == 28, "asset layout follows ChunkVertex");

    std::unique_ptr<Mesh<ChunkVertex>> mesh;
    std::unique_ptr<Texture> palette;
    glm::vec3 center {0.0f};
    float extent = 1.0f;
}

namespace model3ds {

bool load(const char* path) {
    unload();
    FILE* file = fopen(path, "rb");
    if (!file) {
        logger.warning() << "no model at " << path;
        return false;
    }
    Header head {};
    // paletteSide 0 means the vertex colour already holds albedo * light
    bool ok = fread(&head, sizeof(head), 1, file) == 1 &&
              std::memcmp(head.magic, "VCM3", 4) == 0 && head.version == 1 &&
              head.vertexCount > 0 && head.indexCount > 0 &&
              head.vertexCount <= 65535;
    if (!ok) {
        logger.error() << "bad header in " << path;
        fclose(file);
        return false;
    }

    std::vector<ChunkVertex> vertices(head.vertexCount);
    std::vector<uint16_t> raw(head.indexCount);
    std::vector<ubyte> pixels(size_t(head.paletteSide) * head.paletteSide * 4);
    ok = fread(vertices.data(), sizeof(ChunkVertex), vertices.size(), file) ==
             vertices.size() &&
         fread(raw.data(), sizeof(uint16_t), raw.size(), file) == raw.size() &&
         (pixels.empty() ||
          fread(pixels.data(), 1, pixels.size(), file) == pixels.size());
    fclose(file);
    if (!ok) {
        logger.error() << "truncated model " << path;
        return false;
    }

    std::vector<uint32_t> indices(raw.begin(), raw.end());
    mesh = std::make_unique<Mesh<ChunkVertex>>(
        vertices.data(), vertices.size(),
        std::vector<IndexBufferData> {
            IndexBufferData {indices.data(), indices.size()}});

    if (!pixels.empty()) {
        palette = std::make_unique<Texture>(
            pixels.data(), head.paletteSide, head.paletteSide,
            ImageFormat::RGBA8888);
        palette->setNearestFilter();
    }

    glm::vec3 lo(head.bounds[0], head.bounds[1], head.bounds[2]);
    glm::vec3 hi(head.bounds[3], head.bounds[4], head.bounds[5]);
    center = (lo + hi) * 0.5f;
    extent = glm::max(glm::max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
    if (extent <= 0.0f) {
        extent = 1.0f;
    }
    logger.info() << "model " << path << ": " << vertices.size()
                  << " vertices, " << indices.size() / 3 << " triangles, palette "
                  << head.paletteSide;
    return true;
}

void unload() {
    mesh.reset();
    palette.reset();
}

bool ready() {
    return mesh != nullptr;
}

void draw(float angleRad, float tiltRad) {
    if (!mesh) {
        return;
    }
    const float fov = 42.0f;
    // pull back far enough that the whole model fits the screen height
    float dist = (extent * 0.5f) / std::tan(glm::radians(fov) * 0.5f) * 1.35f;

    glm::vec3 eye(0.0f, std::sin(tiltRad) * dist, std::cos(tiltRad) * dist);
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    // focal length = distance to the model, so it sits at screen depth and the
    // near half of the island pops out of the display
    gfx3ds::setViewProjection(view, fov, 0.1f, dist * 3.0f, dist);

    glm::mat4 model = glm::rotate(glm::mat4(1.0f), angleRad, glm::vec3(0, 1, 0));
    model = glm::translate(model, -center);
    gfx3ds::setModel(model);

    // the world pipeline's fog LUT is built for the world's far plane; at this
    // camera distance it would only tint the model
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);

    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    if (palette) {
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR,
                      GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        palette->bind();
    } else {
        // vertex colour is the finished pixel; no texture unit involved
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR,
                      GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    }
    mesh->draw();
}

}  // namespace model3ds
