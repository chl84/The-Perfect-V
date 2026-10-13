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
} state;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec2 textureUV;
layout(location = 2) out float fogDistance;
layout(location = 3) flat out float textured;
layout(location = 4) flat out vec4 fogParameters;
layout(location = 5) flat out vec3 fogColor;

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
        vec3 lit = color.rgb * (state.ambient.rgb + state.diffuse0.rgb * d0 + state.diffuse1.rgb * d1);
        lit += state.emission.rgb + state.materialSpecular.rgb * state.specularLight0.rgb * shine;
        vertexColor.rgb = clamp(lit, 0.0, 1.0);
    }
    textureUV = uv;
    fogDistance = abs(eye.z);
    textured = state.flags.z;
    fogParameters = vec4(state.parameters.yz, state.flags.y, 0.0);
    fogColor = state.fogColor.rgb;
}
