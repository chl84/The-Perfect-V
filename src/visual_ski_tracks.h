#pragma once
#include "original_skeleton.h"
#include "visual_scenery_shadow.h"
#include "visual_ski_spray.h"

namespace sjp::visual {
struct SkiTrackVertex { Vec3 position; double time = 0, edge = 0; };
struct SkiTrackTriangle { std::array<SkiTrackVertex,3> vertices; double endTime = 0; };
class SkiTrackCache {
  public:
    static constexpr double lifetime = 8;
    std::vector<SkiTrackTriangle> triangles;
    void update(const Hill &hill, const std::vector<Frame> &frames) {
        if (source != &frames || sourceData != frames.data() || hillId != hill.id || processed > frames.size() ||
            (processed && !sameFrame(frames[processed-1], lastFrame))) {
            triangles.clear();
            source = &frames; sourceData = frames.data(); hillId = hill.id; processed = 0; previous.reset();
            brakingStarted = false;
        }
        for (; processed < frames.size(); ++processed) {
            const auto &f = frames[processed];
            lastFrame = f;
            const auto phase = Phase(f.phase);
            if (brakingStarted || (phase != Phase::Outrun && phase != Phase::Finished) || f.crashed ||
                std::abs(f.y - hill.ground(f.x)) > .40) {
                previous.reset();
                continue;
            }
            OriginalSkeleton skeleton = f.hasRenderSkeleton ? OriginalSkeleton{f.renderSkeleton}
                                                            : originalSkeleton(f.pose, f.leadingLeg);
            if (brakingSkiWedge(skeleton, 0) > 0 || brakingSkiWedge(skeleton, 1) > 0) {
                brakingStarted = true;
                previous.reset();
                continue;
            }
            Contact current;
            current.time = f.elapsed;
            for (size_t ski = 0; ski < 2; ++ski) {
                const auto joint = ski ? OriginalJoint::Ankle2 : OriginalJoint::Ankle1;
                current.skis[ski] = skeleton[joint] + Vec3{f.x,f.y,0};
            }
            if (previous && current.time > previous->time && current.time - previous->time < .15) {
                const auto delta = current.skis[0] - previous->skis[0];
                const double distance = std::hypot(delta.x,delta.z);
                if (distance < .06) continue;
                if (distance < 3)
                    for (size_t ski = 0; ski < 2; ++ski)
                        append(hill,previous->skis[ski],current.skis[ski],previous->time,current.time);
            }
            previous = current;
        }
    }
    static double opacity(double now, double born, double edge) {
        const double age = now-born;
        if (age < 0 || age >= lifetime) return 0;
        const double fade = 1-age/lifetime;
        return .18 * fade * fade * edge;
    }
  private:
    struct Contact { std::array<Vec3,2> skis; double time = 0; };
    std::optional<Contact> previous;
    const std::vector<Frame> *source = nullptr;
    const Frame *sourceData = nullptr;
    std::string hillId;
    size_t processed = 0;
    Frame lastFrame;
    bool brakingStarted = false;
    static bool sameFrame(const Frame &a,const Frame &b) {
        return a.elapsed == b.elapsed && a.x == b.x && a.y == b.y && a.pose == b.pose && a.crashed == b.crashed;
    }
    void append(const Hill &h, Vec3 a, Vec3 b, double start, double end) {
        auto delta = b-a; delta.y = 0;
        const double length2 = dot(delta,delta);
        if (length2 < 1e-8) return;
        constexpr double radius = .055;
        const Vec3 side = Vec3{-delta.z,0,delta.x}.normalized();
        for (int half : {-1,1}) {
            const auto offset = side*(half*radius);
            snowDecalTriangles(h,{a,a+offset,b+offset,b},[&](Vec3 p,Vec3 q,Vec3 r) {
                SkiTrackTriangle triangle;
                triangle.endTime = end;
                const std::array<Vec3,3> points{p,q,r};
                for (size_t i = 0; i < 3; ++i) {
                    auto &vertex = triangle.vertices[i];
                    vertex.position = points[i]+Vec3{0,.010,0};
                    vertex.time = start+(end-start)*clamp(dot(points[i]-a,delta)/length2,0.,1.);
                    vertex.edge = clamp(1-std::abs(dot(points[i]-a,side))/radius,0.,1.);
                }
                triangles.push_back(triangle);
            });
        }
    }
};
} // namespace sjp::visual
