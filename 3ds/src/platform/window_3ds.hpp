#pragma once

// Window/Input implementations for the 3DS port.
// The UI lives on the bottom screen (320x240); the touch panel acts as
// the mouse cursor.

#include "window/Window.hpp"
#include "window/input.hpp"
#include "graphics/core/ImageData.hpp"

class Window3DS : public Window {
public:
    Window3DS() : Window({320, 240}) {}

    void swapBuffers() override {}
    bool isMaximized() const override { return true; }
    bool isFocused() const override { return true; }
    bool isIconified() const override { return false; }
    bool isShouldClose() const override { return shouldClose; }
    void setShouldClose(bool flag) override { shouldClose = flag; }
    void setCursor(CursorShape) override {}
    void setMode(WindowMode newMode) override { mode = newMode; }
    WindowMode getMode() const override { return mode; }
    void focus() override {}
    void setTitle(const std::string&) override {}
    void setIcon(const ImageData*) override {}
    void pushScissor(glm::vec4 area) override;
    void popScissor() override;
    void resetScissor() override;
    void setShouldRefresh() override { refresh = true; }
    bool checkShouldRefresh() override {
        bool value = refresh;
        refresh = false;
        return value;
    }
    double time() override;
    void setFramerate(int) override {}
    std::unique_ptr<ImageData> takeScreenshot() override { return nullptr; }
private:
    bool shouldClose = false;
    bool refresh = false;
    std::vector<glm::vec4> scissors;
};

class Input3DS : public Input {
public:
    // registers the engine bindings (movement.*, player.*, camera.*)
    // mapped onto 3DS HID buttons
    Input3DS();

    // feed 3DS HID state (call once per frame after hidScanInput)
    void update();

    // queue a scroll step for the next GUI::act (D-pad drives the wheel)
    void setScroll(int value) { scroll = value; }

    void pollEvents(bool) override {}
    const char* getClipboardText() const override { return ""; }
    void setClipboardText(const char*) override {}
    int getScroll() override {
        int value = scroll;
        scroll = 0;
        return value;
    }
    bool pressed(Keycode keycode) const override;
    bool jpressed(Keycode keycode) const override;
    bool clicked(Mousecode code) const override {
        return code == Mousecode::BUTTON_1 && touchHeld;
    }
    bool jclicked(Mousecode code) const override {
        return code == Mousecode::BUTTON_1 && touchStarted;
    }
    void simulateKey(Keycode, bool) override {}
    void simulateClick(int, bool) override {}
    void simulateCursorPos(double, double) override {}
    void simulateCodepoint(uint) override {}
    CursorState getCursor() const override { return cursor; }
    bool isCursorLocked() const override { return false; }
    void toggleCursor() override {}
    Bindings& getBindings() override { return bindings; }
    const Bindings& getBindings() const override { return bindings; }
    ObserverHandler addKeyCallback(Keycode, KeyCallback) override {
        return ObserverHandler();
    }
    const std::vector<Keycode>& getPressedKeys() const override {
        return pressedKeys;
    }
    const std::vector<uint>& getCodepoints() const override {
        return codepoints;
    }
private:
    CursorState cursor {};
    Bindings bindings;
    std::vector<Keycode> pressedKeys;
    std::vector<uint> codepoints;
    bool touchHeld = false;
    bool touchStarted = false;
    int scroll = 0;
    unsigned int held = 0;
    unsigned int heldPrev = 0;
    int cpadX = 0;
    int cpadY = 0;

    bool evalKeycode(Keycode keycode, unsigned int buttons) const;
};
