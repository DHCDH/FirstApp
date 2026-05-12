#version 450

layout(set = 1, binding = 0) uniform sampler2D sdfTexture;

layout(set = 1, std430, binding = 1) buffer LossBuffer {
    uint values[];
} lossBuffer;

layout(push_constant) uniform SlicePush {
    layout(offset = 96) uint index;
} push;

layout(location = 0) out uint outColor;

void main()
{
    outColor = 2u;

    // --- 采样SDF并计算误差 ---
    vec2 texSize = textureSize(sdfTexture, 0);
    vec2 uv = gl_FragCoord.xy / texSize;

    // 采样物理距离
    float dist = texture(sdfTexture, uv).r;

    uint uError = uint(dist * dist * 1000000.0);

    atomicAdd(lossBuffer.values[push.index], uError);

}
