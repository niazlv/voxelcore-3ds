#include "hud_3ds.hpp"

#include <glm/glm.hpp>

#include "content/Content.hpp"
#include "graphics/ui/GUI.hpp"
#include "graphics/ui/elements/InventoryView.hpp"
#include "graphics/ui/elements/Panel.hpp"
#include "items/Inventories.hpp"
#include "items/Inventory.hpp"
#include "objects/Player.hpp"
#include "world/Level.hpp"
#include "window/input.hpp"
#include "debug/Logger.hpp"

static debug::Logger logger("hud-3ds");

using namespace gui;

Hud3DS::Hud3DS(GUI& gui, Level& level, Player& player)
    : gui(gui), level(level), player(player) {
    const auto& content = level.content;
    auto& indices = *content.getIndices();
    auto inventory = player.getInventory();

    // grabbed-item slot required by SlotView::clicked (follows the cursor)
    exchangeInventory = level.inventories->createVirtual(1);
    exchangeSlot = std::make_shared<SlotView>(
        gui, SlotLayout(-1, glm::vec2(), false, false, nullptr, nullptr, nullptr)
    );
    exchangeSlot->setId("hud.exchange-slot");
    exchangeSlot->bind(
        exchangeInventory->getId(), exchangeInventory->getSlot(0), 0, &content
    );
    exchangeSlot->setColor(glm::vec4());
    exchangeSlot->setInteractive(false);
    exchangeSlot->setZIndex(2);
    // never shown: a touch UI has no "held on the cursor" state, items go
    // straight into the chosen hotbar slot
    exchangeSlot->setVisible(false);
    gui.store(SlotView::EXCHANGE_SLOT_NAME, exchangeSlot);
    gui.add(exchangeSlot);

    // hotbar: first 10 slots of the player inventory, bottom center.
    // tapping a slot selects it; tapping while holding an item drops it in
    // (taking is off so a bare tap never empties the slot)
    {
        auto* playerPtr = &player;
        SlotLayout slotLayout(-1, glm::vec2(), false, false,
        [playerPtr](uint index, ItemStack&) {
            playerPtr->setChosenSlot(static_cast<int>(index));
            logger.info() << "chosen slot " << index;
        }, nullptr, nullptr);
        slotLayout.taking = false;
        slotLayout.placing = true;
        InventoryBuilder builder(gui);
        builder.addGrid(10, 10, glm::vec2(), glm::vec4(2), true, slotLayout);
        hotbar = builder.build();
        hotbar->setId("hud.hotbar");
        hotbar->bind(inventory, &content);
        auto size = hotbar->getSize();
        hotbar->setPos(glm::vec2((320 - size.x) / 2, 240 - size.y - 2));
        gui.add(hotbar);
    }

    // creative content access: all items, tap to grab / tap slot to place
    {
        size_t itemsCount = indices.items.count();
        accessInventory = std::make_shared<Inventory>(0, itemsCount);
        for (size_t id = 1; id < itemsCount; id++) {
            accessInventory->getSlot(id - 1).set(ItemStack(id, 1));
        }
        // one tap = the item goes into the currently chosen hotbar slot
        auto chosenPtr = &player;
        auto grabbed = exchangeSlot;
        SlotLayout slotLayout(-1, glm::vec2(), false, true,
        [chosenPtr, inventory, grabbed](uint, ItemStack& item) {
            inventory->getSlot(chosenPtr->getChosenSlot()).set(item);
            grabbed->getStack().clear();
        }, nullptr, nullptr);
        InventoryBuilder builder(gui);
        builder.addGrid(
            8, itemsCount - 1, glm::vec2(), glm::vec4(4, 4, 4, 4), true,
            slotLayout);
        contentAccess = builder.build();
        contentAccess->setId("hud.content-access");
        contentAccess->bind(accessInventory, &content);
        contentAccess->setMargin(glm::vec4());

        auto caSize = contentAccess->getSize();
        const float panelH = 176.0f;
        contentPanel = std::make_shared<Panel>(
            gui, glm::vec2(caSize.x, panelH), glm::vec4(0.0f), 0.0f);
        contentPanel->setColor(glm::vec4(0.06f, 0.06f, 0.08f, 0.94f));
        // Panel::add crops the panel to its content height; pin it instead
        contentPanel->setMinLength(0);
        contentPanel->setMaxLength(panelH);
        contentPanel->add(contentAccess);
        contentPanel->setScrollable(true);
        contentPanel->setId("hud.content-access-panel");
        contentPanel->setPos(glm::vec2((320 - caSize.x) / 2, 4));
        contentPanel->setZIndex(1);
        contentPanel->setVisible(false);
        gui.add(contentPanel);
        logger.info() << "content access " << caSize.x << "x" << caSize.y
                      << " slots=" << contentAccess->getSlotsCount()
                      << " panel=" << contentPanel->getSize().x << "x"
                      << contentPanel->getSize().y;
    }
}

Hud3DS::~Hud3DS() {
    gui.remove(hotbar.get());
    gui.remove(contentPanel.get());
    gui.remove(exchangeSlot.get());
}

void Hud3DS::update() {
    hotbar->setSelected(player.getChosenSlot());
}

void Hud3DS::toggleInventory() {
    bool open = !contentPanel->isVisible();
    contentPanel->setVisible(open);
    auto pos = contentPanel->calcPos();
    auto size = contentPanel->getSize();
    logger.info() << "inventory " << (open ? "open" : "closed") << " at "
                  << pos.x << "," << pos.y << " " << size.x << "x" << size.y;
}

bool Hud3DS::isInventoryOpen() const {
    return contentPanel->isVisible();
}

void Hud3DS::scrollInventory(int delta) {
    contentPanel->scrolled(delta);
}
