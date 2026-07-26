// Native (macOS) replica of the 3DS core smoke test, for ASan debugging.
#include <cstdio>
#include <memory>
#include <thread>

#include "content/Content.hpp"
#include "content/ContentControl.hpp"
#include "devtools/Project.hpp"
#include "engine/CoreParameters.hpp"
#include "engine/EnginePaths.hpp"
#include "debug/Logger.hpp"
#include "logic/ChunksController.hpp"
#include "objects/Player.hpp"
#include "objects/Players.hpp"
#include "settings.hpp"
#include "voxels/Chunks.hpp"
#include "voxels/Chunk.hpp"
#include "world/Level.hpp"
#include "world/World.hpp"

// native impl of platform:: (instead of platform_3ds.cpp)
#include "util/platform.hpp"
namespace platform {
    void configure_encoding() {}
    std::string detect_locale() { return "en_US"; }
    void open_folder(const std::filesystem::path&) {}
    void sleep(size_t millis) {
        std::this_thread::sleep_for(std::chrono::milliseconds(millis));
    }
    int get_process_id() { return 0; }
    std::filesystem::path get_executable_path() { return "/tmp/vc_native_test"; }
    void new_engine_instance(
        const std::vector<std::string>&, std::filesystem::path
    ) {}
    bool open_url(const std::string&) { return false; }
    bool stdin_has_data() { return false; }
}

int main(int argc, char** argv) {
    printf("VoxelCore native core test\n");
    try {
        CoreParameters params;
        params.headless = true;
        params.resFolder = argv[1];  // path to res/
        params.userFolder = argv[2];  // scratch user dir

        debug::Logger::init(params.userFolder.string() + "/latest.log");

        EnginePaths paths(params);
        printf("[ok] EnginePaths\n");

        EngineSettings settings;

        Project project;
        project.basePacks.push_back("base");

        ContentControl content_control(project, paths, nullptr, []() {});
        content_control.resetContent({});
        printf("[ok] resetContent\n");
        content_control.loadContent();
        const Content* content = content_control.get();
        printf("[ok] content: %zu blocks\n",
               content->getIndices()->blocks.count());

        auto folder = paths.getWorldsFolder() / "test";
        paths.setCurrentWorldFolder(folder);
        auto level = World::create(
            "test", "base:demo", folder, 42,
            settings, *content, content_control.getContentPacks()
        );
        printf("[ok] World::create\n");

        auto player = level->players->create(1);
        printf("[ok] player %p\n", (void*)player);

        ChunksController chunksController(*level);
        for (int i = 0; i < 400; i++) {
            glm::vec3 position = player->getPosition();
            player->chunks->configure(
                std::floor(position.x), std::floor(position.z), 3
            );
            chunksController.update(50, 2, 1, *player, true);
        }
        printf("[ok] chunks loaded: %zu\n",
               player->chunks->getChunksCount());

        auto& chunks = *player->chunks;
        for (int y = 90; y >= 50; y -= 5) {
            const auto vox = chunks.get({0, y, 0});
            printf("  block(0,%d,0) = %u\n", y, vox ? vox->id : 9999);
        }
    } catch (const std::exception& err) {
        printf("EXCEPTION: %s\n", err.what());
        return 1;
    }
    printf("DONE\n");
    return 0;
}
