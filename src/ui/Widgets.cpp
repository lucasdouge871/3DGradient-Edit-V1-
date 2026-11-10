#include "Widgets.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace g3dui {

static std::string pickFont(std::string const& preferred) {
    // A missing .fnt would crash CCLabelBMFont, so fall back to GD's own font.
    return fileExistsInSearchPaths(preferred.c_str()) ? preferred : std::string("bigFont.fnt");
}

std::string const& fontMedium() {
    static std::string font = pickFont("Rubik.fnt"_spr);
    return font;
}

std::string const& fontBold() {
    static std::string font = pickFont("RubikBold.fnt"_spr);
    return font;
}

CCSprite* sprite(char const* name) {
    auto path = Mod::get()->expandSpriteName(name);
    if (fileExistsInSearchPaths(path.c_str())) {
        if (auto spr = CCSprite::create(path.c_str())) return spr;
    }
    log::warn("3DGradient Edit: missing sprite {}", name);
    auto spr = CCSprite::createWithSpriteFrameName("whiteSquare60_001.png");
    if (!spr) spr = CCSprite::create();
    return spr;
}

NineSlice* nine(char const* name, CCSize size, NineSlice::Insets insets) {
    NineSlice* ret = nullptr;
    auto path = Mod::get()->expandSpriteName(name);
    if (fileExistsInSearchPaths(path.c_str())) {
        ret = NineSlice::create(path, {}, insets);
    }
    if (!ret) {
        log::warn("3DGradient Edit: missing sprite {}", name);
        ret = NineSlice::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
        if (ret) {
            ret->setColor({20, 14, 54});
        } else {
            ret = NineSlice::create("square02b_001.png");
        }
    }
    if (ret) {
        ret->setContentSize(size);
        ret->setAnchorPoint({0.f, 0.f});
    }
    return ret;
}

NineSlice* nine(char const* name, CCSize size, float inset) {
    return nine(name, size, NineSlice::Insets{inset, inset, inset, inset});
}

CCLabelBMFont* label(std::string const& text, bool bold, float scale, ccColor3B color) {
    auto l = CCLabelBMFont::create(text.c_str(), (bold ? fontBold() : fontMedium()).c_str());
    l->setScale(scale);
    l->setColor(color);
    return l;
}

CCMenuItemSpriteExtra* pillButton(
    std::string const& text, CCSize size, CCObject* target, SEL_MenuHandler callback,
    float textScale, bool bold, CCLabelBMFont** outLabel
) {
    auto node = CCNode::create();
    node->setContentSize(size);

    if (auto bg = nine("ui-pill.png", size, NineSlice::Insets{5.f, 6.f, 5.f, 6.f})) {
        bg->setPosition({0.f, 0.f});
        node->addChild(bg);
    }
    auto lbl = label(text, bold, textScale);
    lbl->limitLabelWidth(size.width - 8.f, textScale, .25f);
    lbl->setPosition({size.width / 2, size.height / 2});
    node->addChild(lbl, 1);
    if (outLabel) *outLabel = lbl;

    return CCMenuItemSpriteExtra::create(node, target, callback);
}

// ---------------------------------------------------------------------------
// DragPad
// ---------------------------------------------------------------------------

DragPad* DragPad::create(CCSize size, Mode mode, Callback callback) {
    auto ret = new DragPad();
    if (ret->init(size, mode, std::move(callback))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool DragPad::init(CCSize size, Mode mode, Callback callback) {
    if (!CCLayer::init()) return false;
    m_box = size;
    m_mode = mode;
    m_callback = std::move(callback);
    this->setContentSize(size);
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.f, 0.f});
    this->setTouchEnabled(true);
    return true;
}

void DragPad::registerWithTouchDispatcher() {
    // Popups register at -500; sit just above so drags reach the pad first.
    CCTouchDispatcher::get()->addTargetedDelegate(this, -501, true);
}

void DragPad::setThumb(CCNode* thumb) {
    if (m_thumb) m_thumb->removeFromParent();
    m_thumb = thumb;
    if (m_thumb) {
        this->addChild(m_thumb, 10);
        this->placeThumb();
    }
}

void DragPad::placeThumb() {
    if (!m_thumb) return;
    float x = m_mode == Mode::Vertical ? m_box.width / 2 : m_u * m_box.width;
    float y = m_mode == Mode::Horizontal ? m_box.height / 2 : m_v * m_box.height;
    m_thumb->setPosition({x, y});
}

void DragPad::setValue(float u, float v) {
    m_u = std::max(0.f, std::min(1.f, u));
    m_v = std::max(0.f, std::min(1.f, v));
    this->placeThumb();
}

bool DragPad::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible()) return false;
    for (CCNode* n = this->getParent(); n; n = n->getParent()) {
        if (!n->isVisible()) return false;
    }
    auto p = this->convertToNodeSpace(touch->getLocation());
    if (p.x < -m_slop || p.y < -m_slop || p.x > m_box.width + m_slop || p.y > m_box.height + m_slop) {
        return false;
    }
    this->applyTouch(touch);
    return true;
}

void DragPad::ccTouchMoved(CCTouch* touch, CCEvent*) {
    this->applyTouch(touch);
}

void DragPad::applyTouch(CCTouch* touch) {
    auto p = this->convertToNodeSpace(touch->getLocation());
    float u = m_box.width > 0.f ? p.x / m_box.width : 0.f;
    float v = m_box.height > 0.f ? p.y / m_box.height : 0.f;
    if (m_mode == Mode::Horizontal) v = m_v;
    if (m_mode == Mode::Vertical) u = m_u;
    this->setValue(u, v);
    if (m_callback) m_callback(m_u, m_v);
}

}
