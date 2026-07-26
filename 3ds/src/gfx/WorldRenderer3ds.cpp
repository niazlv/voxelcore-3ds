#include "WorldRenderer3ds.hpp"

#include <3ds.h>
#include <citro3d.h>
#include <glm/gtc/matrix_transform.hpp>

#include "assets/Assets.hpp"
#include "util/ObjectsKeeper.hpp"
#include "coders/imageio.hpp"
#include "content/Content.hpp"
#include "debug/Logger.hpp"
#include "frontend/ContentGfxCache.hpp"
#include "graphics/core/Atlas.hpp"
#include "graphics/core/ImageData.hpp"
#include "graphics/core/Mesh.hpp"
#include "voxels/Block.hpp"
#include "graphics/core/Texture.hpp"
#include "graphics/core/Font.hpp"
#include "graphics/render/BlocksRenderer.hpp"
#include "graphics/render/ModelBatch.hpp"
#include "engine/Engine.hpp"
#include "content/ContentControl.hpp"
#include "objects/Entities.hpp"
#include "world/Level.hpp"
#include "io/io.hpp"
#include "settings.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Chunks.hpp"

#include <cstring>

#include "gfx3ds.hpp"

static debug::Logger logger("world-renderer-3ds");

WorldRenderer3DS::WorldRenderer3DS(
    const Content& content,
    EngineSettings& settings,
    Assets& assets,
    std::vector<std::string> packIds
)
    : content(content),
      settings(settings),
      assets(&assets),
      packIds(std::move(packIds)) {
}

WorldRenderer3DS::~WorldRenderer3DS() = default;

bool WorldRenderer3DS::loadAssets() {
    AtlasBuilder builder;
    std::vector<std::string> textureDirs {"res:textures/blocks"};
    for (const auto& id : packIds) {
        textureDirs.push_back(id + ":textures/blocks");
    }
    for (const auto& dir : textureDirs) {
        io::path folder(dir);
        if (!io::is_directory(folder)) {
            logger.warning() << "no textures folder " << folder.string();
            continue;
        }
        for (const auto& file : io::directory_iterator(folder)) {
            if (file.extension() != ".png") {
                continue;
            }
            std::string name = file.stem();
            if (builder.has(name)) {
                continue;
            }
            try {
                auto image = imageio::read(file);
                if (image) {
                    builder.add(name, std::move(image));
                }
            } catch (const std::exception& err) {
                logger.error() << file.string() << ": " << err.what();
            }
        }
    }
    logger.info() << "atlas textures: " << builder.getNames().size();
    auto built = builder.build(2, true, 512);
    if (!built) {
        return false;
    }
    atlas = built.get();
    logger.info() << "atlas built: " << atlas->getImage()->getWidth()
                  << "x" << atlas->getImage()->getHeight();

    assets->store<Atlas>(std::move(built), "blocks");

    // flat block previews for inventory slots: alias regions of the blocks
    // atlas by block name (no offscreen 3D icon rendering on 3DS)
    {
        auto* indices = content.getIndices();
        std::unordered_map<std::string, UVRegion> regions;
        for (size_t i = 0; i < indices->blocks.count(); i++) {
            const auto& def = indices->blocks.require(i);
            const std::string& tex = def.defaults.textureFaces[FACE_PZ];
            if (atlas->has(tex)) {
                regions[def.name] = atlas->get(tex);
            } else if (atlas->has("notfound")) {
                regions[def.name] = atlas->get("notfound");
            }
        }
        auto* img = atlas->getImage();
        auto copy = std::make_unique<ImageData>(
            img->getFormat(), img->getWidth(), img->getHeight(),
            img->getData());
        assets->store<Atlas>(
            std::make_unique<Atlas>(std::move(copy), std::move(regions), true),
            "block-previews");
    }

    // items atlas: sprite icons (items:coal etc.) for pack items
    {
        AtlasBuilder itemsBuilder;
        std::vector<std::string> itemDirs {"res:textures/items"};
        for (const auto& id : packIds) {
            itemDirs.push_back(id + ":textures/items");
        }
        for (const auto& dir : itemDirs) {
            io::path folder(dir);
            if (!io::is_directory(folder)) {
                continue;
            }
            for (const auto& file : io::directory_iterator(folder)) {
                if (file.extension() != ".png" ||
                    itemsBuilder.has(file.stem())) {
                    continue;
                }
                try {
                    auto image = imageio::read(file);
                    if (image) {
                        itemsBuilder.add(file.stem(), std::move(image));
                    }
                } catch (const std::exception& err) {
                    logger.error() << file.string() << ": " << err.what();
                }
            }
        }
        if (!itemsBuilder.getNames().empty()) {
            assets->store<Atlas>(
                itemsBuilder.build(2, true, 256), "items");
        }
    }

    // entity/custom-block models and generated item models must exist
    // before ContentGfxCache resolves custom block models by name
    {
        extern bool vc3ds_load_entity_assets(Engine&, Content&, Assets&);
        auto& engine = Engine::getInstance();
        vc3ds_load_entity_assets(
            engine, *engine.getContentControl().get(), *assets);
    }

    cache = std::make_unique<ContentGfxCache>(
        content, *assets, settings.graphics);
    renderer = std::make_unique<BlocksRenderer>(
        60000, content, *cache, settings);
    volume = std::make_unique<VoxelsRenderVolume>();
    return true;
}

void WorldRenderer3DS::setupEntityBatch(const Chunks& chunks) {
    modelBatch = std::make_unique<ModelBatch>(
        4096, *assets, chunks, settings);
}

static int count_lighted_neighbors(Chunks& chunks, int cx, int cz) {
    int count = 0;
    for (int oz = -1; oz <= 1; oz++) {
        for (int ox = -1; ox <= 1; ox++) {
            auto* c = chunks.getChunk(cx + ox, cz + oz);
            if (c && c->flags.lighted) count++;
        }
    }
    return count;
}

void WorldRenderer3DS::update(Chunks& chunks, int budget) {
    const auto& list = chunks.getChunks();
    for (const auto& chunk : list) {
        if (budget <= 0) {
            break;
        }
        if (chunk == nullptr || !chunk->flags.lighted) {
            continue;
        }
        glm::ivec2 key(chunk->x, chunk->z);
        int neighbors = count_lighted_neighbors(chunks, chunk->x, chunk->z);
        auto found = meshes.find(key);
        if (found != meshes.end() && !chunk->flags.modified &&
            found->second.neighborsLighted == neighbors) {
            continue;
        }
        volume->setPosition(
            chunk->x * CHUNK_W - VOXELS_BUFFER_PADDING, 0,
            chunk->z * CHUNK_D - VOXELS_BUFFER_PADDING);
        chunks.getVoxels(
            *volume, settings.graphics.backlight.get(), chunk->top + 1);
        Entry entry;
        entry.mesh = renderer->render(chunk.get(), *volume);
        entry.neighborsLighted = neighbors;
        // unsorted translucent geometry (water/glass): one merged mesh
        const auto& sorting = entry.mesh.sortingMeshData.entries;
        if (!sorting.empty()) {
            size_t total = 0;
            for (const auto& e : sorting) total += e.vertexData.size();
            std::vector<ChunkVertex> merged;
            merged.reserve(total);
            for (const auto& e : sorting) {
                merged.insert(
                    merged.end(),
                    e.vertexData.data(),
                    e.vertexData.data() + e.vertexData.size());
            }
            entry.translucent = std::make_unique<Mesh<ChunkVertex>>(
                merged.data(), merged.size());
        }
        meshes[key] = std::move(entry);
        chunk->flags.modified = false;
        budget--;
    }
}

void WorldRenderer3DS::draw(
    Chunks& chunks,
    const glm::vec3& cameraPos,
    Level* level,
    unsigned long long fpsEntity
) {
    if (atlas == nullptr) {
        return;
    }
    gfx3ds::useWorldPipeline();
    atlas->getTexture()->bind();

    // ibo0/ibo1 are ALTERNATIVE full-chunk variants (ibo0 = far version
    // with opaque leaves, ibo1 = near version with alpha-cut dense
    // leaves): draw exactly ONE per chunk, selected by camera distance
    // like the upstream ChunksRenderer
    float denseDist = settings.graphics.denseRenderDistance.get();
    float denseDist2 = denseDist * denseDist;

    for (auto it = meshes.begin(); it != meshes.end();) {
        const auto& key = it->first;
        auto* chunk = chunks.getChunk(key.x, key.y);
        if (chunk == nullptr) {
            // chunk unloaded: drop its mesh
            it = meshes.erase(it);
            continue;
        }
        auto& mesh = it->second.mesh;
        if (mesh.mesh) {
            glm::mat4 model = glm::translate(
                glm::mat4(1.0f),
                glm::vec3(key.x * CHUNK_W + 0.5f, 0.5f, key.y * CHUNK_D + 0.5f));
            gfx3ds::setModel(model);
            glm::vec2 delta =
                glm::vec2(cameraPos.x, cameraPos.z) -
                glm::vec2(key.x * CHUNK_W + CHUNK_W / 2.0f,
                          key.y * CHUNK_D + CHUNK_D / 2.0f);
            bool dense = glm::dot(delta, delta) < denseDist2;
            mesh.mesh->draw(4, dense ? 1 : 0);
        }
        ++it;
    }

    // entity models (drops, mobs) between the opaque and translucent
    // passes so water still blends over them
    if (level != nullptr && modelBatch != nullptr) {
        gfx3ds::useEntityPipeline();
        level->entities->render(*assets, *modelBatch, nullptr, fpsEntity);
        modelBatch->render();
        gfx3ds::useWorldPipeline();
        atlas->getTexture()->bind();
        gfx3ds::setModel(glm::mat4(1.0f));
    }

    // translucent pass: depth test on, depth write off.
    // sorting-mesh vertices are already in WORLD space (BlocksRenderer
    // adds the chunk origin) - draw with the identity model matrix
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
    gfx3ds::setModel(glm::mat4(1.0f));
    for (auto& [key, entry] : meshes) {
        if (!entry.translucent) {
            continue;
        }
        entry.translucent->draw();
    }
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
}
