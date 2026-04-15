#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    // 后面的参数描边用不到，但为了兼容你的 descriptor layout 可以保留
} ubo;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    // 这里的 0.1 是描边粗细，你需要根据砂轮的实际尺寸适当调大或调小
    float outlineThickness = 0.1; 
    
    // 让顶点沿着法线方向向外膨胀
    vec3 inflatedPos = position + normal * outlineThickness;
    
    gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(inflatedPos, 1.0);
}