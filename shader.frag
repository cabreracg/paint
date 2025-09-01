#version 330 core

in float quad_index;
in vec2 nor_coord;
in vec2 tex_coord;

out vec4 frag_color;

uniform int hot_ui_element;
uniform int active_tool;
uniform float color_wheel_radius;
uniform vec2 color_wheel_center;
uniform vec3 wheel_color;
uniform vec3 active_color;
uniform vec3 hsv;
uniform sampler2D canvas;
uniform sampler2D tex_btn;

vec4 hue_to_rgb(float ang) {
	ang *= 180.0/3.14159;
	if (ang < 0) ang += 360;
	float r = 0.0, g = 0.0, b = 0.0;
	float f = ang/60.0;
    if (ang < 60.) {
        r = 1.0;
        g = f;
    } else if (ang < 120.) {
        r = 2.0 - f;
        g = 1.0;
    } else if (ang < 180.) {
        g = 1.0;
        b = f - 2.0;
    } else if (ang < 240.) {
        g = 4.0 - f;
        b = 1.0;
    } else if (ang < 300.) {
        r = f - 4.0;
        b = 1.0;
    } else {
        r = 1.0;
        b = 6.0 - f;
    }
	return vec4(r, g, b, 1.0);
}

//void main() {
	/*vec2 pos = vec2(gl_FragCoord.xy/8); // 8
	float dis = int(pos.x) % 2 - int(pos.y) % 2;
	float c = 0.2 * (1 + dis) * (1 - dis);
	c += 0.75;
	vec4 pattern = vec4(c, c, c, 1.0);
	vec4 canvas_color = texture(canvas, tex_coord);
	frag_color = mix(pattern, canvas_color, canvas_color.a);*/
	//frag_color = vec4(tex_coord, 0.0, 1.0);
//}

void main() {
	if (abs(gl_FragCoord.x - color_wheel_center.x) <= color_wheel_radius) {
		vec4 bg_color = vec4(0.25, 0.25, 0.25, 1.0);
		frag_color = bg_color;
		vec2 uv = vec2(gl_FragCoord.xy - (color_wheel_center - color_wheel_radius));
		uv = uv/color_wheel_radius - 1;
		//vec2 uv = tex_coord * 2 - 1;
		float d = length(uv) - 0.85;
		if (abs(d) < 0.11) {
			d = abs(d);
			//float ang = atan(gl_FragCoord.y - color_wheel_center.y, gl_FragCoord.x - color_wheel_center.x);
			float ang = atan(uv.y, uv.x);
			vec4 wheel_color = hue_to_rgb(ang);
			float ssw = smoothstep(0.1, 0.11, d);
			float d2 = abs(ang - hsv.x);
			float ssa = smoothstep(0, 0.025, abs(d2));
			frag_color = mix(bg_color, wheel_color, (1-ssw)*ssa);
		}
		if (abs(uv.x) <= 0.5 && abs(uv.y) < 0.5) {
			float v = uv.y + 0.5;
			float s = uv.x + 0.5;
			float c = v * s;
			float m = v - c;
			vec2 point = vec2(s, v);
			if (abs(length(point - hsv.yz) - 0.05) < 0.01) {
				float w = 1 - round(hsv.z);
				frag_color = vec4(w, w, w, 1.0);
			}
			else frag_color = vec4(c*wheel_color + m, 1.0);
		}
		if (uv.y >= -1.0) {
			if (uv.x < -0.8 && uv.y < -0.8) {
				frag_color = vec4(active_color, 1.0);
			}
		} else {
			frag_color.a = 0.0;
			vec2 new_coord = nor_coord * 2 - 1;
			vec2 r = vec2(0.6, 0.6);
			if (length(max(abs(new_coord)-r, 0)) - (1.0 - r.x) <= 0) {
				vec3 icon_bg = vec3(0.35, 0.35, 0.35);
				int qi = int(quad_index);
				if (qi == active_tool - 1)
					icon_bg -= 0.25;
				else if (qi == hot_ui_element)
					icon_bg += 0.1;
				frag_color = vec4(icon_bg, 1.0);
			}
			vec4 icon = texture(tex_btn, tex_coord);
			frag_color = mix(frag_color, icon, icon.a);
		}
	}
	else {
		vec2 pos = vec2(gl_FragCoord.xy/8); // 8
		float dis = int(pos.x) % 2 - int(pos.y) % 2;
		float c = 0.15 * (1 + dis) * (1 - dis);
		c += 0.8;
		vec4 pattern = vec4(c, c, c, 1.0);
		vec4 canvas_color = texture(canvas, tex_coord);
		frag_color = mix(pattern, canvas_color, canvas_color.a);
		//frag_color = pattern;
	}
}
