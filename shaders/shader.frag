#version 450

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform LightInfo {
    vec4 ambientColor;
    vec4 diffuseLightPos;
    vec4 diffuseLightColor;
} lightInfo;

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
//    outColor = vec4(fragNormal, 1.0);
    vec3 lightDir = normalize(lightInfo.diffuseLightPos.xyz - fragPosition);
    float diff = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3 diffuse = diff * lightInfo.diffuseLightColor.rgb;
    outColor = vec4((diffuse + lightInfo.ambientColor.rgb) * fragColor * texture(texSampler, fragTexCoord).rgb, 1.0);
}
