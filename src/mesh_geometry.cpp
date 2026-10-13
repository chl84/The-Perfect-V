#include "draw_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace sjp::gfx {
Frustum::Frustum(const Matrix &projection, const Matrix &modelView) {
    std::array<double, 16> clip{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                clip[column * 4 + row] += double(projection[k * 4 + row]) * modelView[column * 4 + k];
    // The shader converts the original -W..W depth to Vulkan's 0..W after
    // transforming positions. These planes test that original clip volume.
    for (int axis = 0; axis < 3; ++axis)
        for (int sign = 0; sign < 2; ++sign)
            for (int column = 0; column < 4; ++column)
                planes[size_t(axis * 2 + sign)][size_t(column)] =
                    clip[size_t(column * 4 + 3)] + (sign ? -1 : 1) * clip[size_t(column * 4 + axis)];
}
bool Frustum::intersects(const Bounds &bounds) const {
    // Lines expand after projection; their centerline cannot safely reject
    // their visible edge pixels. Keep those small ranges unconditionally.
    if (bounds.screenExpanded)
        return true;
    for (const auto &plane : planes) {
        double value = plane[3], magnitude = std::abs(plane[3]);
        for (int axis = 0; axis < 3; ++axis) {
            value += plane[size_t(axis)] *
                     (plane[size_t(axis)] >= 0 ? bounds.high[size_t(axis)] : bounds.low[size_t(axis)]);
            magnitude += std::abs(plane[size_t(axis)]) *
                         std::max(std::abs(bounds.low[size_t(axis)]), std::abs(bounds.high[size_t(axis)]));
        }
        // Conservative tolerance covers float transforms/FMA on the GPU,
        // including distant geometry and bounds crossing the near plane.
        if (value < -std::max(.0001, magnitude * .000004))
            return false;
    }
    return true;
}
void appendMeshRanges(Mesh &mesh, uint32_t first, uint32_t count) {
    constexpr uint32_t chunkSize = 128 * 3;
    if (count % 3 || size_t(first) + count > mesh.vertices.size())
        throw std::runtime_error("Invalid cached triangle range");
    for (uint32_t offset = 0; offset < count; offset += chunkSize) {
        MeshRange range;
        range.first = first + offset;
        range.count = std::min(chunkSize, count - offset);
        range.bounds.low = range.bounds.high = mesh.vertices[range.first].position;
        for (uint32_t i = range.first; i < range.first + range.count; ++i) {
            const auto &v = mesh.vertices[i];
            for (size_t axis = 0; axis < 3; ++axis) {
                range.bounds.low[axis] = std::min(range.bounds.low[axis], v.position[axis]);
                range.bounds.high[axis] = std::max(range.bounds.high[axis], v.position[axis]);
            }
            range.bounds.screenExpanded |= v.lineSide != 0;
        }
        mesh.ranges.push_back(range);
    }
}
namespace {
uint64_t hash(const Vertex &vertex) {
    uint64_t value = 14695981039346656037ULL;
    const auto *bytes = reinterpret_cast<const unsigned char *>(&vertex);
    for (size_t i = 0; i < sizeof(Vertex); i += sizeof(uint64_t)) {
        uint64_t word;
        std::memcpy(&word, bytes + i, sizeof(word));
        value = (value ^ word) * 1099511628211ULL;
    }
    // Avalanche float keys before addressing a power-of-two table: regular
    // geometry often has identical low coordinate bits.
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    return value;
}
} // namespace
void indexMesh(Mesh &mesh) {
    if (mesh.indexed)
        return;
    if (mesh.vertices.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Cached mesh exceeds index limits");
    const size_t count = mesh.vertices.size();
    size_t slots = 1;
    while (slots < count * 2)
        slots *= 2;
    constexpr uint32_t empty = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> table(slots, empty), indices;
    std::vector<Vertex> unique;
    indices.reserve(count);
    unique.reserve(count / 2);
    for (const auto &vertex : mesh.vertices) {
        size_t slot = size_t(hash(vertex)) & (slots - 1);
        while (table[slot] != empty && std::memcmp(&unique[table[slot]], &vertex, sizeof(Vertex)) != 0)
            slot = (slot + 1) & (slots - 1);
        if (table[slot] == empty) {
            table[slot] = uint32_t(unique.size());
            unique.push_back(vertex);
        }
        indices.push_back(table[slot]);
    }
    mesh.vertices.swap(unique);
    mesh.indices.swap(indices);
    mesh.vertexCount = uint32_t(mesh.vertices.size());
    mesh.indexCount = uint32_t(mesh.indices.size());
    mesh.indexed = true;
}
} // namespace sjp::gfx
