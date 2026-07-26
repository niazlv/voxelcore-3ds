// Minimal Engine facade implementation for the 3DS port.
// Implements the subset of Engine used by the scripting layer and
// controllers; UI/window/network parts stay empty.
#include "engine/Engine.hpp"

#include <stdexcept>

#include "assets/Assets.hpp"
#include "audio/audio.hpp"
#include "content/ContentControl.hpp"
#include "devtools/Project.hpp"
#include "engine/EnginePaths.hpp"
#include "io/io.hpp"
#include "io/settings_io.hpp"
#include "logic/EngineController.hpp"
#include "logic/LevelController.hpp"
#include "logic/scripting/scripting.hpp"
#include "world/Level.hpp"
#include "objects/Players.hpp"
#include "engine/AssetsManagement.hpp"
#include "engine/WindowControl.hpp"
#include "devtools/AppScriptsControl.hpp"
#include "devtools/Editor.hpp"
#include "devtools/DebuggingServer.hpp"
#include "logic/CommandsInterpreter.hpp"
#include "network/Network.hpp"
#include "window/Window.hpp"
#include "window/input.hpp"
#include "graphics/ui/GUI.hpp"
#include "frontend/screens/Screen.hpp"
#include "../platform/window_3ds.hpp"

static Engine* g_engine = nullptr;
// assets registry owned by WorldRenderer3DS, wired via vc3ds_set_assets
static Assets* g_assets = nullptr;
static std::unique_ptr<LevelController> g_level_controller;

extern "C" void vc3ds_set_assets(void* assets) {
    g_assets = static_cast<Assets*>(assets);
}

LevelController* vc3ds_level_controller() {
    return g_level_controller.get();
}

Engine::Engine() = default;
Engine::~Engine() = default;

Engine& Engine::getInstance() {
    if (!g_engine) {
        g_engine = new Engine();
    }
    return *g_engine;
}

void Engine::initialize(CoreParameters coreParameters) {
    params = std::move(coreParameters);
    paths = std::make_unique<EnginePaths>(params);
    settingsHandler = std::make_unique<SettingsHandler>(settings);
    project = std::make_unique<Project>();
    project->basePacks.push_back("base");
    // auto-enable every content pack dropped into sdmc:/3ds/voxelcore/content
    {
        io::path contentDir("user:content");
        if (io::is_directory(contentDir)) {
            for (const auto& entry : io::directory_iterator(contentDir)) {
                if (!io::is_directory(entry)) {
                    continue;
                }
                std::string id = entry.name();
                if (id != "base" &&
                    io::is_regular_file(entry / "package.json")) {
                    project->basePacks.push_back(id);
                }
            }
        }
    }
    content = std::make_unique<ContentControl>(
        *project, *paths, nullptr, [this]() { onContentLoad(); });
    controller = std::make_unique<EngineController>(*this);
    cmd = std::make_unique<cmd::CommandsInterpreter>();
    window = std::make_unique<Window3DS>();
    input = std::make_unique<Input3DS>();
    gui = std::make_unique<gui::GUI>(*this);
    // NDSP backend; falls back to NoAudio without a DSP firmware dump
    audio::initialize(true, false, settings.audio);
    time.update(0.0);
    scripting::initialize(this);
}

void Engine::onContentLoad() {}

void Engine::close() {}
void Engine::terminate() {}
void Engine::run() {}
void Engine::postUpdate() {
    postRunnables.run();
}
void Engine::applicationTick() {}
void Engine::updateFrontend() {}
void Engine::renderFrame() {}
void Engine::nextFrame(bool) {}
void Engine::startPauseLoop() {}
void Engine::setScreen(std::shared_ptr<Screen> newScreen) {
    screen = std::move(newScreen);
}

Assets* Engine::getAssets() {
    return g_assets;
}
Assets& Engine::requireAssets() {
    if (!g_assets) {
        throw std::runtime_error("assets are not initialized");
    }
    return *g_assets;
}
AssetsLoader& Engine::acquireBackgroundLoader() {
    throw std::runtime_error("background loader is not available on 3DS");
}

EngineSettings& Engine::getSettings() {
    return settings;
}
EnginePaths& Engine::getPaths() {
    return *paths;
}
ResPaths& Engine::getResPaths() {
    return paths->resPaths;
}

void Engine::onWorldOpen(std::unique_ptr<Level> level, int64_t localPlayer) {
    Player* clientPlayer = level->players->get(localPlayer);
    g_level_controller = std::make_unique<LevelController>(
        *this, std::move(level), clientPlayer);
    scripting::on_world_load(g_level_controller.get());
}
void Engine::onWorldClosed() {
    g_level_controller.reset();
    scripting::on_world_quit();
}

void Engine::quit() {
    quitSignal = true;
}
bool Engine::isQuitSignal() const {
    return quitSignal;
}
std::shared_ptr<Screen> Engine::getScreen() {
    return screen;
}
EngineController* Engine::getController() {
    return controller.get();
}
void Engine::setLevelConsumer(OnWorldOpen consumer) {
    levelConsumer = std::move(consumer);
}
SettingsHandler& Engine::getSettingsHandler() {
    return *settingsHandler;
}
Time& Engine::getTime() {
    return time;
}
const CoreParameters& Engine::getCoreParameters() const {
    return params;
}
bool Engine::isHeadless() const {
    return params.headless;
}
ContentControl& Engine::getContentControl() {
    return *content;
}
void Engine::detachDebugger() {}
void Engine::loadControls() {}
void Engine::loadSettings() {}
void Engine::saveSettings() {}
void Engine::updateHotkeys() {}
void Engine::loadAssets() {}
void Engine::loadProject() {}
void Engine::initializeClient() {}
