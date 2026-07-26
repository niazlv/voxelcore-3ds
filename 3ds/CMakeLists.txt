cmake_minimum_required(VERSION 3.13)
project(VoxelCore3DS CXX C ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# ---- engine core sources (grown incrementally) ----
set(ENGINE_SOURCES
    # maths
    ${VC_SRC}/maths/Heightmap.cpp
    ${VC_SRC}/maths/LMPacker.cpp
    ${VC_SRC}/maths/rays.cpp
    # data
    ${VC_SRC}/data/dv.cpp
    ${VC_SRC}/data/setting.cpp
    ${VC_SRC}/data/StructLayout.cpp
    # coders
    ${VC_SRC}/coders/commons.cpp
    ${VC_SRC}/coders/json.cpp
    ${VC_SRC}/coders/toml.cpp
    ${VC_SRC}/coders/byte_utils.cpp
    ${VC_SRC}/coders/rle.cpp
    ${VC_SRC}/coders/gzip.cpp
    ${VC_SRC}/coders/binary_json.cpp
    ${VC_SRC}/coders/compression.cpp
    # util
    ${VC_SRC}/util/stringutil.cpp
    ${VC_SRC}/util/random.cpp
    ${VC_SRC}/util/timeutil.cpp
    ${VC_SRC}/util/Clock.cpp
    # debug
    ${VC_SRC}/debug/Logger.cpp
    # voxels
    ${VC_SRC}/voxels/Block.cpp
    ${VC_SRC}/voxels/Chunk.cpp
    ${VC_SRC}/voxels/Chunks.cpp
    ${VC_SRC}/voxels/GlobalChunks.cpp
    ${VC_SRC}/voxels/VoxelsVolume.cpp
    ${VC_SRC}/voxels/blocks_agent.cpp
    ${VC_SRC}/voxels/compressed_chunks.cpp
    ${VC_SRC}/voxels/Pathfinding.cpp
    # lighting
    ${VC_SRC}/lighting/LightSolver.cpp
    ${VC_SRC}/lighting/Lighting.cpp
    ${VC_SRC}/lighting/Lightmap.cpp
    # physics
    ${VC_SRC}/physics/Hitbox.cpp
    ${VC_SRC}/physics/PhysicsSolver.cpp
    # items
    ${VC_SRC}/items/Inventories.cpp
    ${VC_SRC}/items/Inventory.cpp
    ${VC_SRC}/items/ItemDef.cpp
    ${VC_SRC}/items/ItemStack.cpp
    # content
    ${VC_SRC}/content/Content.cpp
    ${VC_SRC}/content/ContentBuilder.cpp
    ${VC_SRC}/content/ContentLoader.cpp
    ${VC_SRC}/content/ContentPack.cpp
    ${VC_SRC}/content/ContentPackVersion.cpp
    ${VC_SRC}/content/ContentReport.cpp
    ${VC_SRC}/content/loading/BlockLoader.cpp
    ${VC_SRC}/content/loading/ItemLoader.cpp
    ${VC_SRC}/content/loading/EntityLoader.cpp
    ${VC_SRC}/content/loading/GeneratorLoader.cpp
    # objects
    ${VC_SRC}/objects/Entities.cpp
    ${VC_SRC}/objects/Entity.cpp
    ${VC_SRC}/objects/EntityDef.cpp
    ${VC_SRC}/objects/Player.cpp
    ${VC_SRC}/objects/Players.cpp
    ${VC_SRC}/objects/Rigidbody.cpp
    ${VC_SRC}/objects/Transform.cpp
    ${VC_SRC}/objects/rigging.cpp
    # world
    ${VC_SRC}/world/Level.cpp
    ${VC_SRC}/world/LevelEvents.cpp
    ${VC_SRC}/world/World.cpp
    ${VC_SRC}/world/Weather.cpp
    ${VC_SRC}/world/generator/GeneratorDef.cpp
    ${VC_SRC}/world/generator/SurroundMap.cpp
    ${VC_SRC}/world/generator/VoxelFragment.cpp
    ${VC_SRC}/world/generator/WorldGenerator.cpp
    ${VC_SRC}/world/files/RegionsLayer.cpp
    ${VC_SRC}/world/files/WorldFiles.cpp
    ${VC_SRC}/world/files/WorldRegions.cpp
    ${VC_SRC}/world/files/compatibility.cpp
    ${VC_SRC}/world/files/WorldConverter.cpp
    # io
    ${VC_SRC}/io/io.cpp
    ${VC_SRC}/io/path.cpp
    ${VC_SRC}/io/devices/Device.cpp
    ${VC_SRC}/io/devices/StdfsDevice.cpp
    ${VC_SRC}/io/devices/MemoryDevice.cpp
    ${VC_SRC}/io/devices/ZipFileDevice.cpp
    # coders (images)
    ${VC_SRC}/coders/png.cpp
    ${VC_SRC}/coders/imageio.cpp
    # graphics (CPU-side only)
    ${VC_SRC}/graphics/core/ImageData.cpp
    # engine
    ${VC_SRC}/engine/EnginePaths.cpp
    # logic controllers
    ${VC_SRC}/logic/BlocksController.cpp
    ${VC_SRC}/logic/ChunksController.cpp
    ${VC_SRC}/logic/LevelController.cpp
    ${VC_SRC}/logic/PlayerController.cpp
    ${VC_SRC}/logic/EngineController.cpp
    ${VC_SRC}/io/settings_io.cpp
    ${VC_SRC}/audio/audio.cpp
    ${VC_SRC}/audio/NoAudio.cpp
    ${VC_SRC}/audio/MemoryPCMStream.cpp
    ${VC_SRC}/logic/CommandsInterpreter.cpp
    ${VC_SRC}/coders/xml.cpp
    ${VC_SRC}/coders/yaml.cpp
    ${VC_SRC}/coders/wav.cpp
    ${VC_SRC}/coders/vcm.cpp
    ${VC_SRC}/coders/obj.cpp
    ${VC_SRC}/coders/vec3.cpp
    ${VC_SRC}/coders/syntax_parser.cpp
    ${VC_SRC}/graphics/render/NamedSkeletons.cpp
    # content control
    ${VC_SRC}/content/ContentControl.cpp
    ${VC_SRC}/content/PacksManager.cpp
    ${VC_SRC}/core_defs.cpp
    ${VC_SRC}/devtools/Project.cpp
    ${VC_SRC}/window/Camera.cpp
    ${VC_SRC}/presets/NotePreset.cpp
    ${VC_SRC}/presets/ParticlesPreset.cpp
    ${VC_SRC}/presets/WeatherPreset.cpp
)

file(GLOB GUI_SOURCES
    ${VC_SRC}/graphics/ui/*.cpp
    ${VC_SRC}/graphics/ui/elements/*.cpp
)

set(ENGINE_RENDER_SOURCES
    ${GUI_SOURCES}
    ${VC_SRC}/graphics/core/Batch2D.cpp
    ${VC_SRC}/graphics/core/Font.cpp
    ${VC_SRC}/graphics/core/FontMetrics.cpp
    ${VC_SRC}/frontend/UiDocument.cpp
    ${VC_SRC}/assets/Assets.cpp
    ${VC_SRC}/assets/assets_util.cpp
    ${VC_SRC}/graphics/core/Atlas.cpp
    ${VC_SRC}/graphics/commons/Model.cpp
    ${VC_SRC}/frontend/ContentGfxCache.cpp
    ${VC_SRC}/frontend/LevelFrontend.cpp
    ${VC_SRC}/frontend/hud.cpp
    ${VC_SRC}/frontend/locale.cpp
    ${VC_SRC}/graphics/commons/FontStyle.cpp
    ${VC_SRC}/graphics/render/BlocksRenderer.cpp
    ${VC_SRC}/graphics/render/MainBatch.cpp
    ${VC_SRC}/graphics/render/ModelBatch.cpp
    ${VC_SRC}/graphics/render/ModelsGenerator.cpp
)

file(GLOB SCRIPTING_SOURCES
    ${VC_SRC}/logic/scripting/*.cpp
    ${VC_SRC}/logic/scripting/lua/*.cpp
    ${VC_SRC}/logic/scripting/lua/libs/*.cpp
    ${VC_SRC}/logic/scripting/lua/usertypes/*.cpp
)
# UI/network/devtools-bound bindings are not available on 3DS (v1)
list(REMOVE_ITEM SCRIPTING_SOURCES
    ${VC_SRC}/logic/scripting/lua/libs/libassets.cpp
    ${VC_SRC}/logic/scripting/lua/libs/libnetwork.cpp
    ${VC_SRC}/logic/scripting/lua/libs/libblockwraps.cpp
    ${VC_SRC}/logic/scripting/lua/libs/libparticles.cpp
    ${VC_SRC}/logic/scripting/lua/libs/libtext3d.cpp
    ${VC_SRC}/logic/scripting/lua/libs/libposteffects.cpp
    ${VC_SRC}/logic/scripting/lua/libs/libtest.cpp
)

set(PORT_SOURCES
    src/main_3ds.cpp
    src/platform/heap_3ds.c
    src/platform/platform_3ds.cpp
    src/platform/engine_3ds.cpp
    src/stubs/input_stub.cpp
    src/stubs/libs_stub.cpp
    src/stubs/debug_panel_3ds.cpp
    src/gen/CoreGenerator.cpp
    src/gfx/gfx3ds.cpp
    src/gfx/texture_3ds.cpp
    src/gfx/mesh_3ds.cpp
    src/gfx/WorldRenderer3ds.cpp
    src/gfx/hud_3ds.cpp
    src/gfx/model_3ds.cpp
    src/gfx/model_assets_3ds.cpp
    src/gfx/drawcontext_3ds.cpp
    src/platform/window_3ds.cpp
    src/audio/ndsp_audio.cpp
    src/audio/ogg3ds.cpp
    external/stb/stb_vorbis.c
)
set_source_files_properties(external/stb/stb_vorbis.c PROPERTIES
    COMPILE_FLAGS "-Wno-unused-value -Wno-maybe-uninitialized")

# ---- PICA vertex shader: picasso -> bin2s -> object ----
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.v.pica)
set(SHADER_BIN ${CMAKE_CURRENT_BINARY_DIR}/world.shbin)
set(SHADER_ASM ${CMAKE_CURRENT_BINARY_DIR}/world_shbin.s)
add_custom_command(
    OUTPUT ${SHADER_BIN}
    COMMAND ${CTR_PICASSO_EXE} -o ${SHADER_BIN} ${SHADER_SRC}
    DEPENDS ${SHADER_SRC})
add_custom_command(
    OUTPUT ${SHADER_ASM}
    COMMAND bin2s -a 4 ${SHADER_BIN} > ${SHADER_ASM}
    DEPENDS ${SHADER_BIN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(ENTITY_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entity.v.pica)
set(ENTITY_SHADER_BIN ${CMAKE_CURRENT_BINARY_DIR}/entity.shbin)
set(ENTITY_SHADER_ASM ${CMAKE_CURRENT_BINARY_DIR}/entity_shbin.s)
add_custom_command(
    OUTPUT ${ENTITY_SHADER_BIN}
    COMMAND ${CTR_PICASSO_EXE} -o ${ENTITY_SHADER_BIN} ${ENTITY_SHADER_SRC}
    DEPENDS ${ENTITY_SHADER_SRC})
add_custom_command(
    OUTPUT ${ENTITY_SHADER_ASM}
    COMMAND bin2s -a 4 ${ENTITY_SHADER_BIN} > ${ENTITY_SHADER_ASM}
    DEPENDS ${ENTITY_SHADER_BIN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set(UI_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.v.pica)
set(UI_SHADER_BIN ${CMAKE_CURRENT_BINARY_DIR}/ui.shbin)
set(UI_SHADER_ASM ${CMAKE_CURRENT_BINARY_DIR}/ui_shbin.s)
add_custom_command(
    OUTPUT ${UI_SHADER_BIN}
    COMMAND ${CTR_PICASSO_EXE} -o ${UI_SHADER_BIN} ${UI_SHADER_SRC}
    DEPENDS ${UI_SHADER_SRC})
add_custom_command(
    OUTPUT ${UI_SHADER_ASM}
    COMMAND bin2s -a 4 ${UI_SHADER_BIN} > ${UI_SHADER_ASM}
    DEPENDS ${UI_SHADER_BIN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(voxelcore3ds
    ${ENGINE_SOURCES} ${ENGINE_RENDER_SOURCES} ${SCRIPTING_SOURCES}
    ${PORT_SOURCES} ${SHADER_ASM} ${UI_SHADER_ASM} ${ENTITY_SHADER_ASM})

target_include_directories(voxelcore3ds PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/luajit/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stubs
    ${VC_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/external/entt/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
)

target_compile_definitions(voxelcore3ds PRIVATE
    VC_BUILD_NAME="3ds-port"
    VC_NO_GL
    VC_NO_AL
    VC_AUDIO_CUSTOM_BACKEND
    # memory/screen profile (see src/constants.hpp, src/voxels/GlobalChunks.cpp)
    VC_CHUNK_H=128
    VC_ITEM_ICON_SIZE=24
    VC_CHUNKS_POOL_SIZE=0
    # port-specific patches to engine sources (kept out of mainline)
    VC_PORT_3DS
    GLM_ENABLE_EXPERIMENTAL
)

target_compile_options(voxelcore3ds PRIVATE
    -Wall -Wno-unused-parameter -Wno-sign-compare
    -O2 -ffunction-sections -fdata-sections
)

target_link_options(voxelcore3ds PRIVATE
    -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
    -Wl,--wrap=memalign -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign
    -Wl,--wrap=malloc_usable_size
    -Wl,--wrap=_malloc_r -Wl,--wrap=_free_r -Wl,--wrap=_calloc_r
    -Wl,--wrap=_realloc_r -Wl,--wrap=_memalign_r)

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_library(CITRO3D_LIB citro3d REQUIRED)
find_library(CITRO2D_LIB citro2d REQUIRED)
target_link_libraries(voxelcore3ds PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/luajit/src/libluajit.a
    ${CITRO2D_LIB} ${CITRO3D_LIB}
    PNG::PNG ZLIB::ZLIB
)

ctr_generate_smdh(voxelcore3ds.smdh
    NAME "VoxelCore"
    DESCRIPTION "VoxelCore engine 3DS port"
    AUTHOR "MihailRis / 3DS port"
    ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.png
)

ctr_create_3dsx(voxelcore3ds
    SMDH voxelcore3ds.smdh
    ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs
)
