#version 450

void main() {
    // 什么都不做，Pipeline 会将颜色写入掩码设为 0
    // 我们只需要这个 Pass 运行来触发 Stencil Replace 操作
}