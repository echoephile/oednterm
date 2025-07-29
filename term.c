#define GLAD_GLES2_USE_SYSTEM_EGL
#define GLAD_GLES2_IMPLEMENTATION
#include "gles2.h"

#include <GLFW/glfw3.h>

#include <stdbool.h>
#include <stdio.h>

#include "font_data.h"
#include "shaders.h"
#include "encoding.h"

#define MAX_USER_INPUT (256)

#define return_defer(Result) do { result = (Result); goto defer; } while (0)

static const char* gl_error_names[] = {
    [GL_NO_ERROR] = "No Error",
    [GL_INVALID_ENUM] = "Invalid Enum",
    [GL_INVALID_VALUE] = "Invalid Value",
    [GL_INVALID_OPERATION] = "Invalid Operation",
    [GL_INVALID_FRAMEBUFFER_OPERATION] = "Invalid Framebuffer Operation",
    [GL_OUT_OF_MEMORY] = "Out Of Memory",
};

typedef struct vertex {
    unsigned short x, y;
    unsigned char c, style, tx, ty;
} vertex;

typedef struct term_state {
    GLFWwindow* window;
    bool is_fullscreen;
    int previous_width, previous_height;

    int glyph_width, glyph_height;
    int aspect;
    float scale;
    int columns, rows;

    GLuint font;
    GLuint vbo;
    GLuint uniform_glyph_aspect, uniform_viewport_size;
    unsigned int max_vertices, vertex_count;
    vertex* vertex_data;

    unsigned char user_input[MAX_USER_INPUT];
    int user_cursor, user_length;
    unsigned char buffer[262144];
} term_state;

static struct {
    int x, y;
} aspect_ratios[] = {
    { 1, 1 },
    { 2, 2 },
    { 2, 3 },
    { 3, 3 },
    { 3, 4 },
    { 4, 4 },
    { 4, 5 },
    { 0 },
};

static term_state state = {
    .glyph_width = 10,
    .glyph_height = 16,
    .aspect = 1,
    .scale = 1.0,
};

static bool check_gl_errors(const char* file, int line) {
    bool has_errored = false;

    GLenum err;
    while(err = glGetError(), err != GL_NO_ERROR) {
        has_errored = true;
        fprintf(stderr, "OpenGL Error: %s:%d: %s\n", file, line, gl_error_names[err]);
    }

    return has_errored;
}

static void log_term_state() {
    if (state.window == NULL) return;

    int width, height;
    glfwGetWindowSize(state.window, &width, &height);

    int fb_width, fb_height;
    glfwGetFramebufferSize(state.window, &fb_width, &fb_height);

    GLFWmonitor* monitor = glfwGetWindowMonitor(state.window);
    if (monitor == NULL) monitor = glfwGetPrimaryMonitor();
    int monitor_x, monitor_y;
    glfwGetMonitorPos(monitor, &monitor_x, &monitor_y);
    const char* monitor_name = glfwGetMonitorName(monitor);

    const GLFWvidmode* video_mode = glfwGetVideoMode(monitor);

    fprintf(stderr, "Terminal State:\n");
    fprintf(stderr, "  Monitor:               %s\n", monitor_name);
    fprintf(stderr, "  Monitor Width:         %i\n", video_mode->width);
    fprintf(stderr, "  Monitor Height:        %i\n", video_mode->height);
    fprintf(stderr, "  Monitor X:             %i\n", monitor_x);
    fprintf(stderr, "  Monitor Y:             %i\n", monitor_y);
    fprintf(stderr, "  Is Fullscreen:         %s\n", state.is_fullscreen ? "true" : "false");
    fprintf(stderr, "  Window Width:          %i\n", width);
    fprintf(stderr, "  Window Height:         %i\n", height);
    fprintf(stderr, "  Framebuffer Width:     %i\n", fb_width);
    fprintf(stderr, "  Framebuffer Height:    %i\n", fb_height);
    fprintf(stderr, "  Glyph Width:           %i\n", state.glyph_width);
    fprintf(stderr, "  Glyph Height:          %i\n", state.glyph_height);
    fprintf(stderr, "  Aspect X:              %i\n", aspect_ratios[state.aspect].x);
    fprintf(stderr, "  Aspect Y:              %i\n", aspect_ratios[state.aspect].y);
    fprintf(stderr, "  Scale:                 %f\n", state.scale);
    fprintf(stderr, "  Column Count:          %i\n", state.columns);
    fprintf(stderr, "  Row Count:             %i\n", state.rows);
    fprintf(stderr, "\n");
}

static void set_terminal_rows_and_columns() {
    int aspect_x = aspect_ratios[state.aspect].x;
    int aspect_y = aspect_ratios[state.aspect].y;

    int cell_width = aspect_x * state.glyph_width;
    int cell_height = aspect_y * state.glyph_height;

    int width, height;
    glfwGetWindowSize(state.window, &width, &height);

    state.columns = width / cell_width;
    state.rows = height / cell_height;
}

static void toggle_fullscreen() {
    if (state.is_fullscreen) {
        if (state.previous_width == 0) {
            state.previous_width = state.glyph_width * 80;
            state.previous_height = state.glyph_height * 25;
        }

        glfwSetWindowSize(state.window, state.previous_width, state.previous_height);
        state.is_fullscreen = false;
    } else {
        glfwGetWindowSize(state.window, &state.previous_width, &state.previous_height);

        GLFWmonitor* monitor = glfwGetWindowMonitor(state.window);
        if (monitor == NULL) monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* video_mode = glfwGetVideoMode(monitor);

        glfwSetWindowMonitor(state.window, monitor, 0, 0, video_mode->width, video_mode->height, video_mode->refreshRate);
        state.is_fullscreen = true;
    }

    //set_terminal_rows_and_columns();
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (window != state.window) return; // just in case?
    glViewport(0, 0, width, height);
    set_terminal_rows_and_columns();
    log_term_state();
}

#define GL_CHECK(...) do { __VA_ARGS__; if (check_gl_errors(__FILE__, __LINE__)) exit(1); } while (0)

static void render_init() {
    glClearColor(0.025f, 0.025f, 0.025f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GL_CHECK(glUniform2f(state.uniform_glyph_aspect, (float)aspect_ratios[state.aspect].x, (float)aspect_ratios[state.aspect].y));

    int fb_width, fb_height;
    glfwGetFramebufferSize(state.window, &fb_width, &fb_height);
    GL_CHECK(glUniform2f(state.uniform_viewport_size, (float)fb_width, (float)fb_height));
}

static void render_flush() {
    if (state.vertex_count == 0) return;
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, state.vertex_count * sizeof(vertex), state.vertex_data, GL_DYNAMIC_DRAW));
    GL_CHECK(glDrawElements(GL_TRIANGLES, state.vertex_count * 6 / 4, GL_UNSIGNED_SHORT, 0));
    state.vertex_count = 0;
}

static void render_draw_char(int x, int y, unsigned char c) {
    if (x < 0 || x >= state.columns || y < 0 || y >= state.rows) return;
    unsigned int* i = &state.vertex_count;
    state.vertex_data[(*i)++] = (vertex) { x, y, c, 15, 0, 0 };
    state.vertex_data[(*i)++] = (vertex) { x + 1, y, c, 15, 1, 0 };
    state.vertex_data[(*i)++] = (vertex) { x + 1, y + 1, c, 15, 1, 1 };
    state.vertex_data[(*i)++] = (vertex) { x, y + 1, c, 15, 0, 1 };
}

static void render_user_input() {
    int cursor = state.user_cursor;
    int length = state.user_length;
    int cols = state.columns;
    int y = state.rows - 1;
    int x = 0;

    render_draw_char(x++, y, 6);
    render_draw_char(x++, y, 5);
    render_draw_char(x++, y, 48);

    for (int i = 0; i < length; i++) {
        render_draw_char(x++, y, state.user_input[i]);
    }
}

static void user_insert_codepoint(unsigned char codepoint) {
    if (state.user_cursor >= MAX_USER_INPUT) return;
    if (state.user_length >= MAX_USER_INPUT) return;

    if (state.user_cursor != state.user_length) {
        memmove(state.user_input + state.user_cursor, state.user_input + state.user_cursor + 1, state.user_length - state.user_cursor);
    }

    state.user_input[state.user_cursor] = codepoint;

    state.user_cursor++;
    state.user_length++;
}

static void user_backspace(int mods) {
}

static void user_delete(int mods) {
}

static void user_enter(int mods) {
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (window != state.window) return;

    mods = mods & ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (mods == 0) {
            unsigned char codepoint = key_to_codepoint[key];
            if (codepoint != 0) {
                user_insert_codepoint(codepoint);
                return;
            }
        } else if (mods == GLFW_MOD_SHIFT) {
            unsigned char codepoint = shift_key_to_codepoint[key];
            if (codepoint == 0) codepoint = key_to_codepoint[key];
            if (codepoint != 0) {
                user_insert_codepoint(codepoint);
                return;
            }
        }

        switch (key) {
            case GLFW_KEY_BACKSPACE: user_backspace(mods); break;
            case GLFW_KEY_DELETE: user_delete(mods); break;
            case GLFW_KEY_ENTER: user_enter(mods); break;
        }
    }
}

int main(int argc, char** argv) {
    int result = 0;

    GLuint ibo = 0;
    GLuint vertex_shader = 0, fragment_shader = 0, shader_program = 0;

    do {
        int major, minor, rev;
        glfwGetVersion(&major, &minor, &rev);
        fprintf(stderr, "Running against GLFW %i.%i.%i\n", major, minor, rev);

        if (major != 3 && minor < 4) {
            fprintf(stderr, "Expecting GLFW 3, at least version 3.4\n");
            return 1;
        }
    } while (0);

    if (!glfwInit()) {
        const char* description = NULL;
        glfwGetError(&description);
        fprintf(stderr, "Failed to initialize GLFW: %s\n", description);
        return 1;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* video_mode = glfwGetVideoMode(monitor);

    int window_width = state.previous_width = 2 * state.glyph_width * 80;
    int window_height = state.previous_height = 2 * state.glyph_height * 25;

    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_RED_BITS, video_mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, video_mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, video_mode->blueBits);
    state.window = glfwCreateWindow(window_width, window_height, "oednterm", NULL, NULL);
    if (state.window == NULL) {
        const char* description = NULL;
        glfwGetError(&description);
        fprintf(stderr, "Failed to create window with GLFW: %s\n", description);
        return_defer(1);
    }

    set_terminal_rows_and_columns();
    log_term_state();

    glfwSetKeyCallback(state.window, key_callback);

    glfwMakeContextCurrent(state.window);
    if (!gladLoadGLES2(glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize OpenGL ES 2.0\n");
        return_defer(1);
    }

    glfwSetWindowSizeLimits(state.window, state.glyph_width * 40, state.glyph_height * 13, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetFramebufferSizeCallback(state.window, framebuffer_size_callback);

    GL_CHECK(glDisable(GL_CULL_FACE));

    GL_CHECK(glGenTextures(1, &state.font));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, state.font));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    unsigned char* font_data_bytes = malloc(3 * font_width * font_height);
    for (int y = 0; y < font_height; y++) {
        for (int x = 0; x < font_width; x++) {
            int i = x + y * font_width;
            int value = 255 * ((font_data_bits[i / 8] >> (7 - i % 8)) & 0x01);
            font_data_bytes[i * 3 + 0] = value;
            font_data_bytes[i * 3 + 1] = value;
            font_data_bytes[i * 3 + 2] = value;
        }
    }
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 160, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, font_data_bytes));
    free(font_data_bytes);

    state.max_vertices = 65536;

    GL_CHECK(glGenBuffers(1, &state.vbo));
    GL_CHECK(glGenBuffers(1, &ibo));

    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, state.vbo));
    state.vertex_data = calloc(state.max_vertices, sizeof(vertex));

    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo));
    unsigned short* index_data = calloc(state.max_vertices / 4 * 6, sizeof(unsigned short));
    for (int i = 0; i < state.max_vertices / 4; i++) {
        index_data[i * 6 + 0] = (unsigned short)(i * 4 + 0);
        index_data[i * 6 + 1] = (unsigned short)(i * 4 + 2);
        index_data[i * 6 + 2] = (unsigned short)(i * 4 + 1);
        index_data[i * 6 + 3] = (unsigned short)(i * 4 + 0);
        index_data[i * 6 + 4] = (unsigned short)(i * 4 + 3);
        index_data[i * 6 + 5] = (unsigned short)(i * 4 + 2);
    }
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, state.max_vertices / 4 * 6, index_data, GL_STATIC_DRAW));
    free(index_data);

    GL_CHECK(glVertexAttribPointer(0, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(vertex), (void*)(0)));
    GL_CHECK(glVertexAttribPointer(1, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(vertex), (void*)(2 * sizeof(short))));
    GL_CHECK(glVertexAttribPointer(2, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(vertex), (void*)(2 * sizeof(short) + 1 * sizeof(char))));
    GL_CHECK(glVertexAttribPointer(3, 2, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(vertex), (void*)(2 * sizeof(short) + 2 * sizeof(char))));
    GL_CHECK(glEnableVertexAttribArray(0));
    GL_CHECK(glEnableVertexAttribArray(1));
    GL_CHECK(glEnableVertexAttribArray(2));
    GL_CHECK(glEnableVertexAttribArray(3));

    GLsizei info_log_length;
    GLchar* info_log;

    GL_CHECK(vertex_shader = glCreateShader(GL_VERTEX_SHADER));
    GL_CHECK(fragment_shader = glCreateShader(GL_FRAGMENT_SHADER));
    GL_CHECK(shader_program = glCreateProgram());

    GL_CHECK(glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL));
    GL_CHECK(glCompileShader(vertex_shader));
    GL_CHECK(glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &info_log_length));
    if (info_log_length != 0) {
        info_log = calloc(info_log_length, sizeof(GLchar));
        GL_CHECK(glGetShaderInfoLog(vertex_shader, info_log_length, NULL, info_log));
        fprintf(stderr, "Vertex Shader Log: %s\n", info_log);
    }

    GL_CHECK(glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL));
    GL_CHECK(glCompileShader(fragment_shader));
    GL_CHECK(glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &info_log_length));
    if (info_log_length != 0) {
        info_log = calloc(info_log_length, sizeof(GLchar));
        GL_CHECK(glGetShaderInfoLog(fragment_shader, info_log_length, NULL, info_log));
        fprintf(stderr, "Fragment Shader Log: %s\n", info_log);
    }

    GL_CHECK(glAttachShader(shader_program, vertex_shader));
    GL_CHECK(glAttachShader(shader_program, fragment_shader));
    GL_CHECK(glBindAttribLocation(shader_program, 0, "position"));
    GL_CHECK(glBindAttribLocation(shader_program, 1, "glyph_codepoint"));
    GL_CHECK(glBindAttribLocation(shader_program, 2, "glyph_style"));
    GL_CHECK(glBindAttribLocation(shader_program, 3, "glyph_coords"));
    GL_CHECK(glLinkProgram(shader_program));
    GL_CHECK(glGetProgramiv(shader_program, GL_INFO_LOG_LENGTH, &info_log_length));
    if (info_log_length != 0) {
        info_log = calloc(info_log_length, sizeof(GLchar));
        GL_CHECK(glGetProgramInfoLog(shader_program, info_log_length, NULL, info_log));
        fprintf(stderr, "Shader Program Log: %s\n", info_log);
    }

    GL_CHECK(glUseProgram(shader_program));

    GLuint uniform_glyph_size, uniform_atlas_size;
    GL_CHECK(state.uniform_viewport_size = glGetUniformLocation(shader_program, "viewport_size"));
    GL_CHECK(state.uniform_glyph_aspect = glGetUniformLocation(shader_program, "glyph_aspect"));
    GL_CHECK(uniform_glyph_size = glGetUniformLocation(shader_program, "glyph_size"));
    GL_CHECK(uniform_atlas_size = glGetUniformLocation(shader_program, "atlas_size"));

    GL_CHECK(glUniform2f(uniform_glyph_size, 10.0f, 16.0f));
    GL_CHECK(glUniform2f(uniform_atlas_size, 160.0f, 64.0f));

    do {
        glfwPollEvents();
        if (glfwWindowShouldClose(state.window) || glfwGetKey(state.window, GLFW_KEY_ESCAPE)) {
            break;
        }

        glfwSwapBuffers(state.window);
        render_init();

        for (int i = 0; i < 16 * 3; i++) {
            render_draw_char(i % 8, i / 8, (unsigned char)i);
        }

        render_user_input();

        render_flush();
    } while (1);

defer:;
    glUseProgram(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (shader_program != 0) glDeleteProgram(shader_program);
    if (fragment_shader != 0) glDeleteShader(fragment_shader);
    if (vertex_shader != 0) glDeleteShader(vertex_shader);

    if (ibo != 0) glDeleteBuffers(1, &ibo);
    if (state.vbo != 0) glDeleteBuffers(1, &state.vbo);

    if (state.vertex_data != NULL) free(state.vertex_data);
    if (state.window != NULL) glfwDestroyWindow(state.window);

    glfwTerminate();
    return result;
}
