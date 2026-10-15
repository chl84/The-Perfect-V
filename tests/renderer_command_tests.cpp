// CPU-only regression test. Every renderer GL entry point is supplied below;
// this executable does not link to OpenGL or create a window/context.
// The model checks finite arguments, primitive/stack balance, resource lifetime
// and current-attribute writes after a display list's outer restoration. It does
// not emulate rasterization, lighting, or the driver's complete display list.
#include "renderer.h"
#include "visual_weather.h"
#include "visual_shadow.h"
#include "visual_start_signal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace audit {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void singleShadow() {
    using sjp::visual::ShadowCapsule;
    using sjp::visual::ShadowMask;
    const ShadowCapsule body{{-.6, 0, 0}, {.6, 0, 0}, .18};
    ShadowMask single, overlap;
    for (double blur : {0.0, .025, .10, .205}) {
        single.build({body}, blur);
        overlap.build({body, body, body}, blur);
        require(single.coverage == overlap.coverage,
                "Overlapping body parts darken the silhouette shadow");
        for (float alpha : overlap.coverage)
            require(std::isfinite(alpha) && alpha >= 0 && alpha <= 1.000001f,
                    "Shadow coverage exceeds one layer");
    }
    auto at = [](const ShadowMask &mask, double x, double z) {
        const int column = std::clamp(int(std::round((x - mask.x) / mask.dx)), 0, ShadowMask::columns - 1);
        const int row = std::clamp(int(std::round((z - mask.z) / mask.dz)), 0, ShadowMask::rows - 1);
        return mask.coverage[size_t(row * ShadowMask::columns + column)];
    };
    const std::vector<ShadowCapsule> silhouette{
        {{-.25, 0, 0}, {.25, 0, 0}, .18},
        {{-1.5, 0, -.7}, {1.5, 0, -.7}, .036},
        {{-1.5, 0, .7}, {1.5, 0, .7}, .036}};
    single.build(silhouette, .025);
    require(at(single, 1, 0) == 0, "A separate oval fills the space between the skis");
    require(at(single, 0, 0) > .9 && at(single, 1, .7) > .5,
            "The unified shadow lost its body or ski silhouette");
    overlap.build(silhouette, .205);
    require(at(overlap, 1, .7) < at(single, 1, .7),
            "Thin ski shadows do not soften with height above snow");
    auto area = [](const ShadowMask &mask) {
        double sum = 0;
        for (float alpha : mask.coverage)
            sum += alpha;
        return sum * mask.dx * mask.dz;
    };
    require(std::abs(area(overlap) / area(single) - 1) < .08,
            "Shadow softening creates an extra dark footprint");
    sjp::OriginalPose pose;
    pose.reset(-.4);
    std::array<float, 22> state{};
    std::copy(pose.angles.begin(), pose.angles.end(), state.begin());
    std::copy(pose.offsets.begin(), pose.offsets.end(), state.begin() + 18);
    const auto source = sjp::originalSkeleton(state);
    auto changed = source;
    changed.points[size_t(sjp::OriginalJoint::Hand1)].z += .8;
    single.build(sjp::visual::shadowSilhouette(source), .025);
    overlap.build(sjp::visual::shadowSilhouette(changed), .025);
    require(single.coverage != overlap.coverage, "Shadow does not follow the displayed joints");
}
void startSignalLighting() {
    using namespace sjp;
    for (const auto &hill : hills()) {
        Frame frame;
        frame.phase = int(Phase::Ready);
        const visual::StartSignal green(hill, frame);
        const auto source = green.light().positionRange;
        const Vec3 origin{source[0], source[1], source[2]};
        require(green.lens(0).y > green.lens(1).y && green.base.z == -3 && green.base.x == hill.start() + 2 &&
                    green.base.x + green.range < 12,
                "Start signal is reversed, moved into the track or outside its light field");
        const auto near = green.sample(origin + green.front, green.front * -1);
        const auto distant = green.sample(origin + green.front * 4, green.front * -1);
        require(near.y > .3 && near.y < .55 && distant.y > 0 && distant.y < near.y && near.y > near.x * 8 &&
                    near.y > near.z * 5,
                "Green signal light is missing, excessive, white or ignores distance");
        require(green.sample(origin - green.front, green.front).length() == 0 &&
                    green.sample(origin + green.front * 7, green.front * -1).length() == 0 &&
                    green.sample(origin + green.front, green.front).length() == 0,
                "Signal illuminates behind its housing, beyond range or through a surface");
        frame.phase = int(Phase::Inrun);
        const visual::StartSignal red(hill, frame);
        const auto redSource = red.light().positionRange;
        const Vec3 redOrigin{redSource[0], redSource[1], redSource[2]};
        const auto redLight = red.sample(redOrigin + red.front, red.front * -1);
        require(redLight.x > .3 && redLight.x > redLight.y * 20 && !red.lit(1) && red.lit(0),
                "Red signal light does not come from the upper lens");
        frame.phase = int(Phase::Ready);
        frame.elapsed = 6;
        const visual::StartSignal off(hill, frame);
        require(off.lightColor()[3] == 0 && off.sample(origin + green.front, green.front * -1).length() == 0,
                "Blinking signal emits real light during its off phase");
    }
}
void floodlightsAndLandingShadow() {
    using namespace sjp;
    using namespace sjp::visual;
    using namespace sjp::environment;
    for (const auto &hill : hills()) {
        FloodlightField field(hill, TimeOfDay::Night), day(hill, TimeOfDay::Day);
        const int expectedColumns = hill.k <= 60 ? 3 : hill.k < 100 ? 4 : 5;
        require(field.columns == expectedColumns && field.spacing >= 22,
                "Short hills retain too many or closely packed floodlight masts");
        require(std::abs(field.firstX - 60 * hill.scale()) < 1e-10 &&
                    std::abs(field.firstX + (field.columns - 1) * field.spacing - 176 * hill.scale()) < 1e-10,
                "Reduced floodlights no longer cover the original arena stretch");
        for (int i = field.columns * 2; i < gfx::ARENA_LIGHT_COUNT; ++i)
            require(field.lights[size_t(i)].positionRange == std::array<float, 4>{} &&
                        field.lights[size_t(i)].directionCone == std::array<float, 4>{},
                    "Removed mast retains a ghost light source");
        for (int step = 0; hill.k < 100 && step <= 200; ++step) {
            const double x = field.firstX + (field.columns - 1) * field.spacing * step / 200;
            require(field.sample({x, hill.ground(x), 0}).intensity > .5,
                    "Reduced mast count leaves a dark gap in the landing illumination");
        }
        for (int column = 0; column < field.columns; ++column) {
            const double x = field.firstX + column * field.spacing;
            const Vec3 snow{x, hill.ground(x), 0};
            require(field.sample(snow).intensity > .3, "Floodlights do not reach the landing snow");
            require(day.sample(snow).intensity == 0, "Daylight runs the spotlight calculations");
            for (int side : {-1, 1}) {
                const auto mast = floodlightMast(hill, column, side);
                const double originalSetback = std::max(25 * hill.scale(), 8.0) + 10.5;
                require(side > 0 ? mast.base.z - .55 > 26 + 3
                                 : mast.base.z == -originalSetback,
                        "Floodlight fixture crosses camera 1 or moves the opposite mast row");
                if (originalSetback >= 30)
                    require(mast.base.z == side * originalSetback,
                            "Mast row with existing camera clearance moved unnecessarily");
                const double edge = std::max(0.0, std::abs(mast.base.z) - 19 * hill.scale());
                require(std::abs(mast.base.x - x) < 1e-10 &&
                            std::abs(mast.base.y - (hill.ground(x) - edge * .08 +
                                       std::sin(x * .017 + mast.base.z * .013) * edge * .018)) < 1e-10 &&
                            std::abs(mast.lamp.y - (mast.base.y + 18 * std::sqrt(hill.scale()) + 2)) < 1e-10,
                        "Mast light position does not match its fixture or original valley profile");
                const Vec3 aim = (snow - mast.lamp).normalized();
                auto isolated = field;
                const int index = column * 2 + (side > 0 ? 1 : 0);
                for (int i = 0; i < gfx::ARENA_LIGHT_COUNT; ++i)
                    if (i != index)
                        isolated.lights[size_t(i)].positionRange[3] = 1e8f;
                require(isolated.sample(mast.lamp - aim * 2).intensity == 0,
                        "A spotlight illuminates behind its lamp face");
                require(field.sample(mast.lamp + aim * 300).intensity == 0,
                        "Spotlights ignore their finite range");
            }
            // Switching the neighboring pair must not produce a brightness or
            // shadow-direction step at a mast column, on any of the 32 hills.
            const auto left = field.sample(snow + Vec3{-1e-5, 0, 0});
            const auto right = field.sample(snow + Vec3{1e-5, 0, 0});
            require(std::abs(left.intensity - right.intensity) < 1e-5 &&
                        (left.direction - right.direction).length() < 1e-5,
                    "Floodlight selection causes visible source switching");
            for (double height : {0.0, 5.0, 15.0, 35.0, 50.0}) {
                const auto shadow = landingShadow(environmentLight({TimeOfDay::Night, Weather::Sunny, 0}),
                                                  height, hill.slope(x),
                                                  field.sample(snow + Vec3{0, height, 0}));
                require(std::isfinite(shadow.cast.x) && std::isfinite(shadow.cast.z) &&
                            1 + hill.slope(x) * shadow.cast.x > .1 && shadow.opacity > 0,
                        "Floodlights produce an invalid shadow above the lamp height");
            }
        }
        for (int time = 0; time < 4; ++time)
            for (int weather = 0; weather < 4; ++weather) {
                const auto light = environmentLight({TimeOfDay(time), Weather(weather), 0});
                for (double slope : {-.05, -.6, -1.2, .15}) {
                    const auto contact = landingShadow(light, 0, slope);
                    const auto near = landingShadow(light, 5, slope);
                    const auto high = landingShadow(light, 35, slope);
                    require(contact.opacity >= .28 && near.opacity > .22 &&
                                high.opacity < near.opacity && contact.softness < near.softness &&
                                near.softness < high.softness && contact.softness <= .026,
                            "Landing shadow fades at contact or fails to soften with height");
                    for (const auto shadow : {contact, near, high})
                        require(std::isfinite(shadow.cast.x) && std::isfinite(shadow.cast.z) &&
                                    1 + slope * shadow.cast.x > .1,
                                "Grazing sunlight loses or stretches the landing shadow");
                }
            }
    }
}
void inrunConstruction() {
    using namespace sjp;
    using namespace sjp::visual;
    using environment::TimeOfDay;
    size_t litLarge = 0, litSmall = 0;
    for (const auto &hill : hills()) {
        InrunLayout layout;
        layout.build(hill);
        const CameraSurface snow(hill);
        require(layout.supports.size() >= 4 && layout.supports.size() <= 16 &&
                    layout.supports.back().x == -.75,
                "Inrun has an unsupported takeoff end or unbounded support count");
        for (size_t i = 0; i < layout.supports.size(); ++i) {
            const auto &s = layout.supports[i];
            require(s.x >= hill.inrunStart() && s.x < 0 && s.deck < hill.inrun(s.x) &&
                        s.footingBottom < s.footingTop,
                    "Support changes the track or uses an invalid footing");
            for (double x : {s.x - .55, s.x + .55})
                require(s.footingBottom < snow.height(x, inrunColumnZ) &&
                            s.footingTop > snow.height(x, inrunColumnZ),
                        "Footing floats above or is completely buried below the existing snow");
            if (i > 0)
                require(s.x - layout.supports[i - 1].x <= 9.000001,
                        "Inrun structural bay exceeds its bounded span");
        }
        require(layout.lightColumns >= 1 && layout.lightColumns <= 5 &&
                    std::abs(layout.firstLightX + (layout.lightColumns - 1) * layout.lightSpacing + 2.5) < 1e-9,
                "Missing takeoff lamp or inrun light budget exceeded");
        (layout.lightColumns > 1 ? litLarge : litSmall)++;
        for (int column = 0; column < layout.lightColumns; ++column) {
            const double x = layout.firstLightX + column * layout.lightSpacing;
            const Vec3 surface{x, hill.inrun(x), 0};
            require(layout.sample(surface, TimeOfDay::Night).intensity > .25 &&
                        layout.sample(surface, TimeOfDay::Day).intensity == 0,
                    "Inrun lights fail to reach the snow or stay active in daylight");
            const auto a = layout.sample(surface + Vec3{-1e-5, 0, 0}, TimeOfDay::Night);
            const auto b = layout.sample(surface + Vec3{1e-5, 0, 0}, TimeOfDay::Night);
            require(std::abs(a.intensity - b.intensity) < 1e-5 &&
                        (a.direction - b.direction).length() < 1e-5,
                    "Inrun light selection steps at a column boundary");
            const auto &p = layout.lights[size_t(column)].positionRange;
            require(p[2] < -2.3 && p[0] < 0 &&
                        std::abs(p[1] - (hill.inrun(x) + 2.4)) < 1e-5,
                    "Inrun light hardware intrudes into the ski track or camera 1's side");
        }
        // The two fields cannot illuminate the same vertex, so the combined
        // scene keeps the existing maximum of four local-source evaluations.
        for (double x : {11.9, 12.0, 12.9})
            require(layout.sample({x, 0, 0}, TimeOfDay::Night).intensity == 0,
                    "Inrun lights spill into the arena shader's source selection");
    }
    require(litLarge > 0 && litSmall > 0, "Every venue received the same inrun lighting layout");
}
struct Current {
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 3> normal{0, 0, 1};
    std::array<float, 4> uv{0, 0, 0, 1};
};
Current current;
std::array<float, 4> currentEmission{};
bool signalMaterial = false, captureSignal = false;
bool textureEnabled = false, captureSnow = false;
sjp::gfx::Id boundTexture = 0, protectedTexture = 0;
uint64_t snowHash = 1469598103934665603ULL;
size_t snowVertices = 0;
struct SnowVertex {
    sjp::Vec3 position, normal;
};
std::vector<SnowVertex> snowLighting;
const sjp::Hill *snowHill = nullptr;
void smoothSnow(const sjp::Hill &hill) {
    std::map<std::array<double, 3>, sjp::Vec3> shared;
    size_t inrunShared = 0, landingShared = 0, surroundingShared = 0;
    for (const auto &vertex : snowLighting) {
        const auto p = vertex.position, n = vertex.normal;
        const bool inrun =
            p.x < 0 && std::abs(std::abs(p.z) - 2.3) < 1e-8 && std::abs(p.y - hill.inrun(p.x)) < 1e-8;
        const bool landing =
            p.x > 6 * hill.scale() && p.x < hill.outrunBottom() + 20 && std::abs(p.z) < 10 * hill.scale();
        require(n.y > 0 && std::abs(n.length() - 1) < 1e-6, "Snow normal is not upward and unit length");
        const auto [it, inserted] = shared.emplace(std::array<double, 3>{p.x, p.y, p.z}, n);
        if (!inserted) {
            require((it->second - n).length() < 1e-6,
                    "Shared snow vertices have discontinuous lighting normals");
            (inrun ? inrunShared : landing ? landingShared : surroundingShared)++;
        }
    }
    // Static batching still emits each existing snow quad as six vertices.
    require(snowLighting.size() % 6 == 0, "Snow topology changed");
    require(inrunShared > 20 && landingShared > 20 && surroundingShared > 100,
            "Snow lighting audit did not cover the inrun, landing and surrounding terrain");
}
void protectedSnow(int hill) {
    // Snapshot of every natural/prepared snow and inrun vertex before the
    // construction changes. Keep all X/Z coordinates and protected elevations
    // byte-identical. Only distant flank/continuation elevations are normalized
    // to the original below; their actual geometry is audited separately.
    static const std::array<std::pair<size_t, uint64_t>, sjp::HILL_COUNT> frozen{{
        {52692, 8929634750460910395ULL},
        {52932, 4961535559577657852ULL},
        {52920, 13011848076777423058ULL},
        {52422, 17105584306129191312ULL},
        {57042, 10291631105589817921ULL},
        {52362, 6735142137497314494ULL},
        {51750, 16676245615236058334ULL},
        {53358, 10655617069735740352ULL},
        {51486, 14876695077908833954ULL},
        {53130, 12706393837836967098ULL},
        {51456, 15722584054216609977ULL},
        {77718, 8313475174198820722ULL},
        {52362, 17762803137908481336ULL},
        {53184, 1836508506089985619ULL},
        {52998, 9459794330870045278ULL},
        {51354, 17697855449581398120ULL},
        {52950, 9135949032508433795ULL},
        {76380, 136115027135053538ULL},
        {52464, 15590644764506681616ULL},
        {53202, 6546171082793365974ULL},
        {51282, 2660078780452410792ULL},
        {63174, 10473521836022733824ULL},
        {53364, 13545339765436101732ULL},
        {52770, 13508245450373346062ULL},
        {58326, 1545828841040480761ULL},
        {53646, 5602211231906966122ULL},
        {52632, 4458927447898145323ULL},
        {52104, 18169965633297188017ULL},
        {52362, 14060586234147959493ULL},
        {51540, 5050501703021410990ULL},
        {52236, 11485110930087622931ULL},
        {52368, 11331550327366158445ULL},
    }};
    if (snowVertices != frozen[size_t(hill)].first || snowHash != frozen[size_t(hill)].second)
        throw std::runtime_error("Protected terrain/track changed on " + sjp::hills()[size_t(hill)].name);
}
size_t signalVertices = 0;
std::array<sjp::Vec3, 2> signalCenters;
sjp::Vec3 signalFront;
std::array<size_t, 2> signalLensVertices{};
std::array<std::array<float, 4>, 2> signalEmissions{};
sjp::gfx::SpotLight startLight;
std::array<float, 4> startLightColor{};
struct Saved {
    sjp::gfx::Flags mask;
    Current value;
    bool depth;
    std::array<float, 4> emission;
    bool signal;
    bool textureEnabled;
    sjp::gfx::Id boundTexture;
};
std::vector<Saved> attributes;
std::map<sjp::gfx::Enum, int> matrices{{sjp::gfx::MODELVIEW, 1}, {sjp::gfx::PROJECTION, 1}, {sjp::gfx::TEXTURE, 1}};
sjp::gfx::Enum matrix = sjp::gfx::MODELVIEW, primitiveMode = sjp::gfx::TRIANGLES;
bool primitive = false, depth = true;
sjp::gfx::Id captureRecordList = 0;
std::vector<sjp::Vec3> recordVertices;
sjp::gfx::Id compiling = 0, nextList = 1, nextTexture = 1;
struct List {
    size_t vertices = 0, commands = 0;
    std::vector<std::function<void()>> tail;
};
std::map<sjp::gfx::Id, List> lists;
std::set<sjp::gfx::Id> textures;
size_t vertices = 0, commands = 0, maximumDraw = 0, drawVertices = 0, maximumLists = 0,
       maximumCachedVertices = 0, textureBytes = 0;
int drawableWidth = 960, drawableHeight = 600;
void finite(double x) {
    require(std::isfinite(x), "Non-finite value submitted to OpenGL");
}
void command(const char *name, bool allowed = false) {
    ++commands;
    if (compiling)
        ++lists.at(compiling).commands;
    require(!primitive || allowed, name);
}
uint64_t positionHash = 1469598103934665603ULL, particlePositionHash = 1469598103934665603ULL;
size_t particleVertices = 0;
size_t floodlightGlassVertices = 0;
size_t completedFrames = 0;
std::array<float, 3> primaryLight{}, globalAmbient{};
std::array<sjp::gfx::SpotLight, sjp::gfx::ARENA_LIGHT_COUNT> spotLights{};
float spotStrength = 0;
int spotColumns = 0;
std::array<sjp::gfx::SpotLight, sjp::gfx::ARENA_LIGHT_COUNT> inrunSpots{};
float inrunStrength = 0;
int inrunColumns = 0;
float fogStart = 0, fogEnd = 0;
bool captureBoards = false;
bool captureCamera = false;
sjp::Vec3 cameraEye;
std::array<std::vector<sjp::Vec3>, 2> boardFaces;
std::vector<sjp::Vec3> standTops, spectatorSoles;
void position(double x, double y, double z) {
    if (captureBoards && compiling &&
        currentEmission == std::array<float, 4>{.28f, .27f, .22f, 1})
        ++floodlightGlassVertices;
    if (captureRecordList && compiling == captureRecordList)
        recordVertices.push_back({x, y, z});
    if (captureSnow && compiling && textureEnabled &&
        (boundTexture == protectedTexture || boundTexture == protectedTexture + 1)) {
        ++snowVertices;
        snowLighting.push_back({{x, y, z}, {current.normal[0], current.normal[1], current.normal[2]}});
        double protectedY = y;
        if (snowHill && (x > std::max(snowHill->outrunBottom() + 47, 190 * snowHill->scale()) ||
            (x > snowHill->outrunBottom() && std::abs(z) > std::max(50 * snowHill->scale(), 24.0)))) {
            protectedY = sjp::visual::arenaGround(*snowHill, x, z);
            require(y <= protectedY + 1e-7,
                    "Decorative background rises above the original uphill extrapolation");
        }
        for (double value : {x, protectedY, z}) {
            uint64_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            snowHash = (snowHash ^ bits) * 1099511628211ULL;
        }
    }
    if (captureSignal && !compiling && signalMaterial) {
        const sjp::Vec3 p{x, y, z};
        const int lens = (p - signalCenters[0]).length() < (p - signalCenters[1]).length() ? 0 : 1;
        const auto local = p - signalCenters[size_t(lens)];
        const double depth = dot(local, signalFront), radial2 = dot(local, local) - depth * depth;
        require(depth >= -1e-8 && depth <= .0280001 && radial2 <= .160001 * .160001,
                "Start-signal emission leaks onto its post or other geometry");
        ++signalVertices;
        ++signalLensVertices[size_t(lens)];
        signalEmissions[size_t(lens)] = currentEmission;
    }
    if (captureBoards && compiling && current.color == std::array<float, 4>{.40f, .32f, .26f, 1} &&
        current.normal[1] > .999999f)
        standTops.push_back({x, y, z});
    if (captureBoards && compiling && current.color == std::array<float, 4>{.075f, .105f, .14f, 1} &&
        current.normal[1] < -.999999f)
        spectatorSoles.push_back({x, y, z});
    if (captureBoards && compiling)
        for (size_t face = 0; face < boardFaces.size(); ++face) {
            const std::array<float, 4> color = face == 0 ? std::array<float, 4>{.10f, .28f, .33f, 1}
                                                       : std::array<float, 4>{.20f, .32f, .37f, 1};
            if (current.color == color)
                boardFaces[face].push_back({x, y, z});
        }
    for (double v : {x, y, z}) {
        uint64_t b;
        std::memcpy(&b, &v, sizeof(b));
        positionHash = (positionHash ^ b) * 1099511628211ULL;
        if (!compiling && primitiveMode == sjp::gfx::TRIANGLE_FAN)
            particlePositionHash = (particlePositionHash ^ b) * 1099511628211ULL;
    }
}
void flatGrandstands() {
    require(standTops.size() >= 28 * 3 * 6 && standTops.size() % 6 == 0 &&
                !spectatorSoles.empty() && spectatorSoles.size() % 6 == 0,
            "Missing horizontal grandstand landings or spectator shoes");
    struct Deck { sjp::Vec3 low, high; };
    std::vector<Deck> decks;
    for (size_t first = 0; first < standTops.size(); first += 6) {
        Deck deck{standTops[first], standTops[first]};
        for (size_t i = first; i < first + 6; ++i) {
            const auto p = standTops[i];
            deck.low = {std::min(deck.low.x, p.x), std::min(deck.low.y, p.y), std::min(deck.low.z, p.z)};
            deck.high = {std::max(deck.high.x, p.x), std::max(deck.high.y, p.y), std::max(deck.high.z, p.z)};
        }
        require(std::abs(deck.high.y - deck.low.y) < 1e-9 && deck.high.x - deck.low.x > .78 &&
                    std::abs(deck.high.z - deck.low.z - .60) < 1e-9,
                "Grandstand walking surface slopes or lacks room for both feet");
        decks.push_back(deck);
    }
    for (size_t first = 0; first < spectatorSoles.size(); first += 6) {
        const bool planted = std::any_of(decks.begin(), decks.end(), [&](const Deck &deck) {
            for (size_t i = first; i < first + 6; ++i) {
                const auto p = spectatorSoles[i];
                if (std::abs(p.y - deck.high.y) > 1e-8 || p.x < deck.low.x - 1e-8 ||
                    p.x > deck.high.x + 1e-8 || p.z < deck.low.z - 1e-8 || p.z > deck.high.z + 1e-8)
                    return false;
            }
            return true;
        });
        require(planted, "Spectator shoe floats, clips into a stand, or straddles a terrace edge");
    }
}
void rectangularBoards(const sjp::Hill &hill) {
    const double scale = hill.scale();
    auto ground = [&](double x, double z) {
        const double edge = std::max(0.0, std::abs(z) - 19 * scale);
        return hill.ground(x) - edge * .08 + std::sin(x * .017 + z * .013) * edge * .018;
    };
    for (const auto &faces : boardFaces) {
        require(faces.size() == 28 * 6, "Advertisements must have one rectangular face per board");
        for (size_t i = 0; i < faces.size(); i += 6) {
            // The static batch triangulates a quad as (a,b,c), (a,c,d).
            const auto a = faces[i], b = faces[i + 1], c = faces[i + 2], d = faces[i + 5];
            const auto along = b - a, vertical = d - a;
            require((faces[i + 3] - a).length() < 1e-9 && (faces[i + 4] - c).length() < 1e-9 &&
                        (a + c - b - d).length() < 1e-9,
                    "Advertisement opposite edges must be straight and parallel");
            require(std::abs(sjp::dot(along, vertical)) < 1e-9,
                    "Advertisement corners must be right angles");
            require(std::abs(along.length() - 5.8 * scale) < 1e-9 &&
                        std::abs(vertical.length() - 1.63) < 1e-9,
                    "Advertisement rotation must preserve its width and height");
            const auto up = vertical.normalized() * (vertical.y < 0 ? -1 : 1);
            const auto origin = (a + c) * .5 - up * .975;
            const double x = (79 + (i / 6 % 14) * 6.2) * scale;
            const double z = (a.z < 0 ? -1 : 1) * (std::max(25 * scale, 8.0) - 1);
            const auto tangent = sjp::Vec3{.2, ground(x + .1, z) - ground(x - .1, z), 0}.normalized();
            require(std::abs(origin.x - x) < 1e-9 &&
                        sjp::dot(along.normalized(), tangent) > 1 - 1e-6,
                    "Advertisement must follow the terrain tangent at its mounting position");
            const auto bottomLeft = vertical.y > 0 ? a : d;
            const auto bottomRight = vertical.y > 0 ? b : c;
            for (int sample = 0; sample <= 32; ++sample) {
                const auto p = bottomLeft + (bottomRight - bottomLeft) * (sample / 32.0);
                require(p.y >= ground(p.x, p.z) + .07,
                        "Rigid advertisement must stay above the existing terrain");
            }
        }
    }
}
void vertex() {
    require(primitive, "Vertex outside sjp::gfx::begin/sjp::gfx::end");
    ++vertices;
    ++drawVertices;
    if (!compiling && primitiveMode == sjp::gfx::TRIANGLE_FAN)
        ++particleVertices;
    if (compiling)
        ++lists.at(compiling).vertices;
}
void tail(std::function<void()> op) {
    if (compiling && attributes.empty())
        lists.at(compiling).tail.push_back(std::move(op));
}
void balanced() {
    require(!primitive && !compiling && attributes.empty(), "Unbalanced primitive/list/attribute stack");
    for (auto [mode, size] : matrices)
        require(size == 1, "Unbalanced matrix stack");
}
size_t cached() {
    size_t n = 0;
    for (const auto &[id, list] : lists)
        n += list.vertices;
    return n;
}
} // namespace audit
bool SDL_GetWindowSizeInPixels(SDL_Window *, int *w, int *h) {
    *w = audit::drawableWidth;
    *h = audit::drawableHeight;
    return true;
}
void sjp::gfx::initialize(SDL_Window *, bool, bool, bool) {}
void sjp::gfx::shutdown() noexcept {}
void sjp::gfx::beginFrame(int, int) {}
void sjp::gfx::captureImage(const std::string &) {}
int sjp::gfx::sampleCount() { return 4; }
void sjp::gfx::present(bool synchronous) { if (synchronous) sjp::gfx::finish(); }
void sjp::gfx::finish(void) {
    audit::command("sjp::gfx::finish inside sjp::gfx::begin", false);
    audit::require(!audit::compiling && audit::attributes.empty(),
                   "Frame completed before display list/attribute restoration");
    ++audit::completedFrames;
}
void sjp::gfx::begin(sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::begin inside sjp::gfx::begin", false);
    audit::require(!audit::primitive, "Nested sjp::gfx::begin");
    audit::primitive = true;
    audit::primitiveMode = mode;
    audit::drawVertices = 0;
}
void sjp::gfx::bindTexture(sjp::gfx::Enum target, sjp::gfx::Id texture) {
    audit::command("sjp::gfx::bindTexture inside sjp::gfx::begin", false);
    audit::require(!texture || audit::textures.count(texture), "Bind unknown texture");
    audit::boundTexture = texture;
}
void sjp::gfx::blendFunc(sjp::gfx::Enum sfactor, sjp::gfx::Enum dfactor) {
    audit::command("sjp::gfx::blendFunc inside sjp::gfx::begin", false);
}
void sjp::gfx::callList(sjp::gfx::Id list) {
    audit::command("sjp::gfx::callList inside sjp::gfx::begin", false);
    audit::require(audit::lists.count(list), "Use of missing display list");
    for (auto &op : audit::lists.at(list).tail)
        op();
}
bool sjp::gfx::boundsVisible(const std::array<float, 3> &low, const std::array<float, 3> &high) {
    for (size_t axis = 0; axis < 3; ++axis) {
        audit::finite(low[axis]);
        audit::finite(high[axis]);
        audit::require(low[axis] <= high[axis], "Invalid scenery bounds");
    }
    // Audit every generated vertex independently of GPU visibility.
    return true;
}
void sjp::gfx::clear(sjp::gfx::Flags mask) {
    audit::command("sjp::gfx::clear inside sjp::gfx::begin", false);
}
void sjp::gfx::clearColor(float red, float green, float blue, float alpha) {
    audit::command("sjp::gfx::clearColor inside sjp::gfx::begin", false);
    audit::finite(red);
    audit::finite(green);
    audit::finite(blue);
    audit::finite(alpha);
}
void sjp::gfx::color4f(float red, float green, float blue, float alpha) {
    audit::command("sjp::gfx::color4f inside sjp::gfx::begin", true);
    audit::finite(red);
    audit::finite(green);
    audit::finite(blue);
    audit::finite(alpha);
    audit::current.color = {red, green, blue, alpha};
    auto value = audit::current.color;
    audit::tail([value] { audit::current.color = value; });
}
void sjp::gfx::color4fv(const float *v) {
    audit::command("sjp::gfx::color4fv inside sjp::gfx::begin", true);
    for (int i = 0; i < 4; ++i)
        audit::finite(v[i]);
    std::copy(v, v + 4, audit::current.color.begin());
    auto value = audit::current.color;
    audit::tail([value] { audit::current.color = value; });
}
void sjp::gfx::colorMaterial(sjp::gfx::Enum face, sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::colorMaterial inside sjp::gfx::begin", false);
}
void sjp::gfx::deleteLists(sjp::gfx::Id list, int range) {
    audit::command("sjp::gfx::deleteLists inside sjp::gfx::begin", false);
    for (int i = 0; i < range; ++i)
        audit::require(audit::lists.erase(list + i) == 1, "Invalid display list deletion");
}
void sjp::gfx::deleteTextures(int n, const sjp::gfx::Id *textures) {
    audit::command("sjp::gfx::deleteTextures inside sjp::gfx::begin", false);
    for (int i = 0; i < n; ++i)
        if (textures[i])
            audit::require(audit::textures.erase(textures[i]) == 1, "Invalid texture deletion");
}
void sjp::gfx::depthMask(bool flag) {
    audit::command("sjp::gfx::depthMask inside sjp::gfx::begin", false);
    audit::depth = flag;
}
void sjp::gfx::disable(sjp::gfx::Enum cap) {
    audit::command("sjp::gfx::disable inside sjp::gfx::begin", false);
    if (cap == sjp::gfx::TEXTURE_2D)
        audit::textureEnabled = false;
}
void sjp::gfx::enable(sjp::gfx::Enum cap) {
    audit::command("sjp::gfx::enable inside sjp::gfx::begin", false);
    if (cap == sjp::gfx::TEXTURE_2D)
        audit::textureEnabled = true;
}
void sjp::gfx::end(void) {
    audit::command("sjp::gfx::end inside sjp::gfx::begin", true);
    audit::require(audit::primitive, "Unmatched sjp::gfx::end");
    audit::primitive = false;
    audit::maximumDraw = std::max(audit::maximumDraw, audit::drawVertices);
}
void sjp::gfx::endList(void) {
    audit::command("sjp::gfx::endList inside sjp::gfx::begin", false);
    audit::require(audit::compiling && !audit::primitive, "Unmatched sjp::gfx::endList");
    audit::maximumCachedVertices = std::max(audit::maximumCachedVertices, audit::cached());
    audit::compiling = 0;
}
void sjp::gfx::fogf(sjp::gfx::Enum pname, float param) {
    audit::command("sjp::gfx::fogf inside sjp::gfx::begin", false);
    audit::finite(param);
    if (pname == sjp::gfx::FOG_START)
        audit::fogStart = param;
    else if (pname == sjp::gfx::FOG_END)
        audit::fogEnd = param;
}
void sjp::gfx::fogfv(sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::fogfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void sjp::gfx::fogi(sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::fogi inside sjp::gfx::begin", false);
}
void sjp::gfx::frustum(double left, double right, double bottom, double top, double near_val,
                          double far_val) {
    audit::command("sjp::gfx::frustum inside sjp::gfx::begin", false);
    audit::finite(left);
    audit::finite(right);
    audit::finite(bottom);
    audit::finite(top);
    audit::finite(near_val);
    audit::finite(far_val);
    audit::require(left != right && bottom != top && near_val > 0 && far_val > near_val,
                   "Invalid perspective projection");
}
sjp::gfx::Id sjp::gfx::genLists(int range) {
    audit::command("sjp::gfx::genLists inside sjp::gfx::begin", false);
    audit::require(range > 0, "Invalid list range");
    auto id = audit::nextList;
    audit::nextList += range;
    return id;
}
void sjp::gfx::genTextures(int n, sjp::gfx::Id *textures) {
    audit::command("sjp::gfx::genTextures inside sjp::gfx::begin", false);
    for (int i = 0; i < n; ++i) {
        textures[i] = audit::nextTexture++;
        audit::textures.insert(textures[i]);
    }
}
void sjp::gfx::getFloatv(sjp::gfx::Enum pname, float *params) {
    audit::command("sjp::gfx::getFloatv inside sjp::gfx::begin", false);
    if (pname == sjp::gfx::CURRENT_COLOR)
        std::copy(audit::current.color.begin(), audit::current.color.end(), params);
    else if (pname == sjp::gfx::CURRENT_NORMAL)
        std::copy(audit::current.normal.begin(), audit::current.normal.end(), params);
    else if (pname == sjp::gfx::CURRENT_TEXTURE_COORDS)
        std::copy(audit::current.uv.begin(), audit::current.uv.end(), params);
    else
        throw std::runtime_error("Unsupported audit sjp::gfx::getFloatv");
}
void sjp::gfx::getIntegerv(sjp::gfx::Enum pname, int *params) {
    audit::command("sjp::gfx::getIntegerv inside sjp::gfx::begin", false);
    audit::require(pname == sjp::gfx::UNPACK_ALIGNMENT, "Unsupported audit sjp::gfx::getIntegerv");
    *params = 1;
}
const unsigned char *sjp::gfx::getString(sjp::gfx::Enum name) {
    audit::command("sjp::gfx::getString inside sjp::gfx::begin", false);
    return reinterpret_cast<const unsigned char *>("CPU command audit; no graphics context");
}
void sjp::gfx::hint(sjp::gfx::Enum target, sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::hint inside sjp::gfx::begin", false);
}
void sjp::gfx::lightModelfv(sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::lightModelfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
    if (pname == sjp::gfx::LIGHT_MODEL_AMBIENT)
        std::copy_n(params, 3, audit::globalAmbient.begin());
}
void sjp::gfx::arenaLights(const std::array<SpotLight, ARENA_LIGHT_COUNT> &lights, float firstX,
                         float spacing, int columns, float strength, float attenuationDistance) {
    audit::command("arenaLights inside a primitive", false);
    for (const auto &light : lights)
        for (const auto &values : {light.positionRange, light.directionCone})
            for (float value : values)
                audit::finite(value);
    for (float value : {firstX, spacing, strength, attenuationDistance})
        audit::finite(value);
    audit::require(columns >= 1 && columns <= ARENA_LIGHT_COLUMNS &&
                       spacing > 0 && attenuationDistance > 0 && strength >= 0,
                   "Invalid arena light field");
    audit::spotLights = lights;
    audit::spotStrength = strength;
    audit::spotColumns = columns;
}
void sjp::gfx::inrunLights(const std::array<SpotLight, ARENA_LIGHT_COUNT> &lights, float firstX,
                         float spacing, int columns, float strength, float attenuationDistance) {
    audit::command("inrunLights inside a primitive", false);
    audit::require(columns >= 1 && columns <= 5 && spacing > 0 && strength >= 0 && attenuationDistance > 0,
                   "Invalid inrun light field");
    for (const auto &light : lights)
        for (const auto &values : {light.positionRange, light.directionCone})
            for (float value : values)
                audit::finite(value);
    audit::inrunSpots = lights;
    audit::inrunStrength = strength;
    audit::inrunColumns = columns;
}
void sjp::gfx::signalLight(const SpotLight &light, const std::array<float, 4> &color,
                           float attenuationDistance) {
    audit::command("signalLight inside a primitive", false);
    for (const auto &values : {light.positionRange, light.directionCone, color})
        for (float value : values)
            audit::finite(value);
    audit::require(light.positionRange[3] > 0 && attenuationDistance > 0 && color[3] >= 0 && color[3] <= 1,
                   "Invalid signal light range or intensity");
    audit::startLight = light;
    audit::startLightColor = color;
}
void sjp::gfx::lightfv(sjp::gfx::Enum light, sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::lightfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
    if (light == sjp::gfx::LIGHT0 && pname == sjp::gfx::POSITION)
        std::copy_n(params, 3, audit::primaryLight.begin());
}
void sjp::gfx::lineWidth(float width) {
    audit::command("sjp::gfx::lineWidth inside sjp::gfx::begin", false);
    audit::finite(width);
}
void sjp::gfx::loadIdentity(void) {
    audit::command("sjp::gfx::loadIdentity inside sjp::gfx::begin", false);
}
void sjp::gfx::materialf(sjp::gfx::Enum face, sjp::gfx::Enum pname, float param) {
    audit::command("sjp::gfx::materialf inside sjp::gfx::begin", false);
    audit::finite(param);
}
void sjp::gfx::materialfv(sjp::gfx::Enum face, sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::materialfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
    if (pname == sjp::gfx::EMISSION) {
        std::copy_n(params, 4, audit::currentEmission.begin());
        audit::signalMaterial = params[3] == 0;
    }
}
void sjp::gfx::matrixMode(sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::matrixMode inside sjp::gfx::begin", false);
    audit::require(audit::matrices.count(mode), "Invalid matrix mode");
    audit::matrix = mode;
}
void sjp::gfx::multMatrixf(const float *m) {
    audit::command("sjp::gfx::multMatrixf inside sjp::gfx::begin", false);
    for (int i = 0; i < 16; ++i)
        audit::finite(m[i]);
}
void sjp::gfx::newList(sjp::gfx::Id list, sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::newList inside sjp::gfx::begin", false);
    audit::require(!audit::compiling && list != 0 && mode == sjp::gfx::COMPILE_AND_EXECUTE,
                   "Invalid list compilation");
    audit::compiling = list;
    audit::lists[list] = {};
    audit::maximumLists = std::max(audit::maximumLists, audit::lists.size());
}
void sjp::gfx::normal3d(double nx, double ny, double nz) {
    audit::command("sjp::gfx::normal3d inside sjp::gfx::begin", true);
    audit::finite(nx);
    audit::finite(ny);
    audit::finite(nz);
    audit::current.normal = {float(nx), float(ny), float(nz)};
    auto value = audit::current.normal;
    audit::tail([value] { audit::current.normal = value; });
}
void sjp::gfx::normal3dv(const double *v) {
    audit::command("sjp::gfx::normal3dv inside sjp::gfx::begin", true);
    for (int i = 0; i < 3; ++i) {
        audit::finite(v[i]);
        audit::current.normal[i] = v[i];
    }
    auto value = audit::current.normal;
    audit::tail([value] { audit::current.normal = value; });
}
void sjp::gfx::ortho(double left, double right, double bottom, double top, double near_val,
                        double far_val) {
    audit::command("sjp::gfx::ortho inside sjp::gfx::begin", false);
    audit::finite(left);
    audit::finite(right);
    audit::finite(bottom);
    audit::finite(top);
    audit::finite(near_val);
    audit::finite(far_val);
    audit::require(left != right && bottom != top && near_val != far_val, "Invalid orthographic projection");
}
void sjp::gfx::pixelStorei(sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::pixelStorei inside sjp::gfx::begin", false);
}
void sjp::gfx::polygonOffset(float factor, float units) {
    audit::command("sjp::gfx::polygonOffset inside sjp::gfx::begin", false);
    audit::finite(factor);
    audit::finite(units);
}
void sjp::gfx::popAttrib(void) {
    audit::command("sjp::gfx::popAttrib inside sjp::gfx::begin", false);
    audit::require(!audit::attributes.empty(), "Attribute stack underflow");
    auto saved = audit::attributes.back();
    audit::attributes.pop_back();
    if (saved.mask & sjp::gfx::CURRENT_BIT)
        audit::current = saved.value;
    if (saved.mask & sjp::gfx::DEPTH_BUFFER_BIT)
        audit::depth = saved.depth;
    if (saved.mask & sjp::gfx::LIGHTING_BIT) {
        audit::currentEmission = saved.emission;
        audit::signalMaterial = saved.signal;
    }
    if (saved.mask & (sjp::gfx::ENABLE_BIT | sjp::gfx::TEXTURE_BIT))
        audit::textureEnabled = saved.textureEnabled;
    if (saved.mask & sjp::gfx::TEXTURE_BIT)
        audit::boundTexture = saved.boundTexture;
}
void sjp::gfx::popMatrix(void) {
    audit::command("sjp::gfx::popMatrix inside sjp::gfx::begin", false);
    audit::require(--audit::matrices[audit::matrix] >= 1, "Matrix stack underflow");
}
void sjp::gfx::pushAttrib(sjp::gfx::Flags mask) {
    audit::command("sjp::gfx::pushAttrib inside sjp::gfx::begin", false);
    audit::attributes.push_back({mask, audit::current, audit::depth, audit::currentEmission,
                                 audit::signalMaterial, audit::textureEnabled, audit::boundTexture});
    audit::require(audit::attributes.size() <= 16, "Attribute stack exceeds guaranteed capacity");
}
void sjp::gfx::pushMatrix(void) {
    audit::command("sjp::gfx::pushMatrix inside sjp::gfx::begin", false);
    ++audit::matrices[audit::matrix];
    audit::require(audit::matrices[audit::matrix] <= (audit::matrix == sjp::gfx::MODELVIEW ? 32 : 2),
                   "Matrix stack overflow");
}

void sjp::gfx::scalef(float x, float y, float z) {
    audit::command("sjp::gfx::scalef inside sjp::gfx::begin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void sjp::gfx::shadeModel(sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::shadeModel inside sjp::gfx::begin", false);
}
void sjp::gfx::texCoord2d(double s, double t) {
    audit::command("sjp::gfx::texCoord2d inside sjp::gfx::begin", true);
    audit::finite(s);
    audit::finite(t);
    audit::current.uv = {float(s), float(t), 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void sjp::gfx::texCoord2dv(const double *v) {
    audit::command("sjp::gfx::texCoord2dv inside sjp::gfx::begin", true);
    for (int i = 0; i < 2; ++i)
        audit::finite(v[i]);
    audit::current.uv = {float(v[0]), float(v[1]), 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void sjp::gfx::texCoord2f(float s, float t) {
    audit::command("sjp::gfx::texCoord2f inside sjp::gfx::begin", true);
    audit::finite(s);
    audit::finite(t);
    audit::current.uv = {s, t, 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void sjp::gfx::texEnvi(sjp::gfx::Enum target, sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::texEnvi inside sjp::gfx::begin", false);
}
void sjp::gfx::texImage2D(sjp::gfx::Enum target, int level, int internalFormat, int width, int height,
                             int border, sjp::gfx::Enum format, sjp::gfx::Enum type, const void *pixels) {
    audit::command("sjp::gfx::texImage2D inside sjp::gfx::begin", false);
    audit::require(width > 0 && height > 0 && pixels, "Invalid texture upload");
    audit::textureBytes += size_t(width) * height;
}
void sjp::gfx::texParameteri(sjp::gfx::Enum target, sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::texParameteri inside sjp::gfx::begin", false);
}
void sjp::gfx::translated(double x, double y, double z) {
    audit::command("sjp::gfx::translated inside sjp::gfx::begin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
    if (audit::captureCamera) {
        audit::cameraEye = {-x, -y, -z};
        audit::captureCamera = false;
    }
}
void sjp::gfx::translatef(float x, float y, float z) {
    audit::command("sjp::gfx::translatef inside sjp::gfx::begin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void sjp::gfx::vertex2f(float x, float y) {
    audit::position(x, y, 0);
    audit::command("sjp::gfx::vertex2f inside sjp::gfx::begin", true);
    audit::finite(x);
    audit::finite(y);
    audit::vertex();
}
void sjp::gfx::vertex3d(double x, double y, double z) {
    audit::position(x, y, z);
    audit::command("sjp::gfx::vertex3d inside sjp::gfx::begin", true);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
    audit::vertex();
}
void sjp::gfx::vertex3dv(const double *v) {
    audit::position(v[0], v[1], v[2]);
    audit::command("sjp::gfx::vertex3dv inside sjp::gfx::begin", true);
    for (int i = 0; i < 3; ++i)
        audit::finite(v[i]);
    audit::vertex();
}
void sjp::gfx::viewport(int x, int y, int width, int height) {
    audit::command("sjp::gfx::viewport inside sjp::gfx::begin", false);
}

int main() {
    try {
        audit::startSignalLighting();
        audit::singleShadow();
        audit::floodlightsAndLandingShadow();
        audit::inrunConstruction();
        size_t snowy = 0;
        constexpr size_t weatherSamples = 100000;
        for (uint32_t i = 0; i < weatherSamples; ++i) {
            const uint32_t seed = sjp::originalWindSeedFromDosTime(
                uint8_t(i / 360000), uint8_t(i / 6000 % 60), uint8_t(i / 100 % 60), uint8_t(i % 100));
            snowy += sjp::visual::snowForJump(seed);
        }
        audit::require(snowy > 29000 && snowy < 31000, "Snowfall probability differs from 30 percent");
        for (double angle : {0.0, sjp::PI / 4, sjp::PI / 2, sjp::PI, -sjp::PI / 2, -sjp::PI / 4}) {
            sjp::Frame f;
            f.windAngle = float(angle);
            f.windSpeed = 3;
            const auto wind = sjp::visual::windDirection(f);
            const auto tail = sjp::visual::windsockPoint(f, 2, 1);
            audit::require(sjp::dot(tail.normalized(), wind) > .98,
                           "Windsock disagrees with displayed wind direction");
            const auto drift = sjp::visual::snowDrift(wind * f.windSpeed, 10);
            audit::require(sjp::dot(drift, wind) >= 0 && drift.z == 0, "Snow drifts against displayed wind");
            f.windSpeed = 0;
            const auto calm = sjp::visual::windsockPoint(f, 2, 1);
            audit::require(calm.x == 0 && calm.y < 0 && calm.z == 0, "Calm windsock does not hang down");
        }
        std::cout << "weather_samples=" << weatherSamples << " snowy_jumps=" << snowy << '\n';
        size_t frames = 0, leaks = 0;
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            for (int hi = 0; hi < int(sjp::HILL_COUNT); ++hi) {
                if (hi == 0)
                    audit::protectedTexture = audit::nextTexture;
                sjp::Simulation s(hi, 0);
                s.resetOriginal(hi, 12345, 98765);
                s.setWind(0, 0);
                r.setJumpWeather(12345 + uint32_t(hi));
                std::vector<sjp::Frame> samples{s.frame()};
                for (int t = 0; t < 14000 && s.phase != sjp::Phase::Finished; ++t) {
                    sjp::Input in;
                    in.start = s.phase == sjp::Phase::Ready;
                    s.step(sjp::ORIGINAL_TICK, in);
                    if (t % 173 == 0 || s.phase == sjp::Phase::Finished)
                        samples.push_back(s.frame());
                }
                sjp::Simulation healthy(hi, 0);
                healthy.resetOriginal(hi, 12345, 98765);
                healthy.setWind(0, 0);
                for (int t = 0; t < 14000 && healthy.phase != sjp::Phase::Finished; ++t) {
                    healthy.step(sjp::ORIGINAL_TICK, sjp::autopilot(healthy));
                    if (t % 173 == 0 || healthy.phase == sjp::Phase::Finished)
                        samples.push_back(healthy.frame());
                }
                r.resetCamera();
                for (auto &faces : audit::boardFaces)
                    faces.clear();
                audit::standTops.clear();
                audit::spectatorSoles.clear();
                audit::floodlightGlassVertices = 0;
                audit::captureBoards = true;
                audit::captureSnow = true;
                audit::snowHill = &s.hill();
                audit::snowHash = 1469598103934665603ULL;
                audit::snowVertices = 0;
                audit::snowLighting.clear();
                r.begin(s.hill(), s.frame(), 4, 0, true);
                audit::captureSnow = false;
                audit::snowHill = nullptr;
                audit::protectedSnow(hi);
                audit::smoothSnow(s.hill());
                audit::captureBoards = false;
                audit::rectangularBoards(s.hill());
                audit::flatGrandstands();
                audit::require(audit::floodlightGlassVertices ==
                                   size_t(sjp::visual::floodlightColumns(s.hill()) * 2 * 6 * 6),
                               "Drawn floodlight fixtures do not match the active light sources");
                r.overlay();
                r.finish();
                audit::balanced();
                ++frames;
                sjp::visual::ChaseCameraPath chasePath;
                chasePath.build(s.hill());
                for (const auto &f : samples)
                    for (int camera = 0; camera < 3; ++camera) {
                        r.resetCamera();
                        audit::captureCamera = camera == 1;
                        r.begin(s.hill(), f, f.elapsed, camera, false, true, &f);
                        if (camera == 1)
                            audit::require(!audit::captureCamera &&
                                               (audit::cameraEye - chasePath.eye(f.x)).length() < 1e-9,
                                           "Camera 2 did not follow its finished hill rail");
                        r.overlay();
                        r.glassPanel(20, 20, 280, 100);
                        r.text(s.hill().name, 35, 35, 20);
                        r.finish();
                        audit::balanced();
                        audit::require(audit::depth, "Depth writes leaked across frame");
                        ++frames;
                    }
                auto id = audit::lists.rbegin()->first;
                audit::current.color = {.2f, .3f, .4f, .5f};
                audit::current.normal = {.6f, .7f, .8f};
                audit::current.uv = {.9f, .8f, 0, 1};
                auto before = audit::current;
                sjp::gfx::callList(id);
                if (before.color != audit::current.color || before.normal != audit::current.normal ||
                    before.uv != audit::current.uv)
                    ++leaks;
            }
            std::cout << "frames=" << frames << " commands=" << audit::commands
                      << " compiled_vertices=" << audit::vertices
                      << " maximum_draw_vertices=" << audit::maximumDraw
                      << " maximum_cached_lists=" << audit::maximumLists
                      << " maximum_cached_vertices=" << audit::maximumCachedVertices
                      << " uploaded_texture_bytes=" << audit::textureBytes
                      << " position_hash=" << audit::positionHash << " list_state_leaks=" << leaks << '\n';
            sjp::Simulation s(0, 0);
            for (const auto &size :
                 std::array<std::array<int, 2>, 4>{{{0, 0}, {960, 0}, {0, 600}, {960, 600}}}) {
                audit::drawableWidth = size[0];
                audit::drawableHeight = size[1];
                r.begin(s.hill(), s.frame(), 0, 0, false);
                r.overlay();
                r.text("minimized", 10, 10, 20);
                r.finish();
                audit::balanced();
                audit::require(r.width > 0 && r.height > 0 && std::isfinite(r.uiScale) && r.uiScale > 0,
                               "Invalid drawable/UI size");
            }
        }
        audit::require(audit::lists.empty() && audit::textures.empty(),
                       "Renderer leaked graphics resources at destruction");
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            auto visit = [&](int hi) {
                sjp::Simulation s(hi, 0);
                r.begin(s.hill(), s.frame(), 0, 0, false);
                r.overlay();
                r.finish();
                audit::balanced();
                audit::require(audit::lists.size() <= 2, "Cache limit exceeded");
            };
            visit(0);
            const auto first = audit::nextList - 1;
            visit(1);
            const auto second = audit::nextList - 1;
            visit(0);
            audit::require(audit::nextList == second + 1, "Cache hit rebuilt the current venue");
            visit(2);
            const auto third = audit::nextList - 1;
            audit::require(audit::lists.count(first) && !audit::lists.count(second) &&
                               audit::lists.count(third),
                           "Cache did not evict least recently used venue");
            visit(1);
            audit::require(audit::nextList == third + 2 && !audit::lists.count(first) &&
                               audit::lists.count(third),
                           "Evicted venue was not rebuilt correctly");
            const auto beforeCompletion = audit::completedFrames;
            r.setSynchronousRendering(true);
            visit(0);
            visit(1);
            audit::require(audit::completedFrames == beforeCompletion + 2,
                           "Synchronous frames left graphics work pending");
            r.setSynchronousRendering(false);
            visit(2);
            audit::require(audit::completedFrames == beforeCompletion + 2,
                           "Normal rendering unexpectedly forced completion");
        }
        audit::require(audit::lists.empty() && audit::textures.empty(),
                       "Repeated renderer lifetime leaked resources");
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            sjp::Simulation sim(2, 0);
            int camera = 0;
            auto settle = [&](const std::vector<double> &cadence) {
                auto f = sim.frame();
                r.resetCamera();
                r.begin(sim.hill(), f, 0, camera, false);
                r.finish();
                f.x += 10;
                f.y -= 4;
                double elapsed = 0;
                for (size_t i = 0; elapsed < .3 - 1e-12; ++i) {
                    elapsed += std::min(cadence[i % cadence.size()], .3 - elapsed);
                    audit::captureCamera = true;
                    r.begin(sim.hill(), f, elapsed, camera, false);
                    r.finish();
                    audit::require(!audit::captureCamera, "Camera transform was not recorded");
                    audit::balanced();
                }
                return audit::cameraEye;
            };
            for (camera = 0; camera <= 1; ++camera) {
                const auto at60 = settle({1. / 60});
                for (const auto &cadence : {std::vector<double>{1. / 30}, {1. / 120}, {1. / 144},
                                           {.004, .013, .007, .021, .005}})
                    audit::require((settle(cadence) - at60).length() < 1e-9,
                                   "Rendered camera response changes with frame cadence");
            }
            camera = 1;
            const auto before = settle({1. / 60});
            auto f = sim.frame();
            f.x += 10;
            f.y += 40;
            audit::captureCamera = true;
            r.begin(sim.hill(), f, .3, camera, false);
            r.finish();
            audit::require((audit::cameraEye - before).length() < 1e-9,
                           "Paused camera rail moved or reacted to jumper height");
        }
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            sjp::Simulation sim(2, 0);
            uint32_t wet = 0, dry = 0;
            while (!sjp::visual::snowForJump(wet))
                ++wet;
            while (sjp::visual::snowForJump(dry))
                ++dry;
            const auto input = sim.frame();
            const auto hashAt = [&](uint32_t seed, float windAngle, float elapsed, double wallTime,
                                    int camera = 0, float height = 0) {
                r.setJumpWeather(seed, {sjp::environment::TimeOfDay::Day, sjp::environment::Weather::Random});
                auto f = input;
                f.windSpeed = 3;
                f.windAngle = windAngle;
                f.elapsed = elapsed;
                f.y += height;
                r.resetCamera();
                r.begin(sim.hill(), f, wallTime, camera, false); // Ensure the terrain is cached.
                r.resetCamera();
                const auto vertices = audit::particleVertices;
                audit::particlePositionHash = 1469598103934665603ULL;
                r.begin(sim.hill(), f, wallTime, camera, false);
                audit::balanced();
                // The daytime view here contains powder/snow fan meshes;
                // skip trees, flags and crowd animation driven by wall time.
                return std::pair<size_t, uint64_t>{audit::particleVertices - vertices,
                                                   audit::particlePositionHash};
            };
            const auto noSnow = hashAt(dry, 0, 0, 0);
            const auto snow = hashAt(wet, 0, 0, 0);
            audit::require(snow.first > noSnow.first && snow.first - noSnow.first <= 1800,
                           "Light snowfall missing or particle budget exceeded");
            audit::require(hashAt(wet, 0, 0, 90) == snow, "Paused snowfall changes with wall clock");
            audit::require(hashAt(wet, float(sjp::PI), 0, 0).second != snow.second,
                           "Snowfall ignores wind direction");
            for (float seconds : {1.f, 30.f, 80.f, 150.f}) {
                const auto snowyFrame = hashAt(wet, 0, seconds, seconds);
                const auto clearFrame = hashAt(dry, 0, seconds, seconds);
                audit::require(snowyFrame.first > clearFrame.first, "Snowfall stops during a selected jump");
            }
            for (int camera = 0; camera < 3; ++camera)
                for (float height : {0.f, 12.f}) {
                    const auto wetView = hashAt(wet, 0, 35, 35, camera, height);
                    const auto dryView = hashAt(dry, 0, 35, 35, camera, height);
                    audit::require(wetView.first > dryView.first && wetView.first - dryView.first <= 1800,
                                   "Snow must remain visible and bounded in every camera and above the hill");
                    audit::require(hashAt(wet, 0, 35, 90, camera, height) == wetView,
                                   "Paused snowfall changes in another camera or above the hill");
                }
            const auto after = sim.frame();
            audit::require(input.x == after.x && input.y == after.y && input.pose == after.pose &&
                               input.windAngle == after.windAngle && input.windSpeed == after.windSpeed &&
                               sim.originalCounter == 1,
                           "Visual weather mutated the simulation");
        }
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            sjp::Simulation sim(2, 0);
            const auto before = sim.frame();
            const sjp::visual::StartSignal layout(sim.hill(), before);
            audit::signalCenters = {layout.lens(0), layout.lens(1)};
            audit::signalFront = layout.front;
            audit::captureSignal = true;
            const auto signalAt = [&](sjp::Phase phase, float elapsed, double wallTime) {
                auto frame = before;
                frame.phase = int32_t(phase);
                frame.elapsed = elapsed;
                audit::signalVertices = 0;
                audit::signalLensVertices = {};
                audit::signalEmissions = {};
                r.begin(sim.hill(), frame, wallTime, 0, false);
                r.finish();
                audit::balanced();
                audit::require(audit::signalVertices == 212 &&
                                   audit::signalLensVertices == std::array<size_t, 2>{106, 106} &&
                                   !audit::signalMaterial,
                               "Signal lens lost its material or failed to restore it");
                const sjp::visual::StartSignal expected(sim.hill(), frame);
                audit::require(audit::startLight.positionRange == expected.light().positionRange &&
                                   audit::startLight.directionCone == expected.light().directionCone &&
                                   audit::startLightColor == expected.lightColor(),
                               "Signal's real light does not follow its lens or blink");
                const int active = phase == sjp::Phase::Ready ? 1 : 0;
                audit::require(audit::signalEmissions[size_t(1 - active)] == std::array<float, 4>{},
                               "Both signal lenses glow at once");
                return audit::signalEmissions[size_t(active)];
            };
            for (int time = 0; time < 4; ++time)
                for (int weather = 0; weather < 4; ++weather) {
                    r.setEnvironment({sjp::environment::TimeOfDay(time), sjp::environment::Weather(weather), 0});
                    audit::require(signalAt(sjp::Phase::Ready, 0, 0) ==
                                       std::array<float, 4>{.035f, .47f, .07f, 0},
                                   "Green signal emission changes with time or weather");
                    const auto off = signalAt(sjp::Phase::Ready, 6, 6);
                    audit::require(off == std::array<float, 4>{0, 0, 0, 0} &&
                                       signalAt(sjp::Phase::Ready, 6, 100) == off,
                                   "Blink's off phase glows or changes while paused");
                    audit::require(signalAt(sjp::Phase::Inrun, 6, 6) ==
                                       std::array<float, 4>{.48f, .016f, .008f, 0},
                                   "Signal does not use red emission after the start");
                }
            audit::captureSignal = false;
            r.setEnvironment({});
            r.begin(sim.hill(), before, 0, 0, false);
            r.finish();
            const auto cached = audit::nextList;
            for (int time = 0; time < 4; ++time)
                for (int weather = 0; weather < 4; ++weather) {
                    const sjp::environment::Conditions conditions{
                        sjp::environment::TimeOfDay(time), sjp::environment::Weather(weather), 98765};
                    r.setEnvironment(conditions, true);
                    r.resetCamera();
                    r.begin(sim.hill(), before, 0, 0, false);
                    r.finish();
                    audit::balanced();
                    const auto expected = sjp::visual::environmentLight(conditions);
                    const sjp::visual::FloodlightField expectedSpots(sim.hill(), conditions.time);
                    sjp::visual::InrunLayout expectedInrun;
                    expectedInrun.build(sim.hill());
                    audit::require(audit::inrunSpots[0].positionRange == expectedInrun.lights[0].positionRange &&
                                       audit::inrunColumns == expectedInrun.lightColumns &&
                                       std::abs(audit::inrunStrength -
                                                sjp::visual::InrunLayout::strength(conditions.time)) < 1e-6,
                                   "Inrun lights ignore the hill, cached geometry or time of day");
                    audit::require(std::abs(audit::spotStrength - expectedSpots.strength) < 1e-6 &&
                                       audit::spotColumns == expectedSpots.columns &&
                                       std::equal(audit::spotLights.begin(), audit::spotLights.end(),
                                                  expectedSpots.lights.begin(), [](const auto &a, const auto &b) {
                                                      return a.positionRange == b.positionRange &&
                                                             a.directionCone == b.directionCone;
                                                  }),
                                   "Cached hill uses stale floodlights after changing conditions");
                    audit::require(r.environmentConditions() == conditions && audit::nextList == cached,
                                   "Changing atmosphere rebuilt hill geometry or reselected conditions");
                    audit::require(std::abs(audit::primaryLight[0] - expected.direction.x) < 1e-6 &&
                                       std::abs(audit::primaryLight[1] - expected.direction.y) < 1e-6 &&
                                       std::abs(audit::primaryLight[2] - expected.direction.z) < 1e-6 &&
                                       std::abs(audit::globalAmbient[0] - expected.ambient.x) < 1e-6 &&
                                       std::abs(audit::fogEnd - expected.fogEnd * sim.hill().scale()) < 1e-3,
                                   "Renderer ignored the chosen time or weather lighting");
                }
            const auto after = sim.frame();
            audit::require(before.x == after.x && before.y == after.y && before.pose == after.pose &&
                               before.windSpeed == after.windSpeed && before.windAngle == after.windAngle &&
                               sim.originalCounter == 1,
                           "Time or cosmetic weather affected jumping physics or wind");
            r.setJumpWeather(98765);
            audit::require(r.environmentConditions().time == sjp::environment::TimeOfDay::Day &&
                               r.environmentConditions().weather == sjp::environment::Weather::Sunny,
                           "A default jump selected random weather instead of day and sun");
        }
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            for (int hi = 0; hi < int(sjp::HILL_COUNT); ++hi) {
                sjp::Simulation sim(hi, 0);
                const auto &hill = sim.hill();
                r.setHillRecord(0);
                r.begin(hill, sim.frame(), 0, 0, false);
                r.finish();
                const auto terrainCache = audit::nextList;
                const double distance = hill.k + 13.5;
                r.setHillRecord(distance);
                audit::captureRecordList = audit::nextList;
                audit::recordVertices.clear();
                r.begin(hill, sim.frame(), 0, 0, false);
                r.finish();
                audit::captureRecordList = 0;
                audit::require(audit::nextList == terrainCache + 1 && !audit::recordVertices.empty(),
                               "Record rebuilt terrain or did not create its small marker mesh");
                const sjp::visual::CameraSurface snow(hill);
                double lowX = 1e9, highX = -1e9;
                for (const auto &p : audit::recordVertices) {
                    audit::require(std::abs(p.y - snow.height(p.x, p.z)) < 1e-7,
                                   "Blue record spray floats above or sinks into snow");
                    lowX = std::min(lowX, p.x);
                    highX = std::max(highX, p.x);
                }
                audit::require(std::abs((lowX + highX) / 2 - hill.atDistance(distance)) < 1e-7,
                               "Blue record marker is not at the measured record distance");
                const auto cached = audit::nextList;
                r.begin(hill, sim.frame(), 1, 0, false);
                r.finish();
                audit::require(audit::nextList == cached, "Record marker geometry rebuilt every frame");
                r.setHillRecord(distance + .5);
                r.begin(hill, sim.frame(), 2, 0, false);
                r.finish();
                audit::require(audit::nextList == cached + 1 && audit::lists.size() <= 3,
                               "Improved record rebuilt terrain or leaked the previous marker");
                r.setHillRecord(0);
                r.begin(hill, sim.frame(), 3, 0, false);
                r.finish();
                audit::require(audit::lists.size() <= 2, "Empty record left its marker visible/cached");
                audit::balanced();
                audit::require(audit::depth, "Record overlay leaked depth-write state");
            }
        }
        audit::require(audit::lists.empty() && audit::textures.empty(), "Record marker leaked GPU resources");
        audit::require(leaks == 0, "Cached terrain freezes the caller's current color/normal/UV");
        audit::require(audit::maximumLists <= 3, "Cache exceeds two venues and one small record marker");
        std::cout << "PASS: finite commands, balanced stacks, bounded cache, live current attributes, "
                     "zero-size window, resource cleanup, rectangular terrain-aligned advertisements "
                     "on all 32 hills; no GPU used\n";
    } catch (const std::exception &e) {
        std::cerr << "Command audit: " << e.what() << '\n';
        return 1;
    }
}
