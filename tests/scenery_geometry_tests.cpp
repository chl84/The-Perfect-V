#include "visual_ski_tracks.h"
#include "visual_floodlights.h"
#include "visual_snow_material.h"
#include "visual_mountains.h"
#include "visual_scenery_mask.h"
#include "visual_terrain_sun.h"
#include "visual_sun_shadow_field.h"
#include "visual_snow_shadow.h"
#include "visual_snow_accumulation.h"
#include "visual_shadow.h"
#include "visual_tree_geometry.h"
#include "camera_path.h"
#include <iostream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void receivers() {
    size_t hits = 0, triangles = 0;
    for (const auto &hill : hills()) {
        for (auto time : {environment::TimeOfDay::Morning, environment::TimeOfDay::Day,
                          environment::TimeOfDay::Evening, environment::TimeOfDay::Night}) {
            const auto light = visual::environmentLight({time,environment::Weather::Sunny,1});
            const auto mast = visual::floodlightMast(hill,0,-1);
            const auto hit = visual::projectToSnow(hill,mast.lamp,light.direction*-1);
            if (hit) {
                ++hits;
                require(std::abs(hit->y-visual::terrainSnowHeight(hill,hit->x,hit->z)) < 1e-7,
                        "Scenery shadow floats above/below its receiver");
                require(cross(*hit-mast.lamp,light.direction).length() < .002,
                        "Scenery shadow does not follow the light ray");
            }
        }
        for (double z : {0.,15*hill.scale(),35*hill.scale()}) {
            const double x = hill.k*.60, r = 2*hill.scale();
            const auto hull = visual::shadowHull({{x-r,0,z-r},{x+r,0,z-r},{x,0,z},{x+r,0,z+r},{x-r,0,z+r}});
            require(hull.size() == 4, "Projected hull retained an interior vertex");
            visual::snowDecalTriangles(hill,hull,[&](Vec3 a,Vec3 b,Vec3 c) {
                ++triangles;
                for (auto p : {a,b,c}) {
                    require(std::abs(p.y-visual::terrainSnowHeight(hill,p.x,p.z)) < 1e-6,
                            "Decal cuts through a coarse/refined snow triangle");
                    require(p.x >= x-r-1e-7 && p.x <= x+r+1e-7 &&
                            p.z >= z-r-1e-7 && p.z <= z+r+1e-7,"Decal escaped its footprint");
                }
            });
        }
    }
    require(hits > 70 && triangles > 500,"Scenery receiver coverage is unexpectedly empty");
}
void tracks() {
    Simulation sim(2,0);
    sim.resetOriginal(2,12345,98765);
    sim.setWind(0,0);
    for (int i = 0; i < 20000 && sim.phase != Phase::Finished; ++i)
        sim.step(ORIGINAL_TICK,autopilot(sim));
    require(sim.phase == Phase::Finished && !sim.crashed,"Standing ski-track fixture did not finish");
    const auto originalFrames = sim.frames;
    visual::SkiTrackCache cache;
    cache.update(sim.hill(),sim.frames);
    require(!cache.triangles.empty(),"Standing landing produced no ski tracks");
    double last = 0;
    for (const auto &triangle : cache.triangles) {
        require(triangle.endTime >= last,"Ski-track cache is not ordered by replay time");
        last = triangle.endTime;
        for (const auto &v : triangle.vertices) {
            require(std::abs(v.position.y-.010-visual::terrainSnowHeight(sim.hill(),v.position.x,v.position.z)) < 1e-6,
                    "Ski track does not follow the actual snow triangles");
            require(v.time <= triangle.endTime+1e-6 && v.edge >= 0 && v.edge <= 1,"Invalid track age/edge fade");
        }
    }
    const auto count = cache.triangles.size();
    cache.update(sim.hill(),sim.frames);
    require(cache.triangles.size() == count,"Pause duplicated ski tracks");
    require(visual::SkiTrackCache::opacity(2,3,1) == 0 &&
            visual::SkiTrackCache::opacity(12,3,1) == 0 &&
            visual::SkiTrackCache::opacity(4,3,1) > visual::SkiTrackCache::opacity(8,3,1),
            "Ski tracks appear in the future or fail to fade with replay time");
    for (size_t i = 0; i < sim.frames.size(); ++i)
        require(sim.frames[i].pose == originalFrames[i].pose && sim.frames[i].x == originalFrames[i].x &&
                sim.frames[i].y == originalFrames[i].y,"Track generation changed recorded physics");
    // Two recordings can have the same duration and identical final stop pose.
    // Replacing the vector must invalidate earlier contact geometry as well.
    const auto previousTrack = cache.triangles.front().vertices[0].position;
    auto replacement = sim.frames;
    for (size_t i = 0; i+1 < replacement.size(); ++i)
        if (Phase(replacement[i].phase) == Phase::Outrun) {
            auto &f = replacement[i];
            f.renderSkeleton = originalSkeleton(f.pose,f.leadingLeg).points;
            f.hasRenderSkeleton = true;
            f.renderSkeleton[size_t(OriginalJoint::Ankle1)].z += .30;
        }
    sim.frames = std::move(replacement);
    cache.update(sim.hill(),sim.frames);
    require((cache.triangles.front().vertices[0].position-previousTrack).length() > .01,
            "A different replay with the same final pose retained old ski tracks");
    sim.reset(2,0);
    cache.update(sim.hill(),sim.frames);
    require(cache.triangles.empty(),"New jump retained the previous jump's tracks");
}
void snowCoverage() {
    for (const auto &h : hills()) {
        const auto mask = visual::opaquePreparedCoverage(h);
        const double step = 3*h.scale(), zStep = 10*h.scale();
        const double first = visual::snowCellStart(mask[2],visual::snowGridStart(h),step);
        for (double x = first; x < mask[1]; x += step)
            for (double z : {-zStep,0.})
                visual::snowCellTriangles(h,x,z,[&](double xx,double zz) { return visual::backdropGround(h,xx,zz); },
                                          [&](Vec3 a,Vec3 b,Vec3 c) {
                    for (auto p : {a,b,c})
                        require(visual::preparedSnowWeight(h,p.x,p.z)*visual::valleyBackOpacity(h,p.x) > .999999,
                                "Natural snow was discarded under a partially transparent groomed triangle");
                });
    }
}
void shadowVolumes() {
    const Vec3 light=Vec3{-.70,.34,-.50}.normalized();
    std::vector<Vec3> box;
    for (double x:{-1.,1.}) for (double y:{2.,4.}) for (double z:{-1.,1.}) box.push_back({x,y,z});
    const visual::SceneryShadowVolume volume(box,light,100);
    auto rayBox=[&](Vec3 p) {
        double entry=0,exit=10000;
        const std::array<double,3> position{p.x,p.y,p.z},direction{light.x,light.y,light.z};
        const std::array<double,3> low{-1,2,-1},high{1,4,1};
        for (size_t i=0;i<3;++i) {
            double a=(low[i]-position[i])/direction[i],b=(high[i]-position[i])/direction[i];
            if (a>b) std::swap(a,b);
            entry=std::max(entry,a); exit=std::min(exit,b);
        }
        return exit>=entry;
    };
    for (double y:{-5.,0.,3.,8.}) for (int x=-24;x<=24;++x) for (int z=-24;z<=24;++z) {
        const Vec3 p{x*.5+.037,y,z*.5+.091};
        require(volume.contains(p)==rayBox(p),"Projected shadow volume disagrees with an independent ray/caster intersection");
    }
    int away=0;
    for (const auto &h:hills()) {
        const double x=h.atDistance(h.k*.55),dx=3*h.scale();
        const auto normal=Vec3{-(h.ground(x+dx)-h.ground(x-dx))/(2*dx),1,0}.normalized();
        if (dot(normal,light)<-.01) {
            ++away;
            require(visual::scenerySunExposure(h,{x,h.ground(x),0},light)==0,
                    "Scenery shadows darken a slope already facing away from the evening sun");
        }
        const auto mast=visual::floodlightMast(h,0,-1);
        std::vector<Vec3> head;
        for (int a:{-1,1}) for (int b:{-1,1}) for (int c:{-1,1})
            head.push_back(mast.lamp+Vec3{a*1.7,b*.625,c*.30});
        const visual::SceneryShadowVolume actual(head,light,200*std::sqrt(h.scale()));
        actual.receivers(h,[&](Vec3 a,Vec3 b,Vec3 c) {
            for (auto p:{a,b,c,(a+b+c)*(1./3)})
                require(actual.contains(p),"A receiver triangle bridges ground outside the caster's light-space silhouette");
        });
    }
    require(away>10,"Evening regression did not exercise steep back-facing slopes");
}
void terrainHorizon() {
    const Vec3 low{-20,0,-20}, high{30,0,20}, light=Vec3{1,.25,0}.normalized();
    const auto ridge=[](double x,double) { return std::max(0.,8-2*std::abs(x-10)); };
    const visual::TerrainSunHorizon horizon(low,high,light,.25,ridge);
    require(horizon.visibility({0,1,0})==0,"A terrain-hidden caster still sees the sun");
    require(horizon.visibility({0,7,0})==1,"Terrain horizon hides a caster above the ridge");
    require(std::abs(horizon.height({0,0,0})-5.5)<.3,"Sun horizon disagrees with analytic ridge projection");
    const visual::TerrainSunHorizon flat(low,high,light,.25,[](double,double){return 0.;});
    require(flat.visibility({0,0,0})==1,"Flat terrain incorrectly blocks its own sunlight");
    require(horizon.sunlitCaster({{0,1,-1},{0,1,1},{0,2,0}}).empty(),
            "A completely terrain-hidden tree retained a shadow caster");
    const auto clipped=horizon.sunlitCaster({{0,1,-1},{0,1,1},{0,7,0}});
    require(clipped.size()>=3,"A partly lit caster disappeared entirely");
    for (auto p:clipped)
        require(p.y>5.1 && p.y<=7 && std::abs(p.z)<=1,"Clipping enlarged a caster beyond its lit geometry");

    const auto found=std::find_if(hills().begin(),hills().end(),[](const Hill &h){return h.id=="dsj2-czech-rep";});
    require(found!=hills().end(),"Missing evening shadow regression venue");
    const auto &h=*found;
    const visual::CameraSurface snow(h);
    const auto evening=visual::environmentLight({environment::TimeOfDay::Evening,environment::Weather::Sunny,1});
    const visual::TerrainSunHorizon actual({visual::snowGridStart(h),0,-200*h.scale()},
        {std::max(420*h.scale(),h.end()+80*h.scale()),0,200*h.scale()},evening.direction,.5*std::sqrt(h.scale()),
        [&](double x,double z) {return snow.height(x,z);});
    require(actual.columns<=1536 && actual.rows<=1536,"Terrain horizon exceeded its bounded CPU grid");
    for (int id:{388,216,274,58,412}) {
        const auto variation=visual::treeGeometryVariation;
        const double x=(-150+variation(id*3.1)*530)*h.scale();
        const double z=(28+variation(id*9.2)*150)*h.scale()*(id%2?1:-1);
        const double height=(3.5+variation(id*7.6)*7.6)*std::sqrt(h.scale());
        const Vec3 top{x,visual::backdropGround(h,x,z)+height,z};
        bool blocked=false;
        for (double t=.5;t<1000;t+=.5) {
            const auto p=top+evening.direction*t;
            if (p.x<visual::snowGridStart(h) || std::abs(p.z)>200*h.scale()) break;
            if (p.y+1<snow.height(p.x,p.z)) {blocked=true;break;}
        }
        require(blocked && actual.visibility(top)==0,
                "A tree behind the evening terrain horizon still casts a distant ghost shadow");
    }
}
void treeGeometry() {
    const Vec3 root{13,7,-8}; const double height=10;
    const auto trunk=visual::treeTrunkGeometry(root,height);
    double bottom=1e9,top=-1e9;
    for (auto p:trunk) {bottom=std::min(bottom,p.y);top=std::max(top,p.y);}
    require(std::abs(bottom-root.y)<1e-8 && top>root.y+4,"Tree shadow trunk does not reach the visible root");
    for (int tiers:{4,5,6}) for (int tier=0;tier<tiers;++tier) {
        const auto bough=visual::treeTierGeometry(root,height,17,tiers,tier);
        for (auto p:bough.base)
            require(p.y<bough.tip.y && std::hypot(p.x-root.x,p.z-root.z)<height*.34,
                    "Shadow bough exceeds the rendered tree's bounds");
        require(bough.tip.y<=root.y+height,"Shadow bough is taller than its rendered tree");
    }
}

void shadowOverlap() {
    const visual::SceneryShadowTriangle a{{Vec3{0,0,0},Vec3{2,0,0},Vec3{2,0,2}},{.25,.25,.25}};
    const visual::SceneryShadowTriangle b{{Vec3{0,0,0},Vec3{2,0,2},Vec3{0,0,2}},{.25,.25,.25}};
    visual::SceneryShadowMask mask(.25);
    require(mask.rasterize(a) && mask.rasterize(b),"Small shadow exhausted the opacity atlas");
    for (int z=0;z<8;++z) for (int x=0;x<8;++x)
        require(mask.sample(x,z)==64,"Adjacent receiver triangles leave an opacity seam");
    const auto before=mask.atlas();
    for (int copy=0;copy<14;++copy) { mask.rasterize(a); mask.rasterize(b); }
    const auto after=mask.atlas();
    require(before.width==after.width && before.height==after.height && before.pixels==after.pixels,
            "Overlapping stand sections accumulate dark shadow bands");
    require(*std::max_element(after.pixels.begin(),after.pixels.end())<=64,"Shadow blur exceeds caster opacity");
    require(mask.sample(-1,0)==0 && mask.sample(8,0)==0,"Shadow coverage escaped its receiver");
}

void snowBankShadows() {
    visual::SnowAccumulation snow;
    snow.cap({0,8,0},{3,8,0},.2,.3,42);
    require(snow.groundTriangles.empty(),"Roof snow was included in ground-shadow receivers");
    const auto flat=[](double,double){return 0.;};
    snow.standBank({0,0,0},{10,0,0},{0,0,1},42,flat,[](double,double){return 3.;});
    bool tall=false;
    for (size_t index:snow.groundTriangles) {
        require(index>=24 && index<snow.triangles.size(),"Snow shadow receiver includes a roof cap or invalid triangle");
        for (const auto &v:snow.triangles[index]) tall|=v.position.y>1.;
    }
    require(tall,"Tall snow banks still lose their shadow above one metre");
    using V=visual::SnowShadowVertex;
    const std::array<V,3> triangle{{{{0,0,0},{-2,0,-2},.2},{{4,2,0},{2,0,-2},.6},{{0,4,4},{-2,0,2},1.}}};
    double area=0; size_t count=0;
    for (int z=-2;z<2;++z) for (int x=-2;x<2;++x)
        visual::snowShadowTile(triangle,x,x+1,z,z+1,[&](V a,V b,V c) {
            ++count;
            area+=cross(b.position-a.position,c.position-a.position).length()*.5;
            for (auto p:{a,b,c}) {
                require(p.receiver.x>=x-1e-8 && p.receiver.x<=x+1+1e-8 &&
                        p.receiver.z>=z-1e-8 && p.receiver.z<=z+1+1e-8,"Snow UV escaped its atlas tile");
                const double u=p.position.x/4,v=p.position.z/4;
                require(u>=-1e-8 && v>=-1e-8 && u+v<=1+1e-8 &&
                        std::abs(p.position.y-2*u-4*v)<1e-8,
                        "Snow shadow creates a lifted shell outside the visible snow triangle");
                require(std::abs(p.weight-(.2+.4*u+.8*v))<1e-8,"Clipping changes snow shadow attenuation");
            }
        });
    require(count>3 && std::abs(area-cross(triangle[1].position-triangle[0].position,
                                        triangle[2].position-triangle[0].position).length()*.5)<1e-8,
            "Snow atlas splitting leaves gaps or overlapping receiver triangles");
    const auto &hill=hills()[2];
    const double x=hill.atDistance(hill.k*.9),z=30*hill.scale();
    const Vec3 base{x,visual::terrainSnowHeight(hill,x,z),z};
    const auto sun=visual::environmentLight({environment::TimeOfDay::Morning,environment::Weather::Sunny,1}).direction;
    const std::array<Vec3,6> buried{{base+Vec3{-1,-1,-1},base+Vec3{1,1,-1},base+Vec3{0,1,1},
                                   Vec3{0,1,0},Vec3{0,1,0},Vec3{0,1,0}}};
    size_t visible=0;
    visual::visibleSnowBank(hill,buried,[&](const auto &bank) {
        ++visible;
        for (size_t i=0;i<3;++i) {
            const auto p=bank[i];
            require(p.y>=visual::terrainSnowHeight(hill,p.x,p.z)-1e-7,
                    "Buried snow skirt survives shadow-receiver clipping");
        }
    });
    require(visible>0,"Clipping a buried skirt removed the raised bank too");
    const auto low=visual::snowShadowVertex(hill,base,{0,1,0},sun);
    const auto high=visual::snowShadowVertex(hill,base+Vec3{0,2,0},{0,1,0},sun);
    require((low.receiver-base).length()<1e-6,"Bank shadow does not join its ground receiver at the toe");
    require((high.receiver-base).length()>.5 && cross(high.receiver-high.position,sun).length()<.002,
            "Raised snow samples the ground shadow vertically instead of along sunlight");
}

void sunShadowConsistency() {
    using environment::TimeOfDay;
    using environment::Weather;
    // Only a visible direct sun bakes scenery shadows; weather never rebuilds them.
    for (int time = 0; time < 4; ++time)
        for (int weather = 0; weather < 4; ++weather) {
            const auto light = visual::environmentLight({TimeOfDay(time), Weather(weather), 1});
            const bool expected = time != int(TimeOfDay::Night) && weather == int(Weather::Sunny);
            require(visual::bakedSunShadows(light) == expected,
                    "Scenery shadows bake under overcast, fog, snowfall or moonlight");
        }
    // A ridge between athlete and sun removes the sun shadow, but keeps the
    // close sky contact. A sunlit athlete matches the baked scenery darkness.
    const auto day = visual::environmentLight({TimeOfDay::Day, Weather::Sunny, 1});
    const auto lit = visual::landingShadow(day, 0, -.05, {}, 1);
    const auto hidden = visual::landingShadow(day, 0, -.05, {}, 0);
    require(std::abs(lit.opacity - visual::sunShadowOpacity(day)) < 1e-9,
            "Athlete and scenery sun shadows differ in darkness");
    require(hidden.opacity >= .28 - 1e-9 && hidden.opacity < lit.opacity &&
                std::abs(hidden.cast.x) < 1e-9 && std::abs(hidden.cast.z) < 1e-9,
            "Terrain-shadowed athlete keeps a directional sun shadow");
    // The ridge shadow covers snow that faces the sun but cannot see it, and
    // leaves both openly sunlit snow and snow facing away untouched.
    size_t shaded = 0;
    for (const auto &hill : hills())
        for (auto time : {TimeOfDay::Morning, TimeOfDay::Evening}) {
            const auto light = visual::environmentLight({time, Weather::Sunny, 1});
            const double s = hill.scale();
            const visual::TerrainSunHorizon horizon(
                {visual::snowGridStart(hill), 0, -200 * s},
                {std::max(420 * s, hill.end() + 80 * s), 0, 200 * s}, light.direction, .5 * std::sqrt(s),
                [&](double x, double z) { return visual::terrainSnowHeight(hill, x, z); });
            const auto exposure = [&](Vec3 p) { return visual::scenerySunExposure(hill, p, light.direction); };
            const double strength = visual::sunShadowOpacity(light);
            const visual::TerrainShadowGrid grid(hill, horizon, strength, exposure, 2 * std::sqrt(s));
            for (float value : grid.opacity)
                require(std::isfinite(value) && value >= 0 && value <= strength + 1e-6,
                        "Terrain shadow exceeds the sun shadow darkness");
            auto lit = [&](double x, double z) {
                const Vec3 q{x, visual::terrainSnowHeight(hill, x, z), z};
                return exposure(q) < 1e-9 || horizon.visibility(q) > 1 - 1e-9;
            };
            for (double x = 0; x < hill.end(); x += 9 * s)
                for (double z = -150 * s; z <= 150 * s; z += 15 * s) {
                    const Vec3 p{x, visual::terrainSnowHeight(hill, x, z), z};
                    // Blur and bilinear filtering reach two grid cells; beyond
                    // any transition sunlit (or unlit) snow must stay clear.
                    bool open = true;
                    for (int dx = -3; dx <= 3 && open; ++dx)
                        for (int dz = -3; dz <= 3 && open; ++dz)
                            open = lit(x + dx * grid.step, z + dz * grid.step);
                    if (open)
                        // Below 1% (2-3 of 255 levels) is sub-cell horizon sampling.
                        require(grid.sample(x, z) < .01, "Terrain shadow darkens sunlit snow or snow facing away");
                    else if (horizon.visibility(p) < 1e-9 && exposure(p) > .999 &&
                             visual::valleyEdgeOpacity(hill, z, x) > .999) {
                        bool deep = true;
                        for (int dx = -3; dx <= 3 && deep; ++dx)
                            for (int dz = -3; dz <= 3 && deep; ++dz) {
                                const double xx = x + dx * grid.step, zz = z + dz * grid.step;
                                const Vec3 q{xx, visual::terrainSnowHeight(hill, xx, zz), zz};
                                deep = horizon.visibility(q) < 1e-9 && exposure(q) > .999 &&
                                       visual::valleyEdgeOpacity(hill, zz, xx) > .999;
                            }
                        if (!deep) continue;
                        ++shaded;
                        require(std::abs(grid.sample(x, z) - strength) < 1e-3,
                                "Snow behind a ridge keeps sunlight or differs from a cast shadow");
                    }
                }
        }
    require(shaded > 100, "Terrain shadow test found no ridge-shadowed snow");
}
}
int main() {
    try { receivers(); tracks(); snowCoverage(); shadowVolumes(); shadowOverlap(); terrainHorizon(); treeGeometry(); snowBankShadows(); sunShadowConsistency();
          std::cout << "32-venue receivers, evening terrain horizons, shared tree geometry, shadow volumes, overlap isolation, opaque snow and replay-time tracks, sun-only baking and terrain/athlete shadow consistency passed\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
