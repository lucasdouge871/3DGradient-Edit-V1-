// 3DGradient Edit - pure geometry core.
// No Geode / cocos2d / GD includes in this file on purpose: it is compiled and
// unit-tested natively (see tests/), and the Geode UI code is a thin layer on top.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace g3d {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-5;

struct Vec3 { double x = 0, y = 0, z = 0; };
struct Vec2 { double x = 0, y = 0; };

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(Vec3 a, Vec3 fallback = {0, 0, 1}) {
    double l = length(a);
    return l < 1e-12 ? fallback : a * (1.0 / l);
}
inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline double deg2rad(double d) { return d * kPi / 180.0; }

// ---------------------------------------------------------------------------
// Shape = a cloud of points. Faces are the convex hull of those points, which
// is what lets the user add / remove / move points freely in the editor.
// ---------------------------------------------------------------------------

struct Face {
    std::vector<int> idx;     // vertex indices, counter-clockwise seen from outside
    Vec3 normal;              // outward unit normal (model space)
    bool doubleSided = false; // true only for flat (all points coplanar) shapes
};

// Convex hull of a point cloud as a list of convex polygon faces.
// - Coplanar points are merged into one polygon (a cube gives 6 quads, not 12 tris).
// - Points inside the hull / inside a face polygon are ignored.
// - If every point is coplanar the result is a single double-sided polygon.
// - Fewer than 3 usable points (or all collinear) gives an empty list.
inline std::vector<Face> buildHull(const std::vector<Vec3>& p) {
    std::vector<Face> faces;
    const int n = static_cast<int>(p.size());
    if (n < 3) return faces;

    std::set<std::vector<int>> seen;
    for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
    for (int k = j + 1; k < n; k++) {
        Vec3 nrm = cross(p[j] - p[i], p[k] - p[i]);
        double l = length(nrm);
        if (l < kEps) continue;
        nrm = nrm * (1.0 / l);

        int pos = 0, neg = 0;
        std::vector<int> on;
        for (int m = 0; m < n; m++) {
            double d = dot(nrm, p[m] - p[i]);
            if (std::fabs(d) < kEps) on.push_back(m);
            else if (d > 0) pos++;
            else neg++;
        }
        if (pos > 0 && neg > 0) continue;        // not a supporting plane
        if (!seen.insert(on).second) continue;   // same plane already handled

        Face f;
        f.doubleSided = (pos == 0 && neg == 0);
        if (f.doubleSided)  f.normal = nrm;
        else if (pos > 0)   f.normal = nrm * -1.0; // others are on the + side -> outward is -
        else                f.normal = nrm;

        // Order the coplanar points: 2D convex hull in the face plane (drops
        // interior and collinear points), CCW when seen from outside.
        Vec3 c;
        for (int m : on) c = c + p[m];
        c = c * (1.0 / on.size());
        Vec3 u = normalized(p[on[0]] - c, {1, 0, 0});
        Vec3 w = cross(f.normal, u);
        struct P2 { double x, y; int id; };
        std::vector<P2> pts;
        for (int m : on) pts.push_back({dot(p[m] - c, u), dot(p[m] - c, w), m});
        std::sort(pts.begin(), pts.end(), [](const P2& a, const P2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        auto cr = [](const P2& o, const P2& a, const P2& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<P2> h(2 * pts.size());
        size_t t = 0;
        for (size_t q = 0; q < pts.size(); q++) {
            while (t >= 2 && cr(h[t - 2], h[t - 1], pts[q]) <= kEps) t--;
            h[t++] = pts[q];
        }
        for (size_t q = pts.size() - 1, lo = t + 1; q > 0; q--) {
            while (t >= lo && cr(h[t - 2], h[t - 1], pts[q - 1]) <= kEps) t--;
            h[t++] = pts[q - 1];
        }
        h.resize(t > 0 ? t - 1 : 0);
        if (h.size() < 3) continue;
        for (auto& q : h) f.idx.push_back(q.id);
        faces.push_back(std::move(f));
    }
    return faces;
}

// ---------------------------------------------------------------------------
// View / projection / lighting
// ---------------------------------------------------------------------------

struct View {
    double yaw = 35, pitch = 25, roll = 0; // degrees
    bool perspective = true;
    double camDist = 3.6;                  // in normalised shape radii
};

struct Light {
    double yaw = -35, pitch = 40; // degrees, direction TOWARD the light, view space
    Vec3 dir() const {
        double y = deg2rad(yaw), p = deg2rad(pitch);
        return normalized({std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)});
    }
};

inline Vec3 rotateView(Vec3 v, const View& view) {
    double cy = std::cos(deg2rad(view.yaw)),   sy = std::sin(deg2rad(view.yaw));
    double cp = std::cos(deg2rad(view.pitch)), sp = std::sin(deg2rad(view.pitch));
    double cr = std::cos(deg2rad(view.roll)),  sr = std::sin(deg2rad(view.roll));
    double x1 = v.x * cy + v.z * sy, z1 = -v.x * sy + v.z * cy, y1 = v.y;
    double y2 = y1 * cp - z1 * sp,   z2 = y1 * sp + z1 * cp,    x2 = x1;
    return {x2 * cr - y2 * sr, x2 * sr + y2 * cr, z2};
}

// Centre + radius of a point cloud (bounding-box centre, max distance from it).
inline void centerAndRadius(const std::vector<Vec3>& p, Vec3& center, double& radius) {
    center = {};
    radius = 1;
    if (p.empty()) return;
    Vec3 lo = p[0], hi = p[0];
    for (auto& v : p) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    center = (lo + hi) * 0.5;
    double r = 0;
    for (auto& v : p) r = std::max(r, length(v - center));
    radius = r < 1e-9 ? 1 : r;
}

// One drawable gradient cell: a quad or a triangle (3 vertices) of a face.
struct Cell {
    int face = 0;
    int n = 4;                  // 3 or 4
    std::array<int, 4> v{};     // model vertex indices (CCW on screen); triangle repeats the last
    std::array<Vec2, 4> pos{};  // projected positions, in GD units, relative to the centre
    std::array<int, 4> level{}; // shade level per corner, 0 = shadow .. steps-1 = lit
    double depth = 0;           // average view-space z (bigger = closer to the camera)
};

struct RenderOptions {
    double size = 150; // shape diameter in GD units
    int steps = 3;     // number of shade levels / colour channels (2..6)
    bool smooth = true; // blend vertex normals into the shading
    View view;
    Light light;
};

// Split a convex polygon (m >= 3 vertices) into quads and at most one triangle.
inline std::vector<std::vector<int>> splitPolygon(const std::vector<int>& poly) {
    std::vector<std::vector<int>> out;
    const int m = static_cast<int>(poly.size());
    int i = 1;
    while (i < m - 1) {
        if (i + 2 < m) { out.push_back({poly[0], poly[i], poly[i + 1], poly[i + 2]}); i += 2; }
        else           { out.push_back({poly[0], poly[i], poly[i + 1]});               i += 1; }
    }
    return out;
}

// Rotate, cull back faces, light and quantise. Result is sorted far -> near.
inline std::vector<Cell> renderCells(const std::vector<Vec3>& pts, const RenderOptions& o) {
    std::vector<Cell> cells;
    auto faces = buildHull(pts);
    if (faces.empty()) return cells;

    Vec3 center;
    double radius;
    centerAndRadius(pts, center, radius);
    const double scale = (o.size * 0.5) / radius;
    const int steps = std::max(2, std::min(6, o.steps));
    const Vec3 L = o.light.dir();

    std::vector<Vec3> rot(pts.size());
    std::vector<Vec2> scr(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        Vec3 r = rotateView(pts[i] - center, o.view);
        rot[i] = r;
        double f = 1.0;
        if (o.view.perspective) {
            double dz = o.view.camDist * radius - r.z;
            f = (o.view.camDist * radius) / std::max(dz, 0.25 * radius);
        }
        scr[i] = {r.x * f * scale, r.y * f * scale};
    }
    const Vec3 cam = {0, 0, o.view.camDist * radius};

    for (size_t fi = 0; fi < faces.size(); fi++) {
        const Face& face = faces[fi];
        Vec3 nView = normalized(rotateView(face.normal, o.view));
        Vec3 fc;
        for (int id : face.idx) fc = fc + rot[id];
        fc = fc * (1.0 / face.idx.size());

        double facing = o.view.perspective ? dot(nView, cam - fc) : nView.z;
        if (face.doubleSided) {
            if (facing < 0) nView = nView * -1.0;
        } else if (facing <= 1e-9) {
            continue; // back face
        }

        for (auto& poly : splitPolygon(face.idx)) {
            Cell c;
            c.face = static_cast<int>(fi);
            c.n = static_cast<int>(poly.size());
            double zsum = 0;
            for (int k = 0; k < 4; k++) {
                int vi = poly[std::min(k, c.n - 1)]; // triangle: last corner repeated
                c.v[k] = vi;
                c.pos[k] = scr[vi];
                Vec3 nu = nView;
                if (o.smooth) nu = normalized(nView * 0.5 + normalized(rot[vi], nView) * 0.5, nView);
                double s = clamp01(dot(nu, L));
                c.level[k] = static_cast<int>(std::lround(s * (steps - 1)));
                if (k < c.n) zsum += rot[vi].z;
            }
            c.depth = zsum / c.n;
            cells.push_back(c);
        }
    }
    std::stable_sort(cells.begin(), cells.end(),
                     [](const Cell& a, const Cell& b) { return a.depth < b.depth; });
    return cells;
}

// ---------------------------------------------------------------------------
// Conversion plan: which GD objects are needed, before IDs are known.
// ---------------------------------------------------------------------------

struct PlanPoint { Vec2 pos; int level = 0; };
struct PlanTrigger { std::array<int, 4> pt{}; int n = 4; }; // indices into points: BL, BR, TR, TL

struct Plan {
    std::vector<PlanPoint> points;
    std::vector<PlanTrigger> triggers;
};

// Points are shared between cells when they sit on the same model vertex AND
// have the same shade level (same position, same colour -> one object is enough).
inline Plan makePlan(const std::vector<Cell>& cells) {
    Plan plan;
    std::map<std::pair<int, int>, int> index; // (vertex, level) -> point index
    for (auto& c : cells) {
        PlanTrigger t;
        t.n = c.n;
        for (int k = 0; k < 4; k++) {
            auto key = std::make_pair(c.v[k], c.level[k]);
            auto it = index.find(key);
            if (it == index.end()) {
                it = index.emplace(key, static_cast<int>(plan.points.size())).first;
                plan.points.push_back({c.pos[k], c.level[k]});
            }
            t.pt[k] = it->second;
        }
        plan.triggers.push_back(t);
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

struct Color { int r = 255, g = 255, b = 255, a = 255; };

// level 0 = shadow colour, level steps-1 = lit colour, linear in between.
inline Color shadeColor(const Color& lit, const Color& shadow, int level, int steps) {
    double t = steps <= 1 ? 1.0 : static_cast<double>(level) / (steps - 1);
    auto mix = [&](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
    return {mix(shadow.r, lit.r), mix(shadow.g, lit.g), mix(shadow.b, lit.b), mix(shadow.a, lit.a)};
}

// ---------------------------------------------------------------------------
// Level-string generation (the same text GD uses for copy/paste)
// ---------------------------------------------------------------------------

struct BuildParams {
    int pointObjectID = 3802;
    int triggerObjectID = 2903;
    std::vector<int> gradientIDs; // one per trigger; if too short, falls back to 1, 2, 3...
    Vec2 center;                 // where the shape is placed in the level
    double size = 150;
    std::vector<int> colorIDs;   // index = level, size == steps
    std::vector<int> groupIDs;   // at least plan.points.size() entries, one per point
};

inline std::string fmtNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0" || s.empty()) s = "0";
    return s;
}

// Property keys (from the 2.2081 bindings):
//   gradient trigger: 209 gradient ID, 203 = bottom-left group, 204 = bottom-right,
//                     205 = top-left, 206 = top-right, 207 = vertex mode
//   any object: 1 id, 2 x, 3 y, 21 / 22 colour channels, 57 groups
inline std::string buildLevelString(const Plan& plan, const BuildParams& bp) {
    std::string out;
    auto add = [&](const std::string& s) { out += s; out += ';'; };

    for (size_t i = 0; i < plan.points.size(); i++) {
        const auto& p = plan.points[i];
        int level = std::max(0, std::min(p.level, static_cast<int>(bp.colorIDs.size()) - 1));
        int color = bp.colorIDs.empty() ? 1 : bp.colorIDs[level];
        add("1," + std::to_string(bp.pointObjectID) +
            ",2," + fmtNum(bp.center.x + p.pos.x) +
            ",3," + fmtNum(bp.center.y + p.pos.y) +
            ",21," + std::to_string(color) +
            ",22," + std::to_string(color) +
            ",57," + std::to_string(bp.groupIDs.at(i)));
    }

    // Triggers sit in a row just below the shape so they are easy to find and select.
    const double spacing = 30.0;
    const double rowY = bp.center.y - bp.size * 0.5 - 60.0;
    const double rowX0 = bp.center.x - spacing * (plan.triggers.size() - 1) * 0.5;
    for (size_t i = 0; i < plan.triggers.size(); i++) {
        const auto& t = plan.triggers[i];
        auto grp = [&](int k) { return std::to_string(bp.groupIDs.at(t.pt[k])); };
        // t.pt order is BL, BR, TR, TL  ->  keys 203, 204, 206, 205
        add("1," + std::to_string(bp.triggerObjectID) +
            ",2," + fmtNum(rowX0 + spacing * i) +
            ",3," + fmtNum(rowY) +
            ",209," + std::to_string(i < bp.gradientIDs.size() ? bp.gradientIDs[i] : static_cast<int>(i) + 1) +
            ",203," + grp(0) +
            ",204," + grp(1) +
            ",205," + grp(3) +
            ",206," + grp(2) +
            ",207,1");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

struct Preset {
    std::string name;
    std::vector<Vec3> pts;
};

inline std::vector<Preset> presets() {
    std::vector<Preset> r;
    auto ngonPrism = [](int sides, double radius, double halfH) {
        std::vector<Vec3> v;
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < sides; i++) {
                double a = 2 * kPi * i / sides + kPi / sides;
                v.push_back({radius * std::cos(a), s ? halfH : -halfH, radius * std::sin(a)});
            }
        return v;
    };
    std::vector<Vec3> cube, box;
    for (int x = -1; x <= 1; x += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int z = -1; z <= 1; z += 2) {
                cube.push_back({double(x), double(y), double(z)});
                box.push_back({x * 1.6, y * 0.8, z * 1.0});
            }
    r.push_back({"Cube", cube});
    r.push_back({"Box", box});
    r.push_back({"Pyramid", {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {0, 1, 0}}});
    r.push_back({"Tetrahedron", {{1, 1, 1}, {-1, -1, 1}, {-1, 1, -1}, {1, -1, -1}}});
    r.push_back({"Octahedron", {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}});
    r.push_back({"Triangular Prism", ngonPrism(3, 1.2, 1.0)});
    r.push_back({"Hexagonal Prism", ngonPrism(6, 1.0, 0.8)});
    r.push_back({"Cylinder", ngonPrism(10, 1.0, 1.0)});
    return r;
}

} // namespace g3d
