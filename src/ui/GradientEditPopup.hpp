#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <array>
#include "../core/Shape3D.hpp"
#include "Preview3D.hpp"
#include "Widgets.hpp"

// Hue in degrees (0..360), saturation and value in 0..1. Kept next to the RGB colour so
// that dragging inside the picker never loses the hue when the colour is grey or black.
struct Hsv {
    double h = 0, s = 1, v = 1;
};

class GradientEditPopup : public geode::Popup {
protected:
    LevelEditorLayer* m_lel = nullptr;

    // ---- shape state ----------------------------------------------------
    std::vector<g3d::Preset> m_presets;
    int m_preset = 0;
    std::vector<g3d::Vec3> m_pts;
    int m_sel = -1;      // selected point (index into m_pts) or -1
    int m_listTop = 0;   // first visible row of the point list

    // ---- colour state ---------------------------------------------------
    g3d::Color m_lit{120, 170, 255, 255};
    g3d::Color m_shadow{20, 25, 70, 255};
    Hsv m_hsv[2];        // [0] = Color (lit), [1] = Shadow
    int m_editing = 0;   // 0 = Color (lit), 1 = Shadow
    int m_steps = 3;
    std::vector<int> m_colorIDs; // index = shade level, 0 = shadow ... steps-1 = lit
    std::vector<std::string> m_recent;
    std::vector<std::string> m_palette;
    int m_selSwatch = -1; // tag of the Recent/Saved swatch picked for "use"

    // ---- widgets --------------------------------------------------------
    Preview3D* m_preview = nullptr;
    g3dui::DragPad* m_svPad = nullptr;
    g3dui::DragPad* m_huePad = nullptr;
    std::array<g3dui::DragPad*, 4> m_bars{};            // opacity, red, green, blue
    std::array<cocos2d::CCLabelBMFont*, 4> m_barValues{};
    cocos2d::CCSprite* m_svBase = nullptr;              // tinted with the current hue
    std::array<cocos2d::CCSprite*, 2> m_swatch{};       // Color / Shadow swatches
    std::array<cocos2d::CCSprite*, 2> m_ring{};         // selection rings around them
    geode::TextInput* m_hexIn = nullptr;
    geode::TextInput* m_xIn = nullptr;
    geode::TextInput* m_yIn = nullptr;
    geode::TextInput* m_zIn = nullptr;
    cocos2d::CCLabelBMFont* m_litID = nullptr;
    cocos2d::CCLabelBMFont* m_shadowID = nullptr;
    cocos2d::CCLabelBMFont* m_shapeName = nullptr;
    cocos2d::CCLabelBMFont* m_stats = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pointHint = nullptr;
    cocos2d::CCLabelBMFont* m_spinLabel = nullptr;
    std::vector<cocos2d::CCNode*> m_listNodes;   // rebuilt by refreshList()
    std::vector<cocos2d::CCNode*> m_swatchNodes; // rebuilt by refreshSwatches()

    inline static GradientEditPopup* s_open = nullptr;

    bool init(LevelEditorLayer* lel);

    // layout builders
    void buildColorPanel();
    void buildPreviewPanel();
    void buildPointPanel();

    // helpers
    g3d::Color& editedColor() { return m_editing == 0 ? m_lit : m_shadow; }
    void loadPreset(int index);
    void refreshColorWidgets(cocos2d::CCNode* except = nullptr);
    void refreshPreview();
    void refreshList();
    void refreshSwatches();
    void refreshIDs();
    void refreshPointInputs();
    void refreshStats();
    void rememberColor(g3d::Color const& c);
    void saveState();
    void setEditedRgb(int r, int g, int b);

    // picker input
    void onSV(float u, float v);
    void onHue(float u, float v);
    void onBar(int which, float u);
    void onHexInput(std::string const& text);
    void onCoordInput(int axis, std::string const& text);

    // callbacks
    void onTab(cocos2d::CCObject* sender);
    void onSwatch(cocos2d::CCObject* sender);
    void onUse(cocos2d::CCObject*);
    void onAddPalette(cocos2d::CCObject*);
    void onSaveLook(cocos2d::CCObject*);
    void onLoadLook(cocos2d::CCObject*);
    void onPrevShape(cocos2d::CCObject*);
    void onNextShape(cocos2d::CCObject*);
    void onToggleSpin(cocos2d::CCObject*);
    void onResetView(cocos2d::CCObject*);
    void onSelectPoint(cocos2d::CCObject* sender);
    void onListUp(cocos2d::CCObject*);
    void onListDown(cocos2d::CCObject*);
    void onDuplicatePoint(cocos2d::CCObject*);
    void onAddPoint(cocos2d::CCObject*);
    void onRemovePoint(cocos2d::CCObject*);
    void onEditPoint(cocos2d::CCObject*);
    void onSettings(cocos2d::CCObject*);
    void onConvert(cocos2d::CCObject*);

public:
    static GradientEditPopup* create(LevelEditorLayer* lel);
    ~GradientEditPopup() override {
        if (s_open == this) s_open = nullptr;
    }
    static bool isOpen() { return s_open != nullptr; }
};
