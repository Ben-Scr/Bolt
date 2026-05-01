#version 330 core

in VS_OUT {
    vec3 Normal;
    vec3 WorldPos;
    vec2 TexCoord;
} fs_in;

out vec4 FragColor;

uniform vec4 uColor;
uniform vec3 uViewPos;

struct DirectionalLight {
    vec3 direction;
    vec3 color;
};

uniform DirectionalLight uDirectionalLight;

void main()
{
    vec3 normal = normalize(fs_in.Normal);
    vec3 lightDir = normalize(-uDirectionalLight.direction);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 diffuse = diff * uDirectionalLight.color * uColor.rgb;
    vec3 ambient = 0.1 * uColor.rgb;

    FragColor = vec4(ambient + diffuse, uColor.a);
}