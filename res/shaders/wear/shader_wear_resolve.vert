#version 450
void main() {
    // 根据顶点 ID 自动生成全屏三角形，不需要任何外部 Buffer 传入
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
}