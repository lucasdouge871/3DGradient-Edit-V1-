#include "Preview3D.hpp"

using namespace geode::prelude;

namespace {
    // Each gradient cell is drawn as a grid of small flat quads whose colour is
    // bilinearly interpolated between the four corner shade levels. That is what
    // a gradient trigger does between its four point objects.
    constexpr int kTess = 4;

    CCPoint lerp(CCPoint a, CCPoint b, float t) {
        return ccp(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }
}

Preview3D* Preview3D::create(CCSize size) {
    auto ret = new Preview3D();
    if (ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool Preview3D::init(CCSize size) {
    if (!CCLayer::init()) return false;

    m_box = size;
    this->setContentSize(size);
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.5f});

    m_draw = CCDrawNode::create();
    this->addChild(m_draw);

    this->setTouchEnabled(true);
    this->scheduleUpdate();
    return true;
}

void Preview3D::registerWithTouchDispatcher() {
    // Popups register at -500, so sit just above them to receive drags first.
    CCTouchDispatcher::get()->addTargetedDelegate(this, -501, true);
}

void Preview3D::setPoints(std::vector<g3d::Vec3> points) {
    m_points = std::move(points);
    this->redraw();
}

void Preview3D::setColors(g3d::Color lit, g3d::Color shadow) {
    m_lit = lit;
    m_shadow = shadow;
    this->redraw();
}

void Preview3D::update(float dt) {
    if (m_autoSpin && !m_dragging) {
        m_opts.view.yaw = std::fmod(m_opts.view.yaw + 28.0 * dt, 360.0);
        this->redraw();
    }
}

bool Preview3D::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible()) return false;
    auto local = this->convertToNodeSpace(touch->getLocation());
    if (local.x < 0 || local.y < 0 || local.x > m_box.width || local.y > m_box.height) return false;
    m_dragging = true;
    m_lastTouch = touch->getLocation();
    return true;
}

void Preview3D::ccTouchMoved(CCTouch* touch, CCEvent*) {
    auto now = touch->getLocation();
    auto d = now - m_lastTouch;
    m_lastTouch = now;
    m_opts.view.yaw += d.x * 0.7;
    m_opts.view.pitch -= d.y * 0.7;
    m_opts.view.pitch = std::max(-89.0, std::min(89.0, m_opts.view.pitch));
    this->redraw();
}

void Preview3D::ccTouchEnded(CCTouch*, CCEvent*) {
    m_dragging = false;
}

void Preview3D::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_dragging = false;
}

void Preview3D::redraw() {
    if (!m_draw) return;
    m_draw->clear();

    g3d::RenderOptions o = m_opts;
    // Fit the shape inside the box with a little margin.
    o.size = std::min(m_box.width, m_box.height) * 0.62;
    const int steps = std::max(2, std::min(6, o.steps));

    auto cells = g3d::renderCells(m_points, o);
    const CCPoint mid = ccp(m_box.width / 2, m_box.height / 2);

    for (auto const& cell : cells) {
        // corners in BL, BR, TR, TL order (triangles repeat the last corner)
        CCPoint c[4];
        float lv[4];
        for (int k = 0; k < 4; k++) {
            c[k] = ccp(mid.x + static_cast<float>(cell.pos[k].x), mid.y + static_cast<float>(cell.pos[k].y));
            lv[k] = static_cast<float>(cell.level[k]);
        }
        for (int i = 0; i < kTess; i++) {
            for (int j = 0; j < kTess; j++) {
                float u0 = static_cast<float>(i) / kTess, u1 = static_cast<float>(i + 1) / kTess;
                float v0 = static_cast<float>(j) / kTess, v1 = static_cast<float>(j + 1) / kTess;
                auto at = [&](float u, float v) {
                    return lerp(lerp(c[0], c[1], u), lerp(c[3], c[2], u), v);
                };
                CCPoint quad[4] = {at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)};

                float um = (u0 + u1) * 0.5f, vm = (v0 + v1) * 0.5f;
                float bottom = lv[0] + (lv[1] - lv[0]) * um;
                float top = lv[3] + (lv[2] - lv[3]) * um;
                float level = bottom + (top - bottom) * vm;
                float t = std::max(0.f, std::min(1.f, level / static_cast<float>(steps - 1)));

                ccColor4F col = {
                    (m_shadow.r + (m_lit.r - m_shadow.r) * t) / 255.f,
                    (m_shadow.g + (m_lit.g - m_shadow.g) * t) / 255.f,
                    (m_shadow.b + (m_lit.b - m_shadow.b) * t) / 255.f,
                    1.f
                };
                // A same-colour hairline border hides the seams between the small quads.
                m_draw->drawPolygon(quad, 4, col, 0.5f, col);
            }
        }
    }
}
