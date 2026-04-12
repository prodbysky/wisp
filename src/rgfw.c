#include <assert.h>
#include <stdint.h>
#include "simp.h"

#define RGFW_IMPLEMENTATION
#define RGFW_OPENGL
#include "../extern/glad/include/glad/glad.h"
#include "../extern/rgfw.h"

#define YAR_IMPLEMENTATION
#include "../extern/yar.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../extern/stb_truetype.h"

#define SIMP_COLOR_BG (SimpColor){0.1, 0.1, 0.1, 1}


int main() {
    RGFW_window* win =
        RGFW_createWindow("simp", 0, 0, 800, 600, RGFW_windowOpenGL);

    RGFW_window_makeCurrentContext_OpenGL(win);
    gladLoadGLLoader((GLADloadproc)RGFW_getProcAddress_OpenGL);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    SimpRender r = simp_init();

    SimpFont font1 = simp_font_load("res/Iosevka.ttf", 64);
    SimpFont font2 = simp_font_load("res/Iosevka.ttf", 32);
    while (!RGFW_window_shouldClose(win)) {
        int w = win->w;
        int h = win->h;

        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        simp_rectangle(&r, (SimpRectangle){0, 0, 200, 100}, SIMP_COLOR_RED);

        simp_text(&r, &font1, "Big text", 000, 100, SIMP_COLOR_RED);
        simp_text(&r, &font2, "Small text", 100, 200, SIMP_COLOR_RED);

        simp_flush(&r, w, h);

        RGFW_window_swapBuffers_OpenGL(win);
        RGFW_pollEvents();
    }
}
