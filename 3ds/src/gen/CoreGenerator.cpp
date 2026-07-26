
#include "CoreGenerator.hpp"

#include <cmath>

#include "constants.hpp"

CoreGenerator::CoreGenerator(const GeneratorDef& def) : def(def) {
    terrainNoise = fnlCreateState();
    reliefNoise = fnlCreateState();
}

void CoreGenerator::initialize(uint64_t seed) {
    this->seed = seed;

    terrainNoise = fnlCreateState();
    terrainNoise.seed = static_cast<int>(seed & 0x7fffffff);
    terrainNoise.noise_type = FNL_NOISE_OPENSIMPLEX2;
    terrainNoise.fractal_type = FNL_FRACTAL_FBM;
    terrainNoise.octaves = 5;
    terrainNoise.frequency = 0.004f;

    reliefNoise = fnlCreateState();
    reliefNoise.seed = static_cast<int>((seed >> 8) & 0x7fffffff) + 17;
    reliefNoise.noise_type = FNL_NOISE_OPENSIMPLEX2;
    reliefNoise.frequency = 0.0012f;

    paramNoises.clear();
    for (uint i = 0; i < def.biomeParameters; i++) {
        fnl_state noise = fnlCreateState();
        noise.seed = static_cast<int>((seed + i * 7919) & 0x7fffffff) + 100;
        noise.noise_type = FNL_NOISE_OPENSIMPLEX2;
        noise.fractal_type = FNL_FRACTAL_FBM;
        noise.octaves = 2;
        noise.frequency = 0.0015f;
        paramNoises.push_back(noise);
    }
}

std::shared_ptr<Heightmap> CoreGenerator::generateHeightmap(
    const glm::ivec2& offset,
    const glm::ivec2& size,
    uint bpd,
    const std::vector<std::shared_ptr<Heightmap>>& inputs
) {
    auto map = std::make_shared<Heightmap>(size.x, size.y);
    float* values = map->getValues();
    const float seaLevel = def.seaLevel > 0 ? static_cast<float>(def.seaLevel) / CHUNK_H : 0.25f;
    for (int y = 0; y < size.y; y++) {
        for (int x = 0; x < size.x; x++) {
            float wx = (offset.x + x * static_cast<int>(bpd));
            float wz = (offset.y + y * static_cast<int>(bpd));
            // base terrain: fBm noise around sea level
            float n = fnlGetNoise2D(&terrainNoise, wx, wz);  // -1..1
            // large-scale relief scales mountains up in some regions
            float relief = fnlGetNoise2D(&reliefNoise, wx, wz) * 0.5f + 0.5f;
            float h = seaLevel + 0.045f + n * (0.06f + 0.22f * relief * relief);
            values[y * size.x + x] = h;
        }
    }
    map->clamp();
    return map;
}

std::vector<std::shared_ptr<Heightmap>> CoreGenerator::generateParameterMaps(
    const glm::ivec2& offset, const glm::ivec2& size, uint bpd
) {
    std::vector<std::shared_ptr<Heightmap>> maps;
    for (uint i = 0; i < def.biomeParameters; i++) {
        auto map = std::make_shared<Heightmap>(size.x, size.y);
        float* values = map->getValues();
        for (int y = 0; y < size.y; y++) {
            for (int x = 0; x < size.x; x++) {
                float wx = (offset.x + x * static_cast<int>(bpd));
                float wz = (offset.y + y * static_cast<int>(bpd));
                float n = fnlGetNoise2D(&paramNoises[i], wx, wz) * 0.5f + 0.5f;
                values[y * size.x + x] = n;
            }
        }
        map->clamp();
        maps.push_back(std::move(map));
    }
    return maps;
}

std::vector<Placement> CoreGenerator::placeStructuresWide(
    const glm::ivec2&, const glm::ivec2&, uint
) {
    return {};
}

static uint32_t hash_coords(uint64_t seed, int x, int z) {
    uint32_t h = static_cast<uint32_t>(seed) ^ 0x9e3779b9u;
    h ^= static_cast<uint32_t>(x) * 0x85ebca6bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(z) * 0xc2b2ae35u;
    h *= 0x27d4eb2fu;
    h ^= h >> 15;
    return h;
}

std::vector<Placement> CoreGenerator::placeStructures(
    const glm::ivec2& offset,
    const glm::ivec2& size,
    const std::shared_ptr<Heightmap>& heightmap,
    uint chunkHeight
) {
    std::vector<Placement> placements;
    if (def.structures.empty()) {
        return placements;
    }
    // place trees on a jittered grid (structures tree0..tree2 if present)
    std::vector<int> treeIndices;
    for (const auto& name : {"tree0", "tree1", "tree2"}) {
        auto found = def.structuresIndices.find(name);
        if (found != def.structuresIndices.end()) {
            treeIndices.push_back(found->second);
        }
    }
    if (treeIndices.empty()) {
        return placements;
    }
    const float seaLevel = def.seaLevel > 0
        ? static_cast<float>(def.seaLevel) / chunkHeight : 0.25f;
    const int step = 10;
    for (int gz = 0; gz < size.y; gz += step) {
        for (int gx = 0; gx < size.x; gx += step) {
            uint32_t h = hash_coords(seed, offset.x + gx, offset.y + gz);
            if ((h % 100) >= 22) {
                continue;  // tree density
            }
            int lx = gx + (h >> 8) % step;
            int lz = gz + (h >> 16) % step;
            if (lx >= size.x || lz >= size.y) {
                continue;
            }
            float hv = heightmap->getUnchecked(lx, lz);
            if (hv <= seaLevel + 0.004f) {
                continue;  // don't plant trees underwater / on beach edge
            }
            int wy = static_cast<int>(hv * chunkHeight);
            int structure = treeIndices[(h >> 24) % treeIndices.size()];
            placements.emplace_back(
                0,
                StructurePlacement(
                    structure,
                    glm::ivec3(lx, wy, lz),
                    static_cast<uint8_t>((h >> 4) & 3)
                )
            );
        }
    }
    return placements;
}
