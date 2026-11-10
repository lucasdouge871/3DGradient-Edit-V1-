#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>

#include <functional>
#include <string>

// Small UI toolkit used by the popup. Everything here degrades gracefully: if one
// of the mod's own sprites or fonts is missing, a plain stand-in is used instead of crashing.
namespace g3dui {
    // Rubik bitmap fonts generated at build time from fonts/*.ttf (see mod.json "fonts").
    std::string const& fontMedium();
    std::string const& fontBold();

    // The mod's own sprite ("ui-thumb.png" ...). Never returns null.
    cocos2d::CCSprite* sprite(char const* name);

    // Stretchable sprite (frames, panels, buttons). Content size is set to `size`.
    // `inset` is the unstretched corner size in points. Never returns null.
    geode::NineSlice* nine(char const* name, cocos2d::CCSize size, float inset);
    geode::NineSlice* nine(char const* name, cocos2d::CCSize size, geode::NineSlice::Insets insets);

    cocos2d::CCLabelBMFont* label(
        std::string const& text, bool bold, float scale,
        cocos2d::ccColor3B color = {235, 238, 255}
    );

    // Dark rounded button with a centered label. `outLabel` (optional) receives the label.
    CCMenuItemSpriteExtra* pillButton(
        std::string const& text, cocos2d::CCSize size, cocos2d::CCObject* target,
        cocos2d::SEL_MenuHandler callback, float textScale = .8f, bool bold = false,
        cocos2d::CCLabelBMFont** outLabel = nullptr
    );

    // A rectangle that turns touches into normalised values (0..1, y up) and keeps a
    // thumb sprite on top of the current value. Used for the saturation/brightness
    // square (both axes), the hue bar (vertical) and the slider bars (horizontal).
    class DragPad : public cocos2d::CCLayer {
    public:
        enum class Mode { Both, Horizontal, Vertical };
        using Callback = std::function<void(float u, float v)>;

    protected:
        Mode m_mode = Mode::Both;
        cocos2d::CCSize m_box;
        Callback m_callback;
        cocos2d::CCNode* m_thumb = nullptr;
        float m_u = 0.f, m_v = 0.f;
        float m_slop = 3.f; // extra touch area around the rectangle, in points

        bool init(cocos2d::CCSize size, Mode mode, Callback callback);
        void registerWithTouchDispatcher() override;
        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void applyTouch(cocos2d::CCTouch* touch);
        void placeThumb();

    public:
        static DragPad* create(cocos2d::CCSize size, Mode mode, Callback callback);

        // The thumb is added as a child and kept at the current value.
        void setThumb(cocos2d::CCNode* thumb);
        // Move to a value without calling the callback.
        void setValue(float u, float v);
        float u() const { return m_u; }
        float v() const { return m_v; }
    };
}
