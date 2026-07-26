#include <3ds.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <new>
#include <sys/stat.h>

// print to both the 3DS console and a result file on sdmc
// (the file is how we verify runs in the emulator headlessly)
static FILE* result_file = nullptr;
static void report(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (result_file) {
        va_start(args, fmt);
        vfprintf(result_file, fmt, args);
        va_end(args);
        fflush(result_file);
    }
}

extern "C" void vc3ds_heap_stats(size_t* total, size_t* used);
extern "C" void vc3ds_mark(const char* tag) {
    report("[m] %s\n", tag);
}
static void heap_stat(const char* tag) {
    size_t total = 0, used = 0;
    vc3ds_heap_stats(&total, &used);
    report("HEAP %s: pool=%u KiB used=%u KiB\n",
           tag, (unsigned)(total >> 10), (unsigned)(used >> 10));
}

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "content/Content.hpp"
#include "content/ContentControl.hpp"
#include "core_defs.hpp"
#include "devtools/Project.hpp"
#include "engine/CoreParameters.hpp"
#include "engine/EnginePaths.hpp"
#include "debug/Logger.hpp"
#include "logic/ChunksController.hpp"
#include "objects/Player.hpp"
#include "objects/Players.hpp"
#include "objects/Entities.hpp"
#include "settings.hpp"
#include "voxels/Chunks.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Block.hpp"
#include "voxels/blocks_agent.hpp"
#include "lighting/Lighting.hpp"
#include "world/Level.hpp"
#include "world/World.hpp"
#include "world/files/WorldFiles.hpp"
#include "io/io.hpp"

#include "gfx/gfx3ds.hpp"
#include "gfx/model_3ds.hpp"
#include "gfx/WorldRenderer3ds.hpp"
#include "frontend/hud.hpp"
#include "frontend/LevelFrontend.hpp"
#include "frontend/UiDocument.hpp"
#include "frontend/locale.hpp"
#include "content/ContentPack.hpp"
#include "logic/scripting/scripting_hud.hpp"
#include "logic/scripting/scripting.hpp"
#include "items/Inventory.hpp"
#include "items/ItemDef.hpp"
#include "items/ItemStack.hpp"
#include "engine/Engine.hpp"
#include "logic/LevelController.hpp"
#include "logic/BlocksController.hpp"
#include "logic/PlayerController.hpp"
#include "graphics/ui/GUI.hpp"
#include "graphics/ui/elements/Label.hpp"
#include "graphics/ui/elements/Button.hpp"
#include "graphics/ui/elements/Panel.hpp"
#include "graphics/core/DrawContext.hpp"
#include "graphics/core/Batch2D.hpp"
#include "assets/Assets.hpp"
#include "audio/audio.hpp"
#include "coders/imageio.hpp"
#include "graphics/core/Atlas.hpp"
#include "graphics/core/Font.hpp"
#include "graphics/core/Texture.hpp"
#include "maths/UVRegion.hpp"
#include "physics/Hitbox.hpp"
#include "util/ObjectsKeeper.hpp"
#include "window/Camera.hpp"
#include "platform/window_3ds.hpp"

LevelController* vc3ds_level_controller();
extern "C" void vc3ds_set_assets(void*);

static void wait_exit() {
    printf("\npress START to exit\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

int main(int argc, char** argv) {
    gfxInitDefault();
    romfsInit();

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/voxelcore", 0777);
    result_file = fopen("sdmc:/3ds/voxelcore/result.txt", "w");

    report("VoxelCore 3DS\n");

    if (!gfx3ds::init()) {
        report("gfx3ds init FAILED\n");
        wait_exit();
        return 1;
    }

    try {
        CoreParameters params;
        // client mode: entity visuals + the real player.lua movement run
        params.headless = false;
        params.resFolder = "romfs:/res";
        params.userFolder = "sdmc:/3ds/voxelcore";

        debug::Logger::init("sdmc:/3ds/voxelcore/latest.log");

        auto& engine = Engine::getInstance();
        engine.initialize(params);
        report("[ok] engine + lua\n");
        EnginePaths& paths = engine.getPaths();
        EngineSettings& settings = engine.getSettings();
        settings.chunks.loadDistance.set(4);
        settings.chunks.padding.set(1);
        // touch screen: tapping a hotbar slot selects it
        settings.ui.hotbarInteractive.set(true);
        // leaves with a real alpha channel (the opaque fallback texture is
        // used otherwise); their geometry goes to the dense index buffer
        settings.graphics.denseRender.set(true);

        auto& content_control = engine.getContentControl();
        content_control.resetContent({});
        content_control.loadContent();
        const Content* content = content_control.get();
        report("[ok] content: %zu blocks\n",
               content->getIndices()->blocks.count());

        langs::setup("ru_RU", paths.resPaths.collectRoots());

        auto folder = paths.getWorldsFolder() / "world2";
        bool haveSave = io::is_regular_file(folder / "world.json");
        bool loadExisting = false;

        // assets live for the whole session (menu needs the font before
        // any world exists)
        static util::ObjectsKeeper assetsVault;
        Assets assets(&assetsVault);
        {
            std::vector<std::unique_ptr<ImageData>> pages;
            for (int i = 0;; i++) {
                io::path page("res:fonts/font_" + std::to_string(i) + ".png");
                if (!io::is_regular_file(page)) {
                    break;
                }
                pages.push_back(imageio::read(page));
            }
            if (pages.empty()) {
                throw std::runtime_error("no font pages found");
            }
            assets.store<Font>(
                Font::createBitmapFont(std::move(pages)), "normal");
        }

        auto& gui = engine.getGUI();
        auto* input3ds = static_cast<Input3DS*>(&engine.getInput());
        Batch2D uiBatch(1024);

        // rotating voxel island on the top screen while the menus are up.
        // Missing or broken asset just means no decoration.
        float menuSpin = 0.0f;
        bool haveMenuModel = model3ds::load("romfs:/models/island.vcm3ds");
        if (haveMenuModel) {
            gfx3ds::setStereo(true);
            report("[ok] menu model, stereo eyes: %d\n", gfx3ds::eyes());
        }
        // real frame time, not 1/60: a dropped frame would otherwise show up as
        // a jump in the rotation
        TickCounter menuClock;
        osTickCounterStart(&menuClock);
        auto drawMenuModel = [&]() {
            if (!haveMenuModel) {
                return;
            }
            osTickCounterUpdate(&menuClock);
            float dt = float(osTickCounterRead(&menuClock)) / 1000.0f;
            menuSpin += glm::min(dt, 0.1f) * 0.5f;   // clamp after long stalls
            for (int eye = 0; eye < gfx3ds::eyes(); eye++) {
                gfx3ds::setEye(eye);
                model3ds::draw(menuSpin);
            }
        };

        // graphical start menu on the bottom screen (touch + buttons)
        {
            int choice = -1;  // 0 = continue, 1 = new world, 2 = exit
            auto menuPanel = std::make_shared<gui::Panel>(
                gui, glm::vec2(220, 160), glm::vec4(12, 10, 12, 10), 6.0f);
            menuPanel->setColor(glm::vec4(0.08f, 0.08f, 0.12f, 0.9f));

            auto title = std::make_shared<gui::Label>(gui, L"VoxelCore 3DS");
            menuPanel->add(title);

            auto addButton = [&](const std::wstring& text, int value) {
                auto button = std::make_shared<gui::Button>(
                    gui, text, glm::vec4(8, 6, 8, 6),
                    [&choice, value](gui::GUI&) { choice = value; },
                    glm::vec2(190, 26));
                menuPanel->add(button);
            };
            if (haveSave) {
                addButton(L"Продолжить", 0);
            }
            addButton(L"Новый мир", 1);
            addButton(L"Выход", 2);

            auto size = menuPanel->getSize();
            menuPanel->setPos(glm::vec2(
                (320 - size.x) / 2, (240 - size.y) / 2));
            gui.add(menuPanel);

            FILE* autostart = fopen("sdmc:/3ds/voxelcore/autostart.txt", "r");
            if (autostart) {
                fclose(autostart);
                choice = 1;
            }
            while (aptMainLoop() && choice < 0) {
                hidScanInput();
                u32 k = hidKeysDown();
                if ((k & KEY_A) && haveSave) choice = 0;
                if (k & KEY_X) choice = 1;
                if (k & KEY_START) choice = 2;

                input3ds->update();
                gui.act(1.0f / 60.0f, glm::uvec2(320, 240));
                gui.postAct();

                gfx3ds::beginFrame(0.06f, 0.07f, 0.1f);
                drawMenuModel();
                gfx3ds::beginBottomUI(0.05f, 0.05f, 0.08f);
                {
                    DrawContext ctx(nullptr, engine.getWindow(), &uiBatch);
                    gui.draw(ctx, assets);
                }
                gfx3ds::endFrame();
            }
            gui.remove(menuPanel);
            if (choice == 0) {
                loadExisting = true;
            }
            if (choice == 2 || choice < 0) {
                throw std::runtime_error("exit from menu");
            }
        }

        // world mode: creative (all blocks, infinite) or survival
        int gamemode = 0;
        if (loadExisting) {
            io::path modeFile = folder / "gamemode3ds.txt";
            if (io::is_regular_file(modeFile) &&
                io::read_string(modeFile) == "survival") {
                gamemode = 1;
            }
        } else if (fopen("sdmc:/3ds/voxelcore/autostart.txt", "r") == nullptr) {
            int mode = -1;
            auto modePanel = std::make_shared<gui::Panel>(
                gui, glm::vec2(220, 130), glm::vec4(12, 10, 12, 10), 6.0f);
            modePanel->setColor(glm::vec4(0.08f, 0.08f, 0.12f, 0.9f));
            modePanel->add(
                std::make_shared<gui::Label>(gui, L"Режим мира"));
            auto addModeButton = [&](const std::wstring& text, int value) {
                modePanel->add(std::make_shared<gui::Button>(
                    gui, text, glm::vec4(8, 6, 8, 6),
                    [&mode, value](gui::GUI&) { mode = value; },
                    glm::vec2(190, 26)));
            };
            addModeButton(L"Креатив", 0);
            addModeButton(L"Выживание", 1);
            auto msize = modePanel->getSize();
            modePanel->setPos(glm::vec2(
                (320 - msize.x) / 2, (240 - msize.y) / 2));
            gui.add(modePanel);
            while (aptMainLoop() && mode < 0) {
                hidScanInput();
                u32 k = hidKeysDown();
                if (k & KEY_A) mode = 0;
                if (k & KEY_X) mode = 1;

                input3ds->update();
                gui.act(1.0f / 60.0f, glm::uvec2(320, 240));
                gui.postAct();

                gfx3ds::beginFrame(0.06f, 0.07f, 0.1f);
                drawMenuModel();
                gfx3ds::beginBottomUI(0.05f, 0.05f, 0.08f);
                {
                    DrawContext ctx(nullptr, engine.getWindow(), &uiBatch);
                    gui.draw(ctx, assets);
                }
                gfx3ds::endFrame();
            }
            gui.remove(modePanel);
            gamemode = mode < 0 ? 0 : mode;
        }
        bool creativeMode = gamemode == 0;

        // the world needs the whole top screen (and its VRAM) back
        gfx3ds::setStereo(false);
        model3ds::unload();
        haveMenuModel = false;

        paths.setCurrentWorldFolder(folder);
        std::unique_ptr<Level> levelPtr;
        {
        std::unique_ptr<Level>& level = levelPtr;
        if (loadExisting) {
            auto worldFiles = std::make_shared<WorldFiles>(folder);
            level = World::load(
                worldFiles, settings, *content,
                content_control.getContentPacks());
            report("[ok] world loaded\n");
        } else {
            // stale region files from a previous world in the same folder
            // would leak old chunks into the new one
            if (io::is_directory(folder)) {
                io::remove_all(folder);
            }
            uint64_t seed = osGetTime();
            level = World::create(
                "world2", "base:demo", folder, seed,
                settings, *content, content_control.getContentPacks()
            );
            report("[ok] world created, seed=%llu\n",
                   (unsigned long long)seed);
        }
        }
        auto player = levelPtr->players->get(1);
        if (player == nullptr) {
            player = levelPtr->players->create(1);
        }
        engine.onWorldOpen(std::move(levelPtr), 1);
        LevelController* lc = vc3ds_level_controller();
        Level* level = lc->getLevel();
        report("[ok] world ready (lua scripts loaded)\n");

        std::vector<std::string> packIds;
        for (const auto& pack : content_control.getContentPacks()) {
            packIds.push_back(pack.id);
        }
        WorldRenderer3DS worldRenderer(
            *content, settings, assets, packIds);
        if (!worldRenderer.loadAssets()) {
            throw std::runtime_error("failed to load assets");
        }
        vc3ds_set_assets(worldRenderer.getAssets());

        gui.onAssetsLoad(worldRenderer.getAssets());

        // let entity spawns create skeleton components
        level->entities->setAssets(assets);
        worldRenderer.setupEntityBatch(*player->chunks);
        report("[ok] entity assets\n");

        // ui layouts (core + packs): documents like core:inventory
        {
            auto loadLayouts = [&](const std::string& prefix,
                                   const io::path& folder,
                                   const scriptenv& env) {
                if (!io::is_directory(folder)) {
                    return;
                }
                for (const auto& file : io::directory_iterator(folder)) {
                    if (file.extension() != ".xml") {
                        continue;
                    }
                    std::string name = prefix + ":" + file.stem();
                    try {
                        auto doc = UiDocument::read(
                            gui, env, name, file, name);
                        assets.store<UiDocument>(std::move(doc), name);
                    } catch (const std::exception& err) {
                        debug::Logger("layouts").error()
                            << name << ": " << err.what();
                    }
                }
            };
            loadLayouts("core", io::path("res:layouts"), nullptr);
            // 3DS override: the PC player inventory is 10 slots wide
            // (~296px) and cannot fit next to the content panel on a
            // 320px screen; use a 5x8 grid instead
            {
                io::create_directory(io::path("user:layouts3ds"));
                io::write_string(
                    io::path("user:layouts3ds/inventory.xml"),
                    "<inventory>\n"
                    "    <slots-grid rows=\"3\" start-index=\"10\" "
                    "count=\"30\"/>\n"
                    "    <slots-grid pos=\"0, 92\" rows=\"1\" "
                    "count=\"10\"/>\n"
                    "</inventory>\n");
                try {
                    auto doc = UiDocument::read(
                        gui, nullptr, "core:inventory",
                        io::path("user:layouts3ds/inventory.xml"),
                        "core:inventory");
                    assets.store<UiDocument>(
                        std::move(doc), "core:inventory");
                } catch (const std::exception& err) {
                    debug::Logger("layouts").error()
                        << "3ds inventory override: " << err.what();
                }
            }
            for (const auto& pack : content_control.getContentPacks()) {
                auto* runtime = content->getPackRuntime(pack.id);
                loadLayouts(
                    pack.id, pack.folder / "layouts",
                    runtime ? runtime->getEnvironment() : nullptr);
            }
            report("[ok] layouts\n");
        }

        ChunksController& chunksController = *lc->getChunksController();

        // real player controller: interaction, selection, camera control
        settings.camera.fov.set(65.0f);
        settings.camera.shaking.set(false);
        settings.camera.inertia.set(false);
        PlayerController pc(
            settings, *level, *player, *lc->getBlocksController());
        player->setInstantDestruction(true);
        player->setInfiniteItems(creativeMode);
        if (!loadExisting) {
            // the world folder does not exist yet for a fresh world
            io::create_directory(folder);
            io::write_string(
                folder / "gamemode3ds.txt",
                creativeMode ? "creative" : "survival");
        }

        // the real engine HUD stack: LevelFrontend wires sounds, Hud owns
        // hotbar/inventory/debug panel and the scripting HUD API
        LevelFrontend frontend(engine, pc, *lc, settings);
        Hud hud(engine, frontend, *player);
        hud.setContentAccess(false);  // tab toggled with X (creative)
        for (const auto& entry : content->getPacks()) {
            auto* runtime = entry.second.get();
            const ContentPack& info = runtime->getInfo();
            io::path scriptFile = info.folder / "scripts/hud.lua";
            if (io::is_regular_file(scriptFile)) {
                scripting::load_hud_script(
                    runtime->getEnvironment(),
                    info.id,
                    scriptFile,
                    info.id + ":scripts/hud.lua");
            }
        }
        scripting::on_frontend_init(&hud, nullptr, nullptr);
        {
            extern void vc3ds_run_lua(const char* src, const char* tag);
            if (!creativeMode) {
                vc3ds_run_lua(
                    "pcall(function()"
                    " local gm = require 'base_survival:gamemodes'"
                    " gm.set(hud.get_player(), 'survival')"
                    " end)",
                    "gamemode-bridge");
            }
        }
        report("[ok] hud\n");
        heap_stat("assets");

        glm::vec3 spawnPos(0.5f, 74.0f, 0.5f);
        if (loadExisting) {
            spawnPos = player->getPosition();
        }
        int frame = 0;

        // starting kit: building blocks + all four lamp colors for the
        // lighting demo
        {
            auto inventory = player->getInventory();
            const char* kit[10] = {
                "base:stone", "base:planks", "base:dirt", "base:glass",
                "base:lamp", "base:red_lamp", "base:green_lamp",
                "base:blue_lamp", "base:torch", "base:wood"
            };
            for (int slot = 0; slot < 10; slot++) {
                auto def = content->items.find(std::string(kit[slot]) + ".item");
                if (def && inventory->getSlot(slot).isEmpty()) {
                    inventory->getSlot(slot).set(ItemStack(def->rt.id, 99));
                }
            }
        }

        // colored-light showcase built near spawn once terrain is ready
        auto setBlock = [&](int x, int y, int z, const char* name) {
            auto def = content->blocks.find(name);
            if (def == nullptr) return;
            blocks_agent::set(*player->chunks, x, y, z, def->rt.id, {});
            if (chunksController.lighting) {
                chunksController.lighting->onBlockSet(x, y, z, def->rt.id);
            }
        };
        auto buildLightShowcase = [&](int h) {
            // terrace around the spawn point: the player stands on it,
            // lamps ahead under a roof so the colored glow reads clearly
            for (int x = -5; x <= 5; x++) {
                for (int z = -9; z <= 1; z++) {
                    setBlock(x, h - 1, z, "base:stone");
                    // clear space above so lights are visible
                    for (int y = h; y < h + 6; y++) {
                        blocks_agent::set(*player->chunks, x, y, z, 0, {});
                        if (chunksController.lighting) {
                            chunksController.lighting->onBlockSet(x, y, z, 0);
                        }
                    }
                }
            }
            // four lamps on pedestals: red, white, green, blue
            const char* lamps[4] = {
                "base:red_lamp", "base:lamp", "base:green_lamp",
                "base:blue_lamp"
            };
            for (int i = 0; i < 4; i++) {
                int x = -3 + i * 2;
                setBlock(x, h, -8, "base:planks");
                setBlock(x, h + 1, -8, lamps[i]);
            }
            // enclosed room: walls + full roof make it dark inside so the
            // colored lamp light and its falloff are unmistakable
            for (int x = -5; x <= 5; x++) {
                for (int z = -9; z <= 1; z++) {
                    bool wall = (x == -5 || x == 5 || z == -9 || z == 1);
                    for (int y = h; y < h + 4; y++) {
                        if (wall) {
                            setBlock(x, y, z, "base:stone");
                        }
                    }
                    setBlock(x, h + 4, z, "base:stone");
                }
            }
            // doorway behind the spawn point lets a shaft of daylight in
            setBlock(0, h, 1, "core:air");
            setBlock(0, h + 1, 1, "core:air");
            setBlock(1, h, 1, "core:air");
            setBlock(1, h + 1, 1, "core:air");
            report("[ok] light showcase built at y=%d\n", h);
        };

        // block material sounds (steps / place / break) + variants
        {
            auto loadSound = [&](const std::string& name) {
                if (name.empty() || assets.get<audio::Sound>(name)) {
                    return;
                }
                static const char* exts[] = {".ogg", ".wav"};
                std::vector<std::string> roots {"res:sounds/"};
                for (const auto& id : packIds) {
                    roots.push_back(id + ":sounds/");
                }
                std::unique_ptr<audio::Sound> baseSound = nullptr;
                std::string base;
                for (const auto& root : roots) {
                    for (const char* ext : exts) {
                        std::string f = root + name + ext;
                        if (io::is_regular_file(io::path(f))) {
                            baseSound = audio::load_sound(io::path(f), false);
                        } else if (io::is_regular_file(
                                       io::path(root + name + "_0" + ext))) {
                            baseSound = audio::load_sound(
                                io::path(root + name + "_0" + ext), false);
                        }
                        if (baseSound) {
                            base = std::string(root) + name;
                            break;
                        }
                    }
                    if (baseSound) break;
                }
                if (baseSound == nullptr) {
                    return;
                }
                for (int i = 1;; i++) {
                    std::string variant;
                    for (const char* ext : exts) {
                        std::string f = base + "_" + std::to_string(i) + ext;
                        if (io::is_regular_file(io::path(f))) {
                            variant = f;
                            break;
                        }
                    }
                    if (variant.empty()) break;
                    baseSound->variants.emplace_back(
                        audio::load_sound(io::path(variant), false));
                }
                assets.store<audio::Sound>(std::move(baseSound), name);
            };
            for (const auto& [mname, material] : content->getBlockMaterials()) {
                loadSound(material->stepsSound);
                loadSound(material->placeSound);
                loadSound(material->breakSound);
                loadSound(material->hitSound);
            }
        }

        // crosshair for the top screen
        {
            auto image = imageio::read(io::path("res:textures/gui/crosshair.png"));
            if (image) {
                assets.store<Texture>(Texture::from(image.get()), "gui/crosshair");
            }
        }

        report("[ok] entering main loop\n");

        bool spawnAdjusted = loadExisting;
        bool xWasHeld = false;
        bool yWasHeld = false;
        bool showcaseBuilt = loadExisting;
        int physicsWarned = 0;

        while (aptMainLoop()) {
            engine.getTime().update(osGetTime() / 1000.0);
            hidScanInput();
            u32 kDown = hidKeysDown();
            u32 kHeld = hidKeysHeld();
            if (kDown & KEY_START) break;

            bool inventoryOpen = hud.isInventoryOpen();

            // Y opens/closes the inventory. Edge-detect on the held state:
            // the emulator produces a phantom key-down on release which
            // makes the jactive-driven binding toggle twice
            bool yHeldNow = (kHeld & KEY_Y) != 0;
            if (yHeldNow && !yWasHeld) {
                if (inventoryOpen) {
                    hud.closeInventory();
                } else {
                    hud.openInventory();
                }
                inventoryOpen = hud.isInventoryOpen();
            }
            yWasHeld = yHeldNow;

            // X switches the inventory <-> all-blocks tab (creative only).
            // Edge-detect via held state: the emulator produces a phantom
            // key-down event on release
            bool xHeldNow = (kHeld & KEY_X) != 0;
            if (inventoryOpen && creativeMode && xHeldNow && !xWasHeld) {
                hud.setContentAccess(!hud.isContentAccess());
            }
            xWasHeld = xHeldNow;

            input3ds->update();

            // look: D-pad rotates the player camera
            // (or scrolls the inventory panel while it is open)
            glm::vec3 rotation = player->getRotation();
            const float lookSpeed = 2.6f;  // degrees per frame
            if (inventoryOpen) {
                if (kDown & KEY_DUP) input3ds->setScroll(1);
                if (kDown & KEY_DDOWN) input3ds->setScroll(-1);
            } else {
                if (kHeld & KEY_DLEFT) rotation.x += lookSpeed;
                if (kHeld & KEY_DRIGHT) rotation.x -= lookSpeed;
                if (kHeld & KEY_DUP) rotation.y += lookSpeed;
                if (kHeld & KEY_DDOWN) rotation.y -= lookSpeed;
            }
            rotation.y = glm::clamp(rotation.y, -89.9f, 89.9f);
            if (rotation.x > 180.0f) rotation.x -= 360.0f;
            if (rotation.x < -180.0f) rotation.x += 360.0f;
            player->setRotation(rotation);

            // UI captures input while the inventory panel is open
            const Input* interactionInput = inventoryOpen ? nullptr : input3ds;
            pc.update(1.0f / 60.0f, interactionInput);

            // liquid check at the player's position (feet + head)
            bool inFluid = false;
            {
                auto ppos = player->getPosition();
                for (float dy : {0.4f, 1.4f}) {
                    auto vox = blocks_agent::get(
                        *player->chunks,
                        (int)std::floor(ppos.x),
                        (int)std::floor(ppos.y + dy),
                        (int)std::floor(ppos.z));
                    if (vox && vox->id) {
                        const auto& d =
                            content->getIndices()->blocks.require(vox->id);
                        for (const auto& tag : d.tags) {
                            if (tag == "core:liquid") {
                                inFluid = true;
                                break;
                            }
                        }
                    }
                    if (inFluid) break;
                }
            }

            // movement driver: circle pad -> hitbox velocity
            // (the PC build does this in Lua, which needs the hud/input libs)
            if (Hitbox* hitbox = player->getHitbox()) {
                const auto& bindings = input3ds->getBindings();
                auto camera = player->fpCamera;
                glm::vec3 f(camera->front.x, 0, camera->front.z);
                glm::vec3 r(camera->right.x, 0, camera->right.z);
                if (glm::length(f) > 0.001f) f = glm::normalize(f);
                if (glm::length(r) > 0.001f) r = glm::normalize(r);
                // horizontal movement is driven by the real player.lua
                // component now (client mode); only water buoyancy stays
                // port-side
                (void)f;
                (void)r;
                if (inFluid) {
                    // buoyancy: slow sinking, A swims up
                    if (hitbox->velocity.y < -2.0f) {
                        hitbox->velocity.y = -2.0f;
                    }
                    if (!inventoryOpen && bindings.active(BIND_MOVE_JUMP)) {
                        hitbox->velocity.y = 2.5f;
                    }
                }
            } else if (physicsWarned < 3 && frame > 120) {
                report("[warn] player has no hitbox yet (f%d)\n", frame);
                physicsWarned++;
            }

            // world upkeep: generation + lighting + ticking + physics
            lc->update(1.0f / 60.0f, false);
            worldRenderer.update(*player->chunks, 2);

            // camera follows the player entity
            pc.postUpdate(1.0f / 60.0f, 240, interactionInput, false);
            Camera& camera = *player->currentCamera;
            glm::mat4 view = camera.getView();
            float fovDeg = glm::degrees(camera.getFov()) * camera.zoom;

            hud.update(true);
            scripting::on_entities_render(1.0f / 60.0f);
            scripting::on_frontend_render();
            gui.act(1.0f / 60.0f, glm::uvec2(320, 240));
            gui.postAct();

            audio::set_listener(
                camera.position, glm::vec3(),
                camera.position + camera.front, camera.up);
            audio::update(1.0 / 60.0);

            gfx3ds::beginFrame(0.55f, 0.75f, 0.95f);
            gfx3ds::setViewProjection(view, fovDeg, 0.1f, 120.0f);
            worldRenderer.draw(
                *player->chunks, camera.position, level,
                player->getEntity());

            // underwater tint when the camera is inside a liquid
            {
                auto cpos = camera.position;
                auto vox = blocks_agent::get(
                    *player->chunks,
                    (int)std::floor(cpos.x),
                    (int)std::floor(cpos.y),
                    (int)std::floor(cpos.z));
                bool camInFluid = false;
                if (vox && vox->id) {
                    const auto& d =
                        content->getIndices()->blocks.require(vox->id);
                    for (const auto& tag : d.tags) {
                        if (tag == "core:liquid") {
                            camInFluid = true;
                            break;
                        }
                    }
                }
                if (camInFluid) {
                    gfx3ds::beginTopUI();
                    uiBatch.begin();
                    uiBatch.texture(nullptr);
                    uiBatch.setColor(glm::vec4(0.1f, 0.25f, 0.6f, 0.4f));
                    uiBatch.rect(0, 0, 400, 240);
                    uiBatch.flush();
                    uiBatch.setColor(glm::vec4(1.0f));
                }
            }

            // crosshair over the world
            if (!inventoryOpen) {
                gfx3ds::beginTopUI();
                auto crosshair = assets.get<Texture>("gui/crosshair");
                uiBatch.begin();
                uiBatch.texture(crosshair);
                int cw = crosshair ? crosshair->getWidth() : 8;
                int chh = crosshair ? crosshair->getHeight() : 8;
                uiBatch.rect(
                    (400 - cw) / 2, (240 - chh) / 2, cw, chh,
                    0, 0, 1, 1, 1, 1, 1, 0.7f);
                uiBatch.flush();
            }

            // bottom screen UI
            gfx3ds::beginBottomUI(0.08f, 0.08f, 0.1f);
            {
                DrawContext ctx(
                    nullptr, engine.getWindow(), &uiBatch);
                hud.draw(ctx);
                gui.draw(ctx, *worldRenderer.getAssets());
            }
            gfx3ds::endFrame();

            // once terrain exists: drop the player onto the surface
            if (!spawnAdjusted) {
                auto* c0 = player->chunks->getChunk(0, 0);
                if (c0 && c0->flags.lighted) {
                    spawnPos.y = c0->top + 3;
                    player->teleport(spawnPos);
                    spawnAdjusted = true;
                }
            } else if (!showcaseBuilt) {
                // once the player lands, carve the lighting demo into the
                // terrain ahead at their own ground level
                if (Hitbox* hb = player->getHitbox()) {
                    if (hb->grounded) {
                        buildLightShowcase(
                            (int)std::floor(player->getPosition().y));
                        showcaseBuilt = true;
                    }
                }
            }

            // autosave every ~40 seconds
            if (frame > 0 && frame % 2400 == 0) {
                lc->saveWorld();
            }

            frame++;
            if (frame == 90 || frame == 300) {
                int lighted = 0;
                for (const auto& c : player->chunks->getChunks()) {
                    if (c && c->flags.lighted) lighted++;
                }
                auto pos = player->getPosition();
                report("[ok] f%d meshes=%zu chunks=%zu lighted=%d pos=(%.1f,%.1f,%.1f) hitbox=%d\n",
                       frame,
                       worldRenderer.meshesCount(),
                       player->chunks->getChunksCount(), lighted,
                       pos.x, pos.y, pos.z,
                       player->getHitbox() != nullptr);
                heap_stat("loop");
            }
        }

        // save on exit
        if (hud.isInventoryOpen()) {
            hud.closeInventory();
        }
        scripting::on_frontend_close();
        lc->saveWorld();
        engine.onWorldClosed();
        report("[ok] world saved\n");
    } catch (const std::exception& err) {
        report("EXCEPTION: %s\n", err.what());
        wait_exit();
    }

    report("DONE\n");
    if (result_file) fclose(result_file);
    gfx3ds::shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
