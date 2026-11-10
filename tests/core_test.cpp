// Build & run:  g++ -std=c++20 -O1 -Wall -Wextra tests/core_test.cpp -o core_test && ./core_test
#include "../src/core/Shape3D.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>

using namespace g3d;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            g_fail++;                                                                \
            std::cerr << "FAIL line " << __LINE__ << ": " << msg << "\n";            \
        }                                                                            \
    } while (0)

static const Preset& preset(const std::string& name) {
    static auto all = presets();
    for (auto& p : all) if (p.name == name) return p;
    std::abort();
}

static double signedArea(const Cell& c) {
    double a = 0;
    for (int i = 0; i < c.n; i++) {
        auto p = c.pos[i], q = c.pos[(i + 1) % c.n];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

static bool inCell(const Cell& c, double x, double y) {
    // convex polygon, CCW: inside if left of every edge (strict)
    for (int i = 0; i < c.n; i++) {
        auto p = c.pos[i], q = c.pos[(i + 1) % c.n];
        double cr = (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
        if (cr <= 1e-6) return false;
    }
    return true;
}

int main() {
    // ---- hull topology of every preset -----------------------------------
    struct Expect { const char* name; size_t faces; };
    for (Expect e : {Expect{"Cube", 6}, {"Box", 6}, {"Pyramid", 5}, {"Tetrahedron", 4},
                     {"Octahedron", 8}, {"Triangular Prism", 5}, {"Hexagonal Prism", 8},
                     {"Cylinder", 12}}) {
        auto faces = buildHull(preset(e.name).pts);
        CHECK(faces.size() == e.faces, e.name << " faces=" << faces.size() << " expected " << e.faces);
        // Euler: V - E + F = 2 for a closed convex polyhedron
        std::set<std::pair<int, int>> edges;
        std::set<int> verts;
        for (auto& f : faces)
            for (size_t i = 0; i < f.idx.size(); i++) {
                int a = f.idx[i], b = f.idx[(i + 1) % f.idx.size()];
                edges.insert({std::min(a, b), std::max(a, b)});
                verts.insert(a);
            }
        CHECK(int(verts.size()) - int(edges.size()) + int(faces.size()) == 2, e.name << " Euler characteristic");
    }

    // ---- face winding: CCW seen from outside, normal points away from centre
    for (auto& pr : presets()) {
        auto faces = buildHull(pr.pts);
        Vec3 c; double r; centerAndRadius(pr.pts, c, r);
        for (auto& f : faces) {
            Vec3 fc;
            for (int i : f.idx) fc = fc + pr.pts[i];
            fc = fc * (1.0 / f.idx.size());
            CHECK(dot(f.normal, fc - c) > 0, pr.name << ": normal points outward");
            Vec3 a = pr.pts[f.idx[0]], b = pr.pts[f.idx[1]], d = pr.pts[f.idx[2]];
            CHECK(dot(cross(b - a, d - b), f.normal) > 0, pr.name << ": CCW from outside");
        }
    }

    // ---- interior / redundant points are ignored --------------------------
    {
        auto pts = preset("Cube").pts;
        pts.push_back({0, 0, 0});        // inside the solid
        pts.push_back({1, 0, 0});        // centre of a face
        pts.push_back({1, 1, 0});        // middle of an edge
        auto faces = buildHull(pts);
        CHECK(faces.size() == 6, "cube + junk points still 6 faces, got " << faces.size());
        for (auto& f : faces) CHECK(f.idx.size() == 4, "junk points must not enter the polygon");
    }

    // ---- flat shape -------------------------------------------------------
    {
        auto faces = buildHull({{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}});
        CHECK(faces.size() == 1 && faces[0].doubleSided, "flat quad is one double-sided face");
        CHECK(buildHull({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}).empty(), "collinear points give no faces");
        CHECK(buildHull({{0, 0, 0}, {1, 0, 0}}).empty(), "two points give no faces");
    }

    // ---- polygon splitting ------------------------------------------------
    {
        CHECK(splitPolygon({0, 1, 2}).size() == 1, "tri -> 1 cell");
        CHECK(splitPolygon({0, 1, 2, 3}).size() == 1, "quad -> 1 cell");
        auto s5 = splitPolygon({0, 1, 2, 3, 4});
        CHECK(s5.size() == 2 && s5[0].size() == 4 && s5[1].size() == 3, "pentagon -> quad + tri");
        auto s10 = splitPolygon({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        size_t tot = 0;
        for (auto& c : s10) tot += c.size() - 2;
        CHECK(tot == 8, "10-gon cells cover 8 triangles' worth of area");
    }

    // ---- rendering: culling, winding, no overlap --------------------------
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> ang(0, 360);
    for (auto& pr : presets()) {
        for (int trial = 0; trial < 60; trial++) {
            RenderOptions o;
            o.view.yaw = ang(rng);
            o.view.pitch = ang(rng);
            o.view.roll = ang(rng);
            o.view.perspective = (trial % 2 == 0);
            o.steps = 2 + trial % 5;
            o.smooth = (trial % 3 != 0);
            auto cells = renderCells(pr.pts, o);
            CHECK(!cells.empty(), pr.name << " always shows something");
            for (auto& c : cells) {
                CHECK(signedArea(c) > -1e-6, pr.name << " visible cell is CCW on screen (area=" << signedArea(c) << ")");
                for (int k = 0; k < c.n; k++)
                    CHECK(c.level[k] >= 0 && c.level[k] < o.steps, "level in range");
            }
            // visible cells of a convex solid must not overlap on screen
            if (trial % 6 == 0) {
                for (double x = -80; x <= 80; x += 3.3)
                    for (double y = -80; y <= 80; y += 3.3) {
                        int hits = 0;
                        for (auto& c : cells) hits += inCell(c, x, y) ? 1 : 0;
                        CHECK(hits <= 1, pr.name << " cells overlap at " << x << "," << y << " (" << hits << ")");
                    }
            }
        }
    }

    // ---- a cube seen head-on-ish shows 1-3 faces, never more --------------
    for (int t = 0; t < 400; t++) {
        RenderOptions o;
        o.view.yaw = ang(rng);
        o.view.pitch = ang(rng);
        o.view.perspective = false;
        size_t n = renderCells(preset("Cube").pts, o).size();
        CHECK(n >= 1 && n <= 3, "orthographic cube shows 1..3 faces, got " << n);
    }

    // ---- shading: faces turned toward the light are brighter --------------
    {
        RenderOptions o;
        o.view.yaw = 0; o.view.pitch = 0; o.view.perspective = false;
        o.smooth = false; o.steps = 5;
        o.light.yaw = 0; o.light.pitch = 0; // light straight from the camera
        auto cells = renderCells(preset("Cube").pts, o);
        CHECK(cells.size() == 1, "straight-on cube is one face");
        CHECK(cells[0].level[0] == 4, "face pointing at the light is fully lit");
        o.light.yaw = 90; // light from the side: face is edge-on to the light
        cells = renderCells(preset("Cube").pts, o);
        CHECK(cells[0].level[0] == 0, "face edge-on to the light is fully shadowed");
    }

    // ---- plan + level string ---------------------------------------------
    {
        RenderOptions o;
        o.size = 150; o.steps = 3;
        auto cells = renderCells(preset("Cube").pts, o);
        auto plan = makePlan(cells);
        CHECK(plan.triggers.size() == cells.size(), "one trigger per cell");
        CHECK(plan.points.size() <= cells.size() * 4, "points are shared, never more than 4 per cell");
        CHECK(plan.points.size() >= 7, "a 3-face cube view has at least 7 distinct corners");

        BuildParams bp;
        bp.center = {1000, 500};
        bp.size = o.size;
        bp.colorIDs = {11, 12, 13};
        bp.gradientIDs = {5, 9, 12};
        for (size_t i = 0; i < plan.points.size(); i++) bp.groupIDs.push_back(200 + int(i));
        std::string s = buildLevelString(plan, bp);

        // parse it back
        std::vector<std::map<int, std::string>> objs;
        std::stringstream ss(s);
        std::string objStr;
        while (std::getline(ss, objStr, ';')) {
            std::map<int, std::string> kv;
            std::stringstream os(objStr);
            std::string k, v;
            while (std::getline(os, k, ',') && std::getline(os, v, ',')) kv[std::stoi(k)] = v;
            objs.push_back(kv);
        }
        size_t nPts = 0, nTrg = 0;
        std::set<int> definedGroups;
        for (auto& kv : objs) {
            if (kv[1] == "3802") {
                nPts++;
                definedGroups.insert(std::stoi(kv[57]));
                int col = std::stoi(kv[21]);
                CHECK(col == 11 || col == 12 || col == 13, "point colour comes from the allocated channels");
                CHECK(kv[21] == kv[22], "main and detail colour agree");
            } else if (kv[1] == "2903") {
                nTrg++;
                CHECK(kv[207] == "1", "vertex mode on");
                for (int key : {203, 204, 205, 206})
                    CHECK(definedGroups.count(std::stoi(kv[key])), "trigger references a group that a point owns (key " << key << ")");
            } else CHECK(false, "unexpected object id " << kv[1]);
        }
        CHECK(nPts == plan.points.size() && nTrg == plan.triggers.size(), "object counts match the plan");
        CHECK(definedGroups.size() == nPts, "every point has its own unique group");
        std::set<std::string> gradIDs;
        for (auto& kv : objs) if (kv[1] == "2903") gradIDs.insert(kv[209]);
        CHECK(gradIDs.size() == nTrg, "gradient IDs are unique per trigger");
        CHECK(gradIDs.count("5") && gradIDs.count("9") && gradIDs.count("12"), "gradient IDs come from the supplied list");
        std::cout << "sample level string (cube, " << nPts << " points, " << nTrg << " triggers):\n" << s << "\n\n";
    }

    // ---- shade colours ----------------------------------------------------
    {
        Color lit{200, 100, 50, 255}, sh{0, 0, 0, 128};
        auto c0 = shadeColor(lit, sh, 0, 3), c1 = shadeColor(lit, sh, 1, 3), c2 = shadeColor(lit, sh, 2, 3);
        CHECK(c0.r == 0 && c2.r == 200 && c1.r == 100 && c1.a == 192, "shade colours interpolate shadow..lit");
    }

    std::cout << (g_checks - g_fail) << "/" << g_checks << " checks passed\n";
    return g_fail ? 1 : 0;
}
