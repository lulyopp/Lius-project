#version 450 core

layout(location = 0) in vec3 inPosition; // 顶点位置

uniform mat4 projection;  // 投影矩阵
uniform mat4 view;        // 视图矩阵
uniform mat4 model;     //模型矩阵

void main() {
    gl_Position = projection * view * model * vec4(inPosition, 1.0); // 应用投影和视图变换
}
