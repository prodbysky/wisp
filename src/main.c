#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "audio.h"
#include "library.h"

#define DFT_SIZE 512
static float dft_shared_buf[DFT_SIZE];
static size_t dft_shared_buf_head = 0;
static volatile bool dft_shared_buf_ready = false;

void audio_callback(void* samples, uint32_t n_samples) {
    float* s = samples;

    for (int i = 0; i < n_samples; i++) {
        float l = s[i*2];
        float r = s[i*2+1];
        dft_shared_buf[dft_shared_buf_head++] = fabsf(l) > fabsf(r) ? l : r;
        if (dft_shared_buf_head >= DFT_SIZE) {
            dft_shared_buf_ready = true;
            dft_shared_buf_head = 0;
            return;
        }
    }
}

typedef enum {
    PANE_ALBUMS,
    PANE_QUEUE,
    PANE_VISUAL,
    PANE_COUNT,
} Pane;

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
void wisp_update(Wisp* w);
void wisp_draw(const Wisp* w);

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
        wisp_update(&ws);
        wisp_draw(&ws);
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

static void draw_album_list(const Wisp* w, Rectangle bound);
static void draw_tracklist(const Wisp* w, Rectangle bound);
static void draw_queue(const Wisp* w, Rectangle bound);
static void draw_dft(const Wisp* w, Rectangle bound);

void wisp_draw(const Wisp* w) {
    const float window_w = GetScreenWidth();
    const float window_h = GetScreenHeight();
    BeginDrawing();
    float t = (GetTime() - w->last_switch) / 0.2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    ClearBackground(ColorLerp(w->last_color, ColorBrightness(w->album_average_colors[w->selected_album], 0.0), t));

    const Theme theme = wisp_derive_theme(w);

    switch (w->pane) {
        case PANE_ALBUMS: {
            const Rectangle side_bar = {
                .width = window_w,
                .height = ALBUM_COVER_SIDE_LENGTH,
            };
            draw_album_list(w, side_bar);

            const Rectangle player_state = {
                .x = 8,
                .y = window_h - 136,
                .width = window_w - 16,
                .height = 128
            };

            const Rectangle track_list = {.x = 8,
                                          .y = side_bar.height + 8,
                                          .width = window_w,
                                          .height = window_h - side_bar.height - player_state.height - 16};
            draw_tracklist(w, track_list);
            DrawRectangleRounded(player_state, 0.1, 16, ColorAlpha(theme.rectangle, 1.0));
            break;
        }
        case PANE_QUEUE: {
            const Rectangle track_list = {.x = 8, .y = 8, .width = window_w, .height = window_h};
            draw_queue(w, track_list);
            break;
        }
        case PANE_VISUAL: {
            const Rectangle dft_rect = {
                .x = 0,
                .y = -window_h,
                .width = window_w,
                .height = window_h * 2
            };
            if (w->audio.current_track == NULL) break;
            const char* title = w->audio.current_track->title;
            const char* album = w->audio.current_track->album;
            const char* artist = w->audio.current_track->artist;
            draw_dft(w, dft_rect);
            const Theme t = wisp_derive_theme(w);
            DrawTextEx(w->font, title, (Vector2){.x = 8, .y = 8}, 24, 0.0, t.focused_text);
            DrawTextEx(w->font, album, (Vector2){.x = 8, .y = 40}, 24, 0.0, t.focused_text);
            DrawTextEx(w->font, artist, (Vector2){.x = 8, .y = 72}, 24, 0.0, t.focused_text);

            DrawTextEx(w->font, title, (Vector2){.x = 8 + 2, .y = 8 + 2}, 24, 0.0, ColorAlpha(t.shadow, -.1));
            DrawTextEx(w->font, album, (Vector2){.x = 8 + 2, .y = 40 + 2}, 24, 0.0, ColorAlpha(t.shadow, -.1));
            DrawTextEx(w->font, artist, (Vector2){.x = 8 + 2, .y = 72 + 2}, 24, 0.0, ColorAlpha(t.shadow, -.1));
            break;
}
        case PANE_COUNT: assert(false);
    }
    EndDrawing();
}

static void draw_queue(const Wisp* w, Rectangle bound) {
    const float font_size = 24.0f;
    const float child_spacing = 8.0f;
    const float item_height = font_size + child_spacing;

    const Theme theme = wisp_derive_theme(w);

    float center_y = bound.y + bound.height * 0.5f;

    if (w->audio.current_track) {
        Rectangle rect = {
            .x = bound.x,
            .y = center_y,
            .width = GetScreenWidth() - bound.x * 2,
            .height = font_size + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, theme.rectangle);

        DrawTextEx(w->font, w->audio.current_track->title, (Vector2){bound.x + 5, center_y + 3}, font_size, 0.0f,
                   theme.focused_text);
    }

    for (size_t i = 0; i < w->audio.queue.history.items.count; i++) {
        size_t idx = w->audio.queue.history.items.count - 1 - i;

        float y = center_y - item_height * (i + 1);

        Rectangle rect = {
            .x = bound.x,
            .y = y,
            .width = GetScreenWidth() - bound.x * 2,
            .height = font_size + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, ColorBrightness(theme.rectangle, -0.2f));

        DrawTextEx(w->font, w->audio.queue.history.items.items[idx]->title, (Vector2){bound.x + 5, y + 3}, font_size,
                   0.0f, ColorBrightness(theme.focused_text, -0.2f));
    }

    for (size_t i = 0; i < w->audio.queue.upcoming.items.count; i++) {
        float y = center_y + item_height * (i + 1);

        Rectangle rect = {
            .x = bound.x,
            .y = y,
            .width = GetScreenWidth() - bound.x * 2,
            .height = font_size + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, ColorBrightness(theme.rectangle, -0.2f));

        DrawTextEx(w->font, w->audio.queue.upcoming.items.items[i]->title, (Vector2){bound.x + 5, y + 3}, font_size,
                   0.0f, ColorBrightness(theme.focused_text, -0.2f));
    }
}

static void draw_dft(const Wisp* w, Rectangle bound) {
    const Theme t = wisp_derive_theme(w);

    #define BARS 128

    for (int i = 0; i < BARS; i++) {
        float t0 = (float)i / BARS;
        float t1 = (float)(i + 1) / BARS;

        float min_k = 1.0f;
        float max_k = DFT_SIZE / 2.0f;

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
            t.unfocused_text,
            t.rectangle
        );
    }
}
Theme wisp_derive_theme(const Wisp* w) {
    Color base = w->album_average_colors[w->selected_album];
    float lum = color_luminance(base);

    Color styled = ColorLerp((lum > 0.5f) ? BLACK : WHITE, base, 0.15f);


    Color rect_color = (lum > 0.5f) ? ColorLerp(base, BLACK, 0.5) : ColorLerp(base, WHITE, 0.5);
    Color shadow_color = (lum > 0.5) ? BLACK : WHITE;
    shadow_color = ColorAlpha(shadow_color, 0.8);
    return (Theme){.focused_text = styled,
                   .unfocused_text = ColorAlpha(styled, -.2),
                   .rectangle = rect_color,
                   .shadow = shadow_color};
}

static void draw_tracklist(const Wisp* w, Rectangle bound) {
    BeginScissorMode(bound.x, bound.y, bound.width, bound.height);

    const float font_size = 24;
    const float child_spacing = 8;

    Album* selected = &w->library.albums.items[w->selected_album];

    const Theme theme = wisp_derive_theme(w);

    for (size_t i = 0; i < selected->tracks.count; i++) {
        float y = (font_size + child_spacing) * i + bound.y - w->track_scroll;

        if (y < bound.y - font_size || y > bound.y + bound.height) continue;

        Rectangle rect = {
            .x = bound.x,
            .y = y,
            .width = bound.width - bound.x * 2,
            .height = font_size + 4,
        };

        DrawRectangleRounded(rect, 0.25f, 17, theme.rectangle);

        const char* title = selected->tracks.items[i]->title;

        Color text_color = (i == w->selected_track) ? theme.focused_text : theme.unfocused_text;

        DrawTextEx(w->font, title, (Vector2){rect.x + 5, rect.y + 3}, font_size, 0.0f, theme.shadow);

        DrawTextEx(w->font, title, (Vector2){rect.x + 4, rect.y + 2}, font_size, 0.0f, text_color);
    }

    EndScissorMode();
}
static void draw_album_list(const Wisp* w, Rectangle bound) {
    BeginScissorMode(bound.x, bound.y, bound.width, bound.height);
    for (size_t i = 0; i < w->library.albums.count; i++) {
        if ((i * ALBUM_COVER_SIDE_LENGTH + w->actual_album_offset + bound.x) > bound.width) continue;
        const Rectangle cover_rect_dest = {
            .width = ALBUM_COVER_SIDE_LENGTH,
            .height = ALBUM_COVER_SIDE_LENGTH,
            .x = i * ALBUM_COVER_SIDE_LENGTH + w->actual_album_offset + bound.x,
        };
        const Rectangle cover_rect_src = {
            .width = w->covers[i].width,
            .height = w->covers[i].height,
        };
        DrawTexturePro(w->covers[i], cover_rect_src, cover_rect_dest, (Vector2){}, 0.0, WHITE);
        if (i != w->selected_album) { DrawRectangleRec(cover_rect_dest, GetColor(0x000000aa)); }
    }
    EndScissorMode();
}

typedef struct {
    float real;
    float imag;
} Complex;

void compute_dft(float* in, Complex* out, int N) {
    for (int k = 0; k < N; k++) {
        float real = 0.0f;
        float imag = 0.0f;

        for (int n = 0; n < N; n++) {
            float angle = 2.0f * PI * k * n / N;
            real += in[n] * cosf(angle);
            imag -= in[n] * sinf(angle);
        }

        out[k].real = real;
        out[k].imag = imag;
    }
}

void wisp_update(Wisp* wisp) {
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);


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

    // screen controls
    {
        if (IsKeyPressed(KEY_TAB)) {
            wisp->pane++;
            wisp->pane %= PANE_COUNT;
        }
    }

    // audio related shit
    {
        if (IsKeyPressed(KEY_SPACE)) audio_toggle_playing_state(&wisp->audio);

        if (IsKeyPressed(KEY_PERIOD) && shift) audio_skip_track_forward(&wisp->audio);

        if (IsKeyPressed(KEY_RIGHT)) audio_try_seeking_by(&wisp->audio, 5.0);
        if (IsKeyPressed(KEY_LEFT)) audio_try_seeking_by(&wisp->audio, -5.0);

        if (IsKeyPressed(KEY_COMMA) && shift) { audio_skip_track_backward(&wisp->audio); }
        if (IsKeyPressed(KEY_S) && ctrl) { wisp->audio.shuffle = !wisp->audio.shuffle; }

        // cycle loop mode
        if (IsKeyPressed(KEY_R) && ctrl) {
            wisp->audio.loop_mode++;
            if (wisp->audio.loop_mode > LOOP_ALL) wisp->audio.loop_mode = LOOP_NONE;
        }
        audio_update(&wisp->audio);
    }
    // album selection top or side bar still dont know?
    {
        if (IsKeyPressed(KEY_L) && wisp->selected_album < wisp->library.albums.count - 1) {
            wisp->wanted_album_offset -= ALBUM_COVER_SIDE_LENGTH;
            wisp->last_color = wisp->album_average_colors[wisp->selected_album];
            wisp->last_switch = GetTime();
            wisp->selected_album++;
            wisp->selected_track = 0;
        }

        if (IsKeyPressed(KEY_H) && wisp->selected_album != 0) {
            wisp->wanted_album_offset += ALBUM_COVER_SIDE_LENGTH;
            wisp->last_color = wisp->album_average_colors[wisp->selected_album];
            wisp->last_switch = GetTime();
            wisp->selected_album--;
            wisp->selected_track = 0;
        }
    }

    // track selection
    {
        Album* selected_album = &wisp->library.albums.items[wisp->selected_album];
        size_t album_track_count = selected_album->tracks.count;
        if (IsKeyPressed(KEY_J) && wisp->selected_track < album_track_count - 1) wisp->selected_track++;
        if (IsKeyPressed(KEY_K) && wisp->selected_track != 0) wisp->selected_track--;

        if (IsKeyPressed(KEY_ENTER))
            audio_start_playback(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);

        // push currently selected song into the queue
        if (IsKeyPressed(KEY_Q) && !ctrl && !shift)
            audio_enqueue_single(&wisp->audio, selected_album->tracks.items[wisp->selected_track]);

        // push songs from the current track to the end from the currently
        // selected album
        if (shift && IsKeyPressed(KEY_Q)) {
            for (size_t i = wisp->selected_track; i < selected_album->tracks.count; i++) {
                audio_enqueue_single(&wisp->audio, selected_album->tracks.items[i]);
            }
        }
    }
    // track offset calculation :))
    {
        const float font_size = 24;
        const float spacing = 8;
        const float item_height = font_size + spacing;
        const float track_area_height = GetScreenHeight() - 160 - ALBUM_COVER_SIDE_LENGTH - 8;
        const float desired_y_from_top = track_area_height - item_height * 5;
        const float selected_y = wisp->selected_track * item_height;

        wisp->wanted_track_scroll = selected_y - desired_y_from_top;

        Album* album = &wisp->library.albums.items[wisp->selected_album];
        float max_scroll = fmaxf(0.0f, album->tracks.count * item_height - track_area_height);

        if (wisp->wanted_track_scroll < 0.0f) wisp->wanted_track_scroll = 0.0f;
        if (wisp->wanted_track_scroll > max_scroll) wisp->wanted_track_scroll = max_scroll;

        wisp->track_scroll += (wisp->wanted_track_scroll - wisp->track_scroll) * 0.15f;
    }

    const float min_offset = fminf(0.0f, GetScreenWidth() - wisp->library.albums.count * ALBUM_COVER_SIDE_LENGTH);

    if (wisp->wanted_album_offset < min_offset) wisp->wanted_album_offset = min_offset;
    if (wisp->wanted_album_offset > 0.0) wisp->wanted_album_offset = 0.0;

    wisp->actual_album_offset += (wisp->wanted_album_offset - wisp->actual_album_offset) * 0.1f;
}



Wisp wisp_init(int argc, char** argv) {
    char* prog = argv[0];
    char* home = getenv("HOME");
    char default_path[512] = {0};
    snprintf(default_path, 512, "%s/Music/", home);
    char* path = default_path;
    if (argc > 1) path = argv[1];
    Library lib = prepare_library(path);

    SetWindowState(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "wispy");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    InitAudioDevice();
    SetTargetFPS(180);
    AttachAudioMixedProcessor(audio_callback);

    Font font = LoadFontEx("res/Iosevka.ttf", 24, NULL, 0);
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

    return (Wisp){.font = font, .library = lib, .covers = tex, .album_average_colors = tints};
}

