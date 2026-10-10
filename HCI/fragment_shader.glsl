#version 330 core

out vec4 FragColor; // 输出颜色

uniform vec3 objectColor;

void main() {
    FragColor = vec4(objectColor, 1.0f); // 设置折线图的颜色为黄色
}
