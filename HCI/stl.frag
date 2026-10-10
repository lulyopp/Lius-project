#version 330 core
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 lightColor;
uniform vec3 objectColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

void main()
{
    // 1. 环境光（稍微降一点，避免整体过白）
    float ambientStrength = 0.30;
    vec3 ambient = ambientStrength * lightColor;

    // 2. 漫反射
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // 3. 镜面反射 —— 关键修改在这里
    float specularStrength = 0.00;      // 从 0.4 降到 0.03，仅保留微弱反光
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 16.0);  // shininess 从 32 降到 16，高光更柔和分散
    vec3 specular = specularStrength * spec * lightColor;

    // 4. 边缘微光（大幅降低或注释掉）
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim) * 0.03;  // 从 0.15 降到 0.03，几乎不可见但保留轮廓
    vec3 rimLight = vec3(rim);

    // 合成（加 clamp 防止极少数角度过曝）
    vec3 result = ambient + diffuse + specular + rimLight;
    result = min(result, vec3(1.2));  // 硬上限，防止任何情况爆白
    result *= objectColor;

    FragColor = vec4(result, 1.0);
}