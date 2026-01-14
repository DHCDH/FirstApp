#version 450

layout(triangles) in;
layout(line_strip, max_vertices = 2) out;   // 输出：一条线段

// 输入，来自顶点着色器
layout(location = 0) in vec3 inWorldPos[];

// UBO是对所有图形阶段可见的
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
} ubo;

layout(push_constant) uniform Push {
    mat4 unused;
    float yM;
    float thickness;
} push;

// 辅助函数：计算两点与平面交点的线性插值
vec3 intersect(vec3 p1, vec3 p2, float h)
{
    float t = (h - p1.y) / (p2.y - p1.y);
    // 线性插值
    return mix(p1, p2, t);
}

void main()
{
    float h = push.yM;

    float y0 = inWorldPos[0].y;
    float y1 = inWorldPos[1].y;
    float y2 = inWorldPos[2].y;

    bool b0 = y0 > h;
    bool b1 = y1 > h;
    bool b2 = y2 > h;

    // 统计三角形有多少点在切面上方
    int count = int(b0) + int(b1) + int(b2);

    // 如果 count 是 0 (全在下) 或 3 (全在上)，说明三角形没被切到，直接丢弃
    if (count == 0 || count == 3) {
        return;
    }

    vec3 p_start;
    vec3 p_end;

    // 计算两个交点，两个交点所在的两条边一定穿过平面
    if (b0 != b1 && b0 != b2) { 
        // 0 号点是异类 -> 交点在 0-1 和 0-2 边上
        p_start = intersect(inWorldPos[0], inWorldPos[1], h);
        p_end   = intersect(inWorldPos[0], inWorldPos[2], h);
    } 
    else if (b1 != b0 && b1 != b2) {
        // 1 号点是异类 -> 交点在 1-0 和 1-2 边上
        p_start = intersect(inWorldPos[1], inWorldPos[0], h);
        p_end   = intersect(inWorldPos[1], inWorldPos[2], h);
    } 
    else {
        // 2 号点是异类 -> 交点在 2-0 和 2-1 边上
        p_start = intersect(inWorldPos[2], inWorldPos[0], h);
        p_end   = intersect(inWorldPos[2], inWorldPos[1], h);
    }

    // 点 1
    gl_Position = ubo.projection * ubo.view * vec4(p_start, 1.0);
    // 把计算好的数据打包发送，作为一个新顶点
    EmitVertex();

    // 点 2
    gl_Position = ubo.projection * ubo.view * vec4(p_end, 1.0);
    EmitVertex();

    // 结束这条线段
    EndPrimitive();
}