#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "shader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define CANVAS_HEIGHT 600
#define CANVAS_WIDTH CANVAS_HEIGHT
#define COLOR_WHEEL_SIDE 200

#define HISTORY 16
#define MAX_HIST_MOV HISTORY

#define initialize_quad(x, y, width, height, s1, t1, s2, t2) \
	        x,          y, -1,   0, 0,  s1, t1, \
	x + width,          y, -1,   1, 0,  s2, t1, \
	x + width, y + height, -1,   1, 1,  s2, t2, \
	        x, y + height, -1,   0, 1,  s1, t2

union Vec2 {
	struct { float x, y; };
	struct { float width, height; };
};

union Vec2i {
	struct { int x, y; };
	struct { int width, height; };
};

union Vec3 {
	struct { float x, y, z; };
	struct { float r, g, b; };
	struct { float h, s, v; };
};

struct Vec4uc {
	unsigned char r, g, b, a;
};

struct Color {
	Vec3 rgb, hsv;
};

struct Vertex {
	Vec3 pos;
	Vec2 nor_coord;
	Vec2 tex_coord;
};

struct Quad {
	Vertex v[4];
};

enum UiElement {
	CANVAS,
	COLOR_WHEEL,
	SV_SQUARE,
	BUTTON_CLEAR_CANVAS,
	BUTTON_UNDO,
	BUTTON_REDO,
	BUTTON_BRUSH,
	BUTTON_BUCKET
};

Vec2 window_size = { CANVAS_WIDTH + COLOR_WHEEL_SIDE + 48, CANVAS_HEIGHT + 48 };
Vec2 mouse = { 0.0, 0.0 };
Vec3 wheel_color = { 1.0, 0.0, 0.0 };
Color active_color = { .rgb = wheel_color, .hsv = { 0.0, 1.0, 1.0 } };
int active_ui_element = -1, hot_ui_element = -1;
//Vec2i last_pix = { -1, -1 };
int brush_r = 5;
int active_tool = BUTTON_BRUSH;
Vec4uc first_filled_pix;

struct CanvasHistory {
	int index, past, future;
	Vec4uc *colors[HISTORY];
	Vec2i coords[3];
};

struct Canvas {
	Vec2i size;
	float scale;
	Vec4uc *colors;
	GLuint texture;

	CanvasHistory history;
};

Canvas canvas = {
	.size = { 512, 512 },
	.scale = 1.0,
	.history = { .index = 1, .past = 0, .future = 0 }
};

struct Ui {
	int active, hot;
};

Ui ui = { -1, -1 };

glm::mat4 model, view, projection;
int loc_model, loc_view, loc_projection;
int loc_hot_ui_element, loc_active_tool;
int loc_color_wheel_center, loc_color_wheel_radius;
int loc_wheel_color, loc_active_color, loc_hsv;
int loc_canvas, loc_tex_btn;

void change_canvas_unit(int row, int column) {
	if (row < 0 || row >= canvas.size.height) return;
	if (column < 0 || column >= canvas.size.width) return;
	*(canvas.colors + row*canvas.size.width + column) = {
		active_color.rgb.r * 255,
		active_color.rgb.g * 255,
		active_color.rgb.b * 255,
		255
	};
}

void clear_canvas(bool reset_history) {
	for (int i = 0; i < canvas.size.height; i++) {
		for (int j = 0; j < canvas.size.width; j++) {
			*(canvas.colors + i*canvas.size.width + j) = { 0, 0, 0, 0 };
			*(canvas.history.colors[canvas.history.index] + i*canvas.size.width + j) = { 0, 0, 0, 0 };
		}
	}

	if (reset_history) {
		//canvas.history = { .past = 0, .future = 0 };
		canvas.history.past = 0;
		canvas.history.future = 0;
		canvas.history.index = (canvas.history.index + 1) % HISTORY;
		canvas.history.coords[0] = { -1, -1 };
		canvas.history.coords[1] = { -1, -1 };
		canvas.history.coords[2] = { -1, -1 };
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
	glGenerateMipmap(GL_TEXTURE_2D);
}

int clamp(int value, int minimum, int maximum) {
	if (value < minimum) return minimum;
	if (value > maximum) return maximum;
	return value;
}

void join_points(Vec2i start, Vec2i end) {
	//printf("coords0 = (%d, %d)   start = (%d, %d)   end = (%d, %d)\n", canvas.history.coords[0].x, canvas.history.coords[0].y, start.x, start.y, end.x, end.y);
	if (start.x + start.y == -2) return;

	if (start.x == end.x) {
		int dir = end.y > start.y? 1: -1;
		start.y = clamp(start.y, -1, canvas.size.height);
		end.y = clamp(end.y, -1, canvas.size.height);
		for (int y = start.y; y != end.y; y += dir) {
			for (int w = -brush_r; w <= brush_r; w++) {
				int x = start.x + w;
				change_canvas_unit(y, x);
			}
		}
		return;
	}

	if (start.y == end.y) {
		int dir = end.x > start.x? 1: -1;
		start.x = clamp(start.x, -1, canvas.size.width);
		end.x = clamp(end.x, -1, canvas.size.width);
		//printf("after clamp:   start = (%d, %d)   end = (%d, %d)\n", start.x, start.y, end.x, end.y);
		for (int x = start.x; x != end.x; x += dir) {
			for (int h = -brush_r; h <= brush_r; h++) {
				int y = start.y + h;
				change_canvas_unit(y, x);
			}
		}
		return;
	}

	int dy = fabsf(start.y - end.y);
	int dx = fabsf(start.x - end.x);
	Vec2i dir = {
		end.x > start.x ? 1 : -1,
		end.y > start.y ? 1 : -1
	};
	if (dy < dx) {
		float pk = 2*dy - dx;
		int y = start.y;
		for (int x = start.x; x != end.x; x += dir.x) {
			int step_y = pk < 0 ? 0 : 1;
			y += step_y*dir.y;
			pk = pk + 2*dy - 2*dx*step_y;
			for (int h = -brush_r; h <= brush_r; h++) {
				int y2 = y + h;
				change_canvas_unit(y2, x);
			}
		}
	} else {
		float pk = 2*dx - dy;
		int x = start.x;
		for (int y = start.y; y != end.y; y += dir.y) {
 			int step_x = pk < 0 ? 0 : 1;
			x += step_x*dir.x;
			pk = pk - 2*dy*step_x + 2*dx;
			for (int w = -brush_r; w <= brush_r; w++) {
				int x2 = x + w;
				change_canvas_unit(y, x2);
			}
		}
	}
}

void update_canvas(double x, double y) {
	float side = canvas.scale * CANVAS_WIDTH;
	y = y - (window_size.height - side);
	int row    = y / side * canvas.size.height;
	int column = x / side * canvas.size.width;

	int pk = 1 - brush_r;
	int max = (float)brush_r/1.414214;
	max = 2*max + 2;
	Vec2i point = { 0, brush_r };
	Vec2i points[max] = { 0, brush_r };
	int ix = 0;
	while (point.x < point.y) {
		//printf("%2d  pk = %3d  ", ix, pk);
		point.x++;
		if (pk < 0) {
			pk = pk + 2*point.x + 1;
		} else {
			point.y--;
			pk = pk + 2*point.x + 1 - 2*point.y;
		}
		//printf("2x = %2d  2y = %2d\n", 2*point.x, 2*point.y);
		ix++;
		points[ix] = point;
	}
	int c = 3;
	if (point.x == point.y) {
		max--;
		c = 2;
	}
	//printf("ix = %d  max = %d\n", ix, max);
	for (int i = ix+1; i-c >= 0; i++) {
		//printf("%2d -> %2d\n", i-c, i);
		points[i].x = points[i-c].y;
		points[i].y = points[i-c].x;
		c += 2;
	}
	ix = 0;
	while (ix < max) {
		//printf("[%2d] %d, %d\n", ix, points[ix].x, points[ix].y);
		for (int i = points[ix].y; i >= -points[ix].y; i--) {
			Vec2i p = { column + points[ix].x, row + i };
			change_canvas_unit(p.y, p.x);
			p.x = column - points[ix].x;
			change_canvas_unit(p.y, p.x);
		}
		ix++;
	}

	//if (fabsf(column - last_pix.x) > 1 || fabsf(row - last_pix.y) > 1) {
		join_points(canvas.history.coords[1] /*last_pix*/, (Vec2i){ column, row });
		canvas.history.coords[0] /*last_pix*/ = { column, row };
	//}
	//change_canvas_unit(row, column);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
	glGenerateMipmap(GL_TEXTURE_2D);
}

void calculate_selected_colors(float ang) {
	ang *= 180.0/3.141593;
	if (ang < 0) ang += 360;
	float r = 0, g = 0, b = 0;
	float f = ang/60.0;
    if (ang < 60) {
        r = 1;
        g = f;
    } else if (ang < 120) {
        r = 2 - f;
        g = 1;
    } else if (ang < 180) {
        g = 1;
        b = f - 2;
    } else if (ang < 240) {
        g = 4 - f;
        b = 1;
    } else if (ang < 300) {
        r = f - 4;
        b = 1;
    } else {
        r = 1;
        b = 6 - f;
    }
	wheel_color = { r, g, b };

	float c = active_color.hsv.v * active_color.hsv.s;
	float m = active_color.hsv.v - c;
	active_color.rgb = {
		c * wheel_color.r + m,
		c * wheel_color.g + m,
		c * wheel_color.b + m
	};
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
	glViewport(0, 0, width, height);
	projection = glm::ortho(0.0f, (float)width, 0.0f, (float)height, 0.1f, 50.0f);
	glUniformMatrix4fv(loc_projection, 1, GL_FALSE, glm::value_ptr(projection));
	window_size.width = width;
	window_size.height = height;

	Vec2 scale = { 1.0, 1.0 };
	Vec2 base_size = { CANVAS_WIDTH + COLOR_WHEEL_SIDE, CANVAS_HEIGHT };
	if (window_size.width < base_size.width) {
		scale.x = window_size.width/base_size.width;
	}
	if (window_size.height < base_size.height) {
		scale.y = window_size.height/base_size.height;
	}
	canvas.scale = scale.x < scale.y ? scale.x : scale.y;

	float radius = (float)COLOR_WHEEL_SIDE/2 * canvas.scale;
	glUniform1f(loc_color_wheel_radius, radius);
	glUniform2f(loc_color_wheel_center, window_size.width - radius, window_size.height - radius);
}

void undo() {
	if (canvas.history.past > 0) {
		canvas.history.past--;
		canvas.history.future++;
		int index = canvas.history.index - 2;
		if (index < 0) index += HISTORY;
		memcpy(canvas.colors, canvas.history.colors[index],
				canvas.size.width * canvas.size.height * sizeof(Vec4uc));
		canvas.history.index = (index + 1) % HISTORY;

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
		glGenerateMipmap(GL_TEXTURE_2D);
		printf("[fn undo] Past: %2d  Future: %2d  Index: %2d\n", canvas.history.past, canvas.history.future, canvas.history.index);
	}
}

void redo() {
	if (canvas.history.future > 0) {
		canvas.history.future--;
		canvas.history.past++;
		int index = canvas.history.index;
		memcpy(canvas.colors, canvas.history.colors[index],
				canvas.size.width * canvas.size.height * sizeof(Vec4uc));
		canvas.history.index = (index + 1) % HISTORY;

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
		glGenerateMipmap(GL_TEXTURE_2D);
		printf("[fn redo] Past: %2d  Future: %2d  Index: %2d\n", canvas.history.past, canvas.history.future, canvas.history.index);
	}
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS) {
		switch (key) {
			case GLFW_KEY_B:
				active_tool = BUTTON_BRUSH;
				glUniform1i(loc_active_tool, active_tool);
				break;
			case GLFW_KEY_F:
				active_tool = BUTTON_BUCKET;
				glUniform1i(loc_active_tool, active_tool);
				break;
			case GLFW_KEY_N:
				if (mods == GLFW_MOD_CONTROL) {
					clear_canvas(true);
				}
				break;
			case GLFW_KEY_Q:
				glfwSetWindowShouldClose(window, true);
				break;
			case GLFW_KEY_Y:
				if (mods == GLFW_MOD_CONTROL) {
					redo();
				}
				break;
			case GLFW_KEY_Z:
				if (mods == GLFW_MOD_CONTROL) {
					undo();
				}
				break;
			case GLFW_KEY_UP:
				if (brush_r < 23) brush_r++;
				break;
			case GLFW_KEY_DOWN:
				if (brush_r > 0) brush_r--;
				break;
		}
	}
}

void boundary_fill(int row, int col) {
	//printf("bf %3d\n", (*(canvas.colors + row*canvas.size.width + col)).a);
	//if ( (*(canvas.colors + row*canvas.size.width + col)).a == 0) {
	if (
			(*(canvas.colors + row*canvas.size.width + col)).r == first_filled_pix.r &&
			(*(canvas.colors + row*canvas.size.width + col)).g == first_filled_pix.g &&
			(*(canvas.colors + row*canvas.size.width + col)).b == first_filled_pix.b &&
			(*(canvas.colors + row*canvas.size.width + col)).a == first_filled_pix.a
	) {
		change_canvas_unit(row, col);

		if (col > 0) boundary_fill(row, col-1);
		if (col < canvas.size.width-1) boundary_fill(row, col+1);
		if (row > 0) boundary_fill(row-1, col);
		if (row < canvas.size.height-1) boundary_fill(row+1, col);
	}
}

void copy_vec4uc(Vec4uc *dest, Vec4uc *src) {
	(*dest).r = (*src).r;
	(*dest).g = (*src).g;
	(*dest).b = (*src).b;
	(*dest).a = (*src).a;
}

void check_ui_elements(double xpos, double ypos) {
	ypos = window_size.height - ypos;

	float canvas_side = canvas.scale * CANVAS_WIDTH;
	if (active_ui_element == CANVAS) {
		canvas.history.coords[2] = canvas.history.coords[1];
		canvas.history.coords[1] = canvas.history.coords[0];
		canvas.history.coords[0] = {
			floor(xpos/canvas_side * canvas.size.width),
			floor((ypos - (window_size.height - canvas_side))/canvas_side * canvas.size.height)
		};
		/*printf("[2] %3d, %3d\n", canvas.history.coords[2].x, canvas.history.coords[2].y);
		printf("[1] %3d, %3d\n", canvas.history.coords[1].x, canvas.history.coords[1].y);
		printf("[0] %3d, %3d\n\n", canvas.history.coords[0].x, canvas.history.coords[0].y);*/
		if (active_tool == BUTTON_BRUSH)
			update_canvas(xpos, ypos);
		// revisar cómo cambia cómo cambia el historial al usar el llenado
		/*else if (active_tool == BUTTON_BUCKET) {
			int col = canvas.history.coords[0].x;
			int row = canvas.history.coords[0].y;
			//printf("Bucket start: %3d, %3d\n", col, row);
			copy_vec4uc(&first_filled_pix, canvas.colors + row*canvas.size.width + col);
			printf("ff_px = { %d, %d, %d, %d }\n", first_filled_pix.r, first_filled_pix.g, first_filled_pix.b, first_filled_pix.a);
			boundary_fill(row, col);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
			glGenerateMipmap(GL_TEXTURE_2D);
		}*/
	}

	if (xpos >= 0 && xpos < canvas_side &&
			ypos >= window_size.height - canvas_side && ypos < window_size.height) {
		hot_ui_element = CANVAS;
		return;
	}

	float sv_side = canvas.scale * COLOR_WHEEL_SIDE;
	Vec2 sv_origen = { window_size.width - sv_side, window_size.height - sv_side };
	if (xpos > sv_origen.x && ypos > sv_origen.y) {
		Vec2 uv = { 2*(xpos - sv_origen.x)/sv_side -1, 2*(ypos - sv_origen.y)/sv_side -1 };
		float d = sqrt(uv.x*uv.x + uv.y*uv.y) - 0.85;
		if (fabsf(d) < 0.11) {
			hot_ui_element = COLOR_WHEEL;
			if (active_ui_element == hot_ui_element) {
				active_color.hsv.h = atan2(uv.y, uv.x);
				calculate_selected_colors(active_color.hsv.h);
				glUniform3fv(loc_wheel_color, 1, &wheel_color.r);
				glUniform3fv(loc_active_color, 1, &active_color.rgb.r);
				glUniform3fv(loc_hsv, 1, &active_color.hsv.h);
			}
			return;
		}
		if (fabsf(uv.x) <= 0.5 && fabsf(uv.y) <= 0.5) {
			hot_ui_element = SV_SQUARE;
			if (active_ui_element == hot_ui_element) {
				//printf("Moving inside the sv square   %.2f\n", glfwGetTime());
				active_color.hsv.v = uv.y + 0.5;
				active_color.hsv.s = uv.x + 0.5;
				float c = active_color.hsv.v * active_color.hsv.s;
				float m = active_color.hsv.v - c;
				active_color.rgb = {
					c * wheel_color.r + m,
					c * wheel_color.g + m,
					c * wheel_color.b + m
				};
				glUniform3f(loc_active_color, active_color.rgb.r, active_color.rgb.g, active_color.rgb.b);
				glUniform3f(loc_hsv, active_color.hsv.h, active_color.hsv.s, active_color.hsv.v);
			}
			return;
		}
	}

		switch (active_ui_element) {
			case BUTTON_CLEAR_CANVAS:
				clear_canvas(true);
				break;
			case BUTTON_UNDO:
				undo();
				break;
			case BUTTON_REDO:
				redo();
				break;
			case BUTTON_BRUSH:
				active_tool = BUTTON_BRUSH;
				glUniform1i(loc_active_tool, active_tool);
				break;
			case BUTTON_BUCKET:
				active_tool = BUTTON_BUCKET;
				glUniform1i(loc_active_tool, active_tool);
				break;
		}

	//last_pix = { -1, -1 };
	hot_ui_element = -1;
}

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_RELEASE) {
			canvas.history.coords[0] = { -1, -1 }; //last_pix = { -1, -1 };
			if (active_ui_element == CANVAS) {
				memcpy(canvas.history.colors[canvas.history.index], canvas.colors,
					canvas.size.width * canvas.size.height * sizeof(Vec4uc));
				printf("Copiado a [%2d]\n", canvas.history.index);
				canvas.history.index = (canvas.history.index + 1) % HISTORY;
				if (canvas.history.past < HISTORY - 1) canvas.history.past++;
				canvas.history.future = 0;
				printf("Sigue [%2d]. Pasado: %2d  Futuro: %2d\n", canvas.history.index, canvas.history.past, canvas.history.future);
			}
			active_ui_element = -1;
		}
		if (action == GLFW_PRESS) {
			double xpos, ypos;
			glfwGetCursorPos(window, &xpos, &ypos);
			active_ui_element = hot_ui_element;
			if (active_ui_element == CANVAS && active_tool == BUTTON_BUCKET) {
				ypos = window_size.height - ypos;
				float canvas_side = canvas.scale * CANVAS_WIDTH;
				canvas.history.coords[2] = canvas.history.coords[1];
				canvas.history.coords[1] = canvas.history.coords[0];
				canvas.history.coords[0] = {
					floor(xpos/canvas_side * canvas.size.width),
					floor((ypos - (window_size.height - canvas_side))/canvas_side * canvas.size.height)
				};
				int col = canvas.history.coords[0].x;
				int row = canvas.history.coords[0].y;
				//printf("Bucket start: %3d, %3d\n", col, row);
				copy_vec4uc(&first_filled_pix, canvas.colors + row*canvas.size.width + col);
				printf("ff_px = { %d, %d, %d, %d }\n", first_filled_pix.r, first_filled_pix.g, first_filled_pix.b, first_filled_pix.a);
				boundary_fill(row, col);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
				glGenerateMipmap(GL_TEXTURE_2D);
			} else
				check_ui_elements(xpos, ypos);
		}
	}
}

bool collision_point_rectangle(double px, double py, float rx, float ry, float w, float h) {
	if (px < rx || px > rx+w) return false;
	if (py < ry || py > ry+h) return false;
	return true;
}

static void cursor_position_callback(GLFWwindow *window, double xpos, double ypos) {
	//printf("cursor pos callback %.4f, %.4f\n", xpos, ypos);
	mouse = { xpos, window_size.height - ypos };
	check_ui_elements(xpos, ypos);
}

int main() {
	//printf("%d\n", canvas.size.width*canvas.size.height*sizeof(Vec4uc));
	//return 0;
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow *window = glfwCreateWindow(window_size.width, window_size.height, "Floating window", NULL, NULL);
	if (window == NULL) {
		printf("Failed to create GLFW window\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);

	glewInit();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	Quad quads[] = {
		initialize_quad(0, -CANVAS_HEIGHT, CANVAS_WIDTH, CANVAS_HEIGHT,   0, 0, 1, 1),
		initialize_quad(-COLOR_WHEEL_SIDE, -COLOR_WHEEL_SIDE, COLOR_WHEEL_SIDE, COLOR_WHEEL_SIDE,   0, 0, 1, 1),
		initialize_quad(-COLOR_WHEEL_SIDE, -COLOR_WHEEL_SIDE-55, 36, 36,   0.0, 0.5, 0.5, 1.0),
		initialize_quad(-COLOR_WHEEL_SIDE+40, -COLOR_WHEEL_SIDE-55, 36, 36,   0.5, 0.5, 1.0, 1.0),
		initialize_quad(-COLOR_WHEEL_SIDE+80, -COLOR_WHEEL_SIDE-55, 36, 36, 1.0, 0.5, 0.5, 1),
		initialize_quad(-COLOR_WHEEL_SIDE, -COLOR_WHEEL_SIDE-105, 36, 36,   0.0, 0.0, 0.5, 0.5),
		initialize_quad(-COLOR_WHEEL_SIDE+40, -COLOR_WHEEL_SIDE-105, 36, 36,   0.5, 0.0, 1.0, 0.5),
	};

	int nq = sizeof(quads)/sizeof(quads[0]);
	unsigned int indices[6 * nq];
	for (int i = 0; i < nq; i++) {
		int p = i * 6;
		int v = i * 4;
		indices[p]   = v;
		indices[p+1] = v + 1;
		indices[p+2] = v + 2;
		indices[p+3] = v;
		indices[p+4] = v + 2;
		indices[p+5] = v + 3;
	};

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quads), quads, GL_STATIC_DRAW);

	GLuint ebo;
	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(Vec3)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(Vec3) + sizeof(Vec2)));
	glEnableVertexAttribArray(2);

	for (int i = 0; i < HISTORY; i++) {
		canvas.history.colors[i] = (Vec4uc *)malloc(canvas.size.width * canvas.size.height * sizeof(Vec4uc));
	}
	canvas.history.coords[0] = { -1, -1 };
	canvas.colors = (Vec4uc *)malloc(canvas.size.width * canvas.size.height * sizeof(Vec4uc));
	for (int i = 0; i < canvas.size.height; i++) {
		for (int j = 0; j < canvas.size.width; j++) {
			*(canvas.colors + i*canvas.size.width + j) = { 0, 0, 0, 0 };
			*(canvas.history.colors[0] + i*canvas.size.width + j) = { 0, 0, 0, 0 };
		}
	}

	glGenTextures(1, &canvas.texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, canvas.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); //
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.size.width, canvas.size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.colors);
	glGenerateMipmap(GL_TEXTURE_2D);

	unsigned int texture_btn;
	glGenTextures(1, &texture_btn);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, texture_btn);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	stbi_set_flip_vertically_on_load(true);
	int width, height, n_ch;
	unsigned char *data	= stbi_load("tex_btns.png", &width, &height, &n_ch, 0);
	if (data) {
		/*for (int i = 0; i < height; i++) {
			for (int j = 0; j < width; j++) {
				printf("%3d  ", *(data+i*width+j));
			}
			printf("\n");
		}*/
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	} else return 1;
	stbi_image_free(data);
	glActiveTexture(GL_TEXTURE0);

	Shader program = Shader("shader.vert", "shader.frag");
	program.use();
	model = glm::mat4(1.0f);
	view = glm::mat4(1.0f);
	projection = glm::ortho(0.0f, 100.0f, 0.0f, 100.0f, 0.1f, 50.0f);
	loc_model = glGetUniformLocation(program.id, "model");
	loc_view = glGetUniformLocation(program.id, "view");
	loc_projection = glGetUniformLocation(program.id, "projection");
	glUniformMatrix4fv(loc_view, 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(loc_projection, 1, GL_FALSE, glm::value_ptr(projection));

	loc_hot_ui_element = glGetUniformLocation(program.id, "hot_ui_element");
	glUniform1i(loc_hot_ui_element, -1);
	loc_active_tool = glGetUniformLocation(program.id, "active_tool");
	glUniform1i(loc_active_tool, active_tool);
	loc_color_wheel_radius = glGetUniformLocation(program.id, "color_wheel_radius");
	glUniform1f(loc_color_wheel_radius, 100.0);
	loc_color_wheel_center = glGetUniformLocation(program.id, "color_wheel_center");
	glUniform2f(loc_color_wheel_center, CANVAS_WIDTH + COLOR_WHEEL_SIDE, CANVAS_HEIGHT);
	loc_wheel_color = glGetUniformLocation(program.id, "wheel_color");
	glUniform3f(loc_wheel_color, wheel_color.r, wheel_color.g, wheel_color.b);
	loc_active_color = glGetUniformLocation(program.id, "active_color");
	glUniform3f(loc_active_color, active_color.rgb.r, active_color.rgb.g, active_color.rgb.b);
	loc_hsv = glGetUniformLocation(program.id, "hsv");
	glUniform3f(loc_hsv, active_color.hsv.h, active_color.hsv.s, active_color.hsv.v);
	loc_canvas = glGetUniformLocation(program.id, "canvas");
	glUniform1i(loc_canvas, 0);
	loc_tex_btn = glGetUniformLocation(program.id, "tex_btn");
	glUniform1i(loc_tex_btn, 1);

	glClearColor(0.2, 0.2, 0.2, 1.0);

	while (!glfwWindowShouldClose(window)){
		glClear(GL_COLOR_BUFFER_BIT);

		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, window_size.height, 0.0f));
		model = glm::scale(model, glm::vec3(canvas.scale, canvas.scale, 1.0f));
		glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		for (int i = 2; i < nq; i++) {
			if (collision_point_rectangle(
					(mouse.x - window_size.width)/canvas.scale,
					(mouse.y - window_size.height)/canvas.scale,
					quads[i].v[0].pos.x,
					quads[i].v[0].pos.y,
					quads[i].v[2].pos.x - quads[i].v[0].pos.x,
					quads[i].v[2].pos.y - quads[i].v[0].pos.y)
				) {
				//printf("sobre q[%d]  %.2f\n", i, glfwGetTime());
				//selected = i;
				//busy = 1;
				hot_ui_element = BUTTON_CLEAR_CANVAS - 1 + i - 1;
				break;
			}// else {
				//busy = 0;
				//selected = -1;
			//}
		}
		glUniform1i(loc_hot_ui_element, hot_ui_element-1);

		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(window_size.width, window_size.height, 0.0f));
		model = glm::scale(model, glm::vec3(canvas.scale, canvas.scale, 1.0f));
		glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
		glDrawElements(GL_TRIANGLES, 6*(nq-1), GL_UNSIGNED_INT, (void*)(6 * sizeof(unsigned int)));

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwTerminate();

	return 0;
}
