#include "GradientEditPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/NineSlice.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <unordered_set>

using namespace geode::prelude;

namespace {
    constexpr float kW = 470.f;
    constexpr float kH = 300.f;
    constexpr int kListRows = 5;
    constexpr float kRowH = 24.f;
    constexpr size_t kMaxRecent = 7;
    constexpr size_t kMaxPalette = 6;

    // Panels: top-left corner in popup points (y grows downward) and size.
    constexpr float kPanelY = 30.f, kPanelH = 232.f;
    constexpr float kLeftX = 10.f, kLeftW = 186.f;
    constexpr float kMidX = 202.f, kMidW = 128.f;
    constexpr float kRightX = 336.f, kRightW = 124.f;

    const ccColor3B kDim = {150, 160, 210};
    const ccColor3B kSoft = {190, 198, 235};

    // Position relative to the popup's top-left corner (m_buttonMenu / m_mainLayer share that frame).
    CCPoint TL(float x, float y) { return ccp(x, -y); }

    // Node with its top-left corner at (x, y).
    void putTL(CCNode* parent, CCNode* node, float x, float y, int z = 0) {
        node->setAnchorPoint({0.f, 0.f});
        parent->addChildAtPosition(node, Anchor::TopLeft, ccp(x, -(y + node->getContentSize().height)));
        node->setZOrder(z);
    }
    // Node centred / left-aligned / right-aligned on the point (x, y).
    void putAt(CCNode* parent, CCNode* node, float x, float y, float anchorX, int z = 0) {
        node->setAnchorPoint({anchorX, 0.5f});
        parent->addChildAtPosition(node, Anchor::TopLeft, ccp(x, -y));
        node->setZOrder(z);
    }
    void putCenter(CCNode* p, CCNode* n, float x, float y, int z = 0) { putAt(p, n, x, y, .5f, z); }
    void putLeft(CCNode* p, CCNode* n, float x, float y, int z = 0) { putAt(p, n, x, y, 0.f, z); }
    void putRight(CCNode* p, CCNode* n, float x, float y, int z = 0) { putAt(p, n, x, y, 1.f, z); }

    ccColor3B to3B(g3d::Color const& c) {
        return {static_cast<GLubyte>(c.r), static_cast<GLubyte>(c.g), static_cast<GLubyte>(c.b)};
    }

    std::string hexOf(g3d::Color const& c) {
        return cc3bToHexString(to3B(c));
    }

    bool colorFromHex(std::string const& text, g3d::Color& out) {
        auto res = cc3bFromHexString(text, true);
        if (!res) return false;
        auto c = res.unwrap();
        out.r = c.r;
        out.g = c.g;
        out.b = c.b;
        return true;
    }

    bool parseDouble(std::string const& s, double& out) {
        if (s.empty()) return false;
        char* end = nullptr;
        out = std::strtod(s.c_str(), &end);
        return end != s.c_str() && std::isfinite(out);
    }

    // Removes a node but keeps it alive until the end of the frame, so a button can
    // rebuild the list it lives in from inside its own click handler.
    void dropNode(CCNode* n) {
        if (!n) return;
        n->retain();
        n->removeFromParent();
        n->autorelease();
    }

    void alert(char const* title, std::string const& text) {
        FLAlertLayer::create(title, text, "OK")->show();
    }

    // ---- HSV <-> RGB ----------------------------------------------------------

    g3d::Color hsvToColor(Hsv const& hsv, int alpha) {
        double h = std::fmod(std::fmod(hsv.h, 360.0) + 360.0, 360.0) / 60.0;
        double c = hsv.v * hsv.s;
        double x = c * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
        double m = hsv.v - c;
        double r = 0, g = 0, b = 0;
        switch (static_cast<int>(h)) {
            case 0: r = c; g = x; break;
            case 1: r = x; g = c; break;
            case 2: g = c; b = x; break;
            case 3: g = x; b = c; break;
            case 4: r = x; b = c; break;
            default: r = c; b = x; break;
        }
        auto to255 = [](double v) { return std::max(0, std::min(255, static_cast<int>(std::lround(v * 255.0)))); };
        return {to255(r + m), to255(g + m), to255(b + m), alpha};
    }

    // Updates hsv from an RGB colour. Hue (and saturation for pure black) are kept when
    // the colour has none, so dragging through grey or black doesn't reset the picker.
    void colorToHsv(g3d::Color const& c, Hsv& hsv) {
        double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
        double mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
        hsv.v = mx;
        if (mx > 0.0) hsv.s = d / mx;
        if (d > 1e-9) {
            double h;
            if (mx == r) h = std::fmod((g - b) / d, 6.0);
            else if (mx == g) h = (b - r) / d + 2.0;
            else h = (r - g) / d + 4.0;
            h *= 60.0;
            if (h < 0) h += 360.0;
            hsv.h = h;
        }
    }

    // ---- free-ID allocation --------------------------------------------------

    // Picks `count` colour channels that no object in the level uses and that the
    // level's colour manager does not already define. Ascending order.
    std::vector<int> allocateColorIDs(LevelEditorLayer* lel, int count, int start) {
        std::unordered_set<int> used;
        if (lel && lel->m_objects) {
            for (auto obj : CCArrayExt<GameObject*>(lel->m_objects)) {
                if (!obj) continue;
                if (obj->m_baseColor) used.insert(obj->m_baseColor->m_colorID);
                if (obj->m_detailColor) used.insert(obj->m_detailColor->m_colorID);
            }
        }
        GJEffectManager* mgr = lel ? lel->m_effectManager : nullptr;

        auto scan = [&](bool checkManager) {
            std::vector<int> out;
            // start..999 first, then wrap around to 1..start-1
            for (int i = 0; i < 999 && static_cast<int>(out.size()) < count; i++) {
                int id = ((start - 1 + i) % 999) + 1;
                if (used.count(id)) continue;
                if (checkManager && mgr && mgr->colorExists(id)) continue;
                out.push_back(id);
            }
            return out;
        };
        auto out = scan(true);
        if (static_cast<int>(out.size()) < count) out = scan(false);
        while (static_cast<int>(out.size()) < count) out.push_back(start + static_cast<int>(out.size()));
        std::sort(out.begin(), out.end());
        return out;
    }

    // ---- colour channel writing ----------------------------------------------

    void writeChannel(GJEffectManager* mgr, int id, ccColor3B rgb, float opacity) {
        if (!mgr) return;
        auto action = mgr->getColorAction(id);
        if (!action) {
            action = ColorAction::create(rgb, false, -1);
            action->m_colorID = id;
            mgr->setColorAction(action, id);
        }
        action->m_color = rgb;
        action->m_fromColor = rgb;
        action->m_toColor = rgb;
        action->m_currentOpacity = opacity;
        action->m_fromOpacity = opacity;
        action->m_toOpacity = opacity;
        action->m_playerColor = -1;
        action->m_copyID = 0;
        action->m_blending = false;
        mgr->colorActionChanged(action);
    }

    void applyChannelColor(LevelEditorLayer* lel, int id, g3d::Color const& c) {
        ccColor3B rgb = to3B(c);
        float opacity = static_cast<float>(c.a) / 255.f;
        // The live manager and the one stored in the level settings are normally the
        // same object; write to both when they are not so the colour is saved too.
        GJEffectManager* live = lel->m_effectManager;
        GJEffectManager* saved = lel->m_levelSettings ? lel->m_levelSettings->m_effectManager : nullptr;
        writeChannel(live, id, rgb, opacity);
        if (saved && saved != live) writeChannel(saved, id, rgb, opacity);
    }
}

GradientEditPopup* GradientEditPopup::create(LevelEditorLayer* lel) {
    auto ret = new GradientEditPopup();
    if (ret->init(lel)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool GradientEditPopup::init(LevelEditorLayer* lel) {
    if (!Popup::init(kW, kH)) return false;
    m_noElasticity = true;
    m_lel = lel;
    s_open = this;

    // our own window frame instead of Geode's default background
    m_bgSprite->setVisible(false);
    if (auto frame = g3dui::nine("ui-frame.png", {kW, kH}, 8.f)) {
        frame->setPosition({0.f, 0.f});
        m_mainLayer->addChild(frame, -20);
    }
    this->setTitle("3DGradient Edit", g3dui::fontBold().c_str(), 1.15f, 15.f);

    auto* mod = Mod::get();
    m_steps = static_cast<int>(std::max<int64_t>(2, std::min<int64_t>(6, mod->getSettingValue<int64_t>("shade-steps"))));
    int startID = static_cast<int>(mod->getSettingValue<int64_t>("start-color-id"));
    m_colorIDs = allocateColorIDs(m_lel, m_steps, startID);

    // restore last session
    colorFromHex(mod->getSavedValue<std::string>("lit-hex", "78AAFF"), m_lit);
    colorFromHex(mod->getSavedValue<std::string>("shadow-hex", "141946"), m_shadow);
    m_lit.a = static_cast<int>(mod->getSavedValue<int64_t>("lit-a", 255));
    m_shadow.a = static_cast<int>(mod->getSavedValue<int64_t>("shadow-a", 255));
    m_recent = mod->getSavedValue<std::vector<std::string>>("recent", {});
    m_palette = mod->getSavedValue<std::vector<std::string>>("palette", {});
    colorToHsv(m_lit, m_hsv[0]);
    colorToHsv(m_shadow, m_hsv[1]);

    m_presets = g3d::presets();
    int savedPreset = static_cast<int>(mod->getSavedValue<int64_t>("preset", 0));
    m_preset = std::max(0, std::min(static_cast<int>(m_presets.size()) - 1, savedPreset));
    m_pts = m_presets[m_preset].pts;

    // panel backgrounds
    const std::array<std::pair<float, float>, 3> panels = {{{kLeftX, kLeftW}, {kMidX, kMidW}, {kRightX, kRightW}}};
    for (auto const& [x, w] : panels) {
        if (auto panel = g3dui::nine("ui-panel.png", {w, kPanelH}, 8.f)) putTL(m_mainLayer, panel, x, kPanelY, -10);
    }

    this->buildColorPanel();
    this->buildPreviewPanel();
    this->buildPointPanel();

    // wide action button along the bottom
    {
        const CCSize size = {300.f, 28.f};
        auto node = CCNode::create();
        node->setContentSize(size);
        if (auto bg = g3dui::nine("ui-button.png", size, geode::NineSlice::Insets{10.f, 12.f, 10.f, 12.f})) {
            bg->setPosition({0.f, 0.f});
            node->addChild(bg);
        }
        auto lbl = g3dui::label("Convert into gradient triggers", true, 1.1f);
        lbl->limitLabelWidth(size.width - 24.f, 1.1f, .4f);
        lbl->setPosition({size.width / 2, size.height / 2});
        node->addChild(lbl, 1);
        auto convertBtn = CCMenuItemSpriteExtra::create(node, this, menu_selector(GradientEditPopup::onConvert));
        m_buttonMenu->addChildAtPosition(convertBtn, Anchor::TopLeft, TL(kW / 2, 281.f));
    }

    // settings gear (top right, as in the reference)
    auto gear = CCMenuItemSpriteExtra::create(
        g3dui::sprite("btn-settings.png"), this, menu_selector(GradientEditPopup::onSettings)
    );
    m_buttonMenu->addChildAtPosition(gear, Anchor::TopRight, ccp(-24.f, -16.f));

    this->refreshPreview();
    this->refreshColorWidgets();
    this->refreshIDs();
    this->refreshList();
    this->refreshStats();
    return true;
}

// ===========================================================================
// Left panel: colour editor
// ===========================================================================

void GradientEditPopup::buildColorPanel() {
    const float ox = kLeftX, oy = kPanelY;
    auto at = [&](float x, float y) { return ccp(ox + x, oy + y); }; // panel-local -> popup points

    // Color / Shadow: swatch tabs with the colour channel each one will use
    for (int i = 0; i < 2; i++) {
        float sx = i == 0 ? 8.f : 100.f;

        auto node = CCNode::create();
        node->setContentSize({25.f, 25.f});
        auto ring = g3dui::sprite("ui-swatch-ring.png");
        ring->setPosition({12.5f, 12.5f});
        node->addChild(ring);
        auto swatch = g3dui::sprite("ui-swatch.png");
        swatch->setPosition({12.5f, 12.5f});
        node->addChild(swatch, 1);
        m_ring[i] = ring;
        m_swatch[i] = swatch;

        auto btn = CCMenuItemSpriteExtra::create(node, this, menu_selector(GradientEditPopup::onTab));
        btn->setTag(i);
        auto p = at(sx + 11.f, 17.f);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, TL(p.x, p.y));

        auto name = g3dui::label(i == 0 ? "Color" : "Shadow", false, .9f);
        auto np = at(sx + 28.f, 12.f);
        putLeft(m_mainLayer, name, np.x, np.y);
        auto id = g3dui::label("ID -", false, .65f, kDim);
        auto ip = at(sx + 28.f, 23.f);
        putLeft(m_mainLayer, id, ip.x, ip.y);
        (i == 0 ? m_litID : m_shadowID) = id;
    }

    // hue bar (vertical): top = 0 degrees
    m_huePad = g3dui::DragPad::create({16.f, 76.f}, g3dui::DragPad::Mode::Vertical, [this](float u, float v) { this->onHue(u, v); });
    {
        auto bg = g3dui::sprite("ui-hue.png");
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        m_huePad->addChild(bg, 0);
        m_huePad->setThumb(g3dui::sprite("ui-thumb.png"));
    }
    putTL(m_mainLayer, m_huePad, at(8.f, 36.f).x, at(8.f, 36.f).y);

    // saturation / brightness square: brightness falls left -> right, saturation falls top -> bottom
    m_svPad = g3dui::DragPad::create({78.f, 76.f}, g3dui::DragPad::Mode::Both, [this](float u, float v) { this->onSV(u, v); });
    {
        auto layer = [&](char const* name, int z) {
            auto spr = g3dui::sprite(name);
            spr->setAnchorPoint({0.f, 0.f});
            spr->setPosition({0.f, 0.f});
            m_svPad->addChild(spr, z);
            return spr;
        };
        m_svBase = layer("ui-sv-base.png", 0);   // tinted with the current hue
        layer("ui-sv-white.png", 1);             // fades to white toward the bottom
        layer("ui-sv-black.png", 2);             // fades to black toward the right
        layer("ui-sv-rim.png", 3);
        m_svPad->setThumb(g3dui::sprite("ui-thumb.png"));
    }
    putTL(m_mainLayer, m_svPad, at(28.f, 36.f).x, at(28.f, 36.f).y);

    // opacity / red / green / blue bars
    char const* barSprite[4] = {"ui-bar-a.png", "ui-bar-r.png", "ui-bar-g.png", "ui-bar-b.png"};
    char const* barName[4] = {"Opacity", "Red", "Green", "Blue"};
    for (int i = 0; i < 4; i++) {
        float y = 36.f + 19.f * i;
        auto name = g3dui::label(barName[i], false, .62f, kSoft);
        auto np = at(112.f, y + 3.5f);
        putLeft(m_mainLayer, name, np.x, np.y);
        auto value = g3dui::label("255", false, .62f, kSoft);
        auto vp = at(182.f, y + 3.5f);
        putRight(m_mainLayer, value, vp.x, vp.y);
        m_barValues[i] = value;

        auto pad = g3dui::DragPad::create({70.f, 12.f}, g3dui::DragPad::Mode::Horizontal, [this, i](float u, float) { this->onBar(i, u); });
        auto bg = g3dui::sprite(barSprite[i]);
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        pad->addChild(bg, 0);
        auto thumb = g3dui::sprite("ui-thumb.png");
        thumb->setScale(10.f / 12.f);
        pad->setThumb(thumb);
        m_bars[i] = pad;
        auto bp = at(112.f, y + 7.f);
        putTL(m_mainLayer, pad, bp.x, bp.y);
    }

    // hex
    if (auto field = g3dui::nine("ui-field.png", {84.f, 18.f}, 7.f)) putTL(m_mainLayer, field, at(8.f, 120.f).x, at(8.f, 120.f).y, -5);
    m_hexIn = geode::TextInput::create(140.f, "#000000", g3dui::fontMedium());
    m_hexIn->setScale(.6f);
    m_hexIn->setCommonFilter(geode::CommonFilter::Hex);
    m_hexIn->setMaxCharCount(7);
    m_hexIn->getBGSprite()->setVisible(false);
    m_hexIn->setCallback([this](std::string const& s) { this->onHexInput(s); });
    putCenter(m_mainLayer, m_hexIn, at(50.f, 129.f).x, at(50.f, 129.f).y);
    auto hexLabel = g3dui::label("Hex", false, .7f, kDim);
    putLeft(m_mainLayer, hexLabel, at(100.f, 129.f).x, at(100.f, 129.f).y);

    // recently used + saved colours (swatches themselves are rebuilt by refreshSwatches())
    if (auto box = g3dui::nine("ui-field.png", {170.f, 52.f}, 8.f)) putTL(m_mainLayer, box, at(8.f, 146.f).x, at(8.f, 146.f).y, -5);
    auto recent = g3dui::label("Recent", false, .6f, kDim);
    putLeft(m_mainLayer, recent, at(14.f, 160.f).x, at(14.f, 160.f).y);
    auto saved = g3dui::label("Saved", false, .6f, kDim);
    putLeft(m_mainLayer, saved, at(14.f, 184.f).x, at(14.f, 184.f).y);

    // Save / Load (whole look), add (colour to Saved row), use (apply picked swatch)
    char const* names[4] = {"Save", "Load", "add", "use"};
    SEL_MenuHandler handlers[4] = {
        menu_selector(GradientEditPopup::onSaveLook), menu_selector(GradientEditPopup::onLoadLook),
        menu_selector(GradientEditPopup::onAddPalette), menu_selector(GradientEditPopup::onUse),
    };
    for (int i = 0; i < 4; i++) {
        auto btn = g3dui::pillButton(names[i], {40.f, 16.f}, this, handlers[i], .8f);
        auto p = at(8.f + 43.f * i + 20.f, 206.f + 8.f);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, TL(p.x, p.y));
    }
}

void GradientEditPopup::refreshSwatches() {
    for (auto n : m_swatchNodes) dropNode(n);
    m_swatchNodes.clear();

    auto addRow = [&](std::vector<std::string> const& list, size_t maxCount, float y, int tagBase) {
        for (size_t i = 0; i < list.size() && i < maxCount; i++) {
            g3d::Color col;
            if (!colorFromHex(list[i], col)) continue;
            int tag = tagBase + static_cast<int>(i);

            auto node = CCNode::create();
            node->setContentSize({16.f, 16.f});
            const float k = 14.f / 22.f;
            if (tag == m_selSwatch) {
                auto ring = g3dui::sprite("ui-swatch-ring.png");
                ring->setScale(k);
                ring->setPosition({8.f, 8.f});
                ring->setColor({255, 255, 255});
                node->addChild(ring);
            }
            auto spr = g3dui::sprite("ui-swatch.png");
            spr->setScale(k);
            spr->setColor(to3B(col));
            spr->setPosition({8.f, 8.f});
            node->addChild(spr, 1);

            auto btn = CCMenuItemSpriteExtra::create(node, this, menu_selector(GradientEditPopup::onSwatch));
            btn->setTag(tag);
            m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, TL(kLeftX + 56.f + 17.f * i, kPanelY + y));
            m_swatchNodes.push_back(btn);
        }
    };
    addRow(m_recent, kMaxRecent, 160.f, 1000);
    addRow(m_palette, kMaxPalette, 184.f, 2000);
}

void GradientEditPopup::refreshColorWidgets(CCNode* except) {
    auto& c = this->editedColor();
    auto const& hsv = m_hsv[m_editing];

    if (m_svPad) m_svPad->setValue(static_cast<float>(1.0 - hsv.v), static_cast<float>(hsv.s));
    if (m_huePad) m_huePad->setValue(.5f, static_cast<float>(1.0 - hsv.h / 360.0));
    int vals[4] = {c.a, c.r, c.g, c.b};
    for (int i = 0; i < 4; i++) {
        if (m_bars[i]) m_bars[i]->setValue(static_cast<float>(vals[i]) / 255.f, .5f);
        if (m_barValues[i]) m_barValues[i]->setString(std::to_string(vals[i]).c_str());
    }
    if (m_svBase) m_svBase->setColor(to3B(hsvToColor({hsv.h, 1.0, 1.0}, 255)));

    if (m_hexIn && m_hexIn->getInputNode() != except) m_hexIn->setString("#" + hexOf(c), false);

    for (int i = 0; i < 2; i++) {
        bool selected = (i == m_editing);
        if (m_swatch[i]) m_swatch[i]->setColor(to3B(i == 0 ? m_lit : m_shadow));
        if (m_ring[i]) {
            m_ring[i]->setColor(selected ? ccColor3B{120, 150, 255} : ccColor3B{90, 100, 170});
            m_ring[i]->setOpacity(selected ? 255 : 120);
        }
    }

    this->refreshSwatches();
    this->refreshPreview();
}

void GradientEditPopup::setEditedRgb(int r, int g, int b) {
    auto& c = this->editedColor();
    c.r = std::max(0, std::min(255, r));
    c.g = std::max(0, std::min(255, g));
    c.b = std::max(0, std::min(255, b));
    colorToHsv(c, m_hsv[m_editing]);
}

void GradientEditPopup::onSV(float u, float v) {
    auto& hsv = m_hsv[m_editing];
    hsv.v = 1.0 - u;
    hsv.s = v;
    auto& c = this->editedColor();
    c = hsvToColor(hsv, c.a);
    this->refreshColorWidgets();
}

void GradientEditPopup::onHue(float, float v) {
    auto& hsv = m_hsv[m_editing];
    hsv.h = std::min(359.999, (1.0 - v) * 360.0);
    auto& c = this->editedColor();
    c = hsvToColor(hsv, c.a);
    this->refreshColorWidgets();
}

void GradientEditPopup::onBar(int which, float u) {
    auto& c = this->editedColor();
    int value = std::max(0, std::min(255, static_cast<int>(std::lround(u * 255.f))));
    if (which == 0) {
        c.a = value;
    } else {
        if (which == 1) c.r = value;
        else if (which == 2) c.g = value;
        else c.b = value;
        colorToHsv(c, m_hsv[m_editing]);
    }
    this->refreshColorWidgets();
}

void GradientEditPopup::onHexInput(std::string const& text) {
    g3d::Color tmp = this->editedColor();
    if (!colorFromHex(text, tmp)) return;
    this->setEditedRgb(tmp.r, tmp.g, tmp.b);
    this->refreshColorWidgets(m_hexIn->getInputNode());
}

void GradientEditPopup::onTab(CCObject* sender) {
    m_editing = static_cast<CCNode*>(sender)->getTag();
    this->refreshColorWidgets();
}

void GradientEditPopup::onSwatch(CCObject* sender) {
    m_selSwatch = static_cast<CCNode*>(sender)->getTag();
    this->refreshSwatches();
}

void GradientEditPopup::onUse(CCObject*) {
    std::string hex;
    if (m_selSwatch >= 2000) {
        size_t i = static_cast<size_t>(m_selSwatch - 2000);
        if (i < m_palette.size()) hex = m_palette[i];
    } else if (m_selSwatch >= 1000) {
        size_t i = static_cast<size_t>(m_selSwatch - 1000);
        if (i < m_recent.size()) hex = m_recent[i];
    }
    g3d::Color tmp = this->editedColor();
    if (hex.empty() || !colorFromHex(hex, tmp)) {
        Notification::create("Pick a Recent or Saved swatch first", NotificationIcon::Warning)->show();
        return;
    }
    this->setEditedRgb(tmp.r, tmp.g, tmp.b);
    this->refreshColorWidgets();
}

void GradientEditPopup::onAddPalette(CCObject*) {
    std::string hex = hexOf(this->editedColor());
    m_palette.erase(std::remove(m_palette.begin(), m_palette.end(), hex), m_palette.end());
    m_palette.insert(m_palette.begin(), hex);
    if (m_palette.size() > kMaxPalette) m_palette.resize(kMaxPalette);
    m_selSwatch = 2000;
    this->refreshSwatches();
    this->saveState();
}

void GradientEditPopup::onSaveLook(CCObject*) {
    auto* mod = Mod::get();
    mod->setSavedValue<std::string>("look-lit", hexOf(m_lit));
    mod->setSavedValue<std::string>("look-shadow", hexOf(m_shadow));
    mod->setSavedValue<int64_t>("look-lit-a", m_lit.a);
    mod->setSavedValue<int64_t>("look-shadow-a", m_shadow.a);
    mod->setSavedValue<int64_t>("look-preset", m_preset);
    mod->setSavedValue<bool>("look-saved", true);
    Notification::create("Saved: shape + colors", NotificationIcon::Success)->show();
}

void GradientEditPopup::onLoadLook(CCObject*) {
    auto* mod = Mod::get();
    if (!mod->getSavedValue<bool>("look-saved", false)) {
        Notification::create("Nothing saved yet. Press Save first.", NotificationIcon::Warning)->show();
        return;
    }
    colorFromHex(mod->getSavedValue<std::string>("look-lit", "78AAFF"), m_lit);
    colorFromHex(mod->getSavedValue<std::string>("look-shadow", "141946"), m_shadow);
    m_lit.a = static_cast<int>(mod->getSavedValue<int64_t>("look-lit-a", 255));
    m_shadow.a = static_cast<int>(mod->getSavedValue<int64_t>("look-shadow-a", 255));
    colorToHsv(m_lit, m_hsv[0]);
    colorToHsv(m_shadow, m_hsv[1]);
    this->loadPreset(static_cast<int>(mod->getSavedValue<int64_t>("look-preset", 0)));
    this->refreshColorWidgets();
    Notification::create("Loaded saved look", NotificationIcon::Success)->show();
}

void GradientEditPopup::refreshIDs() {
    if (m_colorIDs.empty()) return;
    if (m_litID) m_litID->setString(fmt::format("ID {}", m_colorIDs.back()).c_str());
    if (m_shadowID) m_shadowID->setString(fmt::format("ID {}", m_colorIDs.front()).c_str());
}

void GradientEditPopup::rememberColor(g3d::Color const& c) {
    std::string hex = hexOf(c);
    m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), hex), m_recent.end());
    m_recent.insert(m_recent.begin(), hex);
    if (m_recent.size() > kMaxRecent) m_recent.resize(kMaxRecent);
}

void GradientEditPopup::saveState() {
    auto* mod = Mod::get();
    mod->setSavedValue<std::string>("lit-hex", hexOf(m_lit));
    mod->setSavedValue<std::string>("shadow-hex", hexOf(m_shadow));
    mod->setSavedValue<int64_t>("lit-a", m_lit.a);
    mod->setSavedValue<int64_t>("shadow-a", m_shadow.a);
    mod->setSavedValue<int64_t>("preset", m_preset);
    mod->setSavedValue<std::vector<std::string>>("recent", m_recent);
    mod->setSavedValue<std::vector<std::string>>("palette", m_palette);
}

// ===========================================================================
// Middle panel: 3D preview + shape picker
// ===========================================================================

// ===========================================================================
// Middle panel: 3D preview + shape picker
// ===========================================================================

void GradientEditPopup::buildPreviewPanel() {
    const float ox = kMidX, oy = kPanelY;

    if (auto back = g3dui::nine("ui-field.png", {112.f, 112.f}, 8.f)) putTL(m_mainLayer, back, ox + 8.f, oy + 8.f, -5);

    m_preview = Preview3D::create({112.f, 112.f});
    auto* mod = Mod::get();
    auto& o = m_preview->options();
    o.steps = m_steps;
    o.smooth = mod->getSettingValue<bool>("smooth-shading");
    o.view.perspective = mod->getSettingValue<bool>("perspective");
    putCenter(m_mainLayer, m_preview, ox + 64.f, oy + 64.f);

    // shape selector:  <  Cube  >
    auto leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    leftSpr->setScale(.5f);
    auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(GradientEditPopup::onPrevShape));
    m_buttonMenu->addChildAtPosition(leftBtn, Anchor::TopLeft, TL(ox + 14.f, oy + 134.f));
    auto rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    rightSpr->setScale(.5f);
    rightSpr->setFlipX(true);
    auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(GradientEditPopup::onNextShape));
    m_buttonMenu->addChildAtPosition(rightBtn, Anchor::TopLeft, TL(ox + kMidW - 14.f, oy + 134.f));

    m_shapeName = g3dui::label(m_presets[m_preset].name, true, 1.f);
    m_shapeName->limitLabelWidth(70.f, 1.f, .4f);
    putCenter(m_mainLayer, m_shapeName, ox + 64.f, oy + 134.f);

    // spin + reset view
    auto spinBtn = g3dui::pillButton("Spin: On", {52.f, 17.f}, this, menu_selector(GradientEditPopup::onToggleSpin), .8f, false, &m_spinLabel);
    m_buttonMenu->addChildAtPosition(spinBtn, Anchor::TopLeft, TL(ox + 34.f, oy + 156.5f));
    auto resetBtn = g3dui::pillButton("Reset", {52.f, 17.f}, this, menu_selector(GradientEditPopup::onResetView), .8f);
    m_buttonMenu->addChildAtPosition(resetBtn, Anchor::TopLeft, TL(ox + 94.f, oy + 156.5f));

    m_stats = g3dui::label("", false, .62f, kSoft);
    m_stats->setAlignment(kCCTextAlignmentCenter);
    putCenter(m_mainLayer, m_stats, ox + 64.f, oy + 190.f);
}

void GradientEditPopup::refreshPreview() {
    if (!m_preview) return;
    m_preview->setPoints(m_pts);
    m_preview->setColors(m_lit, m_shadow);
}

void GradientEditPopup::refreshStats() {
    if (!m_stats) return;
    auto faces = g3d::buildHull(m_pts);
    std::string text = fmt::format("{} points  -  {} faces", m_pts.size(), faces.size());
    if (m_steps > 2 && m_colorIDs.size() >= 3) {
        text += "\nmid channels: ";
        for (int i = 1; i < m_steps - 1; i++) {
            if (i > 1) text += ", ";
            text += std::to_string(m_colorIDs[i]);
        }
    }
    m_stats->setString(text.c_str());
}

void GradientEditPopup::loadPreset(int index) {
    int n = static_cast<int>(m_presets.size());
    m_preset = ((index % n) + n) % n;
    m_pts = m_presets[m_preset].pts;
    m_sel = -1;
    m_listTop = 0;
    m_shapeName->setString(m_presets[m_preset].name.c_str());
    m_shapeName->limitLabelWidth(70.f, 1.f, .4f);
    this->refreshPreview();
    this->refreshList();
    this->refreshPointInputs();
    this->refreshStats();
}

void GradientEditPopup::onPrevShape(CCObject*) { this->loadPreset(m_preset - 1); }

void GradientEditPopup::onNextShape(CCObject*) { this->loadPreset(m_preset + 1); }

void GradientEditPopup::onToggleSpin(CCObject*) {
    bool on = !m_preview->isAutoSpin();
    m_preview->setAutoSpin(on);
    if (m_spinLabel) m_spinLabel->setString(on ? "Spin: On" : "Spin: Off");
}

void GradientEditPopup::onResetView(CCObject*) {
    auto& v = m_preview->options().view;
    v.yaw = 35;
    v.pitch = 25;
    v.roll = 0;
    m_preview->redraw();
}

// ===========================================================================
// Right panel: points list + editing
// ===========================================================================

// ===========================================================================
// Right panel: points list + editing
// ===========================================================================

void GradientEditPopup::buildPointPanel() {
    const float ox = kRightX, oy = kPanelY;

    struct Tool { char const* sprite; SEL_MenuHandler handler; };
    Tool tools[4] = {
        {"btn-copy.png", menu_selector(GradientEditPopup::onDuplicatePoint)},
        {"btn-add.png", menu_selector(GradientEditPopup::onAddPoint)},
        {"btn-remove.png", menu_selector(GradientEditPopup::onRemovePoint)},
        {"btn-edit.png", menu_selector(GradientEditPopup::onEditPoint)},
    };
    for (int i = 0; i < 4; i++) {
        auto btn = CCMenuItemSpriteExtra::create(g3dui::sprite(tools[i].sprite), this, tools[i].handler);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, TL(ox + 20.f + 28.f * i, oy + 18.f));
    }

    // scroll arrows + range label under the list
    auto upSpr = CCSprite::createWithSpriteFrameName("edit_upBtn_001.png");
    upSpr->setScale(.6f);
    auto upBtn = CCMenuItemSpriteExtra::create(upSpr, this, menu_selector(GradientEditPopup::onListUp));
    m_buttonMenu->addChildAtPosition(upBtn, Anchor::TopLeft, TL(ox + kRightW - 36.f, oy + 164.f));
    auto downSpr = CCSprite::createWithSpriteFrameName("edit_downBtn_001.png");
    downSpr->setScale(.6f);
    auto downBtn = CCMenuItemSpriteExtra::create(downSpr, this, menu_selector(GradientEditPopup::onListDown));
    m_buttonMenu->addChildAtPosition(downBtn, Anchor::TopLeft, TL(ox + kRightW - 14.f, oy + 164.f));

    m_pageLabel = g3dui::label("", false, .6f, kSoft);
    putLeft(m_mainLayer, m_pageLabel, ox + 14.f, oy + 164.f);

    // X Y Z of the selected point
    geode::TextInput** slots[3] = {&m_xIn, &m_yIn, &m_zIn};
    char const* placeholders[3] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; axis++) {
        float left = 8.f + 38.f * axis;
        if (auto field = g3dui::nine("ui-field.png", {34.f, 17.f}, 7.f)) putTL(m_mainLayer, field, ox + left, oy + 182.f, -5);
        auto input = geode::TextInput::create(57.f, placeholders[axis], g3dui::fontMedium());
        input->setScale(.6f);
        input->setCommonFilter(geode::CommonFilter::Float);
        input->setMaxCharCount(7);
        input->getBGSprite()->setVisible(false);
        input->setCallback([this, axis](std::string const& s) { this->onCoordInput(axis, s); });
        putCenter(m_mainLayer, input, ox + left + 17.f, oy + 190.5f);
        *slots[axis] = input;
    }

    m_pointHint = g3dui::label("", false, .55f, kDim);
    putCenter(m_mainLayer, m_pointHint, ox + 62.f, oy + 214.f);
}

void GradientEditPopup::refreshList() {
    for (auto n : m_listNodes) dropNode(n);
    m_listNodes.clear();

    const int total = static_cast<int>(m_pts.size());
    m_listTop = std::max(0, std::min(m_listTop, std::max(0, total - kListRows)));

    // which points are actually corners of the hull (the others are ignored)
    std::set<int> onHull;
    for (auto const& f : g3d::buildHull(m_pts))
        for (int id : f.idx) onHull.insert(id);

    for (int r = 0; r < kListRows; r++) {
        int i = m_listTop + r;
        if (i >= total) break;
        bool selected = (i == m_sel);

        const CCSize size = {108.f, 21.f};
        auto row = CCNode::create();
        row->setContentSize(size);
        if (auto bg = g3dui::nine(selected ? "ui-pill-sel.png" : "ui-pill.png", size, geode::NineSlice::Insets{5.f, 6.f, 5.f, 6.f})) {
            bg->setPosition({0.f, 0.f});
            row->addChild(bg);
        }
        auto label = g3dui::label(fmt::format("{} Point", i + 1), false, .85f);
        label->setAnchorPoint({0.f, .5f});
        label->setPosition({10.f, size.height / 2});
        if (!onHull.count(i)) label->setOpacity(110);
        row->addChild(label, 1);

        auto btn = CCMenuItemSpriteExtra::create(row, this, menu_selector(GradientEditPopup::onSelectPoint));
        btn->setTag(i);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, TL(kRightX + 8.f + size.width / 2, kPanelY + 32.f + kRowH * r + size.height / 2));
        m_listNodes.push_back(btn);
    }

    if (m_pageLabel) {
        int first = total ? m_listTop + 1 : 0;
        int last = std::min(total, m_listTop + kListRows);
        m_pageLabel->setString(fmt::format("{}-{} / {}", first, last, total).c_str());
    }
}

void GradientEditPopup::refreshPointInputs() {
    bool has = m_sel >= 0 && m_sel < static_cast<int>(m_pts.size());
    geode::TextInput* ins[3] = {m_xIn, m_yIn, m_zIn};
    for (int a = 0; a < 3; a++) {
        if (!ins[a]) continue;
        ins[a]->setEnabled(has);
        if (!has) { ins[a]->setString("", false); continue; }
        double v = a == 0 ? m_pts[m_sel].x : (a == 1 ? m_pts[m_sel].y : m_pts[m_sel].z);
        ins[a]->setString(fmt::format("{:.2f}", v), false);
    }
    if (m_pointHint) {
        if (!has) {
            m_pointHint->setString("select a point to edit it");
        } else {
            bool used = false;
            for (auto const& f : g3d::buildHull(m_pts))
                for (int id : f.idx) if (id == m_sel) used = true;
            m_pointHint->setString(used ? "corner of the shape" : "inside the shape: ignored");
        }
    }
}

void GradientEditPopup::onSelectPoint(CCObject* sender) {
    m_sel = static_cast<CCNode*>(sender)->getTag();
    this->refreshList();
    this->refreshPointInputs();
}

void GradientEditPopup::onListUp(CCObject*) {
    m_listTop = std::max(0, m_listTop - 1);
    this->refreshList();
}

void GradientEditPopup::onListDown(CCObject*) {
    m_listTop++;
    this->refreshList();
}

void GradientEditPopup::onCoordInput(int axis, std::string const& text) {
    if (m_sel < 0 || m_sel >= static_cast<int>(m_pts.size())) return;
    double v;
    if (!parseDouble(text, v)) return;
    v = std::max(-50.0, std::min(50.0, v));
    auto& p = m_pts[m_sel];
    if (axis == 0) p.x = v;
    else if (axis == 1) p.y = v;
    else p.z = v;
    this->refreshPreview();
    this->refreshStats();
    // keep the list's "inside the shape" dimming accurate without touching the inputs being typed in
    this->refreshList();
}

void GradientEditPopup::onDuplicatePoint(CCObject*) {
    if (m_sel < 0 || m_sel >= static_cast<int>(m_pts.size())) {
        Notification::create("Select a point first", NotificationIcon::Warning)->show();
        return;
    }
    auto p = m_pts[m_sel];
    p.x += 0.3;
    p.y += 0.3;
    p.z += 0.3;
    m_pts.push_back(p);
    m_sel = static_cast<int>(m_pts.size()) - 1;
    m_listTop = static_cast<int>(m_pts.size());
    this->refreshPreview();
    this->refreshList();
    this->refreshPointInputs();
    this->refreshStats();
}

void GradientEditPopup::onAddPoint(CCObject*) {
    double maxY = 0;
    for (size_t i = 0; i < m_pts.size(); i++) maxY = i == 0 ? m_pts[i].y : std::max(maxY, m_pts[i].y);
    m_pts.push_back({0.0, maxY + 0.7, 0.0});
    m_sel = static_cast<int>(m_pts.size()) - 1;
    m_listTop = static_cast<int>(m_pts.size());
    this->refreshPreview();
    this->refreshList();
    this->refreshPointInputs();
    this->refreshStats();
}

void GradientEditPopup::onRemovePoint(CCObject*) {
    if (m_sel < 0 || m_sel >= static_cast<int>(m_pts.size())) {
        Notification::create("Select a point first", NotificationIcon::Warning)->show();
        return;
    }
    if (m_pts.size() <= 3) {
        Notification::create("A shape needs at least 3 points", NotificationIcon::Warning)->show();
        return;
    }
    m_pts.erase(m_pts.begin() + m_sel);
    m_sel = std::min(m_sel, static_cast<int>(m_pts.size()) - 1);
    this->refreshPreview();
    this->refreshList();
    this->refreshPointInputs();
    this->refreshStats();
}

void GradientEditPopup::onEditPoint(CCObject*) {
    if (m_sel < 0 || m_sel >= static_cast<int>(m_pts.size())) {
        Notification::create("Select a point first", NotificationIcon::Warning)->show();
        return;
    }
    m_xIn->focus();
}

void GradientEditPopup::onSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get());
}

// ===========================================================================
// Convert
// ===========================================================================

void GradientEditPopup::onConvert(CCObject*) {
    if (!m_lel || !m_lel->m_editorUI) {
        alert("3DGradient Edit", "The level editor isn't available.");
        return;
    }
    auto* mod = Mod::get();
    const int pointID = static_cast<int>(mod->getSettingValue<int64_t>("point-object-id"));
    const int triggerID = static_cast<int>(mod->getSettingValue<int64_t>("trigger-object-id"));
    const double shapeSize = static_cast<double>(mod->getSettingValue<int64_t>("shape-size"));
    const int startID = static_cast<int>(mod->getSettingValue<int64_t>("start-color-id"));

    // freeze the view so what was previewed is exactly what gets built
    m_preview->setAutoSpin(false);
    if (m_spinLabel) m_spinLabel->setString("Spin: Off");

    g3d::RenderOptions opts = m_preview->options();
    opts.size = shapeSize;
    opts.steps = m_steps;
    auto cells = g3d::renderCells(m_pts, opts);
    if (cells.empty()) {
        alert("3DGradient Edit", "This shape has no visible faces. It needs at least 3 points that aren't in a straight line.");
        return;
    }
    auto plan = g3d::makePlan(cells);

    // --- IDs: colour channels, one group per point, one gradient ID per trigger ---
    m_colorIDs = allocateColorIDs(m_lel, m_steps, startID);
    this->refreshIDs();

    gd::unordered_set<int> usedGroups;
    std::vector<int> groupIDs;
    for (size_t i = 0; i < plan.points.size(); i++) {
        int id = m_lel->getNextFreeGroupID(usedGroups);
        usedGroups.insert(id);
        groupIDs.push_back(id);
    }
    gd::unordered_set<int> usedGradients;
    std::vector<int> gradientIDs;
    for (size_t i = 0; i < plan.triggers.size(); i++) {
        int id = m_lel->getNextFreeGradientID(usedGradients);
        usedGradients.insert(id);
        gradientIDs.push_back(id);
    }

    // --- colour channels: shadow ... lit, interpolated between the two pickers ---
    for (int level = 0; level < m_steps; level++) {
        applyChannelColor(m_lel, m_colorIDs[level], g3d::shadeColor(m_lit, m_shadow, level, m_steps));
    }

    // --- build the objects exactly like a paste would ---
    g3d::BuildParams bp;
    bp.pointObjectID = pointID;
    bp.triggerObjectID = triggerID;
    bp.center = {1000.0, 1000.0};
    bp.size = shapeSize;
    bp.colorIDs = m_colorIDs;
    bp.groupIDs = groupIDs;
    bp.gradientIDs = gradientIDs;
    std::string levelString = g3d::buildLevelString(plan, bp);

    auto created = m_lel->m_editorUI->pasteObjects(levelString, true, false);

    int madePoints = 0, madeTriggers = 0;
    std::vector<GameObject*> made;
    CCPoint lo = ccp(1e9f, 1e9f), hi = ccp(-1e9f, -1e9f);
    if (created) {
        for (auto obj : CCArrayExt<GameObject*>(created)) {
            if (!obj) continue;
            made.push_back(obj);
            if (obj->m_objectID == pointID) madePoints++;
            if (typeinfo_cast<GradientTriggerObject*>(obj)) madeTriggers++;
            auto pos = obj->getPosition();
            lo = ccp(std::min(lo.x, pos.x), std::min(lo.y, pos.y));
            hi = ccp(std::max(hi.x, pos.x), std::max(hi.y, pos.y));
        }
    }

    if (made.empty()) {
        alert(
            "3DGradient Edit",
            fmt::format(
                "GD didn't create any objects.\n\nCheck the object IDs in this mod's settings "
                "(points: {}, gradient trigger: {}).",
                pointID, triggerID
            )
        );
        return;
    }

    // Put the finished shape in the middle of what the editor is showing.
    auto win = CCDirector::get()->getWinSize();
    CCPoint target = m_lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
    CCPoint center = ccp((lo.x + hi.x) / 2, (lo.y + hi.y) / 2);
    CCPoint delta = ccp(target.x - center.x, target.y - center.y);
    for (auto obj : made) m_lel->m_editorUI->moveObject(obj, delta);

    this->rememberColor(m_lit);
    this->rememberColor(m_shadow);
    this->saveState();

    bool complete = madePoints == static_cast<int>(plan.points.size()) &&
                    madeTriggers == static_cast<int>(plan.triggers.size());
    if (complete) {
        Notification::create(
            fmt::format("Created {} points + {} gradient triggers", madePoints, madeTriggers),
            NotificationIcon::Success
        )->show();
    } else {
        alert(
            "3DGradient Edit",
            fmt::format(
                "Expected {} points and {} gradient triggers, but GD created {} and {}.\n\n"
                "Check the object IDs in this mod's settings (points: {}, gradient trigger: {}).",
                plan.points.size(), plan.triggers.size(), madePoints, madeTriggers, pointID, triggerID
            )
        );
    }
    this->onClose(nullptr);
}

