#version 450
layout(set = 2, binding = 0) uniform sampler2D image;
layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 textureUV;
layout(location = 2) in float fogDistance;
layout(location = 3) flat in float textured;
layout(location = 4) flat in vec4 fogParameters;
layout(location = 5) flat in vec3 fogColor;
layout(location = 0) out vec4 fragmentColor;

void main() {
    fragmentColor = vertexColor;
    if (textured != 0.0)
        fragmentColor *= texture(image, textureUV);
    if (fogParameters.z != 0.0) {
        float visibility = clamp((fogParameters.y - fogDistance) /
                                 max(fogParameters.y - fogParameters.x, 0.000001), 0.0, 1.0);
        fragmentColor.rgb = mix(fogColor, fragmentColor.rgb, visibility);
    }
}
