#version 450

layout(binding = 0) uniform VertBindingLayout {
    mat4 model;
    mat4 projView;
    mat3 modelInvTrans;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

void main() {
    vec4 mpos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.projView * mpos;
    fragPosition = mpos.xyz;
    fragNormal = ubo.modelInvTrans * inNormal;
    fragTexCoord = inTexCoord;
}
