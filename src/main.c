#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>

#include "audio.h"
#include "library.h"
#include "dft.h"

#define FONT_SIZE 24
#define BARS 512

typedef enum {
    PANE_MAIN,
    PANE_QUEUE,
    PANE_VISUAL,
    PANE_COUNT,
} Pane;

typedef enum {
    MP_ALBUM,
    MP_TRACK,
} MainPane;

typedef struct {
    Library library;
    Font font;
    Texture2D* covers;
    size_t selected_album;
    size_t selected_track;

    float actual_album_offset;
    float wanted_album_offset;

    float track_scroll;
    float wanted_track_scroll;

    Pane pane;
    MainPane main_pane;

    Audio audio;

    Color* album_average_colors;
    Color last_color;
    float last_switch;

    float magnitudes[DFT_SIZE / 2];
} Wisp;

static float color_luminance(Color c) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;

    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

Wisp wisp_init(int argc, char** argv);
void wisp_tick(Wisp* wisp);

typedef struct {
    Color focused_text;
    Color unfocused_text;
    Color shadow;
    Color rectangle;
} Theme;

Theme wisp_derive_theme(const Wisp* w);

int main(int argc, char** argv) {
    if (argc > 1 && strcmp("--help", argv[1]) == 0) {
        printf("Usage:\n");
        printf("%s [--help]: print this help message\n", argv[0]);
        printf(
            "%s <PATH>: use this path as the base of the music on your "
            "system\n",
            argv[0]);
        return 1;
    }
    Wisp ws = wisp_init(argc, argv);

    while (!WindowShouldClose()) {
        wisp_tick(&ws);
    }

    audio_stop_playback(&ws.audio);
    UnloadFont(ws.font);
    unload_library(&ws.library);
    free(ws.covers);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

const float ALBUM_COVER_SIDE_LENGTH = 128;

static void draw_queue(const Wisp* w, Rectangle bound);
static void draw_dft(const Wisp* w, Rectangle bound);

static void wisp_next_pane(Wisp* wisp);
static void wisp_next_loop_mode(Wisp* wisp);

static void wisp_play_selected_track(Wisp* wisp);

static void wisp_queue_album_from_the_selected_track(Wisp* wisp);

static const Album* wisp_get_selected_album(const Wisp* wisp);

static Color wisp_get_lerped_base_color(const Wisp* wisp);

static void prepare_dft_vis(Wisp* wisp);

static void wisp_draw_visual_pane(Wisp* wisp);

void wisp_tick(Wisp* wisp) {
    const float WW = GetScreenWidth();
    const float WH = GetScreenHeight();

    const float BORDER_PAD = 8;

    { // hotkeys
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        if (IsKeyPressed(KEY_TAB)) wisp_next_pane(wisp);

        if (IsKeyPressed(KEY_SPACE)) audio_toggle_playing_state(&wisp->audio);

        if (IsKeyPressed(KEY_PERIOD) && shift) audio_skip_track_forward(&wisp->audio);
        if (IsKeyPressed(KEY_COMMA) && shift)  audio_skip_track_backward(&wisp->audio); 

        if (IsKeyPressed(KEY_PERIOD) && !shift) audio_try_seeking_by(&wisp->audio, 5.0);
        if (IsKeyPressed(KEY_COMMA) && !shift) audio_try_seeking_by(&wisp->audio, -5.0);

        if (IsKeyPressed(KEY_S) && ctrl)  wisp->audio.shuffle = !wisp->audio.shuffle; 
        if (IsKeyPressed(KEY_R) && ctrl) wisp_next_loop_mode(wisp);

        if (wisp->pane == PANE_MAIN && wisp->main_pane == MP_ALBUM) {
            if (IsKeyPressed(KEY_J)) {
                if (wisp->selected_album != wisp->library.albums.count - 1){
                    wisp->last_color = wisp->album_average_colors[wisp->selected_album++];
                    wisp->last_switch = GetTime();
                }
            }
            if (IsKeyPressed(KEY_K)) {
                if (wisp->selected_album != 0) {
                    wisp->last_color = wisp->album_average_colors[wisp->selected_album--];
                    wisp->last_switch = GetTime();
                }
            }

            if (IsKeyPressed(KEY_L)) {
                wisp->main_pane = MP_TRACK;
                wisp->selected_track = 0;
            }
        }

        if (wisp->pane == PANE_MAIN && wisp->main_pane == MP_TRACK) {
            const float entry_height = FONT_SIZE + BORDER_PAD;
            const float top_bound = wisp->track_scroll + entry_height;
            const float bottom_bound = wisp->track_scroll + WH - entry_height;

            const float current_entry_offset =
                wisp->selected_track * entry_height;

            float max_scroll =
                wisp_get_selected_album(wisp)->tracks.count * entry_height - WH;
            if (max_scroll < 0) max_scroll = 0;

            if (wisp->wanted_track_scroll < 0) wisp->wanted_track_scroll = 0;
            if (wisp->wanted_track_scroll > max_scroll) wisp->wanted_track_scroll = max_scroll;

            if (current_entry_offset < top_bound) {
                wisp->wanted_track_scroll = current_entry_offset - entry_height;
            } else if (current_entry_offset > bottom_bound) {
                wisp->wanted_track_scroll = current_entry_offset - (WH - entry_height);
            }
            if (IsKeyPressed(KEY_J)) {
                if (wisp->selected_track != wisp_get_selected_album(wisp)->tracks.count - 1) wisp->selected_track++;
            }
            if (IsKeyPressed(KEY_K)) {
                if (wisp->selected_track != 0) wisp->selected_track--;
            }

            if (IsKeyPressed(KEY_H)) {
                wisp->main_pane = MP_ALBUM;
            }
        }


        if (IsKeyPressed(KEY_ENTER)) wisp_play_selected_track(wisp);

        if (shift && IsKeyPressed(KEY_Q)) wisp_queue_album_from_the_selected_track(wisp);
        if (IsKeyPressed(KEY_Q)) {
            Track* t = wisp->library.albums.items[wisp->selected_album].tracks.items[wisp->selected_track];
            audio_enqueue_single(&wisp->audio, t);
        }
        const float entry_height = FONT_SIZE + BORDER_PAD + 64;

        const float top_bound = wisp->actual_album_offset + entry_height;
        const float bottom_bound = wisp->actual_album_offset + WH - entry_height;
        const float current_entry_global_offset =
            BORDER_PAD + wisp->selected_album * entry_height;
        float max_offset = (wisp->library.albums.count * entry_height) - WH;
        if (max_offset < 0) max_offset = 0;

        if (wisp->wanted_album_offset < 0) wisp->wanted_album_offset = 0;
        if (wisp->wanted_album_offset > max_offset) wisp->wanted_album_offset = max_offset;
        if (current_entry_global_offset < top_bound) {
            wisp->wanted_album_offset = current_entry_global_offset - entry_height;
        } 
        else if (current_entry_global_offset > bottom_bound) {
            wisp->wanted_album_offset = current_entry_global_offset - (WH - entry_height);
        }
    }
    wisp->actual_album_offset += (wisp->wanted_album_offset - wisp->actual_album_offset) * 0.15;
    wisp->track_scroll += (wisp->wanted_track_scroll - wisp->track_scroll) * 0.15f;

    prepare_dft_vis(wisp);
    audio_update(&wisp->audio);
    BeginDrawing();
    ClearBackground(wisp_get_lerped_base_color(wisp));
    const Theme theme = wisp_derive_theme(wisp);

    switch (wisp->pane) {
        case PANE_MAIN: {
                {
                    BeginScissorMode(0, 0, WW / 3 - BORDER_PAD, WH);
                    Vector2 cursor = {BORDER_PAD, BORDER_PAD - wisp->actual_album_offset - (FONT_SIZE + 64)};
                    for (size_t ai = 0; ai < wisp->library.albums.count; ai++) {
                        const Color text_color = ai == wisp->selected_album ? theme.focused_text : ColorBrightness(theme.unfocused_text, -0.5);
                        const Rectangle text_background_rect = {cursor.x - 2, cursor.y, WW / 3 - BORDER_PAD * 2, FONT_SIZE + 4 + 64};
                        const Color text_background_color = ai == wisp->selected_album ? ColorBrightness(theme.rectangle, .5) : theme.rectangle;
                        const Rectangle cover_src = {.width = wisp->covers[ai].width, .height = wisp->covers[ai].height};
                        const Rectangle cover_dst = {.x = BORDER_PAD + BORDER_PAD / 2, .y = cursor.y + 14, .width = 64, .height = 64};
                        DrawRectangleRounded(text_background_rect, 0.25, 16, text_background_color);
                        DrawTexturePro(wisp->covers[ai], cover_src, cover_dst, (Vector2){}, 0, WHITE);
                        DrawTextPro(wisp->font, wisp->library.albums.items[ai].name, cursor, (Vector2){.x = -3 - 64 - 4 - 4, .y = -3 - 32}, 0, FONT_SIZE, 0, theme.shadow);
                        DrawTextPro(wisp->font, wisp->library.albums.items[ai].name, cursor, (Vector2){.x = -2 - 64 - 4 - 4, .y = -2 - 32}, 0, FONT_SIZE, 0, text_color);
                        cursor.y += FONT_SIZE + BORDER_PAD + 64;
                    }
                    EndScissorMode();
                }
                {
                    Vector2 cursor = {BORDER_PAD + WW / 3, BORDER_PAD - wisp->track_scroll};
                    BeginScissorMode(cursor.x - 3 * BORDER_PAD, cursor.y - BORDER_PAD, (WW * 2) / 3 + BORDER_PAD, WH);
                    for (size_t ti = 0; ti < wisp_get_selected_album(wisp)->tracks.count; ti++) {
                        const Color text_color = ti == wisp->selected_track ? theme.focused_text : ColorBrightness(theme.unfocused_text, -0.5);
                        const Rectangle text_background_rect = {cursor.x - 2, cursor.y, 2 * (WW / 3) - BORDER_PAD * 2, FONT_SIZE + 4};
                        DrawRectangleRounded(text_background_rect, 0.25, 16, theme.rectangle);
                        DrawTextPro(wisp->font, wisp_get_selected_album(wisp)->tracks.items[ti]->title, cursor, (Vector2){.x = -3, .y = -3}, 0, FONT_SIZE, 0, theme.shadow);
                        DrawTextPro(wisp->font, wisp_get_selected_album(wisp)->tracks.items[ti]->title, cursor, (Vector2){.x = -2, .y = -2}, 0, FONT_SIZE, 0, text_color);
                        cursor.y += FONT_SIZE + BORDER_PAD;
                    }
                    EndScissorMode();
                }
            break;
        }
        case PANE_QUEUE: {
            const Rectangle queue = {.x = 8, .y = 8, .width = WW, .height = WH};
            draw_queue(wisp, queue);
            break;
        }
        case PANE_VISUAL: {
            wisp_draw_visual_pane(wisp);
            break;
        }
        case PANE_COUNT: assert(false);
    }

    EndDrawing();
}

static void wisp_draw_visual_pane(Wisp* wisp) {
    const Rectangle dft_rect = {
        .x = 0,
        .y = -GetScreenHeight(),
        .width = GetScreenWidth(),
        .height = GetScreenHeight() * 2
    };
    if (wisp->audio.current_track == NULL) return;
    const char* title = wisp->audio.current_track->title;
    const char* album = wisp->audio.current_track->album;
    const char* artist = wisp->audio.current_track->artist;
    draw_dft(wisp, dft_rect);
    const Theme t = wisp_derive_theme(wisp);
    DrawTextEx(wisp->font, title, (Vector2){.x = 8, .y = 8}, FONT_SIZE, 0.0, t.focused_text);
    DrawTextEx(wisp->font, album, (Vector2){.x = 8, .y = 40}, FONT_SIZE, 0.0, t.focused_text);
    DrawTextEx(wisp->font, artist, (Vector2){.x = 8, .y = 72}, FONT_SIZE, 0.0, t.focused_text);

    DrawTextEx(wisp->font, title, (Vector2){.x = 8 + 2, .y = 8 + 2}, FONT_SIZE, 0.0, ColorAlpha(t.shadow, -.1));
    DrawTextEx(wisp->font, album, (Vector2){.x = 8 + 2, .y = 40 + 2}, FONT_SIZE, 0.0, ColorAlpha(t.shadow, -.1));
    DrawTextEx(wisp->font, artist, (Vector2){.x = 8 + 2, .y = 72 + 2}, FONT_SIZE, 0.0, ColorAlpha(t.shadow, -.1));
}

static void wisp_next_pane(Wisp* wisp) {
    wisp->pane++;
    wisp->pane %= PANE_COUNT;
}

static const Album* wisp_get_selected_album(const Wisp* wisp) {
    return &wisp->library.albums.items[wisp->selected_album];
}

static Color wisp_get_lerped_base_color(const Wisp* wisp) {
    float t = (GetTime() - wisp->last_switch) / 0.2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    return ColorLerp(wisp->last_color, wisp->album_average_colors[wisp->selected_album], t);
}

static void wisp_next_loop_mode(Wisp* wisp) {
    wisp->audio.loop_mode++;
    if (wisp->audio.loop_mode > LOOP_ALL) wisp->audio.loop_mode = LOOP_NONE;
}

static void wisp_play_selected_track(Wisp* wisp) {
    audio_start_playback(&wisp->audio, wisp_get_selected_album(wisp)->tracks.items[wisp->selected_track]);
}

static void wisp_queue_album_from_the_selected_track(Wisp* wisp) {
    const Album* selected_album = wisp_get_selected_album(wisp);
    for (size_t i = wisp->selected_track; i < selected_album->tracks.count; i++) {
        audio_enqueue_single(&wisp->audio, selected_album->tracks.items[i]);
    }
}

static void prepare_dft_vis(Wisp* wisp) {
    if (dft_shared_buf_ready && wisp->pane == PANE_VISUAL) {
        dft_shared_buf_ready = 0;

        static Complex out[DFT_SIZE];
        for (int i = 0; i < DFT_SIZE; i++) {
            float w = 0.5f * (1.0f - cosf(2.0f * PI * i / (DFT_SIZE - 1)));
            dft_shared_buf[i] *= w;
        }
        compute_dft(dft_shared_buf, out, DFT_SIZE);

        for (int k = 0; k < DFT_SIZE / 2; k++) {

            float mag = sqrtf(out[k].real * out[k].real + out[k].imag * out[k].imag);
            float db = 20.0f * log10f(mag + 1e-6f);
            const float min_db = -20.0f;
            const float max_db = 20.0f;

            float norm = (db - min_db) / (max_db - min_db);
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;
            wisp->magnitudes[k] = wisp->magnitudes[k] * 0.6 + norm * 0.4;
        }
    }
}


static void draw_queue(const Wisp* w, Rectangle bound) {
    const float child_spacing = 8.0f;
    const float item_height = FONT_SIZE + child_spacing;

    const Theme theme = wisp_derive_theme(w);

    float center_y = bound.y + bound.height * 0.5f;

    if (w->audio.current_track) {
        Rectangle rect = {
            .x = bound.x,
            .y = center_y,
            .width = GetScreenWidth() - bound.x * 2,
            .height = FONT_SIZE + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, theme.rectangle);

        DrawTextEx(w->font, w->audio.current_track->title, (Vector2){bound.x + 5, center_y + 3}, FONT_SIZE, 0.0f,
                   theme.focused_text);
    }

    for (size_t i = 0; i < w->audio.queue.history.items.count; i++) {
        size_t idx = w->audio.queue.history.items.count - 1 - i;

        float y = center_y - item_height * (i + 1);

        Rectangle rect = {
            .x = bound.x,
            .y = y,
            .width = GetScreenWidth() - bound.x * 2,
            .height = FONT_SIZE + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, ColorBrightness(theme.rectangle, -0.2f));

        DrawTextEx(w->font, w->audio.queue.history.items.items[idx]->title, (Vector2){bound.x + 5, y + 3}, FONT_SIZE,
                   0.0f, ColorBrightness(theme.focused_text, -0.2f));
    }

    for (size_t i = 0; i < w->audio.queue.upcoming.items.count; i++) {
        float y = center_y + item_height * (i + 1);

        Rectangle rect = {
            .x = bound.x,
            .y = y,
            .width = GetScreenWidth() - bound.x * 2,
            .height = FONT_SIZE + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, ColorBrightness(theme.rectangle, -0.2f));

        DrawTextEx(w->font, w->audio.queue.upcoming.items.items[i]->title, (Vector2){bound.x + 5, y + 3}, FONT_SIZE,
                   0.0f, ColorBrightness(theme.focused_text, -0.2f));
    }
}

static void draw_dft(const Wisp* w, Rectangle bound) {
    const Theme t = wisp_derive_theme(w);


    for (int i = 0; i < BARS; i++) {
        float t0 = (float)i / BARS;
        float t1 = (float)(i + 1) / BARS;

        float min_k = 1.0f;
        float max_k = NYQUIST_LIMIT;

        float log_min = logf(min_k);
        float log_max = logf(max_k);

        int k0 = expf(log_min + (log_max - log_min) * t0);
        int k1 = expf(log_min + (log_max - log_min) * t1);

        if (k1 <= k0) k1 = k0 + 1;

        float sum = 0.0f;
        int count = 0;
        for (int k = k0; k < k1; k++) {
            sum += w->magnitudes[k];
            count++;
        }

        float mag = (count > 0) ? sum / count : 0.0f;
        mag = logf(1.0f + mag);

        float h = mag * bound.height / 3.0f;

        float fx0 = bound.x + ((float)i / BARS) * bound.width;
        float fx1 = bound.x + ((float)(i + 1) / BARS) * bound.width;

        int x0 = (int)fx0;
        int x1 = (int)fx1;

        if (x1 <= x0) x1 = x0 + 1;

        int width = x1 - x0;

        DrawRectangleGradientV(
            x0,
            (int)(bound.y + bound.height - h),
            width,
            (int)h,
            ColorAlpha(t.rectangle, 0),
            t.unfocused_text
        );
    }
}

Theme wisp_derive_theme(const Wisp* w) {
    Color base = wisp_get_lerped_base_color(w);
    float lum = color_luminance(base);

    Color styled = ColorLerp((lum > 0.5f) ? BLACK : WHITE, base, 0.15f);

    Color rect_color = (lum > 0.5f) ? ColorLerp(base, BLACK, 0.5) : ColorLerp(base, WHITE, 0.5);
    Color shadow_color = (lum > 0.5) ? BLACK : WHITE;
    shadow_color = ColorAlpha(shadow_color, 0.8);
    return (Theme){.focused_text = styled,
                   .unfocused_text = ColorBrightness(styled, 0.8),
                   .rectangle = rect_color,
                   .shadow = shadow_color};
}


Wisp wisp_init(int argc, char** argv) {
    char* prog = argv[0]; (void)prog;
    char* home = getenv("HOME");
    char default_path[512] = {0};
    snprintf(default_path, 512, "%s/Music/", home);
    char* path = default_path;
    if (argc > 1) path = argv[1];
    {
        DIR* d = opendir(path);
        if (d == NULL) {
            printf("Failed to open root music library directory (%s): %s\n", path, strerror(errno));
            exit(1);
        }
        closedir(d);
    }
    Library lib = prepare_library(path);

    SetWindowState(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "wispy");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    InitAudioDevice();
    SetTargetFPS(180);
    AttachAudioMixedProcessor(fill_dft_buffer_callback);

    Font font = LoadFontEx("res/Iosevka.ttf", FONT_SIZE, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_ANISOTROPIC_16X);

    Image* imgs = malloc(lib.albums.count * sizeof(Image));
    Texture2D* tex = malloc(lib.albums.count * sizeof(Texture2D));
    Color* tints = malloc(lib.albums.count * sizeof(Color));

    for (size_t i = 0; i < lib.albums.count; i++) {
        float r = 0, g = 0, b = 0;
        Track* t = lib.albums.items[i].tracks.items[0];
        imgs[i] = (Image){
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
            .width = t->cover_w,
            .height = t->cover_h,
            .data = t->cover,
            .mipmaps = 1,
        };
        int sample_count = 10;
        int count = 0;
        for (int y = 0; y < t->cover_h; y += sample_count) {
            for (int x = 0; x < t->cover_w; x += sample_count) {
                size_t i = (y * t->cover_w + x) * 3;
                r += (float)t->cover[i] / 255;
                g += (float)t->cover[i + 1] / 255;
                b += (float)t->cover[i + 2] / 255;
                count++;
            }
        }
        r /= (float)count;
        g /= (float)count;
        b /= (float)count;
        r *= 255;
        g *= 255;
        b *= 255;
        tints[i].r = r;
        tints[i].g = g;
        tints[i].b = b;
        tints[i].a = 1;
        tex[i] = LoadTextureFromImage(imgs[i]);
    }

    return (Wisp){.font = font, .library = lib, .covers = tex, .album_average_colors = tints, .main_pane = MP_ALBUM, .pane = PANE_MAIN};
}

