#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// inputMask 现在读取的是 Stencil 值 (uint 类型)
layout(binding = 0) uniform usampler2D inputMask;

layout(push_constant) uniform Push {
    int showMode;       // 0: Solid, 1: Wireframe
    vec2 texelSize;     // 1/width, 1/height
} push;

// 辅助函数：解析当前像素的掩码状态
void parseMask(uint mask, out bool hasBlank, out bool isWheelSection, out bool isIntersection) {
    // 1. 棒料存在：Bit 0 为 1
    hasBlank = (mask & 1u) != 0u;
    
    // 2. 砂轮截面存在：Bit 1 (前) 和 Bit 2 (后) 同时为 1
    // 隐式计算核心：只有既在前投影里，又在后投影里，才是实体截面
    bool hasFront = (mask & 2u) != 0u;
    bool hasBack  = (mask & 4u) != 0u;
    isWheelSection = hasFront && hasBack;

    // 3. 交集存在：既是棒料又是砂轮截面
    isIntersection = hasBlank && isWheelSection;
}

void main()
{
    // 采样中心点
    uint mask_center = texture(inputMask, inUV).r;
    
    // 解析中心点状态
    bool c_Blank, c_Wheel, c_Inter;
    parseMask(mask_center, c_Blank, c_Wheel, c_Inter);

    if(push.showMode == 0) {
        /* ========= 实心模式 (Solid) ========= */
        if (c_Inter) {
            outColor = vec4(0., 0., 1., 1.);       // 交集蓝
        } else if (c_Blank) {
            outColor = vec4(0., 1., 0., 1.);       // 棒料绿
        } else if (c_Wheel) {
            outColor = vec4(1., 0., 0., 1.);       // 砂轮红
        } else {
            outColor = vec4(0.12, 0.12, 0.12, 1.); // 背景灰
        }
    } else {
        /* ========= 线框模式 (Edge Detection) ========= */
        
        // 采样右边和上边的像素
        uint mask_right = texture(inputMask, inUV + vec2(push.texelSize.x, 0.0)).r;
        uint mask_up    = texture(inputMask, inUV + vec2(0.0, push.texelSize.y)).r;

        // 解析邻域状态
        bool r_Blank, r_Wheel, r_Inter;
        parseMask(mask_right, r_Blank, r_Wheel, r_Inter);

        bool u_Blank, u_Wheel, u_Inter;
        parseMask(mask_up, u_Blank, u_Wheel, u_Inter);

        // 1. 设置底色 (有棒料的地方显示深绿，其他地方背景色)
        vec4 baseColor = vec4(0.0); // 透明/黑
        if (c_Blank) {
             baseColor = vec4(0.0, 0.3, 0.0, 1.0); // 深绿背景
        }

        // 2. 检测边缘 (只要状态发生跳变，就是边缘)
        
        // A. 切割线: "是否是交集"的状态发生改变 (蓝/绿分界线)
        bool isCutEdge = (c_Inter != r_Inter) || (c_Inter != u_Inter);

        // B. 轮廓线: "是否是砂轮截面"的状态发生改变 (红/空分界线)
        bool isWheelEdge = (c_Wheel != r_Wheel) || (c_Wheel != u_Wheel);

        // 合并绘制：只要是任意一种边缘，都画红线
        if (isCutEdge || isWheelEdge) {
            outColor = vec4(1.0, 0.0, 0.0, 1.0); 
        } else {
            outColor = baseColor;
        }
    }
}