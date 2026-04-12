#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../extern/stb_truetype.h"
#include "../extern/yar.h"

#include "../extern/glad/include/glad/glad.h"

#include "../extern/rgfw.h"

typedef struct {
    float x, y, w, h;
} SimpRectangle;

typedef struct {
    float r, g, b, a;
} SimpColor;

#define SIMP_COLOR_RED (SimpColor){1, 0, 0, 1}
#define SIMP_COLOR_WHITE (SimpColor){1, 1, 1, 1}
#define SIMP_COLOR_GRAY (SimpColor){0.5, 0.5, 0.5, 1}

typedef struct {
    uint32_t texture;
    stbtt_bakedchar cdata[96];
    int tex_w, tex_h;
} SimpFont;

typedef struct {
    uint32_t texture;
    yar(float) points;
    yar(uint32_t) indices;
} SimpTextBatch;

typedef struct {
    uint32_t texture;
    int w, h;
} SimpTexture;

typedef struct {
    uint32_t texture;
    yar(float) points;
    yar(uint32_t) indices;
} SimpTextureBatch;

typedef struct {
    RGFW_window* window;
    yar(float) color_points;
    yar(uint32_t) color_indices;

    yar(SimpTextBatch) text_batches;

    uint32_t color_vao, color_vbo, color_ebo;
    uint32_t text_vao, text_vbo, text_ebo;

    uint32_t shader;
    uint32_t text_shader;

    int proj_loc;
    int text_proj_loc;

    yar(SimpTextureBatch) tex_batches;
    uint32_t tex_vao, tex_vbo, tex_ebo;
    uint32_t tex_shader;
    int tex_proj_loc;
} SimpRender;

SimpRender simp_init(const char* title, int w, int h);
bool simp_should_close(SimpRender* render);
float simp_get_screen_w(const SimpRender* render);
float simp_get_screen_h(const SimpRender* render);

void simp_rectangle(SimpRender* r, SimpRectangle rect, SimpColor c);
void simp_clear_background(const SimpRender* r, SimpColor c);

SimpTexture simp_load_texture(const char* path);
SimpTexture simp_load_texture_from_memory(
    const uint8_t* bytes,
    int len);  // NOTE: This is not akin to loading a raw seq. of bytes
void simp_draw_texture(SimpRender* r, SimpTexture* tex, float x, float y);
void simp_draw_texture_ex(SimpRender* r,
                          SimpTexture* tex,
                          SimpRectangle src,
                          SimpRectangle dst,
                          SimpColor tint);

SimpFont simp_font_load(const char* path, float pixel_height);
void simp_text(SimpRender* r,
               SimpFont* font,
               const char* text,
               float x,
               float y,
               SimpColor c);

void simp_flush(SimpRender* r);
