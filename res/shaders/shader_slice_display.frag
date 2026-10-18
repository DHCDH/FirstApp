#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// inputMask 现在读取的是 Stencil 值 (uint 类型)
layout(binding = 0) uniform usampler2D inputMask;
layout(binding = 1) uniform usampler2D inputEdge;

layout(push_constant) uniform Push {
    layout(offset = 0)  int   showMode;         // 0: Solid, 1: Wireframe
    layout(offset = 4)  int   gridEnabled;      // 0: 关闭像素网格, 1: 开启
    layout(offset = 8)  float gridCellTexels;   // 每格多少个遮罩像素(texel), 默认 1
    layout(offset = 12) float gridLineWidthPx;  // 网格线宽(屏幕像素)
    layout(offset = 16) vec2  texelSize;        // 1/width, 1/height
    layout(offset = 24) int   rawContourEnabled;// 0: 关闭非亚像素黑色轮廓, 1: 开启
} push;

// 计算像素网格的覆盖度: 1 = 落在格线上, 0 = 格子内部
// 网格按离屏遮罩的 texel 对齐; 1 格 = gridCellTexels 个 texel,
// 线宽按屏幕像素绘制(fwidth), 放大后每个 texel 边界就是一根格线
float computePixelGrid(vec2 uv)
{
    if (push.gridEnabled == 0) {
        return 0.0;
    }

    vec2 texelCount = vec2(1.0) / max(push.texelSize, vec2(1e-8));  // 遮罩分辨率
    float cellTexels = max(floor(push.gridCellTexels + 0.5), 1.0);   // 每格 texel 数
    vec2 cellCount = texelCount / cellTexels;                        // 格数

    vec2 coord = uv * cellCount;
    // 到最近格线的屏幕像素距离
    vec2 dist = abs(fract(coord - 0.5) - 0.5) / max(fwidth(coord), vec2(1e-6));

    float d = min(dist.x, dist.y);
    return 1.0 - smoothstep(0.0, push.gridLineWidthPx, d);
}

// 辅助函数：解析当前像素的掩码状态
void parseMask(uint mask, out bool hasBlank, out bool isWheelSection, out bool isIntersection) {
    // 棒料 ID = 1 (二进制 01)
    // 砂轮 ID = 2 (二进制 10)
    // 交集 ID = 3 (二进制 11)
    hasBlank = (mask & 1u) != 0u;
    isWheelSection = (mask &2u) != 0u;
    isIntersection = (mask == 3u);
}

// 带边界钳制的 texel 读取, 避免越界
uint maskAt(ivec2 tc)
{
    ivec2 size = textureSize(inputMask, 0);
    return texelFetch(inputMask, clamp(tc, ivec2(0), size - ivec2(1)), 0).r;
}

// 判断某个 texel 是否为「未亚像素提取」的容屑槽轮廓。
// 判断某个 texel 是否为「未亚像素提取」的容屑槽轮廓。
// 暂时不做外圆剔除: 交集区(3)的四邻域存在任意非交集(非3)即视为边界。
bool isFluteContourTexel(ivec2 tc)
{
    uint m = maskAt(tc);
    if (m != 3u) return false;  // 必须在交集区(容屑槽)内

    // 四邻域存在任意非交集 -> 边界(包括邻接砂轮的外圆部分)
    if (maskAt(tc + ivec2(0, 1)) != 3u) return true;
    if (maskAt(tc + ivec2(0, -1)) != 3u) return true;
    if (maskAt(tc + ivec2(-1, 0)) != 3u) return true;
    if (maskAt(tc + ivec2(1, 0)) != 3u) return true;

    return false;
}

void main()
{
    outColor = vec4(1.0, 1.0, 1.0, 1.0);  // 默认背景

    // 当前片段对应的 texel 坐标(与网格的 texel 边界对齐)
    vec2 texelCount = vec2(1.0) / max(push.texelSize, vec2(1e-8));
    ivec2 tc = ivec2(clamp(inUV * texelCount, vec2(0.0), texelCount - vec2(1.0)));

    // 采样中心点
    uint mask_center = texelFetch(inputMask, tc, 0).r;

    // 解析中心点状态
    bool c_Blank, c_Wheel, c_Inter;
    parseMask(mask_center, c_Blank, c_Wheel, c_Inter);

    if(push.showMode == 0) {
        /* ========= 实心模式 (Solid) ========= */
        if (c_Inter) {
            outColor = vec4(0.4471, 0.7686, 0.7255, 1.0);       // 交集
        } else if (c_Blank) {
            outColor = vec4(0.7961, 0.7961, 0.7490, 1.0);       // 棒料
        } else if (c_Wheel) {
            outColor = vec4(0.5608, 0.6667, 0.7922, 1.0);       // 砂轮
        } else {
            outColor = vec4(1, 1, 1, 1.0); // 背景
        }
    }

    /* ========= 叠加像素网格 ========= */
    float grid = computePixelGrid(inUV);
    if (grid > 0.0) {
        outColor.rgb = mix(outColor.rgb, vec3(0.20, 0.20, 0.20), grid * 0.6);
    }

    /* ========= 未亚像素提取的容屑槽轮廓(黑色, 1 texel) ========= */
    if (push.rawContourEnabled != 0 && isFluteContourTexel(tc)) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);  // 黑色
    }

    // } else {
    //     /* ========= 线框模式 (Edge Detection) ========= */
        
    //     // 获取背景信息
    //     uint mask_center = texture(inputMask, inUV).r;
    //     bool c_Blank = (mask_center & 0x80u) != 0u;

    //     vec4 baseColor = vec4(0.);
    //     if(c_Blank) {
    //         baseColor = vec4(0.25, 0.35, 0.45, 1.); // 绿色棒料
    //     }
    //     outColor = baseColor;
    // }
}
