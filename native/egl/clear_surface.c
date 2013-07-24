/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@codethink.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define HOOK

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>

#include "esTransform.h"
#include "eglutil.h"
#include "dump_gl_screen.h"
#include "viv_hook.h"

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE, 8,
	EGL_NONE
};

static EGLint const pbuffer_attribute_list[] = {
	EGL_WIDTH, 400,
	EGL_HEIGHT, 240,
	EGL_LARGEST_PBUFFER, EGL_TRUE,
    EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

int main(int argc, char *argv[])
{
	EGLDisplay display;
	EGLint egl_major, egl_minor;
	EGLConfig config;
	EGLint num_config;
	EGLContext context;
	EGLSurface surface;
	GLuint vertex_shader;
	GLuint fragment_shader;
	GLuint program;
	GLint ret;
	GLint width, height;
#ifdef HOOK
        the_hook("/home/debian/egl_clear_surface.fdr");
#endif

	const char *vertex_shader_source =
	  "uniform mat4 modelviewMatrix;\n"
	  "uniform mat4 modelviewprojectionMatrix;\n"
	  "uniform mat3 normalMatrix;\n"
	  "\n"
	  "attribute vec4 in_position;    \n"
	  "attribute vec3 in_normal;      \n"
	  "attribute vec4 in_color;       \n"
	  "\n"
	  "vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);\n"
	  "                             \n"
	  "varying vec4 vVaryingColor;         \n"
	  "                             \n"
	  "void main()                  \n"
	  "{                            \n"
	  "    gl_Position = modelviewprojectionMatrix * in_position;\n"
	  "    vec3 vEyeNormal = normalMatrix * in_normal;\n"
	  "    vec4 vPosition4 = modelviewMatrix * in_position;\n"
	  "    vec3 vPosition3 = vPosition4.xyz / vPosition4.w;\n"
	  "    vec3 vLightDir = normalize(lightSource.xyz - vPosition3);\n"
	  "    float diff = max(0.0, dot(vEyeNormal, vLightDir));\n"
	  "    vVaryingColor = vec4(diff * in_color.rgb, 1.0);\n"
	  "}                            \n";

	const char *fragment_shader_source =
	  "precision mediump float;     \n"
	  "                             \n"
	  "varying vec4 vVaryingColor;         \n"
	  "                             \n"
	  "void main()                  \n"
	  "{                            \n"
	  "    gl_FragColor = vVaryingColor;   \n"
	  "}                            \n";

	GLfloat vVertices[] = {
	  // front
	  -1.0f, -1.0f, +1.0f, // point blue
	  +1.0f, -1.0f, +1.0f, // point magenta
	  -1.0f, +1.0f, +1.0f, // point cyan
	  +1.0f, +1.0f, +1.0f, // point white
	  // back
	  +1.0f, -1.0f, -1.0f, // point red
	  -1.0f, -1.0f, -1.0f, // point black
	  +1.0f, +1.0f, -1.0f, // point yellow
	  -1.0f, +1.0f, -1.0f, // point green
	  // right
	  +1.0f, -1.0f, +1.0f, // point magenta
	  +1.0f, -1.0f, -1.0f, // point red
	  +1.0f, +1.0f, +1.0f, // point white
	  +1.0f, +1.0f, -1.0f, // point yellow
	  // left
	  -1.0f, -1.0f, -1.0f, // point black
	  -1.0f, -1.0f, +1.0f, // point blue
	  -1.0f, +1.0f, -1.0f, // point green
	  -1.0f, +1.0f, +1.0f, // point cyan
	  // top
	  -1.0f, +1.0f, +1.0f, // point cyan
	  +1.0f, +1.0f, +1.0f, // point white
	  -1.0f, +1.0f, -1.0f, // point green
	  +1.0f, +1.0f, -1.0f, // point yellow
	  // bottom
	  -1.0f, -1.0f, -1.0f, // point black
	  +1.0f, -1.0f, -1.0f, // point red
	  -1.0f, -1.0f, +1.0f, // point blue
	  +1.0f, -1.0f, +1.0f  // point magenta
	};

	GLfloat vColors[] = {
	  // front
	  0.0f,  0.0f,  1.0f, // blue
	  1.0f,  0.0f,  1.0f, // magenta
	  0.0f,  1.0f,  1.0f, // cyan
	  1.0f,  1.0f,  1.0f, // white
	  // back
	  1.0f,  0.0f,  0.0f, // red
	  0.0f,  0.0f,  0.0f, // black
	  1.0f,  1.0f,  0.0f, // yellow
	  0.0f,  1.0f,  0.0f, // green
	  // right
	  1.0f,  0.0f,  1.0f, // magenta
	  1.0f,  0.0f,  0.0f, // red
	  1.0f,  1.0f,  1.0f, // white
	  1.0f,  1.0f,  0.0f, // yellow
	  // left
	  0.0f,  0.0f,  0.0f, // black
	  0.0f,  0.0f,  1.0f, // blue
	  0.0f,  1.0f,  0.0f, // green
	  0.0f,  1.0f,  1.0f, // cyan
	  // top
	  0.0f,  1.0f,  1.0f, // cyan
	  1.0f,  1.0f,  1.0f, // white
	  0.0f,  1.0f,  0.0f, // green
	  1.0f,  1.0f,  0.0f, // yellow
	  // bottom
	  0.0f,  0.0f,  0.0f, // black
	  1.0f,  0.0f,  0.0f, // red
	  0.0f,  0.0f,  1.0f, // blue
	  1.0f,  0.0f,  1.0f  // magenta
	};

	GLfloat vNormals[] = {
	  // front
	  +0.0f, +0.0f, +1.0f, // forward
	  +0.0f, +0.0f, +1.0f, // forward
	  +0.0f, +0.0f, +1.0f, // forward
	  +0.0f, +0.0f, +1.0f, // forward
	  // back
	  +0.0f, +0.0f, -1.0f, // backbard
	  +0.0f, +0.0f, -1.0f, // backbard
	  +0.0f, +0.0f, -1.0f, // backbard
	  +0.0f, +0.0f, -1.0f, // backbard
	  // right
	  +1.0f, +0.0f, +0.0f, // right
	  +1.0f, +0.0f, +0.0f, // right
	  +1.0f, +0.0f, +0.0f, // right
	  +1.0f, +0.0f, +0.0f, // right
	  // left
	  -1.0f, +0.0f, +0.0f, // left
	  -1.0f, +0.0f, +0.0f, // left
	  -1.0f, +0.0f, +0.0f, // left
	  -1.0f, +0.0f, +0.0f, // left
	  // top
	  +0.0f, +1.0f, +0.0f, // up
	  +0.0f, +1.0f, +0.0f, // up
	  +0.0f, +1.0f, +0.0f, // up
	  +0.0f, +1.0f, +0.0f, // up
	  // bottom
	  +0.0f, -1.0f, +0.0f, // down
	  +0.0f, -1.0f, +0.0f, // down
	  +0.0f, -1.0f, +0.0f, // down
	  +0.0f, -1.0f, +0.0f  // down
	};

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display == EGL_NO_DISPLAY) {
		printf("Error: No display found!\n");
		return -1;
	}

	if (!eglInitialize(display, &egl_major, &egl_minor)) {
		printf("Error: eglInitialise failed!\n");
		return -1;
	}

	printf("Using display %p with EGL version %d.%d\n",
	       display, egl_major, egl_minor);

	printf("EGL Version \"%s\"\n", eglQueryString(display, EGL_VERSION));
	printf("EGL Vendor \"%s\"\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL Extensions \"%s\"\n", eglQueryString(display, EGL_EXTENSIONS));

	/* get an appropriate EGL frame buffer configuration */
	eglChooseConfig(display, config_attribute_list, &config, 1, &num_config);

	/* create an EGL rendering context */
	context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list);
	if (context == EGL_NO_CONTEXT) {
		printf("Error: eglCreateContext failed: %d\n", eglGetError());
		return -1;
	}

	surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list);
	if (surface == EGL_NO_SURFACE) {
		printf("Error: eglCreatePbufferSurface failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}

	if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
	    !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
		printf("Error: eglQuerySurface failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}
	printf("PBuffer: %dx%d\n", width, height);
        printf("GL Extensions \"%s\"\n", glGetString(GL_EXTENSIONS));

	/* connect the context to the surface */
	if (!eglMakeCurrent(display, surface, surface, context)) {
		printf("Error: eglMakeCurrent() failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}

	//vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	//if (!vertex_shader) {
	//	printf("Error: glCreateShader(GL_VERTEX_SHADER) failed: %d (%s)\n",
	//	       eglGetError(), eglStrError(eglGetError()));
	//	return -1;
	//}


	//glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
	//glCompileShader(vertex_shader);

	/*glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: vertex shader compilation failed!:\n");
		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(vertex_shader, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("Vertex shader compilation succeeded!\n");

	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fragment_shader) {
		printf("Error: glCreateShader(GL_FRAGMENT_SHADER) failed: %d (%s)\n",
		       eglGetError(), eglStrError(eglGetError()));
		return -1;
	}


	glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
	glCompileShader(fragment_shader);*/

	/*glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: fragment shader compilation failed!:\n");
		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(fragment_shader, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("Fragment shader compilation succeeded!\n");

	program = glCreateProgram();
	if (!program) {
		printf("Error: failed to create program!\n");
		return -1;
	}

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	glBindAttribLocation(program, 0, "in_position");
	glBindAttribLocation(program, 1, "in_normal");
	glBindAttribLocation(program, 2, "in_color");

	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("Error: program linking failed!:\n");
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetProgramInfoLog(program, ret, NULL, log);
			printf("%s", log);
		}
		return -1;
	} else
		printf("program linking succeeded!\n");

	glUseProgram(program);*/

	glViewport(0, 0, width, height);

	/* clear the color buffer */
	glClearColor(0.5, 0.5, 0.5, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	//glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices);
	//glEnableVertexAttribArray(0);

	//glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, vNormals);
	//glEnableVertexAttribArray(1);

	//glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, vColors);
	//glEnableVertexAttribArray(2);

	/*ESMatrix modelview;
	esMatrixLoadIdentity(&modelview);
	esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
	esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
	esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
	esRotate(&modelview, 10.0f, 0.0f, 0.0f, 1.0f);

	GLfloat aspect = (GLfloat)(height) / (GLfloat)(width);

	ESMatrix projection;
	esMatrixLoadIdentity(&projection);
	esFrustum(&projection, -2.8f, +2.8f, -2.8f * aspect, +2.8f * aspect, 6.0f, 10.0f);

	ESMatrix modelviewprojection;
	esMatrixLoadIdentity(&modelviewprojection);
	esMatrixMultiply(&modelviewprojection, &modelview, &projection);

	float normal[9];
	normal[0] = modelview.m[0][0];
	normal[1] = modelview.m[0][1];
	normal[2] = modelview.m[0][2];
	normal[3] = modelview.m[1][0];
	normal[4] = modelview.m[1][1];
	normal[5] = modelview.m[1][2];
	normal[6] = modelview.m[2][0];
	normal[7] = modelview.m[2][1];
	normal[8] = modelview.m[2][2];

	GLint modelviewmatrix_handle = glGetUniformLocation(program, "modelviewMatrix");
	GLint modelviewprojectionmatrix_handle = glGetUniformLocation(program, "modelviewprojectionMatrix");
	GLint normalmatrix_handle = glGetUniformLocation(program, "normalMatrix");

	glUniformMatrix4fv(modelviewmatrix_handle, 1, GL_FALSE, &modelview.m[0][0]);
	glUniformMatrix4fv(modelviewprojectionmatrix_handle, 1, GL_FALSE, &modelviewprojection.m[0][0]);
	glUniformMatrix3fv(normalmatrix_handle, 1, GL_FALSE, normal);

        printf("Model view\n");
        for(int i=0; i<4; ++i)
        {
            printf(" ");
            for(int j=0; j<4; ++j)
                printf("%f ", modelview.m[i][j]);
            printf("\n");
        }
        printf("Model view projection\n");
        for(int i=0; i<4; ++i)
        {
            printf(" ");
            for(int j=0; j<4; ++j)
                printf("%f ", modelviewprojection.m[i][j]);
            printf("\n");
        }

	glEnable(GL_CULL_FACE);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 4, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 8, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 12, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 16, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 20, 4);*/

	glFlush();

	fflush(stdout);
        dump_gl_screen("/home/debian/egl_clear_surface.bmp", width, height);

#ifdef HOOK
        close_hook();
#endif

	return 0;
}
