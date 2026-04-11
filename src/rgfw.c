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
} SimpRender;

typedef struct {
    float x, y, w, h;
} SimpRectangle;

const char *vertex_shader_source = 
"#version 460 core\n"
"layout (location = 0) in vec2 aPos;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);\n"
"}\0";

const char *fragment_shader_source = 
"#version 460 core\n"
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"    FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
"}\0";


SimpRender simp_init() {
    SimpRender rendr = {0};
    glGenVertexArrays(1, &rendr.color_vao);
    glGenBuffers(1, &rendr.color_vbo);
    glGenBuffers(1, &rendr.color_ebo);

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

    return rendr;
}

void simp_rectangle(SimpRender* render, SimpRectangle rect) {
    // top right
    *yar_append(&render->color_points) = rect.x + rect.w;
    *yar_append(&render->color_points) = rect.y;

    // bottom right
    *yar_append(&render->color_points) = rect.x + rect.w;
    *yar_append(&render->color_points) = rect.y - rect.h;

    // bottom left
    *yar_append(&render->color_points) = rect.x;
    *yar_append(&render->color_points) = rect.y - rect.h;

    // top left
    *yar_append(&render->color_points) = rect.x;
    *yar_append(&render->color_points) = rect.y;

    // effishiency
    uint32_t base = (render->color_points.count / 2) - 4;

    *yar_append(&render->color_point_indices) = base + 0;
    *yar_append(&render->color_point_indices) = base + 1;
    *yar_append(&render->color_point_indices) = base + 3;

    *yar_append(&render->color_point_indices) = base + 1;
    *yar_append(&render->color_point_indices) = base + 2;
    *yar_append(&render->color_point_indices) = base + 3;}

void simp_flush(SimpRender* render) {
    glBindVertexArray(render->color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, render->color_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render->color_ebo);
    glBufferData(GL_ARRAY_BUFFER, render->color_points.count * sizeof(float), render->color_points.items, GL_DYNAMIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, render->color_point_indices.count * sizeof(uint32_t), render->color_point_indices.items, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glUseProgram(render->shader);
    // use shader
    glDrawElements(GL_TRIANGLES, render->color_point_indices.count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    render->color_point_indices.count = 0;
    render->color_points.count = 0;
}

int main() {
    {
        RGFW_glHints* hints = RGFW_getGlobalHints_OpenGL();
        hints->major = 4;
        hints->minor = 6;
        RGFW_setGlobalHints_OpenGL(hints);
    }

    RGFW_window* win = RGFW_createWindow("hello!", 0, 0, 800, 600, RGFW_windowOpenGL | RGFW_windowFocusOnShow);
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
            if (event.type == RGFW_windowResized) glViewport(0, 0, event.update.w, event.update.h);
            if (event.type == RGFW_windowClose) break;
        }
        simp_rectangle(&render, (SimpRectangle) {
            .x = -0.5,
            .y = 0.5,
            .w = 1,
            .h = 1,
        });
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        simp_flush(&render);
        RGFW_window_swapBuffers_OpenGL(win);

    }

    RGFW_window_close(win);
}
