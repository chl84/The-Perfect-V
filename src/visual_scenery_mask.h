#pragma once
#include "visual_scenery_shadow.h"
#include <map>

namespace sjp::visual {
struct ShadowPlane {
    Vec3 normal;
    double offset = 0;
    double distance(Vec3 p) const { return offset-dot(normal,p); }
};

// An orthographic caster volume, rather than the convex hull of independent
// ground hits. Its side planes stay parallel to sunlight across valleys.
class SceneryShadowVolume {
  public:
    std::vector<ShadowPlane> planes;
    Vec3 low{}, high{};
    SceneryShadowVolume(const std::vector<Vec3> &sources,Vec3 sunlight,double reach) {
        if (sources.size()<3) return;
        sunlight=sunlight.normalized();
        const Vec3 right=cross(sunlight,{0,1,0}).normalized(), up=cross(right,sunlight).normalized();
        std::vector<Vec3> projected;
        low=high=sources.front();
        for (auto p:sources) {
            projected.push_back({dot(p,right),0,dot(p,up)});
            for (auto q:{p,p-sunlight*reach}) {
                low.x=std::min(low.x,q.x); low.z=std::min(low.z,q.z);
                high.x=std::max(high.x,q.x); high.z=std::max(high.z,q.z);
            }
        }
        auto add=[&](Vec3 n,double d) {
            const double length=n.length();
            if (length<1e-9) return;
            n=n*(1/length); d/=length;
            for (const auto &plane:planes)
                if ((plane.normal-n).length()<1e-6 && std::abs(plane.offset-d)<1e-5) return;
            planes.push_back({n,d});
        };
        const auto silhouette=shadowHull(std::move(projected));
        if (silhouette.size()<3) return;
        for (size_t i=0;i<silhouette.size();++i) {
            const auto a=silhouette[i], b=silhouette[(i+1)%silhouette.size()];
            const double dx=b.x-a.x, dy=b.z-a.z;
            add(right*dy-up*dx,dy*a.x-dx*a.z);
        }
        // Sun-facing caps prevent a receiver in front of a caster from being
        // shaded. The opposite faces extrude away from the light indefinitely.
        for (size_t i=0;i<sources.size();++i)
            for (size_t j=i+1;j<sources.size();++j)
                for (size_t k=j+1;k<sources.size();++k) {
                    Vec3 n=cross(sources[j]-sources[i],sources[k]-sources[i]);
                    if (n.length()<1e-8) continue;
                    n=n.normalized(); double d=dot(n,sources[i]);
                    bool positive=false,negative=false;
                    for (auto p:sources) {
                        const double side=dot(n,p)-d;
                        positive|=side>1e-6; negative|=side< -1e-6;
                    }
                    if (positive && negative) continue;
                    if (positive || (!negative && dot(n,sunlight)<0)) { n=n*-1; d=-d; }
                    if (dot(n,sunlight)>1e-7) add(n,d);
                }
    }
    bool contains(Vec3 p) const {
        if (planes.empty()) return false;
        for (const auto &plane:planes) if (plane.distance(p)<-1e-7) return false;
        return true;
    }
    template <class Emit> void receivers(const Hill &h,Emit emit) const {
        if (planes.empty()) return;
        const double dx=3*h.scale(),dz=10*h.scale(),start=snowGridStart(h);
        const double left=std::max(start,low.x),right=std::min(std::max(420*h.scale(),h.end()+80*h.scale()),high.x);
        const double front=std::max(-200*h.scale(),low.z),back=std::min(200*h.scale(),high.z);
        for (double x=snowCellStart(left,start,dx);x<right;x+=dx)
            for (double z=snowCellStart(front,-200*h.scale(),dz);z<back;z+=dz)
                snowCellTriangles(h,x,z,[&](double xx,double zz){return backdropGround(h,xx,zz);},
                                  [&](Vec3 a,Vec3 b,Vec3 c) {
                    std::array<Vec3,64> polygon{{a,b,c}},next{}; size_t count=3;
                    for (const auto &plane:planes) {
                        if (!count) break;
                        size_t output=0; auto previous=polygon[count-1]; double before=plane.distance(previous);
                        for (size_t i=0;i<count;++i) {
                            const auto p=polygon[i]; const double after=plane.distance(p);
                            if ((before>=0)!=(after>=0)) next[output++]=previous+(p-previous)*(before/(before-after));
                            if (after>=0) next[output++]=p;
                            previous=p; before=after;
                        }
                        polygon=next; count=output;
                    }
                    for (size_t i=1;i+1<count;++i)
                        if (std::abs(footprintTurn(polygon[0],polygon[i],polygon[i+1]))>1e-9)
                            emit(polygon[0],polygon[i],polygon[i+1]);
                });
    }
};

inline double scenerySunExposure(const Hill &h,Vec3 p,Vec3 sunlight) {
    const double dx=3*h.scale(),dz=10*h.scale();
    const auto n=Vec3{-(backdropGround(h,p.x+dx,p.z)-backdropGround(h,p.x-dx,p.z))/(2*dx),1,
                     -(backdropGround(h,p.x,p.z+dz)-backdropGround(h,p.x,p.z-dz))/(2*dz)}.normalized();
    const double t=clamp(dot(n,sunlight)/.30,0.,1.);
    return t*t*(3-2*t);
}

struct SceneryShadowTriangle { std::array<Vec3,3> points; std::array<double,3> opacity; };
// A sparse CPU-baked opacity atlas. MAX coverage gives overlapping opaque
// casters one shadow, rather than accumulating dark layers on the snow.
class SceneryShadowMask {
  public:
    static constexpr int core=32,gutter=2,stride=core+2*gutter;
    using Key=std::pair<int,int>; // Z, X, retaining spatial draw order.
    using Tile=std::array<std::array<uint8_t,4>,core*core>;
    std::map<Key,Tile> tiles;
    double step;
    explicit SceneryShadowMask(double spacing):step(spacing) {}
    static int tileIndex(int cell) { return int(std::floor(double(cell)/core)); }
    uint8_t sample(int x,int z) const {
        const int tx=tileIndex(x),tz=tileIndex(z); auto found=tiles.find({tz,tx});
        if (found==tiles.end()) return 0;
        const auto &samples=found->second[size_t((z-tz*core)*core+x-tx*core)];
        return uint8_t((int(samples[0])+samples[1]+samples[2]+samples[3]+2)/4);
    }
    bool rasterize(const SceneryShadowTriangle &triangle,size_t limit=1024) {
        const auto a=triangle.points[0],b=triangle.points[1],c=triangle.points[2];
        const double area=footprintTurn(a,b,c); if (std::abs(area)<1e-9) return true;
        const int left=int(std::floor(std::min({a.x,b.x,c.x})/step)),right=int(std::floor(std::max({a.x,b.x,c.x})/step));
        const int front=int(std::floor(std::min({a.z,b.z,c.z})/step)),back=int(std::floor(std::max({a.z,b.z,c.z})/step));
        for (int z=front;z<=back;++z) for (int x=left;x<=right;++x) {
            size_t subsample=0;
            for (double dz:{.25,.75}) for (double dx:{.25,.75}) {
                const Vec3 p{(x+dx)*step,0,(z+dz)*step};
                const double wa=footprintTurn(b,c,p)/area,wb=footprintTurn(c,a,p)/area,wc=1-wa-wb;
                if (std::min({wa,wb,wc})>=-1e-9) {
                    const auto alpha=uint8_t(std::lround(255*clamp(wa*triangle.opacity[0]+wb*triangle.opacity[1]+wc*triangle.opacity[2],0.,1.)));
                    if (alpha) {
                        const int tx=tileIndex(x),tz=tileIndex(z);
                        auto &value=tiles[{tz,tx}][size_t((z-tz*core)*core+x-tx*core)][subsample];
                        value=std::max(value,alpha);
                        if (tiles.size()>limit) return false;
                    }
                }
                ++subsample;
            }
        }
        return true;
    }
    struct Atlas { int width=1,height=1,columns=1; std::vector<uint8_t> pixels{0}; std::vector<Key> keys; };
    Atlas atlas() {
        std::vector<Key> border;
        for (const auto &[key,tile]:tiles) {
            for (int z=0;z<core;++z) for (int x=0;x<core;++x)
                if (std::any_of(tile[size_t(z*core+x)].begin(),tile[size_t(z*core+x)].end(),[](uint8_t value){return value!=0;}) &&
                    (x==0 || x==core-1 || z==0 || z==core-1)) {
                    for (int dz:{-1,0,1}) for (int dx:{-1,0,1})
                        if ((dx==0 || x==(dx<0?0:core-1)) && (dz==0 || z==(dz<0?0:core-1)))
                            border.emplace_back(key.first+dz,key.second+dx);
                }
        }
        for (auto key:border) tiles.try_emplace(key);
        Atlas result; if (tiles.empty()) return result;
        result.columns=int(std::ceil(std::sqrt(double(tiles.size()))));
        result.width=result.columns*stride; result.height=int((tiles.size()+result.columns-1)/result.columns)*stride;
        result.pixels.assign(size_t(result.width)*result.height,0);
        size_t index=0;
        constexpr int weights[]{1,2,1};
        for (const auto &[key,tile]:tiles) {
            (void)tile; result.keys.push_back(key);
            const int left=int(index%result.columns)*stride,top=int(index/result.columns)*stride;
            for (int z=0;z<stride;++z) for (int x=0;x<stride;++x) {
                const int cx=key.second*core+x-gutter,cz=key.first*core+z-gutter; int sum=0;
                for (int dz=-1;dz<=1;++dz) for (int dx=-1;dx<=1;++dx)
                    sum+=weights[dx+1]*weights[dz+1]*sample(cx+dx,cz+dz);
                result.pixels[size_t((top+z)*result.width+left+x)]=uint8_t((sum+8)/16);
            }
            ++index;
        }
        return result;
    }
};
} // namespace sjp::visual
