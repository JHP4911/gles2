attribute vec3 vertexPosition;
attribute vec3 vertexColor;
varying vec3 fragmentColor;
uniform mat4 rotationMatrix;

void main()
{
    gl_Position = rotationMatrix * vec4(vertexPosition, 1);
    fragmentColor = vertexColor;
}
