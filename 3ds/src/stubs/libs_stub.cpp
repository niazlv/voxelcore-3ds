// Empty Lua binding tables for subsystems not available on 3DS (v1):
// gui/hud/assets/app/console/network/gfx-extras. Scripts see empty modules.
#include "logic/scripting/lua/lua_commons.hpp"

extern const luaL_Reg assetslib[] = {{NULL, NULL}};
extern const luaL_Reg networklib[] = {{NULL, NULL}};
// no-op implementations: is_client-gated core scripts call these
// (gfx.particles.emit in world.lua, gfx.text3d in note scripts)
static int l_noop(lua_State*) { return 0; }
static int l_noop_id(lua_State* L) {
    lua_pushinteger(L, 0);
    return 1;
}
static int l_noop_str(lua_State* L) {
    lua_pushstring(L, "");
    return 1;
}
static int l_noop_vec3(lua_State* L) {
    lua_createtable(L, 3, 0);
    for (int i = 1; i <= 3; i++) {
        lua_pushnumber(L, 0.0);
        lua_rawseti(L, -2, i);
    }
    return 1;
}
extern const luaL_Reg blockwrapslib[] = {
    {"wrap", l_noop_id}, {"unwrap", l_noop}, {"set_pos", l_noop},
    {"set_texture", l_noop}, {NULL, NULL}};
extern const luaL_Reg particleslib[] = {
    {"emit", l_noop_id}, {"stop", l_noop}, {"is_alive", l_noop_id},
    {"get_origin", l_noop_vec3}, {"set_origin", l_noop}, {NULL, NULL}};
extern const luaL_Reg text3dlib[] = {
    {"new", l_noop_id}, {"show", l_noop_id}, {"hide", l_noop},
    {"set_text", l_noop}, {"get_text", l_noop_str},
    {"set_pos", l_noop}, {"get_pos", l_noop_vec3},
    {"set_rotation", l_noop}, {"set_axis_x", l_noop},
    {"get_axis_x", l_noop_vec3}, {"set_axis_y", l_noop},
    {"get_axis_y", l_noop_vec3}, {"update_settings", l_noop},
    {NULL, NULL}};
extern const luaL_Reg posteffectslib[] = {
    {"index", l_noop_id}, {"set_intensity", l_noop},
    {"get_intensity", l_noop_id}, {"is_active", l_noop_id},
    {"set_params", l_noop}, {"set_array", l_noop}, {NULL, NULL}};

#include "logic/scripting/lua/lua_engine.hpp"
void vc3ds_run_lua(const char* src, const char* tag) {
    try {
        lua::execute(lua::get_main_state(), 0, src, tag);
    } catch (const std::exception& err) {
        // logged by the lua layer
    }
}
extern const luaL_Reg testlib[] = {{NULL, NULL}};

#include "logic/scripting/scripting_hud.hpp"
#include "frontend/UiDocument.hpp"
#include "graphics/render/WorldRenderer.hpp"

// scripting_hud registers gfx.weather; there is no WorldRenderer on 3DS,
// scripts see default weather
Weather& WorldRenderer::getWeather() {
    static Weather weather;
    return weather;
}

// ---- misc engine stubs referenced by kept scripting TUs ----
#include "assets/AssetsLoader.hpp"
#include "engine/AssetsManagement.hpp"
#include "devtools/Editor.hpp"
#include "devtools/SyntaxProcessor.hpp"
#include "coders/syntax_parser.hpp"
#include "frontend/UiDocument.hpp"
#include "devtools/DebuggingServer.hpp"
#include "coders/ogg.hpp"
#include "audio/audio.hpp"

AssetsManagement::~AssetsManagement() = default;
devtools::Editor::~Editor() = default;
devtools::DebuggingServer::~DebuggingServer() = default;
void devtools::DebuggingServer::pause(std::string&&, std::string&&, dv::value&&) {}
std::vector<devtools::DebuggingEvent> devtools::DebuggingServer::pullEvents() {
    return {};
}
void devtools::DebuggingServer::sendValue(
    dv::value&&, int, int,
    std::vector<std::variant<std::string, int>>&&) {}

// ogg::load_pcm / ogg::create_stream are implemented for real in
// 3ds/src/audio/ogg3ds.cpp (stb_vorbis)

// ---- final destructor/method stubs for never-constructed classes ----
#include "graphics/ui/GUI.hpp"
#include "network/Network.hpp"
#include "graphics/core/Batch2D.hpp"
#include "graphics/core/Font.hpp"
#include "window/Camera.hpp"
#include "graphics/ui/markdown.hpp"

bool AssetsLoader::loadExternalTexture(
    AssetsLoader&, const std::string&, const std::vector<io::path>&) {
    return false;
}

network::Network::~Network() = default;
devtools::ClientConnection::~ClientConnection() = default;

// ---- libapp support stubs (no GUI on 3DS) ----
#include "frontend/screens/MenuScreen.hpp"
#include "frontend/menu.hpp"
#include "interfaces/Task.hpp"
#include "frontend/locale.hpp"
#include "graphics/ui/gui_util.hpp"
#include "graphics/ui/elements/Menu.hpp"

Screen::Screen(Engine& engine) : engine(engine) {}
Screen::~Screen() = default;

MenuScreen::MenuScreen(Engine& engine) : Screen(engine) {}
MenuScreen::~MenuScreen() = default;
void MenuScreen::onOpen() {}
void MenuScreen::update(float) {}
void MenuScreen::draw(float) {}

namespace menus {
void call(Engine&, runnable func) {
    if (func) func();
}
UiDocument* show(Engine&, const std::string&, std::vector<dv::value>) {
    return nullptr;
}
void show_process_panel(
    Engine&, const std::shared_ptr<Task>&, const std::wstring&) {}
}

int network::Network::findFreePort() const { return 0; }


devtools::SyntaxProcessor& devtools::Editor::getSyntaxProcessor() {
    return *syntaxProcessor;
}
std::unique_ptr<FontStylesScheme> devtools::SyntaxProcessor::highlight(
    const FontStylesScheme&, const std::string&, std::wstring_view) const {
    return nullptr;
}

// ---- 2D/3D helper stubs for UI subsystems not active on 3DS ----
#include "graphics/core/Batch3D.hpp"
#include "graphics/core/Mesh.hpp"
#include "graphics/core/Texture.hpp"
#include "graphics/core/Framebuffer.hpp"
#include "graphics/core/Shader.hpp"
#include "assets/assets_util.hpp"

Batch3D::Batch3D(size_t) {}
Batch3D::~Batch3D() {}
void Batch3D::begin() {}
void Batch3D::setRegion(UVRegion) {}
void Batch3D::texture(const Texture*) {}
void Batch3D::vertex(const glm::vec3&, const glm::vec2&, const glm::vec3&) {}
void Batch3D::flush() {}

Framebuffer::Framebuffer(uint, uint, bool) {}
Framebuffer::~Framebuffer() {}
Texture* Framebuffer::getTexture() const { return nullptr; }
std::shared_ptr<Texture> Framebuffer::getSharedTexture() const {
    return nullptr;
}
void Framebuffer::resize(uint, uint) {}
void Framebuffer::bind() {}
void Framebuffer::unbind() {}
uint Framebuffer::getWidth() const { return 0; }
uint Framebuffer::getHeight() const { return 0; }

void Shader::use() {}
Shader& Shader::getUsed() {
    static Shader* dummy = nullptr;
    return *dummy;
}
void Shader::uniformMatrix(const std::string&, const glm::mat4&) {}

namespace display {
    void clear() {}
    void clearDepth() {}
    void setBgColor(glm::vec3) {}
    void setBgColor(glm::vec4) {}
}

void Binding::reset(Keycode code) {
    type = InputType::KEYBOARD;
    this->code = static_cast<int>(code);
}
void Binding::reset(Mousecode code) {
    type = InputType::MOUSE;
    this->code = static_cast<int>(code);
}

namespace input_util {
    std::string to_string(Keycode) { return "key"; }
    std::string to_string(Mousecode) { return "mouse"; }
    Keycode keycode_from(const std::string& name) {
        // enough for scripts passing single letters/names symbolically;
        // unknown names -> UNKNOWN (never pressed)
        if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') {
            return static_cast<Keycode>(name[0] - 'a' + 65);
        }
        if (name == "space") return Keycode::SPACE;
        if (name == "left-shift") return Keycode::LEFT_SHIFT;
        if (name == "left-ctrl") return Keycode::LEFT_CONTROL;
        return Keycode::UNKNOWN;
    }
    Mousecode mousecode_from(const std::string& name) {
        if (name == "left") return Mousecode::BUTTON_1;
        if (name == "right") return Mousecode::BUTTON_2;
        if (name == "middle") return Mousecode::BUTTON_3;
        return Mousecode::UNKNOWN;
    }
}

