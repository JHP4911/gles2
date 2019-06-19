#version 120
precision mediump float;
uniform sampler2D texture;
varying vec2 varyingTexture;

void main()
{
    gl_FragColor = texture2D(texture, varyingTexture);
}
