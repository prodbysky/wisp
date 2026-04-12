#include <assert.h>
#include <stdio.h>
#define RGFW_IMPLEMENTATION
#define RGFW_OPENGL
#include "../extern/rgfw.h"
#include "../extern/glad/include/glad/glad.h"
#include <time.h>

#define YAR_IMPLEMENTATION
#include "../extern/yar.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../extern/stb_truetype.h"

typedef struct {
    RGFW_window* window;

    yar(float) color_points;
    yar(uint32_t) color_point_indices;

    uint32_t color_vao;
    uint32_t color_vbo;
    uint32_t color_ebo;

    uint32_t shader;
    int proj_loc;

    yar(float) text_points;
    yar(uint32_t) text_indices;

    uint32_t text_vao;
    uint32_t text_vbo;
    uint32_t text_ebo;

    uint32_t font_texture;

    uint32_t text_shader;
    int text_proj_loc;


} SimpRender;

typedef struct {
    float x, y, w, h;
} SimpRectangle;

typedef struct {
    float r, g, b, a;
} SimpColor;

#define SIMP_COLOR_RED (SimpColor){1, 0, 0, 1}
#define SIMP_COLOR_GRUBER_BG (SimpColor){0x18/255.0, 0x18/255.0, 0x18/255.0, 1}

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

const char* text_vertex_shader =
"#version 460 core\n"
"layout (location = 0) in vec2 aPos;\n"
"layout (location = 1) in vec4 aColor;\n"
"layout (location = 2) in vec2 aUV;\n"

"uniform mat4 uProj;\n"

"out vec4 oColor;\n"
"out vec2 oUV;\n"

"void main()\n"
"{\n"
"   gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
"   oColor = aColor;\n"
"   oUV = aUV;\n"
"}\0";

const char* text_frag_shader =
"#version 460 core\n"

"in vec4 oColor;\n"
"in vec2 oUV;\n"
"out vec4 FragColor;\n"

"uniform sampler2D uFontAtlasTexture;\n"
"void main()\n"
"{\n"
"   FragColor = vec4(texture(uFontAtlasTexture, oUV).r) * oColor;\n"
"}\0";

void make_ortho(float matrix[16], float window_w, float window_h);


SimpRender simp_init();
void simp_clear_background(SimpRender* render, SimpColor color);
void simp_rectangle(SimpRender* render, SimpRectangle rect, SimpColor color);
void simp_text(SimpRender* r, const char* text, float x, float y, SimpColor color);
void simp_flush(SimpRender* render, float w, float h);

double get_time();

unsigned char temp_bitmap[512*512];
stbtt_bakedchar cdata[96];

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
    SimpRender render = simp_init();

    uint32_t texture_id;
    { // font setup
        FILE* f = fopen("res/Iosevka.ttf", "rb");
        assert(f);
        fseek(f, 0, SEEK_END);
        size_t len = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint8_t* buffer = malloc(len);
        fread(buffer, 1, len, f);
        fclose(f);

        stbtt_BakeFontBitmap(buffer,0, 64.0, temp_bitmap,512,512, 32,96, cdata);
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 512, 512, 0, GL_RED, GL_UNSIGNED_BYTE, temp_bitmap);        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    render.font_texture = texture_id;

    glViewport(0, 0, 800, 600);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    double last_time = get_time();
    while (!RGFW_window_shouldClose(win)) {
        double curr = get_time();
        double dt = curr - last_time;
        last_time = curr;

        RGFW_event event;
        while (RGFW_checkEvent(&event)) {
            if (event.type == RGFW_windowResized) {
                win_w = event.update.w;
                win_h = event.update.h;
                glViewport(0, 0, event.update.w, event.update.h);
            }
            if (event.type == RGFW_windowClose) break;
        }
        simp_clear_background(&render, SIMP_COLOR_GRUBER_BG);

        simp_rectangle(&render, (SimpRectangle) {
            .x = 0,
            .y = 0,
            .w = 100,
            .h = 100,
        }, SIMP_COLOR_RED);
        simp_text(&render, "Hello world!", 200, 200, SIMP_COLOR_RED);

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

    glGenVertexArrays(1, &rendr.text_vao);
    glGenBuffers(1, &rendr.text_vbo);
    glGenBuffers(1, &rendr.text_ebo);

    glBindVertexArray(rendr.text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rendr.text_vbo);

    // pos (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // color (vec4)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // uv (vec2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

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

    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &text_vertex_shader, NULL);
    glCompileShader(vertex_shader);

    frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_shader, 1, &text_frag_shader, NULL);
    glCompileShader(frag_shader);
    
    rendr.text_shader = glCreateProgram();

    
    glAttachShader(rendr.text_shader, vertex_shader);
    glAttachShader(rendr.text_shader, frag_shader);
    glLinkProgram(rendr.text_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(frag_shader);

    rendr.text_proj_loc = glGetUniformLocation(rendr.text_shader, "uProj");

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

void simp_text(SimpRender* r, const char* text, float x, float y, SimpColor color) {

    while (*text) {
        if (*text >= 32 && *text < 127) {
            stbtt_bakedchar* b = &cdata[*text - 32];

            float x0 = x + b->xoff;
            float y0 = y + b->yoff;
            float x1 = x0 + (b->x1 - b->x0);
            float y1 = y0 + (b->y1 - b->y0);

            float u0 = b->x0 / 512.0f;
            float v0 = b->y0 / 512.0f;
            float u1 = b->x1 / 512.0f;
            float v1 = b->y1 / 512.0f;

            float verts[] = {
                x0, y0, color.r, color.g, color.b, color.a, u0, v0,
                x1, y0, color.r, color.g, color.b, color.a, u1, v0,
                x1, y1, color.r, color.g, color.b, color.a, u1, v1,
                x0, y1, color.r, color.g, color.b, color.a, u0, v1,
            };

            for (int i = 0; i < 32; i++) {
                *yar_append(&r->text_points) = verts[i];
            }

            uint32_t base = (r->text_points.count / 8) - 4;

            *yar_append(&r->text_indices) = base + 0;
            *yar_append(&r->text_indices) = base + 1;
            *yar_append(&r->text_indices) = base + 2;

            *yar_append(&r->text_indices) = base + 0;
            *yar_append(&r->text_indices) = base + 2;
            *yar_append(&r->text_indices) = base + 3;

            x += b->xadvance;
        }
        text++;
    }
}

void simp_clear_background(SimpRender* render, SimpColor color) {
    (void)render;
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
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

    glBindVertexArray(render->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, render->text_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render->text_ebo);

    glBufferData(GL_ARRAY_BUFFER,
        render->text_points.count * sizeof(float),
        render->text_points.items,
        GL_DYNAMIC_DRAW);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        render->text_indices.count * sizeof(uint32_t),
        render->text_indices.items,
        GL_DYNAMIC_DRAW);

    glUseProgram(render->text_shader);

    glUniformMatrix4fv(render->text_proj_loc, 1, GL_FALSE, proj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, render->font_texture);

    glDrawElements(GL_TRIANGLES,
        render->text_indices.count,
        GL_UNSIGNED_INT,
        0);

    glBindVertexArray(0);

    render->text_points.count = 0;
    render->text_indices.count = 0;
}

double get_time() {
    struct timespec ts;
    clock_gettime(TIME_UTC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
