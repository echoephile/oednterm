#define GLAD_GLES2_USE_SYSTEM_EGL
#define GLAD_GLES2_IMPLEMENTATION
#include "gles2.h"

#include <GLFW/glfw3.h>

#include <stdbool.h>
#include <stdio.h>

#include "term.h"

int main(int argc, char** argv) {
    int result = 0;
    fprintf(stderr, "Hello, hunter!\n");
defer:
    return result;
}
