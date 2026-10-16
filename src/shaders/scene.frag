#version 450
layout(set = 2, binding = 0) uniform sampler2D image;
layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 textureUV;
layout(location = 2) in float fogDistance;
layout(location = 3) flat in float textured;
layout(location = 4) flat in vec4 fogParameters;
layout(location = 5) flat in vec3 fogColor;
layout(location = 6) in vec3 snowNormal;
layout(location = 7) in vec3 snowView;
layout(location = 8) flat in vec3 snowSun;
layout(location = 9) in vec4 backdropMist;
layout(location = 10) flat in vec4 valleyEdge;
layout(location = 0) out vec4 fragmentColor;

// Integer hashing is stable for fixed surface cells, including negative world
// coordinates. No frame clock/random seed: a paused camera cannot twinkle.
vec3 crystalRandom(ivec2 cell) {
    uvec3 h = uvec3(uint(cell.x), uint(cell.y), uint(cell.x) ^ uint(cell.y)) *
              uvec3(1597334677u, 3812015801u, 2798796415u);
    h = (h ^ (h.yzx >> 16u)) * 2246822519u;
    h ^= h >> 13u;
    return vec3(h & 0x00ffffffu) / 16777216.0;
}
float snowGlitter() {
    // Eight fixed cells per metre. UVs on both existing snow layers encode
    // world X/Z at 1/13 scale, so their crystals agree across the blended join.
    vec2 crystalUV = textureUV * 104.0;
    float footprint = max(length(dFdx(crystalUV)), length(dFdy(crystalUV)));
    float resolved = 1.0 - smoothstep(0.35, 0.85, footprint);
    if (resolved <= 0.0)
        return 0.0;
    vec3 random = crystalRandom(ivec2(floor(crystalUV)));
    vec2 center = 0.35 + random.xy * 0.30;
    float radius = 0.12 + 0.06 * random.z;
    float aa = max(0.015, footprint * 0.60);
    float spot = 1.0 - smoothstep(radius - aa, radius + aa, length(fract(crystalUV) - center));
    // Most surface pixels lie between crystals. Derivatives are already
    // evaluated, so those pixels can skip the reflection work safely.
    if (spot <= 0.0)
        return 0.0;
    vec3 n = normalize(snowNormal);
    vec3 l = normalize(snowSun);
    vec3 v = normalize(snowView);
    vec3 halfVector = normalize(l + v);
    vec3 tangent = normalize(cross(vec3(0.0, 0.0, 1.0), n));
    vec3 bitangent = cross(n, tangent);
    vec3 crystal = normalize(n + tangent * ((random.x - 0.5) * 2.4) +
                                 bitangent * ((random.y - 0.5) * 2.4));
    float reflection = pow(max(dot(crystal, halfVector), 0.0), 96.0);
    return spot * reflection * resolved * max(dot(n, l), 0.0) * fogParameters.w;
}
void main() {
    vec4 texel = textured != 0.0 ? texture(image, textureUV) : vec4(1.0);
    fragmentColor = vertexColor * texel;
    if (valleyEdge.z != 0.0) {
        // The terrain grid has 10-scaled-metre rows. Evaluate its 5-metre
        // edge per pixel so vertex interpolation cannot widen the fade.
        float opacity = 1.0 - smoothstep(valleyEdge.x, valleyEdge.y, abs(textureUV.y * 13.0));
        // The upstream mesh also ends outside the original inrun. Soften that
        // cap to avoid exposing a straight silhouette when the side fade narrows.
        float cap = smoothstep(valleyEdge.w, valleyEdge.w + valleyEdge.y - valleyEdge.x, textureUV.x * 13.0);
        fragmentColor.a = opacity * cap * texel.a;
    }
    if (fogParameters.w > 0.0) {
        float glint = snowGlitter();
        // Small neutral ice highlights; preserve alpha and retain headroom.
        fragmentColor.rgb = min(vec3(1.0), fragmentColor.rgb + vec3(0.32, 0.34, 0.36) * glint);
    }
    if (fogParameters.z != 0.0) {
        float visibility = clamp((fogParameters.y - fogDistance) /
                                 max(fogParameters.y - fogParameters.x, 0.000001), 0.0, 1.0);
        fragmentColor.rgb = mix(fogColor, fragmentColor.rgb, visibility);
    }
    // Shared air color blends scenery and the photo foot through the same
    // soft veil. Alpha is retained for foliage, shadows and the photo cutout.
    fragmentColor.rgb = mix(fragmentColor.rgb, backdropMist.rgb, backdropMist.a);
}
