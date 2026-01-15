#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out uint outEdge;

layout(push_constant) uniform Push {
    mat4 unused;
    float xM;
    float thickness;
} push;

void main()
{
    // 计算当前三角形的几何法线
    // 利用dFdx和dFdy计算当前像素所在三角形的切线空间
    vec3 dX = dFdx(vWorldPos);
    vec3 dY = dFdy(vWorldPos);
    vec3 crossProd = cross(dX, dY);

    // 防止NaN噪点
    if(length(crossProd) < 1e-4) {
        discard;
    }

    vec3 normal = normalize(crossProd);

    // 过滤水平面
    // 如果发现的Y分量接近1.0或-1.0，该表面平行于y = yM平面，直接丢弃
    if(abs(normal.x) > 0.99) {
        discard;
    }

    // vWorldPos.y 是片元的世界高度。
    // fwidth(vWorldPos.y) 计算屏幕上相邻像素的高度变化率 (即坡度)。
    // 只有当 |y - yM| 小于一定比例的坡度时，说明该像素正好位于切面上。
    float dis = abs(vWorldPos.x - push.xM);
    float dx = fwidth(vWorldPos.x); // 屏幕上每移动一个像素，对应的世界坐标高度Y变化值

    if(dx < 1e-3) {
        discard;
    }

// test
if(dis > 0.1) {
    discard;
}

    outEdge = 1u;

    return;

    // 线宽控制：1.5 * dy，1.5像素宽度
    // max(dy, 1e-5)防止平行平面导致的除零或消失
    float threshold = max(dx, 1e-5) * 1.;

    if(dis < threshold) {
        outEdge = 1u;
    } else {
        discard;
    }
}