#version 450

layout(location = 0) out vec4 outColor;

void main() {
    // 纯黑色，完全不透明
    outColor = vec4(0.0, 0.0, 0.0, 1.0); 
}