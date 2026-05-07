#version 330 core

layout (location = 0) in vec2 aPos; 
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 iColor;
layout (location = 3) in vec2 iSpritePos;
layout (location = 4) in vec2 iScale;
layout (location = 5) in float iRotation;

uniform mat4 uMVP;

uniform vec2 uUVOffset = vec2(0.0);
uniform vec2 uUVScale  = vec2(1.0);
uniform bvec2 uFlip = bvec2(false, false);

out vec2 vUV;
out vec4 vColor;

void main()
{
    float c = cos(iRotation);
    float s = sin(iRotation);
    // GLSL mat constructors are column-major. This layout matches
    // Transform2DComponent::Rotate / TransformPoint on the CPU.
    mat2 R = mat2(c,  s,
                 -s,  c);

    vec2 worldPos = (R * (aPos * iScale)) + iSpritePos;

    gl_Position = uMVP * vec4(worldPos, 0.0, 1.0);

    vec2 uv = aUV;
    if (uFlip.x) uv.x = 1.0 - uv.x;
    if (uFlip.y) uv.y = 1.0 - uv.y;

    vUV    = uUVOffset + uv * uUVScale;
    vColor = iColor;
}
