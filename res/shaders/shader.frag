#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragUV;
layout(location = 4) flat in int useFragColor;

layout(location = 0) out vec4 outColor;	// 输出到颜色附件的第0个位置

struct PointLight {
    vec4 position;  // ignore w
    vec4 color;     // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    mat4 invView;
    vec4 ambientLightColor;
    PointLight pointLights[20]; //应使用特化常量而非硬编码
    int numLights;
}ubo;

/*set = 1，材质贴图*/
layout(set = 1, binding = 0) uniform sampler2D uAlbedo;

/*set = 2，材质参数UBO*/
layout(set = 2, binding = 0, std140) uniform UMaterial {
    vec4 baseColorFactor;   // rgba
    vec4 uvTilingOffset;    // xy=tiling, zw=offset
    vec4 pbrAoAlpha;        // x=metallic, y=roughness, z=ao, w=alphaCutoff
    uvec4 flags;            // bit0: baseColorTex, bit1: normalTex, bit2: ormTex, bit3: emissiveTex...
}matu;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

layout(set = 0, binding = 1) uniform sampler2D uTexture;

vec3 shadePointLights(vec3 N, vec3 base) {
    vec3 sum = base * (ubo.ambientLightColor.rgb * ubo.ambientLightColor.a);
    int n = ubo.numLights;
    for (int i = 0; i < n; ++i) {
        vec3 Lpos = ubo.pointLights[i].position.xyz;
        vec3 L    = normalize(Lpos - fragPosWorld);
        float ndl = max(dot(N, L), 0.0);
        vec3  Lcol= ubo.pointLights[i].color.rgb * ubo.pointLights[i].color.a;
        float d2  = max(dot(Lpos - fragPosWorld, Lpos - fragPosWorld), 0.0001);
        float att = 1.0 / d2;
        sum += base * Lcol * ndl * att;
    }
    return sum;
}

void main() 
{
    /*采样纹理*/
    vec2 uv2 = fragUV * matu.uvTilingOffset.xy + matu.uvTilingOffset.zw;
    bool hasBaseTex = (matu.flags.x & 1u) != 0u;
    vec4 base = hasBaseTex ? texture(uAlbedo, uv2) : vec4(1.0);
    base *= matu.baseColorFactor;

    if (matu.pbrAoAlpha.w > 0.0 && base.a < matu.pbrAoAlpha.w) {
        discard;
    }

    vec3 N   = normalize(fragNormalWorld);
    vec3 rgb = shadePointLights(N, base.rgb);

    if(useFragColor == 1) {
        outColor = vec4(fragColor, 1.);
    } else {
        outColor = vec4(rgb, base.a);
    }
}