#version 330 core

layout(location = 0) in vec2 in_Position;
layout(location = 1) in vec2 in_Barycentric;

uniform mat3 u_ViewMatrix;
uniform vec2 u_Offset;

out vec2 i_Barycentric;

void main()
{
  gl_Position = vec4((u_ViewMatrix * vec3(in_Position , 1)), 1) + vec4(u_Offset , 0, 0);
  i_Barycentric = in_Barycentric;
}