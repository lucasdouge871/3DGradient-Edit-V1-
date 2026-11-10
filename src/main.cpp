#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#ifdef GEODE_IS_DESKTOP
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif

#include <cmath>

#include "ui/GradientEditPopup.hpp"

using namespace geode::prelude;

namespace {
    constexpr char const* kNextToUndo = "Next to Undo/Redo";

    void openPopup(LevelEditorLayer* lel) {
        if (!lel || GradientEditPopup::isOpen()) return;
        if (auto popup = GradientEditPopup::create(lel)) popup->show();
    }

    // Position of `node` (which lives inside its own parent) in `ref`'s coordinates.
    CCPoint inSpaceOf(CCNode* ref, CCNode* node) {
        auto parent = node->getParent();
        CCPoint world = parent ? parent->convertToWorldSpace(node->getPosition()) : node->getPosition();
        return ref->convertToNodeSpace(world);
    }

    CCPoint cornerPosition(std::string const& mode, CCSize win) {
        const float margin = 26.f;
        bool left = mode.find("Left") != std::string::npos;
        bool bottom = mode.find("Bottom") != std::string::npos;
        float x = left ? margin : win.width - margin;
        float y = bottom ? margin + 80.f : win.height - margin - 40.f;
        return ccp(x, y);
    }

    // Puts the button's menu where the user asked. The default is right next to the
    // editor's own Undo / Redo buttons, which are always on screen, instead of a
    // guessed corner that might be covered by something else.
    void placeMenu(EditorUI* ui, CCNode* menu) {
        auto win = CCDirector::get()->getWinSize();
        // If mod.json doesn't have the setting (older copy), just use the default placement.
        std::string mode = kNextToUndo;
        if (Mod::get()->hasSetting("button-position")) {
            mode = Mod::get()->getSettingValue<std::string>("button-position");
        }

        CCPoint pos = cornerPosition(mode == kNextToUndo ? std::string("Top Right") : mode, win);
        char const* how = "corner";

        if (mode == kNextToUndo && ui->m_undoBtn && ui->m_redoBtn) {
            CCPoint u = inSpaceOf(ui, ui->m_undoBtn);
            CCPoint r = inSpaceOf(ui, ui->m_redoBtn);
            CCPoint step = ccp(r.x - u.x, r.y - u.y);
            float len = std::sqrt(step.x * step.x + step.y * step.y);
            if (len < 8.f || len > 90.f) step = ccp(0.f, -38.f); // unexpected layout: go below instead
            CCPoint candidate = ccp(r.x + step.x, r.y + step.y);
            bool onScreen = candidate.x > 12.f && candidate.y > 12.f &&
                            candidate.x < win.width - 12.f && candidate.y < win.height - 12.f;
            if (onScreen) {
                pos = candidate;
                how = "next to undo/redo";
            }
        }
        menu->setPosition(pos);
        log::info("3DGradient Edit: button placed at ({}, {}) [{}], window {}x{}", pos.x, pos.y, how, win.width, win.height);
    }
}

class $modify(GradientEditUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        // The button lives in its own menu, so it doesn't depend on any other
        // mod's node IDs or on the layout of GD's own editor menus.
        CCSprite* icon = CCSprite::create("btn-open.png"_spr);
        if (!icon) {
            log::warn("3DGradient Edit: btn-open.png not found, using a fallback icon");
            icon = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        }
        // Scale the SPRITE, not the button: a GD button animates back to its own base scale
        // when pressed, so scaling the button itself made it jump to full size on click.
        float iconHeight = std::max(1.f, icon->getContentSize().height);
        icon->setScale(30.f / iconHeight);

        auto button = CCMenuItemSpriteExtra::create(icon, this, menu_selector(GradientEditUI::onOpen3DGradientEdit));
        button->setID("open-button"_spr);

        auto menu = CCMenu::create();
        menu->setID("button-menu"_spr);
        menu->setContentSize({0.f, 0.f});
        menu->addChild(button);
        button->setPosition({0.f, 0.f});
        this->addChild(menu, 1000);

        // Visible immediately in a corner, then moved next to Undo/Redo one frame
        // later, once the editor has finished laying out its own buttons.
        placeMenu(this, menu);
        Ref<EditorUI> ui = this;
        Ref<CCMenu> menuRef = menu;
        queueInMainThread([ui, menuRef]() {
            if (ui->getParent() && menuRef->getParent()) placeMenu(ui, menuRef);
        });

        return true;
    }

    void onOpen3DGradientEdit(CCObject*) {
        openPopup(m_editorLayer);
    }
};

#ifdef GEODE_IS_DESKTOP
// Backup way in: press F6 while you are in the editor.
class $modify(GradientEditKeys, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool isRepeat, double timestamp) {
        if (down && !isRepeat && key == KEY_F6) {
            if (auto scene = CCDirector::get()->getRunningScene()) {
                if (auto lel = scene->getChildByType<LevelEditorLayer>(0)) {
                    openPopup(lel);
                    return true;
                }
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, isRepeat, timestamp);
    }
};
#endif
