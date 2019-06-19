attribute vec3 vertexPosition;
attribute vec2 vertexTexture;
varying vec2 varyingTexture;

void main()
{
    gl_Position = vec4(vertexPosition, 1);
    varyingTexture = vertexTexture;
}