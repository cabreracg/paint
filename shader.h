#ifndef SHADER_H
#define SHADER_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <fstream>
#include <sstream>
#include <string>

class Shader {
	public:
		unsigned int id;

		Shader(const char* vertex_shader_path, const char* fragment_shader_path) {
			std::string vertex_code;
			std::string fragment_code;
			std::ifstream vertex_shader_file;
			std::ifstream fragment_shader_file;

			vertex_shader_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
			fragment_shader_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

			try {
				vertex_shader_file.open(vertex_shader_path);
				fragment_shader_file.open(fragment_shader_path);

				std::stringstream vertex_shader_stream, fragment_shader_stream;
				vertex_shader_stream << vertex_shader_file.rdbuf();
				fragment_shader_stream << fragment_shader_file.rdbuf();

				vertex_shader_file.close();
				fragment_shader_file.close();

				vertex_code = vertex_shader_stream.str();
				fragment_code = fragment_shader_stream.str();
			}
			catch (std::ifstream::failure e) {
				std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ\n";
			}
			const char* vertex_shader_code = vertex_code.c_str();
			const char* fragment_shader_code = fragment_code.c_str();

			unsigned int vertex, fragment;
			int success;
			char info_log[512];

			vertex = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(vertex, 1, &vertex_shader_code, NULL);
			glCompileShader(vertex);

			glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
			if (!success) {
				glGetShaderInfoLog(vertex, 512, NULL, info_log);
				std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << info_log << std::endl;
			}

			fragment = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(fragment, 1, &fragment_shader_code, NULL);
			glCompileShader(fragment);

			glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
			if (!success) {
				glGetShaderInfoLog(fragment, 512, NULL, info_log);
				std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << info_log << std::endl;
			}

			id = glCreateProgram();
			glAttachShader(id, vertex);
			glAttachShader(id, fragment);
			glLinkProgram(id);

			glGetProgramiv(id, GL_LINK_STATUS, &success);
			if (!success) {
				glGetProgramInfoLog(id, 512, NULL, info_log);
				std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << info_log << std::endl;
			}

			glDeleteShader(vertex);
			glDeleteShader(fragment);
		}

		void use() {
			glUseProgram(id);
		}

		void setInt(const std::string &name, int value) {
			glUniform1i(glGetUniformLocation(id, name.c_str()), value);
		}

		void setVec3(const std::string &name, float x, float y, float z) {
			glUniform3f(glGetUniformLocation(id, name.c_str()), x, y, z);
		}

		void setVec3A(const std::string &name, float *values) {
			glUniform3f(glGetUniformLocation(id, name.c_str()),
				*values, *(values+1), *(values+2)
			);
		}

		void setIvec2(const std::string &name, int v1, int v2) {
			glUniform2i(glGetUniformLocation(id, name.c_str()),
				v1, v2
			);
		}

		void setIvec2A(const std::string &name, int *values) {
			glUniform2i(glGetUniformLocation(id, name.c_str()),
				*values, *(values+1)
			);
		}
};

#endif
