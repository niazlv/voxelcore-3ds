#pragma once

// Native C++ replacement for Lua generator scripts (3DS port).
// Implements heightmap/biome-parameter generation with FastNoiseLite (C API).

#include "world/generator/GeneratorDef.hpp"
#include "maths/FastNoiseLite.h"

#include <vector>

class CoreGenerator : public GeneratorScript {
    const GeneratorDef& def;
    uint64_t seed = 0;
    fnl_state terrainNoise;
    fnl_state reliefNoise;
    std::vector<fnl_state> paramNoises;
public:
    explicit CoreGenerator(const GeneratorDef& def);

    void initialize(uint64_t seed) override;

    std::shared_ptr<Heightmap> generateHeightmap(
        const glm::ivec2& offset,
        const glm::ivec2& size,
        uint bpd,
        const std::vector<std::shared_ptr<Heightmap>>& inputs
    ) override;

    std::vector<std::shared_ptr<Heightmap>> generateParameterMaps(
        const glm::ivec2& offset, const glm::ivec2& size, uint bpd
    ) override;

    std::vector<Placement> placeStructuresWide(
        const glm::ivec2& offset, const glm::ivec2& size, uint chunkHeight
    ) override;

    std::vector<Placement> placeStructures(
        const glm::ivec2& offset,
        const glm::ivec2& size,
        const std::shared_ptr<Heightmap>& heightmap,
        uint chunkHeight
    ) override;
};
