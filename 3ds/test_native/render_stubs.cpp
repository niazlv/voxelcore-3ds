// Stubs for render/settings symbols pulled in by core TUs.
// On the 3DS build these are dropped by --gc-sections; the macOS linker
// requires definitions.
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <string>

#include "graphics/core/DrawContext.hpp"
#include "graphics/core/LineBatch.hpp"
#include "graphics/core/Texture.hpp"
#include "graphics/render/ModelBatch.hpp"
#include "logic/LevelController.hpp"
#include "io/settings_io.hpp"

void DrawContext::setDepthMask(bool) {}
void DrawContext::setDepthTest(bool) {}
void DrawContext::setLineWidth(float) {}
DrawContext DrawContext::sub(Flushable*) const {
    throw std::runtime_error("DrawContext::sub stub");
}
DrawContext::~DrawContext() {}

Level* LevelController::getLevel() {
    return level.get();
}
ChunksController* LevelController::getChunksController() {
    return chunks.get();
}

void LineBatch::box(float, float, float, float, float, float, float, float, float, float) {}
void LineBatch::line(float, float, float, float, float, float, float, float, float, float) {}

void ModelBatch::draw(glm::mat4, glm::vec3, const model::Model*,
    const std::unordered_map<std::string, std::string>*) {}

std::vector<Section>& SettingsHandler::getSections() {
    static std::vector<Section> sections;
    return sections;
}
Setting* SettingsHandler::getSetting(const std::string&) const {
    return nullptr;
}
bool SettingsHandler::has(const std::string&) const {
    return false;
}
void SettingsHandler::setValue(const std::string&, const dv::value&) {}

std::unique_ptr<Texture> Texture::from(const ImageData*) {
    return nullptr;
}
void Texture::setNearestFilter() {}
