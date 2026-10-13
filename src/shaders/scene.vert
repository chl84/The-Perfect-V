#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;
layout(location = 3) in vec2 uv;
layout(location = 4) in vec3 lineOther;
layout(location = 5) in float lineSide;

layout(set = 1, binding = 0, std140) uniform DrawState {
    mat4 modelView;
    mat4 projection;
    mat4 normalMatrix;
    vec4 ambient;
    vec4 lightDirection0;
    vec4 lightDirection1;
    vec4 diffuse0;
    vec4 diffuse1;
    vec4 specularLight0;
    vec4 materialSpecular;
    vec4 emission;
    vec4 fogColor;
    vec4 parameters; // shininess, fog start, fog end, line width
    vec4 flags;      // lighting, fog, texturing, reserved
    vec4 viewport;   // pixel width/height, their reciprocals
    vec4 spotPositionRange[10];
    vec4 spotDirectionCone[10];
    vec4 arenaAxis;       // world X plane in eye coordinates
    vec4 arenaParameters; // first X, inverse spacing, inverse attenuation distance squared, strength
    vec4 arenaColor;
} state;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec2 textureUV;
layout(location = 2) out float fogDistance;
layout(location = 3) flat out float textured;
layout(location = 4) flat out vec4 fogParameters;
layout(location = 5) flat out vec3 fogColor;

vec3 arenaDiffuse(vec3 eye, vec3 n) {
    if (state.arenaParameters.w == 0.0)
        return vec3(0.0);
    float coordinate = (dot(state.arenaAxis, vec4(eye, 1.0)) - state.arenaParameters.x) * state.arenaParameters.y;
    if (coordinate <= -1.0 || coordinate >= 5.0)
        return vec3(0.0);
    int left = int(floor(coordinate));
    float intensity = 0.0;
    // Fixed budget: two neighboring columns, two mast groups each. Smooth
    // longitudinal weighting reaches zero before a source leaves the set.
    for (int offset = 0; offset < 2; ++offset) {
        int column = left + offset;
        if (column < 0 || column >= 5)
            continue;
        float longitudinal = smoothstep(0.0, 1.0, 1.0 - abs(coordinate - float(column)));
        for (int side = 0; side < 2; ++side) {
            int index = column * 2 + side;
            vec4 source = state.spotPositionRange[index];
            vec4 cone = state.spotDirectionCone[index];
            vec3 delta = source.xyz - eye;
            float d2 = dot(delta, delta);
            float range = 1.0 - d2 * source.w;
            if (range <= 0.0 || d2 < 0.0001)
                continue;
            vec3 l = delta * inversesqrt(d2);
            float beam = smoothstep(cone.w, 0.90, dot(-l, cone.xyz));
            intensity += longitudinal * beam * range * range * max(dot(n, l), 0.0) /
                         (1.0 + d2 * state.arenaParameters.z);
        }
    }
    return state.arenaColor.rgb * (state.arenaParameters.w * intensity);
}

void main() {
    vec4 eye = state.modelView * vec4(position, 1.0);
    vec4 clip = state.projection * eye;
    if (lineSide != 0.0) {
        vec4 other = state.projection * state.modelView * vec4(lineOther, 1.0);
        if (abs(clip.w) > 0.000001 && abs(other.w) > 0.000001) {
            vec2 tangent = (other.xy / other.w - clip.xy / clip.w) * state.viewport.xy;
            float lengthSquared = dot(tangent, tangent);
            if (lengthSquared > 0.000001) {
                vec2 perpendicular = vec2(-tangent.y, tangent.x) * inversesqrt(lengthSquared);
                clip.xy += perpendicular * state.parameters.w * state.viewport.zw * lineSide * clip.w;
            }
        }
    }
    // Retain the existing projection and cameras; SDL_GPU expects depth 0..1
    // and handles the Vulkan viewport's Y convention itself.
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;

    vertexColor = color;
    if (state.flags.x != 0.0) {
        vec3 n = normalize((state.normalMatrix * vec4(normal, 0.0)).xyz);
        vec3 l0 = normalize(state.lightDirection0.xyz);
        vec3 l1 = normalize(state.lightDirection1.xyz);
        float d0 = max(dot(n, l0), 0.0) * state.lightDirection0.w;
        float d1 = max(dot(n, l1), 0.0) * state.lightDirection1.w;
        // The previous renderer used the fixed pipeline's infinite eye vector.
        vec3 halfVector = normalize(l0 + vec3(0.0, 0.0, 1.0));
        float shine = d0 > 0.0 ? (state.parameters.x == 0.0 ? 1.0 :
                      pow(max(dot(n, halfVector), 0.0), state.parameters.x)) : 0.0;
        vec3 lit = color.rgb * (state.ambient.rgb + state.diffuse0.rgb * d0 + state.diffuse1.rgb * d1 +
                               arenaDiffuse(eye.xyz, n));
        // Emission W selects arena tracking. Independently powered indicators
        // keep constant emission; vertex color still supplies surface alpha.
        lit += state.emission.rgb * (1.0 + 2.0 * state.arenaParameters.w * state.emission.w) +
               state.materialSpecular.rgb * state.specularLight0.rgb * shine;
        vertexColor.rgb = clamp(lit, 0.0, 1.0);
    }
    textureUV = uv;
    fogDistance = abs(eye.z);
    textured = state.flags.z;
    fogParameters = vec4(state.parameters.yz, state.flags.y, 0.0);
    fogColor = state.fogColor.rgb;
}
