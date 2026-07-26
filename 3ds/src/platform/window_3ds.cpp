#include "window_3ds.hpp"

#include <3ds.h>
#include <citro3d.h>

#include "core_defs.hpp"

// scissor state is applied by the UI pipeline (bottom screen, 320x240,
// rotated framebuffer: x/y swap)
static void apply_scissor(const glm::vec4* area) {
    if (area == nullptr) {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        return;
    }
    float x = area->x, y = area->y, w = area->z, h = area->w;
    if (w <= 0 || h <= 0) {
        C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 0, 0);
        return;
    }
    // bottom framebuffer is 240x320, rotated 90°:
    // fb_x = 240 - (y + h), fb_y = 320 - (x + w)
    int left = 240 - static_cast<int>(y + h);
    int top = 320 - static_cast<int>(x + w);
    int right = 240 - static_cast<int>(y);
    int bottom = 320 - static_cast<int>(x);
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > 240 ? 240 : right;
    bottom = bottom > 320 ? 320 : bottom;
    if (left >= right || top >= bottom) {
        C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 1, 1);
        return;
    }
    C3D_SetScissor(GPU_SCISSOR_NORMAL, left, top, right, bottom);
}

void Window3DS::pushScissor(glm::vec4 area) {
    if (!scissors.empty()) {
        // intersect with the parent scissor area
        const auto& p = scissors.back();
        float x1 = glm::max(area.x, p.x);
        float y1 = glm::max(area.y, p.y);
        float x2 = glm::min(area.x + area.z, p.x + p.z);
        float y2 = glm::min(area.y + area.w, p.y + p.w);
        area = {x1, y1, glm::max(0.0f, x2 - x1), glm::max(0.0f, y2 - y1)};
    }
    scissors.push_back(area);
    apply_scissor(&scissors.back());
}

void Window3DS::popScissor() {
    if (!scissors.empty()) {
        scissors.pop_back();
    }
    apply_scissor(scissors.empty() ? nullptr : &scissors.back());
}

void Window3DS::resetScissor() {
    scissors.clear();
    apply_scissor(nullptr);
}

double Window3DS::time() {
    return osGetTime() / 1000.0;
}

// engine bindings mapped onto 3DS HID:
//   circle pad -> movement, A -> jump, B -> crouch, X -> sprint,
//   L -> destroy/attack, R -> build, SELECT -> camera mode
Input3DS::Input3DS() {
    auto bindKey = [this](const std::string& name, Keycode code) {
        bindings.bind(name, InputType::KEYBOARD, static_cast<int>(code));
    };
    bindKey(BIND_MOVE_FORWARD, Keycode::W);
    bindKey(BIND_MOVE_BACK, Keycode::S);
    bindKey(BIND_MOVE_LEFT, Keycode::A);
    bindKey(BIND_MOVE_RIGHT, Keycode::D);
    bindKey(BIND_MOVE_JUMP, Keycode::SPACE);
    bindKey(BIND_MOVE_CROUCH, Keycode::LEFT_SHIFT);
    bindKey(BIND_MOVE_SPRINT, Keycode::LEFT_CONTROL);
    bindKey(BIND_MOVE_CHEAT, Keycode::R);
    bindKey(BIND_CAM_ZOOM, Keycode::C);
    // SELECT throws the chosen item (base pack 'player.drop' callback);
    // camera.mode is intentionally left unbound - buttons are scarce
    bindKey("player.drop", Keycode::Q);
    bindKey(BIND_PLAYER_ATTACK, Keycode::F);
    bindKey(BIND_PLAYER_DESTROY, Keycode::F);
    bindKey(BIND_PLAYER_BUILD, Keycode::G);
    bindKey(BIND_PLAYER_FAST_INTERACTOIN, Keycode::X);
    // hud.inventory is intentionally NOT bound to a real key: the main
    // loop toggles the inventory with edge detection (emulator phantom
    // key-down on release). The rest exist so Lua require() never throws.
    for (const char* name :
         {"player.pick", "player.noclip", "player.flight", "hud.inventory",
          "hud.chat", "devtools.console", "chunks.reload", "camera.mode"}) {
        bindings.bind(name, InputType::KEYBOARD,
                      static_cast<int>(Keycode::UNKNOWN));
    }
}

bool Input3DS::evalKeycode(Keycode keycode, unsigned int buttons) const {
    switch (keycode) {
        case Keycode::W: return cpadY > 25;
        case Keycode::S: return cpadY < -25;
        case Keycode::A: return cpadX < -25;
        case Keycode::D: return cpadX > 25;
        case Keycode::SPACE: return (buttons & KEY_A) != 0;
        case Keycode::LEFT_SHIFT: return (buttons & KEY_B) != 0;
        case Keycode::LEFT_CONTROL: return (buttons & KEY_X) != 0;
        case Keycode::F: return (buttons & KEY_L) != 0;
        case Keycode::G: return (buttons & KEY_R) != 0;
        case Keycode::Q: return (buttons & KEY_SELECT) != 0;
        case Keycode::E: return (buttons & KEY_Y) != 0;
        default: return false;
    }
}

bool Input3DS::pressed(Keycode keycode) const {
    return evalKeycode(keycode, held);
}

bool Input3DS::jpressed(Keycode keycode) const {
    return evalKeycode(keycode, held) && !evalKeycode(keycode, heldPrev);
}

void Input3DS::update() {
    heldPrev = held;
    held = hidKeysHeld();
    u32 down = hidKeysDown();
    circlePosition cpad;
    hidCircleRead(&cpad);
    cpadX = cpad.dx;
    cpadY = cpad.dy;

    touchStarted = (down & KEY_TOUCH) != 0;
    touchHeld = (held & KEY_TOUCH) != 0;
    if (touchHeld) {
        touchPosition touch;
        hidTouchRead(&touch);
        glm::vec2 pos(touch.px, touch.py);
        // on touch-down the cursor teleports; a delta there would be read as
        // a drag by the GUI (scrollbars, sliders)
        cursor.delta = touchStarted ? glm::vec2() : pos - cursor.pos;
        cursor.pos = pos;
    } else {
        cursor.delta = {};
    }

    // drive the named bindings from the raw HID state and fire the
    // Lua binding callbacks (input.add_callback) on activation
    for (auto& [name, binding] : bindings.getAll()) {
        bool state = false;
        if (binding.type == InputType::KEYBOARD) {
            state = evalKeycode(static_cast<Keycode>(binding.code), held);
        }
        binding.justChanged = state != binding.state;
        binding.state = state;
        if (state && binding.justChanged && binding.enabled) {
            binding.onactived.notify();
        }
    }
}
