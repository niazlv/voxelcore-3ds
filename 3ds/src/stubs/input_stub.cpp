// Minimal Bindings/Binding implementation for the 3DS port
// (replaces GLFW-based window/input.cpp keycode tables).
#include "window/input.hpp"

#include <stdexcept>

#include "data/dv.hpp"

void Bindings::read(const dv::value&, BindType) {
    // keyboard bindings are not used on 3DS
}

std::string Bindings::write() const {
    return "";
}

Binding& Bindings::require(const std::string& name) {
    auto found = bindings.find(name);
    if (found == bindings.end()) {
        throw std::runtime_error("binding '" + name + "' does not exist");
    }
    return found->second;
}

const Binding& Bindings::require(const std::string& name) const {
    auto found = bindings.find(name);
    if (found == bindings.end()) {
        throw std::runtime_error("binding '" + name + "' does not exist");
    }
    return found->second;
}
