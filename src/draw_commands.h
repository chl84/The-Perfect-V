#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <string>

// The scene's existing construction order is retained by this command recorder.
// These are game-local commands, not OpenGL entry points. The implementation
// records triangle meshes and submits them to SDL3's explicitly selected Vulkan
// GPU backend. The CPU audit supplies its own implementation of this interface.
namespace sjp::gfx {
using Enum = uint32_t;
using Flags = uint32_t;
using Id = uint32_t;

// Five paired mast columns. Only the two adjacent columns illuminate a vertex.
inline constexpr int ARENA_LIGHT_COLUMNS = 5;
inline constexpr int ARENA_LIGHT_COUNT = ARENA_LIGHT_COLUMNS * 2;
struct SpotLight {
    std::array<float, 4> positionRange{}; // world position, inverse squared range
    std::array<float, 4> directionCone{}; // unit aim direction, outer cone cosine
};
void arenaLights(const std::array<SpotLight, ARENA_LIGHT_COUNT> &, float firstX, float spacing,
                 float strength, float attenuationDistance);

inline constexpr Enum ALPHA = 0x1906;
inline constexpr Enum AMBIENT = 0x1200;
inline constexpr Enum AMBIENT_AND_DIFFUSE = 0x1602;
inline constexpr Enum BLEND = 0x0BE2;
inline constexpr Enum CLAMP_TO_EDGE = 0x812F;
inline constexpr Enum COLOR_BUFFER_BIT = 0x00004000;
inline constexpr Enum COLOR_MATERIAL = 0x0B57;
inline constexpr Enum COMPILE_AND_EXECUTE = 0x1301;
inline constexpr Enum CULL_FACE = 0x0B44;
inline constexpr Enum CURRENT_BIT = 0x00000001;
inline constexpr Enum CURRENT_COLOR = 0x0B00;
inline constexpr Enum CURRENT_NORMAL = 0x0B02;
inline constexpr Enum CURRENT_TEXTURE_COORDS = 0x0B03;
inline constexpr Enum DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr Enum DEPTH_TEST = 0x0B71;
inline constexpr Enum DIFFUSE = 0x1201;
inline constexpr Enum EMISSION = 0x1600;
// EMISSION's fourth component selects arena intensity tracking: 0 for an
// independently powered indicator, 1 for arena lamp glass. It is not opacity.
inline constexpr Enum ENABLE_BIT = 0x00002000;
inline constexpr Enum FALSE = 0;
inline constexpr Enum FOG = 0x0B60;
inline constexpr Enum FOG_COLOR = 0x0B66;
inline constexpr Enum FOG_END = 0x0B64;
inline constexpr Enum FOG_HINT = 0x0C54;
inline constexpr Enum FOG_MODE = 0x0B65;
inline constexpr Enum FOG_START = 0x0B63;
inline constexpr Enum FRONT_AND_BACK = 0x0408;
inline constexpr Enum LIGHT0 = 0x4000;
inline constexpr Enum LIGHT1 = 0x4001;
inline constexpr Enum LIGHTING = 0x0B50;
inline constexpr Enum LIGHTING_BIT = 0x00000040;
inline constexpr Enum LIGHT_MODEL_AMBIENT = 0x0B53;
inline constexpr Enum LINEAR = 0x2601;
inline constexpr Enum LINEAR_MIPMAP_LINEAR = 0x2703;
inline constexpr Enum LINES = 0x0001;
inline constexpr Enum LINE_BIT = 0x00000004;
inline constexpr Enum LUMINANCE = 0x1909;
inline constexpr Enum MODELVIEW = 0x1700;
inline constexpr Enum MODULATE = 0x2100;
inline constexpr Enum NICEST = 0x1102;
inline constexpr Enum NORMALIZE = 0x0BA1;
inline constexpr Enum NO_ERROR = 0;
inline constexpr Enum ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr Enum PACK_ALIGNMENT = 0x0D05;
inline constexpr Enum POLYGON_BIT = 0x00000008;
inline constexpr Enum POLYGON_OFFSET_FILL = 0x8037;
inline constexpr Enum POSITION = 0x1203;
inline constexpr Enum PROJECTION = 0x1701;
inline constexpr Enum QUADS = 0x0007;
inline constexpr Enum QUAD_STRIP = 0x0008;
inline constexpr Enum RENDERER = 0x1F01;
inline constexpr Enum REPEAT = 0x2901;
inline constexpr Enum RGB = 0x1907;
inline constexpr Enum SAMPLES = 0x80A9;
inline constexpr Enum SHININESS = 0x1601;
inline constexpr Enum SMOOTH = 0x1D01;
inline constexpr Enum SPECULAR = 0x1202;
inline constexpr Enum SRC_ALPHA = 0x0302;
inline constexpr Enum TEXTURE = 0x1702;
inline constexpr Enum TEXTURE_2D = 0x0DE1;
inline constexpr Enum TEXTURE_BIT = 0x00040000;
inline constexpr Enum TEXTURE_ENV = 0x2300;
inline constexpr Enum TEXTURE_ENV_MODE = 0x2200;
inline constexpr Enum TEXTURE_MAG_FILTER = 0x2800;
inline constexpr Enum TEXTURE_MIN_FILTER = 0x2801;
inline constexpr Enum TEXTURE_WRAP_S = 0x2802;
inline constexpr Enum TEXTURE_WRAP_T = 0x2803;
inline constexpr Enum TRIANGLES = 0x0004;
inline constexpr Enum TRIANGLE_FAN = 0x0006;
inline constexpr Enum TRIANGLE_STRIP = 0x0005;
inline constexpr Enum TRUE = 1;
inline constexpr Enum UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr Enum UNSIGNED_BYTE = 0x1401;

void initialize(SDL_Window *, bool noMsaa, bool vsync, bool validation);
void shutdown() noexcept;
void beginFrame(int width, int height);
void present(bool synchronous);
void captureImage(const std::string &path);
int sampleCount();

void begin(Enum mode);
void end();
void color4f(float, float, float, float);
void color4fv(const float *);
void normal3d(double, double, double);
void normal3dv(const double *);
void vertex2f(float, float);
void vertex3d(double, double, double);
void vertex3dv(const double *);
void texCoord2f(float, float);
void texCoord2d(double, double);
void texCoord2dv(const double *);
void enable(Enum);
void disable(Enum);
void pushAttrib(Flags);
void popAttrib();
void matrixMode(Enum);
void loadIdentity();
void pushMatrix();
void popMatrix();
void multMatrixf(const float *);
void translated(double, double, double);
void translatef(float, float, float);
void scalef(float, float, float);
void ortho(double, double, double, double, double, double);
void frustum(double, double, double, double, double, double);
void lightModelfv(Enum, const float *);
void lightfv(Enum, Enum, const float *);
void materialfv(Enum, Enum, const float *);
void materialf(Enum, Enum, float);
void colorMaterial(Enum, Enum);
void fogfv(Enum, const float *);
void fogf(Enum, float);
void fogi(Enum, int);
void blendFunc(Enum, Enum);
void depthMask(bool);
void polygonOffset(float, float);
void lineWidth(float);
void viewport(int, int, int, int);
void clearColor(float, float, float, float);
void clear(Flags);
void hint(Enum, Enum);
void shadeModel(Enum);
void pixelStorei(Enum, int);
void getFloatv(Enum, float *);
void getIntegerv(Enum, int *);
const unsigned char *getString(Enum);
void finish();

void genTextures(int, Id *);
void deleteTextures(int, const Id *);
void bindTexture(Enum, Id);
void texImage2D(Enum, int, int, int, int, int, Enum, Enum, const void *);
void texParameteri(Enum, Enum, int);
void texEnvi(Enum, Enum, int);
Id genLists(int);
void deleteLists(Id, int);
void newList(Id, Enum);
void endList();
void callList(Id);
} // namespace sjp::gfx
