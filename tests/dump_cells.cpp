// Dumps projected cells as JSON for visual inspection (tests/render_preview.py).
#include "../src/core/Shape3D.hpp"
#include <iostream>
using namespace g3d;
int main() {
    std::cout << "[";
    bool firstShape = true;
    for (auto& pr : presets()) {
        RenderOptions o; o.size = 150; o.steps = 3;
        auto cells = renderCells(pr.pts, o);
        if (!firstShape) std::cout << ",";
        firstShape = false;
        std::cout << "{\"name\":\"" << pr.name << "\",\"steps\":" << o.steps << ",\"cells\":[";
        for (size_t i = 0; i < cells.size(); i++) {
            auto& c = cells[i];
            if (i) std::cout << ",";
            std::cout << "{\"p\":[";
            for (int k = 0; k < c.n; k++) std::cout << (k ? "," : "") << "[" << c.pos[k].x << "," << c.pos[k].y << "]";
            std::cout << "],\"l\":[";
            for (int k = 0; k < c.n; k++) std::cout << (k ? "," : "") << c.level[k];
            std::cout << "]}";
        }
        std::cout << "]}";
    }
    std::cout << "]\n";
}
