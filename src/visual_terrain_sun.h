#pragma once
#include "visual_snow_mesh.h"
#include <vector>

namespace sjp::visual {
// A CPU-only height horizon aligned with the sun. One reverse sweep answers
// whether terrain between a point and the sun blocks direct light. Built once
// with a scenery-shadow cache entry, never evaluated during frame rendering.
class TerrainSunHorizon {
  public:
    Vec3 across{},along{};
    double firstU=0,firstV=0,step=1;
    int columns=0,rows=0;
    std::vector<float> heights;
    template <class Height>
    TerrainSunHorizon(Vec3 low,Vec3 high,Vec3 sunlight,double spacing,Height height) {
        sunlight=sunlight.normalized();
        const double horizontal=std::hypot(sunlight.x,sunlight.z);
        along=horizontal>1e-8?Vec3{sunlight.x/horizontal,0,sunlight.z/horizontal}:Vec3{1,0,0};
        across={-along.z,0,along.x};
        double lastU=0,lastV=0; bool first=true;
        for (double x:{low.x,high.x}) for (double z:{low.z,high.z}) {
            const Vec3 p{x,0,z}; const double u=dot(p,across),v=dot(p,along);
            if (first) { firstU=lastU=u; firstV=lastV=v; first=false; }
            firstU=std::min(firstU,u); lastU=std::max(lastU,u);
            firstV=std::min(firstV,v); lastV=std::max(lastV,v);
        }
        step=std::max(spacing,std::max(lastU-firstU,lastV-firstV)/1533.);
        firstU-=step; firstV-=step;
        columns=int(std::ceil((lastU-firstU)/step))+2;
        rows=int(std::ceil((lastV-firstV)/step))+2;
        heights.resize(size_t(columns)*rows);
        const double rise=horizontal>1e-8?std::max(0.,sunlight.y)/horizontal:1e6;
        for (int x=0;x<columns;++x) {
            double horizon=-1e9;
            for (int z=rows-1;z>=0;--z) {
                const auto p=across*(firstU+(x+.5)*step)+along*(firstV+(z+.5)*step);
                horizon-=rise*step;
                if (p.x>=low.x && p.x<=high.x && p.z>=low.z && p.z<=high.z)
                    horizon=std::max(horizon,height(p.x,p.z));
                heights[size_t(z*columns+x)]=float(horizon);
            }
        }
    }
    double height(Vec3 p) const {
        const double u=clamp((dot(p,across)-firstU)/step-.5,0.,double(columns-1));
        const double v=clamp((dot(p,along)-firstV)/step-.5,0.,double(rows-1));
        const int x=int(u),z=int(v),xx=std::min(x+1,columns-1),zz=std::min(z+1,rows-1);
        auto sample=[&](int col,int row){return double(heights[size_t(row*columns+col)]);};
        const double top=sample(x,z)*(1-(u-x))+sample(xx,z)*(u-x);
        const double bottom=sample(x,zz)*(1-(u-x))+sample(xx,zz)*(u-x);
        return top*(1-(v-z))+bottom*(v-z);
    }
    double visibility(Vec3 p) const {
        // Tolerance prevents the sampled receiver's own surface from shading
        // itself. A distant ridge remains fully opaque, not another tree shadow.
        return clamp(1+(p.y-height(p)+.05)/(.5*step),0.,1.);
    }
    std::vector<Vec3> sunlitCaster(const std::vector<Vec3> &points) const {
        std::vector<double> distances;
        std::vector<Vec3> clipped;
        for (auto p:points) {
            distances.push_back(p.y-height(p)+.05);
            if (distances.back()>=0) clipped.push_back(p);
        }
        if (clipped.empty() || clipped.size()==points.size()) return clipped;
        // A clipped chord lies inside the original convex caster. Keeping its
        // lit endpoints and boundary intersections cannot invent a larger tree.
        for (size_t i=0;i<points.size();++i) for (size_t j=i+1;j<points.size();++j)
            if ((distances[i]>=0)!=(distances[j]>=0))
                clipped.push_back(points[i]+(points[j]-points[i])*(distances[i]/(distances[i]-distances[j])));
        std::vector<Vec3> unique;
        for (auto p:clipped)
            if (std::none_of(unique.begin(),unique.end(),[&](Vec3 q){return (p-q).length()<1e-6;})) unique.push_back(p);
        return unique;
    }
};
} // namespace sjp::visual
