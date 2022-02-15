#version 450

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform FragBindingLayout {
    vec4 ambientColor;
    vec4 diffuseLightPos;
    vec4 diffuseLightColor;
} ubo;

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(ubo.diffuseLightPos.xyz - fragPosition);
    float diff = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3 diffuse = diff * ubo.diffuseLightColor.rgb;
    outColor = vec4((diffuse + ubo.ambientColor.rgb) * texture(texSampler, fragTexCoord).rgb, 1.0);
}
