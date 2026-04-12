#include "simp.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define RGFW_OPENGL
#include "../extern/glad/include/glad/glad.h"
#include "../extern/rgfw.h"
#include "../extern/stb_image.h"

static const char* vs =
    "#version 460 core\n"
    "layout (location=0) in vec2 aPos;"
    "layout (location=1) in vec4 aColor;"
    "uniform mat4 uProj;"
    "out vec4 c;"
    "void main(){ gl_Position=uProj*vec4(aPos,0,1); c=aColor;}";

static const char* fs =
    "#version 460 core\n"
    "in vec4 c; out vec4 Frag;"
    "void main(){ Frag=c;}";

static const char* tvs =
    "#version 460 core\n"
    "layout (location=0) in vec2 aPos;"
    "layout (location=1) in vec4 aColor;"
    "layout (location=2) in vec2 aUV;"
    "uniform mat4 uProj;"
    "out vec4 c; out vec2 uv;"
    "void main(){ gl_Position=uProj*vec4(aPos,0,1); c=aColor; uv=aUV;}";

static const char* tfs =
    "#version 460 core\n"
    "in vec4 c; in vec2 uv; out vec4 Frag;"
    "uniform sampler2D tex;"
    "void main(){ Frag = vec4(texture(tex,uv).r)*c;}";

static const char* imgvs =
    "#version 460 core\n"
    "layout(location=0) in vec2 aPos;"
    "layout(location=1) in vec4 aColor;"
    "layout(location=2) in vec2 aUV;"
    "uniform mat4 uProj;"
    "out vec4 c; out vec2 uv;"
    "void main(){ gl_Position=uProj*vec4(aPos,0,1); c=aColor; uv=aUV;}";

static const char* imgfs =
    "#version 460 core\n"
    "in vec4 c; in vec2 uv; out vec4 Frag;"
    "uniform sampler2D tex;"
    "void main(){ Frag = texture(tex,uv)*c;}";

static uint32_t make_shader(const char* vs_src, const char* fs_src);
static void make_ortho(float m[16], float w, float h);
static SimpTextBatch* get_batch(SimpRender* r, uint32_t texture);
static SimpTextureBatch* get_tex_batch(SimpRender* r, uint32_t texture);

SimpRender simp_init() {
    SimpRender r = {0};

    glGenVertexArrays(1, &r.color_vao);
    glGenBuffers(1, &r.color_vbo);
    glGenBuffers(1, &r.color_ebo);

    glBindVertexArray(r.color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.color_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, 0, 6 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &r.text_vao);
    glGenBuffers(1, &r.text_vbo);
    glGenBuffers(1, &r.text_ebo);

    glBindVertexArray(r.text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.text_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, 0, 8 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, 0, 8 * sizeof(float),
                          (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    r.shader = make_shader(vs, fs);
    r.text_shader = make_shader(tvs, tfs);

    r.proj_loc = glGetUniformLocation(r.shader, "uProj");
    r.text_proj_loc = glGetUniformLocation(r.text_shader, "uProj");

    glUseProgram(r.text_shader);
    glUniform1i(glGetUniformLocation(r.text_shader, "tex"), 0);

    glGenVertexArrays(1, &r.tex_vao);
    glGenBuffers(1, &r.tex_vbo);
    glGenBuffers(1, &r.tex_ebo);

    glBindVertexArray(r.tex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.tex_vbo);
    // stride = 8 floats: xy | rgba | uv  (same layout as text)
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, 0, 8 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, 0, 8 * sizeof(float),
                          (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    r.tex_shader = make_shader(imgvs, imgfs);
    r.tex_proj_loc = glGetUniformLocation(r.tex_shader, "uProj");

    glUseProgram(r.tex_shader);
    glUniform1i(glGetUniformLocation(r.tex_shader, "tex"), 0);

    return r;
}

void simp_rectangle(SimpRender* r, SimpRectangle rect, SimpColor c) {
    float v[] = {
        rect.x,          rect.y,          c.r, c.g, c.b, c.a,
        rect.x + rect.w, rect.y,          c.r, c.g, c.b, c.a,
        rect.x + rect.w, rect.y + rect.h, c.r, c.g, c.b, c.a,
        rect.x,          rect.y + rect.h, c.r, c.g, c.b, c.a,
    };

    for (int i = 0; i < 24; i++)
        *yar_append(&r->color_points) = v[i];

    uint32_t b = (r->color_points.count / 6) - 4;

    uint32_t inds[] = {b, b + 1, b + 2, b, b + 2, b + 3};
    for (int i = 0; i < 6; i++)
        *yar_append(&r->color_indices) = inds[i];
}

void simp_text(SimpRender* r,
               SimpFont* font,
               const char* text,
               float x,
               float y,
               SimpColor c) {
    SimpTextBatch* batch = get_batch(r, font->texture);

    while (*text) {
        if (*text >= 32 && *text < 127) {
            stbtt_bakedchar* b = &font->cdata[*text - 32];

            float x0 = x + b->xoff, y0 = y + b->yoff;
            float x1 = x0 + (b->x1 - b->x0);
            float y1 = y0 + (b->y1 - b->y0);

            float u0 = b->x0 / (float)font->tex_w;
            float v0 = b->y0 / (float)font->tex_h;
            float u1 = b->x1 / (float)font->tex_w;
            float v1 = b->y1 / (float)font->tex_h;

            float vtx[] = {
                x0,  y0,  c.r, c.g, c.b, c.a, u0,  v0,  x1,  y0,  c.r,
                c.g, c.b, c.a, u1,  v0,  x1,  y1,  c.r, c.g, c.b, c.a,
                u1,  v1,  x0,  y1,  c.r, c.g, c.b, c.a, u0,  v1,
            };

            for (int i = 0; i < 32; i++)
                *yar_append(&batch->points) = vtx[i];

            uint32_t base = (batch->points.count / 8) - 4;

            uint32_t inds[] = {base, base + 1, base + 2,
                               base, base + 2, base + 3};

            for (int i = 0; i < 6; i++)
                *yar_append(&batch->indices) = inds[i];

            x += b->xadvance;
        }
        text++;
    }
}

void simp_flush(SimpRender* r, float w, float h) {
    glViewport(0, 0, w, h);
    float proj[16];
    make_ortho(proj, w, h);

    // rectangles
    glBindVertexArray(r->color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->color_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->color_ebo);

    glBufferData(GL_ARRAY_BUFFER, r->color_points.count * sizeof(float),
                 r->color_points.items, GL_DYNAMIC_DRAW);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 r->color_indices.count * sizeof(uint32_t),
                 r->color_indices.items, GL_DYNAMIC_DRAW);

    glUseProgram(r->shader);
    glUniformMatrix4fv(r->proj_loc, 1, 0, proj);

    glDrawElements(GL_TRIANGLES, r->color_indices.count, GL_UNSIGNED_INT, 0);

    r->color_points.count = 0;
    r->color_indices.count = 0;

    // text batches
    glBindVertexArray(r->text_vao);
    glUseProgram(r->text_shader);
    glUniformMatrix4fv(r->text_proj_loc, 1, 0, proj);

    for (size_t i = 0; i < r->text_batches.count; i++) {
        SimpTextBatch* b = &r->text_batches.items[i];

        glBindBuffer(GL_ARRAY_BUFFER, r->text_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->text_ebo);

        glBufferData(GL_ARRAY_BUFFER, b->points.count * sizeof(float),
                     b->points.items, GL_DYNAMIC_DRAW);

        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     b->indices.count * sizeof(uint32_t), b->indices.items,
                     GL_DYNAMIC_DRAW);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->texture);

        glDrawElements(GL_TRIANGLES, b->indices.count, GL_UNSIGNED_INT, 0);

        b->points.count = 0;
        b->indices.count = 0;
    }

    r->text_batches.count = 0;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(r->tex_vao);
    glUseProgram(r->tex_shader);
    glUniformMatrix4fv(r->tex_proj_loc, 1, 0, proj);

    for (size_t i = 0; i < r->tex_batches.count; i++) {
        SimpTextureBatch* b = &r->tex_batches.items[i];

        glBindBuffer(GL_ARRAY_BUFFER, r->tex_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->tex_ebo);

        glBufferData(GL_ARRAY_BUFFER, b->points.count * sizeof(float),
                     b->points.items, GL_DYNAMIC_DRAW);

        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     b->indices.count * sizeof(uint32_t), b->indices.items,
                     GL_DYNAMIC_DRAW);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->texture);

        glDrawElements(GL_TRIANGLES, b->indices.count, GL_UNSIGNED_INT, 0);

        b->points.count = 0;
        b->indices.count = 0;
    }
    r->tex_batches.count = 0;

    glDisable(GL_BLEND);
}

SimpFont simp_font_load(const char* path, float pixel_height) {
    SimpFont font = {0};

    FILE* f = fopen(path, "rb");
    assert(f);

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buffer = malloc(size);
    fread(buffer, 1, size, f);
    fclose(f);

    font.tex_w = 512;
    font.tex_h = 512;

    unsigned char* bitmap = calloc(font.tex_w * font.tex_h, 1);

    stbtt_BakeFontBitmap(buffer, 0, pixel_height, bitmap, font.tex_w,
                         font.tex_h, 32, 96, font.cdata);

    free(buffer);

    glGenTextures(1, &font.texture);
    glBindTexture(GL_TEXTURE_2D, font.texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font.tex_w, font.tex_h, 0, GL_RED,
                 GL_UNSIGNED_BYTE, bitmap);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    free(bitmap);

    return font;
}

SimpTexture simp_load_texture(const char* path) {
    SimpTexture t = {0};

    int channels;
    unsigned char* data = stbi_load(path, &t.w, &t.h, &channels, 4);
    assert(data);

    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return t;
}

SimpTexture simp_load_texture_from_memory(const uint8_t* bytes, int len) {
    SimpTexture t = {0};

    int channels;
    unsigned char* data =
        stbi_load_from_memory(bytes, len, &t.w, &t.h, &channels, 4);
    assert(data);

    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return t;
}

void simp_draw_texture_ex(SimpRender* r,
                          SimpTexture* tex,
                          SimpRectangle src,
                          SimpRectangle dst,
                          SimpColor tint) {
    SimpTextureBatch* batch = get_tex_batch(r, tex->texture);

    float u0 = src.x / (float)tex->w;
    float v0 = src.y / (float)tex->h;
    float u1 = (src.x + src.w) / (float)tex->w;
    float v1 = (src.y + src.h) / (float)tex->h;

    float x0 = dst.x, y0 = dst.y;
    float x1 = dst.x + dst.w, y1 = dst.y + dst.h;

    float vtx[] = {
        x0, y0, tint.r, tint.g, tint.b, tint.a, u0, v0,
        x1, y0, tint.r, tint.g, tint.b, tint.a, u1, v0,
        x1, y1, tint.r, tint.g, tint.b, tint.a, u1, v1,
        x0, y1, tint.r, tint.g, tint.b, tint.a, u0, v1,
    };

    for (int i = 0; i < 32; i++)
        *yar_append(&batch->points) = vtx[i];

    uint32_t base = (batch->points.count / 8) - 4;
    uint32_t inds[] = {base, base + 1, base + 2, base, base + 2, base + 3};
    for (int i = 0; i < 6; i++)
        *yar_append(&batch->indices) = inds[i];
}

void simp_draw_texture(SimpRender* r, SimpTexture* tex, float x, float y) {
    simp_draw_texture_ex(r, tex, (SimpRectangle){0, 0, tex->w, tex->h},
                         (SimpRectangle){x, y, tex->w, tex->h},
                         (SimpColor){1, 1, 1, 1});
}

static uint32_t make_shader(const char* vs_src, const char* fs_src) {
    uint32_t vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);

    uint32_t fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);

    uint32_t prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return prog;
}

static void make_ortho(float m[16], float w, float h) {
    for (int i = 0; i < 16; i++)
        m[i] = 0.0f;

    m[0] = 2.0f / w;
    m[5] = -2.0f / h;
    m[10] = -1.0f;

    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}
static SimpTextBatch* get_batch(SimpRender* r, uint32_t texture) {
    for (size_t i = 0; i < r->text_batches.count; i++) {
        if (r->text_batches.items[i].texture == texture)
            return &r->text_batches.items[i];
    }

    SimpTextBatch batch = {0};
    batch.texture = texture;

    *yar_append(&r->text_batches) = batch;
    return &r->text_batches.items[r->text_batches.count - 1];
}

static SimpTextureBatch* get_tex_batch(SimpRender* r, uint32_t texture) {
    for (size_t i = 0; i < r->tex_batches.count; i++)
        if (r->tex_batches.items[i].texture == texture)
            return &r->tex_batches.items[i];

    SimpTextureBatch b = {0};
    b.texture = texture;
    *yar_append(&r->tex_batches) = b;
    return &r->tex_batches.items[r->tex_batches.count - 1];
}
