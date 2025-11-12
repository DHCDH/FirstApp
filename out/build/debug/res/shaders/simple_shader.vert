#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform Push {
    mat4 transform; // projection * view * model
    mat4 normalMatrix;
} push;

const vec3 DIRECTION_TO_LIGHT = normalize(vec3(1.0, -3.0, -1.0)); //进行光照计算时，应保证输入向量为单位向量
const float AMBIENT = 0.02; // 环境光照，以便所有顶点都能接收到少量的间接光照

void main() {
    // 模型矩阵变换
    gl_Position = push.transform * vec4(position, 1.0);

    // temporary: this is only correct in certain situations!
    // only works correctly if scale is uniform (scaleX == scaleY == scaleZ)
    // vec3 normalWorldSpace = normalize(mat3(push.modelMatrix) * normal); // mat3函数通过移除最后一行和最后一列，将4*4矩阵转换为3x3矩阵；变换矩阵时，不需要平移部分
    
    // calculating the inverse in a shader can be expensive and should be avoided
    // mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    // vec3 normalWorldSpace = normalize(normalMatrix * normal);

    vec3 normalWorldSpace = normalize(mat3(push.normalMatrix) * normal);

    float lightIntensity = AMBIENT + max(dot(normalWorldSpace, DIRECTION_TO_LIGHT), 0.0); // 计算光照强度

    fragColor = color * lightIntensity;
}