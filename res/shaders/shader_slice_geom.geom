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
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
} push;

// 辅助函数：计算线段(p1-p2)与平面(n, p0)的交点
vec3 intersectPlane(vec3 p1, vec3 p2, vec3 n, vec3 p0)
{
    float d1 = dot(p1 - p0, n);
    float d2 = dot(p2 - p0, n);

    float t = d1 / (d1 - d2);

    return mix(p1, p2, t);
}

void main()
{
    vec3 n = push.normal;
    vec3 p0 = push.point;

    // 计算三角形三个顶点相对于平面的状态
    // dist > 0: 在前方 (被切除侧)
    // dist < 0: 在后方 (保留侧)
    float d0 = dot(inWorldPos[0] - p0, n);
    float d1 = dot(inWorldPos[1] - p0, n);
    float d2 = dot(inWorldPos[2] - p0, n);

    // 使用布尔值标记每个点是否在"前方"
    bool b0 = d0 > 0;
    bool b1 = d1 > 0;
    bool b2 = d2 > 0;

    // 统计有多少个点在"前方"
    int frontCount = int(b0) + int(b1) + int(b2);

    // 如果全在前方(3)或全在后方(0)，说明三角形没有跨越平面，不需要生成轮廓线
    if (frontCount == 0 || frontCount == 3) {
        return;
    }

    // 寻找跨越平面的两条边，并计算交点
    vec3 p_start, p_end;

    if (b0 != b1 && b0 != b2) { 
        // 顶点0是孤独的
        // 交点分别在边 0-1 和 0-2 上
        p_start = intersectPlane(inWorldPos[0], inWorldPos[1], n, p0);
        p_end   = intersectPlane(inWorldPos[0], inWorldPos[2], n, p0);
    } 
    else if (b1 != b0 && b1 != b2) {
        // 顶点1是孤独的
        // 交点分别在边 1-0 和 1-2 上
        p_start = intersectPlane(inWorldPos[1], inWorldPos[0], n, p0);
        p_end   = intersectPlane(inWorldPos[1], inWorldPos[2], n, p0);
    } 
    else {
        // 顶点2是孤独的
        // 交点分别在边 2-0 和 2-1 上
        p_start = intersectPlane(inWorldPos[2], inWorldPos[0], n, p0);
        p_end   = intersectPlane(inWorldPos[2], inWorldPos[1], n, p0);
    }

    gl_Position = ubo.projection * ubo.view * vec4(p_start, 1.0);
    EmitVertex();

    gl_Position = ubo.projection * ubo.view * vec4(p_end, 1.0);
    EmitVertex();

    EndPrimitive();
}