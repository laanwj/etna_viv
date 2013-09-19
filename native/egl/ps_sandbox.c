/*
 * Copyright (c) 2013 Wladimir J. van der Laan
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
/* Pixel shader playground */
#include <stdio.h>
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
#include <stdlib.h>

#include "esTransform.h"
#include "eglutil.h"
#include "dump_gl_screen.h"
#include "viv_hook.h"

static EGLint const config_attribute_list[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    //EGL_ALPHA_SIZE, 8,
    //EGL_SAMPLE_BUFFERS, 1,
    //EGL_SAMPLES, 2,
    //EGL_SAMPLES, 4,
    //EGL_RED_SIZE, 4,
    //EGL_GREEN_SIZE, 4,
    //EGL_BLUE_SIZE, 4,
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_DEPTH_SIZE, 8,
    EGL_NONE
};

static EGLint const pbuffer_attribute_list[] = {
    EGL_WIDTH, 256,
    EGL_HEIGHT, 256,
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

    the_hook("/mnt/sdcard/egl2.fdr");

    const char *vertex_shader_source =
      "attribute vec4 in_position;  \n"
      "attribute vec2 in_coord;     \n"
      "\n"
      "varying vec2 coord;          \n"
      "                             \n"
      "void main()                  \n"
      "{                            \n"
      "    gl_Position = in_position;\n"
      "    coord = in_coord;        \n"
      "}                            \n";

    const char *fragment_shader_source =
      "precision mediump float;     \n"
      "                             \n"
      "varying vec2 coord;         \n"
      "                             \n"
      "void main()                  \n"
      "{                            \n"
      "    gl_FragColor = vec4(coord, 0.0, 1.0);   \n"
      "}                            \n";

    GLfloat vVertices[] = {
      -1.0f, -1.0f, +0.0f,
      +1.0f, -1.0f, +0.0f,
      -1.0f, +1.0f, +0.0f,
      +1.0f, +1.0f, +0.0f,
    };

    GLfloat vTexCoords[] = {
      +0.0f, +0.0f, 
      +1.0f, +0.0f, 
      +0.0f, +1.0f, 
      +1.0f, +1.0f, 
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

    eglChooseConfig(display, config_attribute_list, &config, 1, &num_config);
    printEGLConfig(display, config);

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

    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("Error: eglMakeCurrent() failed: %d (%s)\n",
               eglGetError(), eglStrError(eglGetError()));
        return -1;
    }

    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    if (!vertex_shader) {
        printf("Error: glCreateShader(GL_VERTEX_SHADER) failed: %d (%s)\n",
               eglGetError(), eglStrError(eglGetError()));
        return -1;
    }


    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);

    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
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
    glCompileShader(fragment_shader);

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
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
    glBindAttribLocation(program, 1, "in_coord");

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

    glUseProgram(program);

    glViewport(0, 0, width, height);

    /* clear the color buffer */
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vTexCoords);
    glEnableVertexAttribArray(1);

    //glEnable(GL_CULL_FACE);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFlush();

    fflush(stdout);
    dump_gl_screen("/sdcard/egl2.bmp", width, height);

    close_hook();

    return 0;
}
