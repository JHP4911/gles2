precision mediump float;
uniform sampler2D texture;
uniform float opacity;
varying vec2 varyingTexture;

void main()
{
    vec4 sampled = texture2D(texture, varyingTexture);
    gl_FragColor = vec4(1.0f, 1.0f, 1.0f, (sampled.r + sampled.g + sampled.b) / 3.0f * opacity);
}
