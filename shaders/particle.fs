#version 100
precision mediump float;
uniform sampler2D texture;
uniform float opacity;
varying vec2 varyingTexture;

void main()
{
    vec4 sampled = texture2D(texture, varyingTexture);
    gl_FragColor = vec4(1.0, 1.0, 1.0, (sampled.r + sampled.g + sampled.b) / 3.0 * opacity);
}
