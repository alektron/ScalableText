#version 330 core
 
in vec2 i_Barycentric;

uniform vec4 u_Sample = vec4(1, 0, 0, 0);

out vec4 OutColor;

void main()
{
  if (i_Barycentric.x * i_Barycentric.x - i_Barycentric.y > 0.0) {
    discard;
  }

  OutColor = u_Sample * (1.0 / 255.0);
}