#pragma once
#include "visual_scenery_mask.h"

namespace sjp::visual {
struct SnowShadowVertex {
    Vec3 position, receiver;
    double weight = 1;
};

// Clip buried skirts against the exact snow mesh before applying any depth
// offset. Even distant receivers cannot expose the underside as a dark ring.
template <class Emit>
void visibleSnowBank(const Hill &hill, const std::array<Vec3,6> &bank, Emit emit) {
    const double area=footprintTurn(bank[0],bank[1],bank[2]);
    if (std::abs(area)<1e-10) return;
    struct Vertex { Vec3 p,n; double height; };
    snowDecalTriangles(hill,{bank[0],bank[1],bank[2]},[&](Vec3 a,Vec3 b,Vec3 c) {
        std::vector<Vertex> polygon,next;
        for (auto ground:{a,b,c}) {
            const double u=footprintTurn(bank[1],bank[2],ground)/area;
            const double v=footprintTurn(bank[2],bank[0],ground)/area;
            const double w=1-u-v;
            const auto p=bank[0]*u+bank[1]*v+bank[2]*w;
            polygon.push_back({p,bank[3]*u+bank[4]*v+bank[5]*w,p.y-ground.y});
        }
        auto previous=polygon.back(); double before=previous.height;
        for (const auto &v:polygon) {
            if ((before>=0)!=(v.height>=0)) {
                const double t=before/(before-v.height);
                next.push_back({previous.p+(v.p-previous.p)*t,previous.n+(v.n-previous.n)*t,0});
            }
            if (v.height>=0) next.push_back(v);
            previous=v; before=v.height;
        }
        for (size_t i=1;i+1<next.size();++i)
            if (cross(next[i].p-next[0].p,next[i+1].p-next[0].p).length()>1e-9)
                emit(std::array<Vec3,6>{next[0].p,next[i].p,next[i+1].p,
                                       next[0].n.normalized(),next[i].n.normalized(),next[i+1].n.normalized()});
    });
}

// Split only texture coordinates at atlas tile boundaries. Positions remain
// on the original snow triangle: no lifted shell or new outline at its foot.
template <class Emit>
void snowShadowTile(const std::array<SnowShadowVertex, 3> &triangle,
                    double left, double right, double front, double back, Emit emit) {
    std::vector<SnowShadowVertex> polygon(triangle.begin(), triangle.end()), next;
    for (int edge = 0; edge < 4; ++edge) {
        if (polygon.empty()) return;
        auto distance = [&](const SnowShadowVertex &v) {
            switch (edge) {
            case 0: return v.receiver.x-left;
            case 1: return right-v.receiver.x;
            case 2: return v.receiver.z-front;
            default: return back-v.receiver.z;
            }
        };
        next.clear();
        auto previous = polygon.back(); double before = distance(previous);
        for (const auto &v : polygon) {
            const double after = distance(v);
            if ((before >= 0) != (after >= 0)) {
                const double t = before/(before-after);
                next.push_back({previous.position+(v.position-previous.position)*t,
                                previous.receiver+(v.receiver-previous.receiver)*t,
                                previous.weight+(v.weight-previous.weight)*t});
            }
            if (after >= 0) next.push_back(v);
            previous=v; before=after;
        }
        polygon.swap(next);
    }
    for (size_t i=1; i+1<polygon.size(); ++i)
        if (cross(polygon[i].position-polygon[0].position,
                  polygon[i+1].position-polygon[0].position).length()>1e-9)
            emit(polygon[0],polygon[i],polygon[i+1]);
}

inline SnowShadowVertex snowShadowVertex(const Hill &hill, Vec3 p, Vec3 normal, Vec3 sunlight) {
    const auto hit = projectToSnow(hill,p,sunlight*-1);
    const Vec3 receiver = hit.value_or(Vec3{p.x,terrainSnowHeight(hill,p.x,p.z),p.z});
    // The atlas already includes the receiver's sun exposure. Preserve that
    // attenuation at the bank's toe, and remove it on faces without direct sun.
    const double ground = scenerySunExposure(hill,receiver,sunlight);
    const double t = clamp(dot(normal.normalized(),sunlight.normalized())/.30,0.,1.);
    const double facing=t*t*(3-2*t);
    return {p,receiver,hit && ground>1e-6 ? clamp(facing/ground,0.,1.) : 0};
}
} // namespace sjp::visual
