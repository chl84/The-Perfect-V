#include "draw_internal.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
sjp::gfx::Matrix identity() {
    sjp::gfx::Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
sjp::gfx::Matrix perspective(float aspect) {
    sjp::gfx::Matrix m{};
    m[0] = 1 / (aspect * std::tan(48 * 3.14159265358979323846 / 360));
    m[5] = m[0] * aspect;
    m[10] = -(1000.f + .25f) / (1000.f - .25f);
    m[11] = -1;
    m[14] = -2 * 1000.f * .25f / (1000.f - .25f);
    return m;
}
} // namespace
int main() {
    using namespace sjp::gfx;
    try {
        const Uniforms unlit;
        for (auto member : {&Uniforms::signalColor, &Uniforms::signalPositionRange,
                            &Uniforms::signalDirectionCone, &Uniforms::signalParameters,
                            &Uniforms::backdropHaze, &Uniforms::backdropColor}) {
            auto changed = unlit;
            (changed.*member)[0] = 1;
            require(!(changed == unlit), "Different visual uniforms can merge into the same draw packet");
        }
        Mesh mesh;
        Vertex a, b, c, d;
        a.position = {-1, -1, -5};
        b.position = {1, -1, -5};
        c.position = {1, 1, -5};
        d.position = {-1, 1, -5};
        mesh.vertices = {a, b, c, a, c, d};
        Vertex normal = a, uv = a, color = a, nearby = a;
        normal.normal = {0, 1, 0};
        uv.uv = {1, 0};
        color.color[0] = .5;
        nearby.position[0] = std::nextafter(a.position[0], 0.f);
        mesh.vertices.insert(mesh.vertices.end(), {normal, uv, color, nearby, b, c});
        const auto original = mesh.vertices;
        appendMeshRanges(mesh, 0, uint32_t(original.size()));
        indexMesh(mesh);
        require(mesh.vertexCount == 8 && mesh.indexCount == original.size(),
                "Distinct attributes/nearby positions were welded or exact duplicates were not shared");
        for (size_t i = 0; i < original.size(); ++i)
            require(std::memcmp(&original[i], &mesh.vertices[mesh.indices[i]], sizeof(Vertex)) == 0,
                    "Indexing changes triangle order or original vertex bytes");
        const auto indices = mesh.indices;
        indexMesh(mesh);
        require(mesh.indices == indices, "Repeated indexing changes indices");
        Mesh multi;
        for (int i = 0; i < 300; ++i) {
            auto v = a;
            v.position[0] = float(i);
            multi.vertices.insert(multi.vertices.end(), {v, b, c});
        }
        appendMeshRanges(multi, 0, 384);
        appendMeshRanges(multi, 384, uint32_t(multi.vertices.size()) - 384);
        uint32_t next = 0;
        for (const auto &r : multi.ranges) {
            require(r.first == next && r.count % 3 == 0 && r.count <= 384,
                    "Ranges overlap, have gaps or split triangles");
            for (uint32_t i = r.first; i < r.first + r.count; ++i)
                for (size_t axis = 0; axis < 3; ++axis)
                    require(multi.vertices[i].position[axis] >= r.bounds.low[axis] &&
                                multi.vertices[i].position[axis] <= r.bounds.high[axis],
                            "Bounds omit their original geometry");
            next += r.count;
        }
        require(next == multi.vertices.size(), "Ranges omit final triangles");
        a.lineSide = 1;
        Mesh lines;
        lines.vertices = {a, b, c};
        appendMeshRanges(lines, 0, 3);
        require(lines.ranges[0].bounds.screenExpanded, "Screen-expanded lines use narrow centerline bounds");
        Mesh empty;
        indexMesh(empty);
        require(empty.indexed && empty.vertices.empty() && empty.indices.empty(), "Invalid empty mesh");
        const Frustum unit(identity(), identity());
        require(unit.intersects({{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}}),
                "Inside orthographic bounds disappear");
        require(!unit.intersects({{2, 2, 2}, {3, 3, 3}}), "Outside orthographic bounds remain");
        require(unit.intersects({{-2, -.1f, -.1f}, {2, .1f, .1f}}), "Crossing bounds disappear");
        require(unit.intersects({{-10, -10, -10}, {10, 10, 10}}), "Bounds enclosing camera disappear");
        for (float aspect : {1.f, 16.f / 9, 32.f / 9}) {
            const Frustum view(perspective(aspect), identity());
            require(view.intersects({{-.1f, -.1f, -5}, {.1f, .1f, -4}}),
                    "Visible perspective bounds disappear");
            require(!view.intersects({{-.1f, -.1f, 4}, {.1f, .1f, 5}}), "Geometry behind camera remains");
            require(!view.intersects({{-1, -1, -1500}, {1, 1, -1400}}), "Incorrect far-depth convention");
            require(view.intersects({{-.2f, -.2f, -1}, {.2f, .2f, 1}}), "Near-plane crossing causes popping");
            require(!view.intersects({{-.01f, -.01f, -.1f}, {.01f, .01f, -.05f}}),
                    "Near clipping ineffective");
            const float edge = 5 * std::tan(48 * 3.14159265358979323846 / 360) * aspect;
            require(view.intersects({{edge - .01f, -.1f, -5}, {edge + .01f, .1f, -5}}),
                    "Side-plane crossing disappears");
            require(!view.intersects({{edge + 1, -.1f, -5}, {edge + 2, .1f, -5}}),
                    "Outside side-plane bounds remain");
        }
        auto camera = identity();
        camera[12] = -100;
        camera[13] = -20;
        camera[14] = -10;
        const Frustum moved(perspective(16.f / 9), camera);
        require(moved.intersects({{99, 19, 4}, {101, 21, 6}}), "Model-view transform ignored");
        require(!moved.intersects({{-101, 19, 4}, {-99, 21, 6}}), "Translated offscreen bounds remain");
        require(moved.intersects({{-101, 19, 4}, {-99, 21, 6}, true}), "Expanded lines disappear");
        std::cout << "Exact vertex reuse, triangle order, ranges and conservative camera culling passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
