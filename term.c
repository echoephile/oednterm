#define GLAD_GLES2_IMPLEMENTATION
#include "term.h"

#include <stdbool.h>
#include <stdio.h>

static const char* gl_error_names[] = {
    [GL_NO_ERROR] = "No Error",
    [GL_INVALID_ENUM] = "Invalid Enum",
    [GL_INVALID_VALUE] = "Invalid Value",
    [GL_INVALID_OPERATION] = "Invalid Operation",
    [GL_INVALID_FRAMEBUFFER_OPERATION] = "Invalid Framebuffer Operation",
    [GL_OUT_OF_MEMORY] = "Out Of Memory",
};

#define GL( ...)                                    \
    do {                                            \
        (__VA_ARGS__);                              \
        if (!check_gl_errors(__FILE__, __LINE__)) { \
            return_defer(false);                    \
        }                                           \
    } while (0)

static bool check_gl_errors(const char* file, int line) {
    bool has_errored = false;

    GLenum err;
    while(err = glGetError(), err != GL_NO_ERROR) {
        has_errored = true;
        fprintf(stderr, "OpenGL Error: %s:%d: %s\n", file, line, gl_error_names[err]);
    }

    return !has_errored;
}

static bool check_required_glfw_version(void) {
    int major, minor, rev;
    glfwGetVersion(&major, &minor, &rev);
    fprintf(stderr, "Running against GLFW %i.%i.%i\n", major, minor, rev);

    if (major != 3 && minor < 4) {
        fprintf(stderr, "Expecting GLFW 3, at least version 3.4\n");
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    int result = 0;
    term t = {0};

    if (!term_init(&t)) {
        return_defer(1);
    }

    term_loop(&t);

defer:;
    term_deinit(&t);
    return result;
}

bool term_lines_init(term_lines* lines, uint16_t column_count) {
    bool result = true;

    size_t cell_count = column_count * TERM_HISTORY_COUNT;
    lines->cell_data = calloc(cell_count, sizeof(*lines->cell_data));
    if (lines->cell_data == NULL) {
        fprintf(stderr, "Failed to allocate cell storage. Buy more RAM?\n");
        return_defer(false);
    }

defer:;
    return result;
}

void term_lines_deinit(term_lines* lines) {
    free(lines->cell_data);
}

term_cell* term_lines_get_cells_at_index(const term_lines* lines, uint32_t index) {
    return NULL;
}

bool term_lines_reflow(term_lines* lines, uint16_t new_column_count) {
    bool result = true;

    size_t new_cell_count = new_column_count * TERM_HISTORY_COUNT;
    term_cell* new_cell_data = calloc(new_column_count, sizeof(*lines->cell_data));
    if (new_cell_data == NULL) {
        fprintf(stderr, "Failed to allocate reflow cell storage. Buy more RAM?\n");
        return_defer(false);
    }

    free(lines->cell_data);
    lines->cell_data = new_cell_data;

defer:;
    return result;
}

static bool term_realloc_vertices(term* t) {
    bool result = true;
    unsigned short* index_data = NULL;

    free(t->vertices);

    size_t vertex_count = 4 * t->rows * t->columns;
    size_t index_count = vertex_count / 4 * 6;

    t->vertices = calloc(vertex_count, sizeof(*t->vertices));
    if (t->vertices == NULL) {
        fprintf(stderr, "Failed to allocate vertex storage. Buy more RAM?\n");
        return_defer(false);
    }

    index_data = calloc(index_count, sizeof(*index_data));
    if (index_data == NULL) {
        fprintf(stderr, "Failed to allocate index storage. Buy more RAM?\n");
        return_defer(false);
    }

    for (size_t i = 0; i < vertex_count / 4; i++) {
        index_data[i * 6 + 0] = (unsigned short)(i * 4 + 0);
        index_data[i * 6 + 1] = (unsigned short)(i * 4 + 1);
        index_data[i * 6 + 2] = (unsigned short)(i * 4 + 2);
        index_data[i * 6 + 3] = (unsigned short)(i * 4 + 0);
        index_data[i * 6 + 4] = (unsigned short)(i * 4 + 2);
        index_data[i * 6 + 5] = (unsigned short)(i * 4 + 3);
    }

    GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(*index_data) * index_count, index_data, GL_STATIC_DRAW));

defer:;
    free(index_data);
    return result;
}

bool term_init(term* t) {
    bool result = true;
    unsigned char* font_data_bytes = NULL;

    GLsizei info_log_length = 0;
    GLchar* info_log = NULL;

    if (!glfwInit()) {
        const char* description = NULL;
        glfwGetError(&description);
        fprintf(stderr, "Failed to initialize GLFW: %s\n", description);
        return_defer(false);
    }

    t->columns = 80;
    t->rows = 25;
    t->aspect = 1;
    t->dirty = true;

    int aspect_x = aspect_ratios[t->aspect].x;
    int aspect_y = aspect_ratios[t->aspect].y;

    int window_width = t->cached_window_width = t->columns * TERM_GLYPH_WIDTH * aspect_x;
    int window_height = t->cached_window_height = t->rows * TERM_GLYPH_HEIGHT * aspect_y;

    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    t->window = glfwCreateWindow(window_width, window_height, "oednterm", NULL, NULL);
    if (t->window == NULL) {
        const char* description = NULL;
        glfwGetError(&description);
        fprintf(stderr, "Failed to create window with GLFW: %s\n", description);
        return_defer(false);
    }

    glfwMakeContextCurrent(t->window);
    glfwSetWindowSizeLimits(t->window, TERM_GLYPH_WIDTH * 40, TERM_GLYPH_HEIGHT * 13, GLFW_DONT_CARE, GLFW_DONT_CARE);

    if (!gladLoadGLES2(glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize OpenGL ES 2.0\n");
        return_defer(false);
    }

    GL(glDisable(GL_CULL_FACE));

    GL(glGenTextures(1, &t->font_atlas));
    GL(glGenBuffers(1, &t->vbo));
    GL(glGenBuffers(1, &t->ibo));
    GL(t->vertex_shader = glCreateShader(GL_VERTEX_SHADER));
    GL(t->fragment_shader = glCreateShader(GL_FRAGMENT_SHADER));
    GL(t->shader_program = glCreateProgram());

    GL(glBindTexture(GL_TEXTURE_2D, t->font_atlas));
    GL(glBindBuffer(GL_ARRAY_BUFFER, t->vbo));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t->ibo));

    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));

    font_data_bytes = malloc(3 * TERM_ATLAS_WIDTH * TERM_ATLAS_HEIGHT);
    for (int y = 0; y < TERM_ATLAS_HEIGHT; y++) {
        for (int x = 0; x < TERM_ATLAS_WIDTH; x++) {
            int i = x + y * TERM_ATLAS_WIDTH;
            int value = 255 * ((term_bitmap_font_data[i / 8] >> (7 - i % 8)) & 0x01);
            font_data_bytes[i * 3 + 0] = (unsigned char)value;
            font_data_bytes[i * 3 + 1] = (unsigned char)value;
            font_data_bytes[i * 3 + 2] = (unsigned char)value;
        }
    }

    GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TERM_ATLAS_WIDTH, TERM_ATLAS_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, font_data_bytes));

    if (!term_realloc_vertices(t)) return_defer(false);

    GL(glVertexAttribPointer(0, 2, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(*t->vertices), (void*)(0)));
    GL(glVertexAttribPointer(1, 2, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(*t->vertices), (void*)(2 * sizeof(char))));
    GL(glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(*t->vertices), (void*)(4 * sizeof(char))));
    GL(glVertexAttribPointer(3, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(*t->vertices), (void*)(7 * sizeof(char))));
    GL(glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(*t->vertices), (void*)(10 * sizeof(char))));
    GL(glEnableVertexAttribArray(0));
    GL(glEnableVertexAttribArray(1));
    GL(glEnableVertexAttribArray(2));
    GL(glEnableVertexAttribArray(3));
    GL(glEnableVertexAttribArray(4));

    GL(glShaderSource(t->vertex_shader, 1, &vertex_shader_source, NULL));
    GL(glCompileShader(t->vertex_shader));
    GL(glGetShaderiv(t->vertex_shader, GL_INFO_LOG_LENGTH, &info_log_length));
    if (info_log_length != 0) {
        info_log = calloc(info_log_length, sizeof(*info_log));
        GL(glGetShaderInfoLog(t->vertex_shader, info_log_length, NULL, info_log));
        fprintf(stderr, "Vertex Shader Log:\n%s\n", info_log);
    }

    GL(glShaderSource(t->fragment_shader, 1, &fragment_shader_source, NULL));
    GL(glCompileShader(t->fragment_shader));
    GL(glGetShaderiv(t->fragment_shader, GL_INFO_LOG_LENGTH, &info_log_length));
    if (info_log_length != 0) {
        info_log = calloc(info_log_length, sizeof(*info_log));
        GL(glGetShaderInfoLog(t->fragment_shader, info_log_length, NULL, info_log));
        fprintf(stderr, "Fragment Shader Log:\n%s\n", info_log);
    }

    GL(glAttachShader(t->shader_program, t->vertex_shader));
    GL(glAttachShader(t->shader_program, t->fragment_shader));

    GL(glBindAttribLocation(t->shader_program, 0, "position"));
    GL(glBindAttribLocation(t->shader_program, 1, "glyph_coords"));
    GL(glBindAttribLocation(t->shader_program, 2, "glyph_foreground"));
    GL(glBindAttribLocation(t->shader_program, 3, "glyph_background"));
    GL(glBindAttribLocation(t->shader_program, 4, "glyph_codepoint"));

    GL(glLinkProgram(t->shader_program));
    GL(glGetProgramiv(t->shader_program, GL_INFO_LOG_LENGTH, &info_log_length));
    if (info_log_length != 0) {
        info_log = calloc(info_log_length, sizeof(GLchar));
        GL(glGetProgramInfoLog(t->shader_program, info_log_length, NULL, info_log));
        fprintf(stderr, "Shader Program Log:\n%s\n", info_log);
    }

    GL(glUseProgram(t->shader_program));

    GL(t->uniform_viewport_size = glGetUniformLocation(t->shader_program, "viewport_size"));
    GL(t->uniform_glyph_aspect = glGetUniformLocation(t->shader_program, "glyph_aspect"));
    GL(t->uniform_glyph_size = glGetUniformLocation(t->shader_program, "glyph_size"));
    GL(t->uniform_atlas_size = glGetUniformLocation(t->shader_program, "atlas_size"));

    GL(glUniform2f(t->uniform_glyph_size, (float)TERM_GLYPH_WIDTH, (float)TERM_GLYPH_HEIGHT));
    GL(glUniform2f(t->uniform_atlas_size, (float)TERM_ATLAS_WIDTH, (float)TERM_ATLAS_HEIGHT));

    if (!term_lines_init(&t->lines, t->columns)) return_defer(false);

defer:;
    free(font_data_bytes);
    return result;
}

void term_deinit(term* t) {
    term_lines_deinit(&t->lines);

    free(t->vertices);
    if (t->window != NULL) glfwDestroyWindow(t->window);

    glfwTerminate();
}

void term_loop(term* t) {
    while (!glfwWindowShouldClose(t->window)) {
        glfwPollEvents();
        if (glfwGetKey(t->window, GLFW_KEY_ESCAPE)) {
            break;
        }

        glfwSwapBuffers(t->window);

        term_puts(t, "`1234567890_=\nqwertyuiop[\\\nasdfghjkl;'\nzxcvbnm,./?");
        if (!term_flush(t)) break;
    }
}

bool term_flush(term* t) {
    bool result = true;

    size_t vertex_count = 4 * t->rows * t->columns;
    size_t index_count = vertex_count / 4 * 6;

    if (t->dirty) {
        GL(glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(*t->vertices), t->vertices, GL_DYNAMIC_DRAW));
        t->dirty = false;
    }

    GL(glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_SHORT, 0));

defer:;
    return result;
}

void term_putc(term* t, char c) {
}

void term_puts(term* t, const char* str) {
}

void term_printf(term* t, const char* format, ...) {
}
