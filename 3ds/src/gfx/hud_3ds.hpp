#pragma once

#include <memory>

class Level;
class Player;
class Content;

namespace gui {
    class GUI;
    class Panel;
    class InventoryView;
    class SlotView;
}

/// Touch HUD for the bottom screen: hotbar + creative content access panel,
/// built from the engine's real InventoryView/SlotView elements.
class Hud3DS {
public:
    Hud3DS(gui::GUI& gui, Level& level, Player& player);
    ~Hud3DS();

    /// per-frame: chosen slot highlight + exchange slot follows the cursor
    void update();

    void toggleInventory();
    bool isInventoryOpen() const;

    /// scroll the content access panel (positive = up)
    void scrollInventory(int delta);

private:
    gui::GUI& gui;
    Level& level;
    Player& player;
    std::shared_ptr<gui::InventoryView> hotbar;
    std::shared_ptr<gui::InventoryView> contentAccess;
    std::shared_ptr<gui::Panel> contentPanel;
    std::shared_ptr<gui::SlotView> exchangeSlot;
    std::shared_ptr<class Inventory> accessInventory;
    std::shared_ptr<class Inventory> exchangeInventory;
};
