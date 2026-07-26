#pragma once

// Minimal world renderer for the 3DS port: meshes chunks with the engine's
// BlocksRenderer and draws them with citro3d.

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "graphics/render/commons.hpp"

template <typename T> class Mesh;

class Assets;
class Level;
class ModelBatch;
class Atlas;
class Content;
class ContentGfxCache;
class BlocksRenderer;
class Chunks;
class Level;
struct EngineSettings;

class WorldRenderer3DS {
    const Content& content;
    EngineSettings& settings;
    Assets* assets;  // owned by main (shared with the GUI/menu)
    std::vector<std::string> packIds;
    std::unique_ptr<ContentGfxCache> cache;
    std::unique_ptr<BlocksRenderer> renderer;
    std::unique_ptr<ModelBatch> modelBatch;
    std::unique_ptr<VoxelsRenderVolume> volume;
    struct Entry {
        ChunkMesh mesh;
        std::unique_ptr<Mesh<ChunkVertex>> translucent;
        int neighborsLighted = -1;
    };
    std::unordered_map<glm::ivec2, Entry> meshes;
    Atlas* atlas = nullptr;
public:
    WorldRenderer3DS(
        const Content& content,
        EngineSettings& settings,
        Assets& assets,
        std::vector<std::string> packIds
    );
    ~WorldRenderer3DS();

    // build the block texture atlas from res:content/base/textures/blocks
    bool loadAssets();
    void setupEntityBatch(const Chunks& chunks);

    // re-mesh up to `budget` modified chunks per call
    void update(Chunks& chunks, int budget);

    // draw all chunk meshes (camera matrices must be set via gfx3ds)
    void draw(
        Chunks& chunks,
        const glm::vec3& cameraPos,
        Level* level,
        unsigned long long fpsEntity);

    size_t meshesCount() const {
        return meshes.size();
    }

    Assets* getAssets() {
        return assets;
    }
};
