#include <stdio.h>
#define RGFW_IMPLEMENTATION
#define RGFW_OPENGL
#include "../extern/rgfw.h"
#include "../extern/glad/include/glad/glad.h"

#define YAR_IMPLEMENTATION
#include "../extern/yar.h"

typedef struct {
    yar(float) color_points;
    yar(uint32_t) color_point_indices;

    uint32_t color_vao;
    uint32_t color_vbo;
    uint32_t color_ebo;

    uint32_t shader;
    int proj_loc;
} SimpRender;

typedef struct {
    float x, y, w, h;
} SimpRectangle;

typedef struct {
    float r, g, b, a;
} SimpColor;

#define SIMP_COLOR_RED (SimpColor){1, 0, 0, 1}

const char *vertex_shader_source = 
"#version 460 core\n"
"layout (location = 0) in vec2 aPos;\n"
"layout (location = 1) in vec4 aColor;\n"

"uniform mat4 uProj;\n"

"out vec4 oColor;\n"
"void main()\n"
"{\n"
"   gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
"   oColor = aColor;\n"
"}\0";

const char *fragment_shader_source = 
"#version 460 core\n"
"out vec4 FragColor;\n"
"in vec4 oColor;\n"
"void main()\n"
"{\n"
"    FragColor = oColor;\n"
"}\0";

void make_ortho(float matrix[16], float window_w, float window_h);


SimpRender simp_init();
void simp_rectangle(SimpRender* render, SimpRectangle rect, SimpColor color);
void simp_flush(SimpRender* render, float w, float h);

int main() {
    {
        RGFW_glHints* hints = RGFW_getGlobalHints_OpenGL();
        hints->major = 4;
        hints->minor = 6;
        RGFW_setGlobalHints_OpenGL(hints);
    }

    RGFW_window* win = RGFW_createWindow("hello!", 0, 0, 800, 600, RGFW_windowOpenGL | RGFW_windowFocusOnShow);
    int win_w = 800, win_h = 600;

    RGFW_window_makeCurrentContext_OpenGL(win);

    if (!gladLoadGLLoader((GLADloadproc)RGFW_getProcAddress_OpenGL)) {
        printf("Failed to initialize GLAD\n");
        return -1;
    }    

    glViewport(0, 0, 800, 600);

    SimpRender render = simp_init();

    while (!RGFW_window_shouldClose(win)) {
        RGFW_event event;
        while (RGFW_checkEvent(&event)) {
            if (event.type == RGFW_windowResized) {
                win_w = event.update.w;
                win_h = event.update.h;
                glViewport(0, 0, event.update.w, event.update.h);
            }
            if (event.type == RGFW_windowClose) break;
        }
        simp_rectangle(&render, (SimpRectangle) {
            .x = 0,
            .y = 0,
            .w = 100,
            .h = 100,
        }, SIMP_COLOR_RED);
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        simp_flush(&render, win_w, win_h);
        RGFW_window_swapBuffers_OpenGL(win);
    }

    RGFW_window_close(win);
}

void make_ortho(float matrix[16], float window_w, float window_h) {
    float left   = 0.0f;
    float right  = window_w;
    float bottom = window_h;
    float top    = 0.0f;
    float near   = -1.0f;
    float far    = 1.0f;

    for (int i = 0; i < 16; i++)
        matrix[i] = 0.0f;

    matrix[0]  =  2.0f / (right - left);
    matrix[5]  =  2.0f / (top - bottom);
    matrix[10] = -2.0f / (far - near);

    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[14] = -(far + near) / (far - near);
    matrix[15] = 1.0f;
}

SimpRender simp_init() {
    SimpRender rendr = {0};
    glGenVertexArrays(1, &rendr.color_vao);
    glGenBuffers(1, &rendr.color_vbo);
    glGenBuffers(1, &rendr.color_ebo);

    glBindVertexArray(rendr.color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rendr.color_vbo);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0); // pos
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float))); // color
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    uint32_t vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);

    uint32_t frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(frag_shader);
    
    rendr.shader = glCreateProgram();
    
    glAttachShader(rendr.shader, vertex_shader);
    glAttachShader(rendr.shader, frag_shader);
    glLinkProgram(rendr.shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(frag_shader);

    rendr.proj_loc = glGetUniformLocation(rendr.shader, "uProj");

    return rendr;
}

void simp_rectangle(SimpRender* render, SimpRectangle rect, SimpColor color) {
    float verts[] = {
        // top right
        rect.x + rect.w, rect.y,           color.r, color.g, color.b, color.a,

        // bottom right
        rect.x + rect.w, rect.y + rect.h,  color.r, color.g, color.b, color.a,

        // bottom left
        rect.x, rect.y + rect.h,           color.r, color.g, color.b, color.a,

        // top left
        rect.x, rect.y,                    color.r, color.g, color.b, color.a,
    };

    for (int i = 0; i < 24; i++) {
        *yar_append(&render->color_points) = verts[i];
    }

    uint32_t base = (render->color_points.count / 6) - 4;

    *yar_append(&render->color_point_indices) = base + 0;
    *yar_append(&render->color_point_indices) = base + 1;
    *yar_append(&render->color_point_indices) = base + 3;

    *yar_append(&render->color_point_indices) = base + 1;
    *yar_append(&render->color_point_indices) = base + 2;
    *yar_append(&render->color_point_indices) = base + 3;
}

void simp_flush(SimpRender* render, float w, float h) {
    glBindVertexArray(render->color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, render->color_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render->color_ebo);
    glBufferData(GL_ARRAY_BUFFER, render->color_points.count * sizeof(float), render->color_points.items, GL_DYNAMIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, render->color_point_indices.count * sizeof(uint32_t), render->color_point_indices.items, GL_DYNAMIC_DRAW);

    glUseProgram(render->shader);

    float proj[16];
    make_ortho(proj, w, h);
    glUniformMatrix4fv(render->proj_loc, 1, GL_FALSE, proj);

    glDrawElements(GL_TRIANGLES, render->color_point_indices.count, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    render->color_point_indices.count = 0;
    render->color_points.count = 0;
}
