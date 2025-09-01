#version 330 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 ncoord;
layout (location = 2) in vec2 tcoord;

out float quad_index;
out vec2 nor_coord;
out vec2 tex_coord;

uniform mat4 model, view, projection;

void main() {
	gl_Position = projection * view * model * vec4(position, 1.0);
	nor_coord = ncoord;
	tex_coord = tcoord;
	quad_index = gl_VertexID / 4;
}
