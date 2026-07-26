// Port-local replacement for src/frontend/debug_panel.cpp: the PC panel
// pulls eight renderer statics that do not exist on 3DS. This one shows a
// few live counters.
#include <memory>

#include "engine/Engine.hpp"
#include "graphics/core/Mesh.hpp"
#include "graphics/ui/GUI.hpp"
#include "graphics/ui/elements/Label.hpp"
#include "graphics/ui/elements/Panel.hpp"
#include "objects/Player.hpp"
#include "util/stringutil.hpp"
#include "world/Level.hpp"

using namespace gui;

std::shared_ptr<UINode> create_debug_panel(
    Engine& engine, Level& level, Player& player, bool allowDebugCheats
) {
    auto& gui = engine.getGUI();
    auto panel = std::make_shared<Panel>(
        gui, glm::vec2(150, 60), glm::vec4(4.0f), 2.0f);
    panel->setColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
    panel->setPos(glm::vec2(2, 2));

    auto posLabel = std::make_shared<Label>(gui, L"");
    panel->add(posLabel);
    auto statsLabel = std::make_shared<Label>(gui, L"");
    panel->add(statsLabel);

    auto* playerPtr = &player;
    panel->listenInterval(0.25f, [posLabel, statsLabel, playerPtr]() {
        auto pos = playerPtr->getPosition();
        posLabel->setText(
            L"pos " + std::to_wstring((int)pos.x) + L" " +
            std::to_wstring((int)pos.y) + L" " + std::to_wstring((int)pos.z));
        statsLabel->setText(
            L"meshes " + std::to_wstring(MeshStats::meshesCount) +
            L" draws " + std::to_wstring(MeshStats::drawCalls));
    });
    return panel;
}
