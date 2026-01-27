#version 450
layout(location = 0) out uint outColor;

void main() {
    // 只要通过了 Stencil Test (由 Pipeline 配置控制)，就写入 ID 2 (代表砂轮)
    outColor = 2u;
}