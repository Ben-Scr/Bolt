#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out VS_OUT {
    vec3 Normal;
    vec3 WorldPos;
    vec2 TexCoord;
} vs_out;

void main()
{
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vs_out.WorldPos = worldPos.xyz;
    vs_out.Normal = normalize(uNormalMatrix * aNormal);
    vs_out.TexCoord = aTexCoord;

    gl_Position = uProjection * uView * worldPos;
}