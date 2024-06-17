#version 330 core

uniform vec4 u_Color = vec4(1, 1, 1, 1);
uniform int u_NumAntiAliasingPasses = 1;
uniform sampler2D u_GlyphTexture;

out vec4 out_Color;

void main()
{
  vec4 covering = texelFetch(u_GlyphTexture, ivec2(gl_FragCoord.xy), 0) * 255.0;

  //We add a small bias to prevent values like 1.999 (due to floating point inaccuracies) from being mod to itself instead
  //of almost 0. On most Nvidia GPUs this was tested with, the BIAS was not necessary but on a integrated GPU it caused issues.
  const float BIAS = 0.0001;
  vec4 alpha = mod(covering + BIAS, 2.0);

  float alphaOut = (alpha.x + alpha.y + alpha.z + alpha.w) / u_NumAntiAliasingPasses;
  out_Color = vec4(vec3(1 - alphaOut), alphaOut);
}
