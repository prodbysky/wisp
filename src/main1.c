#include <FLAC/format.h>
#include <FLAC/metadata.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "../extern/yar.h"
#include "../extern/stb_image.h"
#include "../extern/raylib/src/raylib.h"

#include "library.h"
#include "audio.h"

#define BG_COLOR            GetColor(0x181818ff)
#define TEXT_COLOR          GetColor(0xaaaaaaff)
#define TEXT_DIM_COLOR      GetColor(0x555555ff)
#define BG_FOCUSED_COLOR    GetColor(0x484848ff)
#define BG_UNFOCUSED_COLOR  GetColor(0x282828ff)

#define TRACK_CELL_H   29.0f
#define TRACK_ROW_H    24.0f
#define COVER_LIST_H   138.0f
#define NOW_PLAYING_H   64.0f
#define PADDING          5.0f
#define COVER_ART_SIZE  54.0f

#define LERP_SPEED_FAST   16.0f
#define LERP_SPEED_MED     8.0f
#define LERP_SPEED_SLOW    5.0f

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

typedef struct {
    Library    library;
    Font       font;
    Texture2D* covers;

    float* cover_scales;

    float  album_offset;
    float  album_offset_vel;
    size_t selected_album;
    size_t selected_track;

    float track_offset;
    float track_offset_vel;

    float  highlight_y;

    float  display_progress;

    float  bar_slide;
    bool   bar_was_visible;

    float  queue_alpha;

    Audio audio;

    bool show_queue;
} Wisp;


static float album_cover_size(const Wisp* w) {
    return 128;
}

static float min_album_offset(const Wisp* w) {
    float total = w->library.albums.count * album_cover_size(w);
    float avail = (float)GetScreenWidth();
    float min   = avail - total;
    return min < 0.0f ? min : 0.0f;
}

static float min_track_offset(const Wisp* w) {
    float avail = (float)GetScreenHeight() - COVER_LIST_H - NOW_PLAYING_H - PADDING * 4;
    float total = w->library.albums.items[w->selected_album].tracks.count * TRACK_CELL_H;
    float min   = avail - total;
    return min < 0.0f ? min : 0.0f;
}

Wisp wisp_init(void);
void wisp_update(Wisp* w);
void wisp_draw(const Wisp* w);

int main(void) {
    Wisp ws = wisp_init();

    while (!WindowShouldClose()) {
        wisp_update(&ws);
        wisp_draw(&ws);
    }

    audio_stop_playback(&ws.audio);
    UnloadFont(ws.font);
    unload_library(&ws.library);
    free(ws.covers);
    free(ws.cover_scales);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

void wisp_update(Wisp* w) {
    const float delta = GetFrameTime();
    const bool  ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool  shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);

    audio_update(&w->audio);

    if (IsKeyPressed(KEY_SPACE)) {
        audio_toggle_playing_state(&w->audio);
    }

    if (IsKeyPressed(KEY_N)) audio_skip_track(&w->audio);

    float seek = 0.0f;
    if (IsKeyPressed(KEY_RIGHT)) seek =  5.0f;
    if (IsKeyPressed(KEY_LEFT))  seek = -5.0f;
    audio_try_seeking_by(&w->audio, seek);

    if (ctrl && IsKeyPressed(KEY_Q)) w->show_queue = !w->show_queue;

    {
        size_t count      = w->library.albums.count;
        float  cover_size = album_cover_size(w);
        float  half_w     = (float)GetScreenWidth() / 2.0f;

        if (IsKeyPressed(KEY_L) && w->selected_album < count - 1) {
            w->selected_album++;
            w->selected_track = 0;
            w->track_offset   = 0.0f;
            w->highlight_y    = 0.0f;
            float sel_x = (float)w->selected_album * cover_size + w->album_offset;
            if (sel_x < half_w) w->album_offset_vel = -3850.0f;
        }

        if (IsKeyPressed(KEY_H) && w->selected_album > 0) {
            w->selected_album--;
            w->selected_track = 0;
            w->track_offset   = 0.0f;
            w->highlight_y    = 0.0f;
            float sel_x = (float)w->selected_album * cover_size + w->album_offset;
            if (sel_x < half_w) w->album_offset_vel = 3850.0f;
        }

        w->album_offset += w->album_offset_vel * delta;
        float min_off = min_album_offset(w);
        if (w->album_offset > 0.0f)    { w->album_offset = 0.0f;    w->album_offset_vel = 0.0f; }
        if (w->album_offset < min_off) { w->album_offset = min_off; w->album_offset_vel = 0.0f; }
        w->album_offset_vel /= 1.2f;

        for (size_t i = 0; i < w->library.albums.count; i++) {
            float target = (i == w->selected_album) ? 1.05f : 1.0f;
            w->cover_scales[i] = lerpf(w->cover_scales[i], target, clampf(LERP_SPEED_MED * delta, 0.0f, 1.0f));
        }
    }

    {
        Album* album       = &w->library.albums.items[w->selected_album];
        size_t track_count = album->tracks.count;
        float  list_h      = (float)GetScreenHeight() - COVER_LIST_H - NOW_PLAYING_H - PADDING * 4;

        if (IsKeyPressed(KEY_J) && w->selected_track < track_count - 1) {
            w->selected_track++;
            float track_bottom = (float)w->selected_track * TRACK_CELL_H + TRACK_CELL_H + w->track_offset;
            if (track_bottom > list_h) {
                w->track_offset -= TRACK_CELL_H;
                float min_off = min_track_offset(w);
                if (w->track_offset < min_off) w->track_offset = min_off;
            }
        }

        if (IsKeyPressed(KEY_K) && w->selected_track > 0) {
            w->selected_track--;
            float track_top = (float)w->selected_track * TRACK_CELL_H + w->track_offset;
            if (track_top < 0.0f) {
                w->track_offset += TRACK_CELL_H;
                if (w->track_offset > 0.0f) w->track_offset = 0.0f;
            }
        }

        if (IsKeyPressed(KEY_ENTER)) {
            audio_start_playback(&w->audio, album->tracks.items[w->selected_track]);
        }

        if (IsKeyPressed(KEY_Q) && !ctrl && !shift) {
            audio_enqueue_single(&w->audio, album->tracks.items[w->selected_track]);
        }

        if (shift && IsKeyPressed(KEY_Q)) {
            for (size_t i = w->selected_track; i < album->tracks.count; i++) {
                audio_enqueue_single(&w->audio, album->tracks.items[i]);
            }
        }

        float ww = (float)GetScreenWidth(), wh = (float)GetScreenHeight();
        Rectangle tracklist_rect = {
            PADDING, COVER_LIST_H + PADDING * 2,
            ww - PADDING * 2, wh - COVER_LIST_H - NOW_PLAYING_H - PADDING * 4,
        };
        if (CheckCollisionPointRec(GetMousePosition(), tracklist_rect)) {
            w->track_offset_vel += GetMouseWheelMove() * 2500.0f;
        }

        w->track_offset += w->track_offset_vel * delta;
        float min_off = min_track_offset(w);
        if (w->track_offset > 0.0f)    { w->track_offset = 0.0f;    w->track_offset_vel = 0.0f; }
        if (w->track_offset < min_off) { w->track_offset = min_off; w->track_offset_vel = 0.0f; }
        w->track_offset_vel /= 1.2f;

        float target_hy = (float)w->selected_track * TRACK_CELL_H;
        w->highlight_y  = lerpf(w->highlight_y, target_hy, clampf(LERP_SPEED_FAST * delta, 0.0f, 1.0f));
    }

    float real_progress = audio_get_current_track_progress(&w->audio);
    w->display_progress = lerpf(w->display_progress, real_progress,
        clampf(LERP_SPEED_FAST * delta, 0.0f, 1.0f));

    float target_slide = audio_has_loaded_track(&w->audio) ? 0.0f : NOW_PLAYING_H + PADDING;
    w->bar_slide = lerpf(w->bar_slide, target_slide, clampf(LERP_SPEED_MED * delta, 0.0f, 1.0f));

    float target_qa = w->show_queue ? 1.0f : 0.0f;
    w->queue_alpha  = lerpf(w->queue_alpha, target_qa,
        clampf(LERP_SPEED_SLOW * delta, 0.0f, 1.0f));
}


static void draw_text_in_row(Font font, const char* text, Rectangle row, float font_size, Color color) {
    BeginScissorMode((int)row.x, (int)row.y, (int)row.width, (int)row.height);
    float ty = row.y + (row.height - font_size) / 2.0f;
    DrawTextEx(font, text, (Vector2){ row.x + 6, ty }, font_size, 0.0f, color);
    EndScissorMode();
}

static Color color_alpha_mul(Color c, float a) {
    c.a = (unsigned char)(c.a * clampf(a, 0.0f, 1.0f));
    return c;
}

static void draw_album_list(const Wisp* w) {
    const float ww        = (float)GetScreenWidth();
    float cover_sz  = album_cover_size(w);
    Rectangle list  = { 
        .x = PADDING, 
        .y = PADDING, 
        .width = ww - PADDING * 2, 
        .height = COVER_LIST_H - PADDING 
    };

    // first draw album covers that are not selected
    // so that the selected one is layered above
    for (size_t i = 0; i < w->library.albums.count; i++) {
        if (i != w->selected_album) {
            float scale   = w->cover_scales[i];
            float base_x  = (float)i * cover_sz + w->album_offset + list.x;
            float base_y  = list.y;
            float sz      = cover_sz * scale;
            float draw_x  = base_x + (cover_sz - sz) / 2.0f;
            float draw_y  = base_y + (cover_sz - sz) / 2.0f;

            Rectangle dst = { draw_x, draw_y, sz, sz };
            Rectangle src = { 0, 0, (float)w->covers[i].width, (float)w->covers[i].height };
            DrawTexturePro(w->covers[i], src, dst, (Vector2){0}, 0.0f, WHITE);
                DrawRectangleRec(dst, GetColor(0x00000099));
        }
    }
    float scale   = w->cover_scales[w->selected_album];
    float base_x  = (float)w->selected_album * cover_sz + w->album_offset + list.x;
    float base_y  = list.y;
    float sz      = cover_sz * scale;
    float draw_x  = base_x + (cover_sz - sz) / 2.0f;
    float draw_y  = base_y + (cover_sz - sz) / 2.0f;

    Rectangle dst = { draw_x, draw_y, sz, sz };
    Rectangle src = { 0, 0, (float)w->covers[w->selected_album].width, (float)w->covers[w->selected_album].height };
    DrawTexturePro(w->covers[w->selected_album], src, dst, (Vector2){0}, 0.0f, WHITE);
    DrawRectangleLinesEx(dst, 2.0f, TEXT_COLOR);
}


static void draw_tracklist(const Wisp* w) {
    float ww   = (float)GetScreenWidth();
    float wh   = (float)GetScreenHeight();
    Rectangle list = {
        PADDING, COVER_LIST_H + PADDING,
        ww - PADDING * 2, wh - COVER_LIST_H - NOW_PLAYING_H - PADDING * 4,
    };

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    Album* album = &w->library.albums.items[w->selected_album];

    {
        Rectangle highlight = {
            list.x,
            list.y + w->highlight_y + w->track_offset,
            list.width,
            TRACK_ROW_H,
        };
        DrawRectangleRounded(highlight, 0.5f, 16, BG_FOCUSED_COLOR);
    }

    for (size_t i = 0; i < album->tracks.count; i++) {
        Rectangle row = {
            list.x,
            list.y + (float)i * TRACK_CELL_H + w->track_offset,
            list.width,
            TRACK_ROW_H,
        };

        if (i != w->selected_track) {
            DrawRectangleRounded(row, 0.5f, 16, BG_UNFOCUSED_COLOR);
        }

        draw_text_in_row(w->font, album->tracks.items[i]->title, row, TRACK_ROW_H, TEXT_COLOR);
    }
    EndScissorMode();
}


static void draw_now_playing(const Wisp* w) {
    float ww   = (float)GetScreenWidth();
    float wh   = (float)GetScreenHeight();
    float y    = wh - NOW_PLAYING_H - PADDING + w->bar_slide;
    Rectangle bar = { PADDING, y, ww - PADDING * 2, NOW_PLAYING_H };
    DrawRectangleRounded(bar, 0.3f, 16, BG_UNFOCUSED_COLOR);

    if (!audio_has_loaded_track(&w->audio) && w->bar_slide > NOW_PLAYING_H * 0.9f) {
        return;
    }

    if (!audio_has_loaded_track(&w->audio)) {
        draw_text_in_row(w->font, "Nothing playing, press `Enter` on a track, to jam out :)", bar, TRACK_ROW_H, TEXT_DIM_COLOR);
        return;
    }

    float text_x  = PADDING * 2;
    float time_w  = 130.0f;
    float text_w  = ww - text_x - PADDING - time_w - PADDING;
    Rectangle title_rect = { text_x, bar.y + 8.0f, text_w, TRACK_ROW_H };
    Color title_color = color_alpha_mul(TEXT_COLOR, 1);
    draw_text_in_row(w->font, audio_get_current_track_title(&w->audio), title_rect, TRACK_ROW_H, title_color);

    float pb_y  = bar.y + NOW_PLAYING_H - 12.0f;
    float pb_x  = text_x;
    float pb_w  = text_w;
    Rectangle pb_bg = { pb_x, pb_y, pb_w, 4.0f };
    DrawRectangleRounded(pb_bg, 1.0f, 8, BG_FOCUSED_COLOR);
    if (w->display_progress > 0.0f) {
        Rectangle pb_fill = { pb_x, pb_y, pb_w * w->display_progress, 4.0f };
        DrawRectangleRounded(pb_fill, 1.0f, 8, TEXT_COLOR);
    }

    float played = GetMusicTimePlayed(w->audio.music);
    float total  = GetMusicTimeLength(w->audio.music);
    int pm = (int)(played / 60), ps = (int)played % 60;
    int tm = (int)(total  / 60), ts = (int)total  % 60;

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d:%02d / %d:%02d", pm, ps, tm, ts);
    float tx = bar.x + bar.width - time_w;
    DrawTextEx(w->font, w->audio.playing ? "Playing" : "Paused",
        (Vector2){ tx, bar.y + 8.0f }, 24.0f, 0.0f, TEXT_COLOR);
    DrawTextEx(w->font, time_str,
        (Vector2){ tx, bar.y + NOW_PLAYING_H - 24.0f }, 24.0f, 0.0f, TEXT_COLOR);
}


static void draw_queue_overlay(const Wisp* w) {
    if (w->queue_alpha < 0.01f) return;

    float ww = (float)GetScreenWidth();
    float wh = (float)GetScreenHeight();

    DrawRectangle(0, 0, (int)ww, (int)wh, color_alpha_mul(GetColor(0x181818ee), w->queue_alpha));

    Color text_c      = color_alpha_mul(TEXT_COLOR,     w->queue_alpha);
    Color dim_c       = color_alpha_mul(TEXT_DIM_COLOR, w->queue_alpha);
    Color focused_c   = color_alpha_mul(BG_FOCUSED_COLOR,   w->queue_alpha);
    Color unfocused_c = color_alpha_mul(BG_UNFOCUSED_COLOR, w->queue_alpha);

    DrawTextEx(w->font, "Queue  (Ctrl+Q to close)",
        (Vector2){ PADDING + 4, PADDING + 4 }, 20.0f, 0.0f, text_c);

    if (audio_queue_is_empty(&w->audio)) {
        DrawTextEx(w->font, "Queue is empty",
            (Vector2){ PADDING + 4, PADDING + 36 }, TRACK_ROW_H, 0.0, dim_c);
        return;
    }

    const float y = PADDING + 36.0f;
    for (size_t i = 0; i < w->audio.queue.tracks.count; i++) {
        Rectangle row = { PADDING, y + (float)i * TRACK_CELL_H, ww - PADDING * 2, TRACK_ROW_H };
        Color bg = (i == 0) ? focused_c : unfocused_c;
        DrawRectangleRounded(row, 0.5f, 16, bg);
        draw_text_in_row(w->font, w->audio.queue.tracks.items[i]->title, row, TRACK_ROW_H, text_c);
    }
}


void wisp_draw(const Wisp* w) {
    BeginDrawing();
    ClearBackground(BG_COLOR);

    draw_tracklist(w);
    draw_album_list(w);
    draw_now_playing(w);
    draw_queue_overlay(w);   

    EndDrawing();
}

Wisp wisp_init(void) {
    Library lib = prepare_library("/home/shr/Downloads/Nicotine");

    SetWindowState(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "wispy");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    InitAudioDevice();
    SetTargetFPS(180);

    Font font = LoadFontEx("res/Iosevka.ttf", 24, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_ANISOTROPIC_16X);

    Image*     imgs         = malloc(lib.albums.count * sizeof(Image));
    Texture2D* tex          = malloc(lib.albums.count * sizeof(Texture2D));
    float*     cover_scales = calloc(lib.albums.count, sizeof(float));

    for (size_t i = 0; i < lib.albums.count; i++) {
        cover_scales[i] = 1.0f;
        Track* t = lib.albums.items[i].tracks.items[0];
        imgs[i] = (Image){
            .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
            .width   = t->cover_w,
            .height  = t->cover_h,
            .data    = t->cover,
            .mipmaps = 1,
        };
        tex[i] = LoadTextureFromImage(imgs[i]);
        free(lib.albums.items[i].tracks.items[0]->cover);
    }
    free(imgs);

    cover_scales[0] = 1.05f;

    return (Wisp){
        .font           = font,
        .library        = lib,
        .covers         = tex,
        .cover_scales   = cover_scales,
        .bar_slide      = NOW_PLAYING_H + PADDING,   
    };
}
