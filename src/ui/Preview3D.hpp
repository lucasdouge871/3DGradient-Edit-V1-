#pragma once

#include <Geode/Geode.hpp>
#include "../core/Shape3D.hpp"

// Live, draggable 3D preview of the current shape. It renders exactly the cells
// that "Convert" will turn into gradient triggers (same culling, same shading,
// same quantised colour levels), so what you see here is what you get.
class Preview3D : public cocos2d::CCLayer {
protected:
    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCSize m_box;
    std::vector<g3d::Vec3> m_points;
    g3d::RenderOptions m_opts;
    g3d::Color m_lit{120, 170, 255, 255};
    g3d::Color m_shadow{20, 25, 70, 255};
    bool m_autoSpin = true;
    bool m_dragging = false;
    cocos2d::CCPoint m_lastTouch;

    bool init(cocos2d::CCSize size);
    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

public:
    static Preview3D* create(cocos2d::CCSize size);

    void setPoints(std::vector<g3d::Vec3> points);
    void setColors(g3d::Color lit, g3d::Color shadow);
    void setAutoSpin(bool on) { m_autoSpin = on; }
    bool isAutoSpin() const { return m_autoSpin; }

    // Rendering options (view angles, light, steps, smooth, perspective).
    // Call redraw() after changing them.
    g3d::RenderOptions& options() { return m_opts; }
    void redraw();
};
