#pragma once
#include "physics.h"
#include <array>

namespace sjp::visual {
inline double treeGeometryVariation(double n) {
    return std::fmod(std::abs(std::sin(n*12.9898)*43758.5453),1.0);
}
struct TreeBoughGeometry {
    std::array<Vec3,7> base;
    Vec3 tip;
};
inline TreeBoughGeometry treeBoughGeometry(Vec3 p,double radius,double height,double seed,Vec3 sway={}) {
    TreeBoughGeometry geometry;
    geometry.tip=p+Vec3{0,height,0}+sway;
    const double rotation=treeGeometryVariation(seed)*PI;
    for (int i=0;i<7;++i) {
        const double angle=i*2*PI/7+rotation;
        const double r=radius*(.90+treeGeometryVariation(seed+i*11.21)*.18);
        geometry.base[size_t(i)]=p+Vec3{std::cos(angle)*r,-height*treeGeometryVariation(seed+i*1.41)*.065,std::sin(angle)*r};
    }
    return geometry;
}
inline TreeBoughGeometry treeTierGeometry(Vec3 base,double height,double seed,int tiers,int tier,Vec3 sway={}) {
    const double t=tier/double(tiers-1);
    return treeBoughGeometry(base+Vec3{0,height*(.12+.53*t),0},height*(.31-.238*t),
                             height*(.43-.12*t),seed+tier*29.7,sway);
}
inline Vec3 treeTrunkCenter(Vec3 base,double height) { return base+Vec3{0,height*.22,0}; }
inline Vec3 treeTrunkSize(double height) { return {height*.085,height*.44,height*.085}; }
inline std::array<Vec3,8> treeTrunkGeometry(Vec3 base,double height) {
    std::array<Vec3,8> corners; size_t index=0;
    const auto center=treeTrunkCenter(base,height),size=treeTrunkSize(height)*.5;
    for (int x:{-1,1}) for (int y:{-1,1}) for (int z:{-1,1})
        corners[index++]=center+Vec3{x*size.x,y*size.y,z*size.z};
    return corners;
}
} // namespace sjp::visual
