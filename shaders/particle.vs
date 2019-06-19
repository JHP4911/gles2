#version 120
uniform mat4 positionMatrix;
attribute vec3 vertexPosition;
attribute vec2 vertexTexture;
varying vec2 varyingTexture;

void main()
{
    gl_Position = positionMatrix * vec4(vertexPosition, 1);
    varyingTexture = vertexTexture;
}
