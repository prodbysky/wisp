#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <threads.h>

#include "../extern/raylib/src/raylib.h"
#include "audio.h"
#include "compile_time_config.h"
#include "draw_utils.h"
#include "fft.h"
#include "kmeans.h"
#include "library.h"
#include "playlist.h"
#include "playlist_overlay.h"
#include "playlist_pane.h"
#include "runtime_config.h"

#define N_CLUSTERS 12

typedef enum {
    PANE_MAIN,
    PANE_QUEUE,
    PANE_VISUAL,
    PANE_PLAYLIST,
    PANE_COUNT,
} Pane;

typedef enum {
    MP_ALBUM,
    MP_TRACK,
} MainPane;

typedef struct {
    atomic_bool done;
    char* root_path;
    char* playlist_path;
    Image* images;
    Color** colors;
    Library lib;
    Playlists playlists;
} StartupWorkCtx;

// https://stackoverflow.com/questions/596216/formula-to-determine-perceived-brightness-of-rgb-color
static float color_lum(Color c) { return sqrtf((0.299 * (c.r * c.r) + 0.587 * (c.g * c.g) + 0.114 * (c.b * c.b))); }

static int cmp_color_by_lum(const void* a, const void* b) {
    if (a == NULL || b == NULL) return 0;
    const Color* a_color = a;
    const Color* b_color = b;
    float diff = color_lum(*a_color) - color_lum(*b_color);
    return (diff > 0) - (diff < 0);
}

static int startup_worker(void* arg) {
    StartupWorkCtx* ctx = arg;

    Library lib = prepare_library(ctx->root_path);
    ctx->images = malloc(lib.albums.count * sizeof(Image));

    for (size_t i = 0; i < lib.albums.count; i++) {
        Track* t = lib.albums.items[i].tracks.items[0];
        Image img = {
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
            .width = t->cover_w,
            .height = t->cover_h,
            .data = t->cover,
            .mipmaps = 1,
        };
        Image new = {
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
            .width = 128,
            .height = 128,
            .data = malloc(128 * 128 * 3),
            .mipmaps = 1,
        };
        ImageDraw(&new, img, (Rectangle){.width = t->cover_w, .height = t->cover_h}, (Rectangle){.width = 128, .height = 128}, WHITE);
        ctx->images[i] = new;
    }
    ctx->lib = lib;

    ctx->colors = malloc(sizeof(Color*) * lib.albums.count);
    for (uint32_t i = 0; i < lib.albums.count; i++) {
        Image im = ctx->images[i];
        ctx->colors[i] = kmeans(im, N_CLUSTERS, 4);
        qsort(ctx->colors[i], N_CLUSTERS, sizeof(Color), cmp_color_by_lum);
    }

    char* pl_dir = playlist_dir_path(ctx->playlist_path);
    playlist_ensure_dir(pl_dir);

    playlists_load(pl_dir, &lib, &ctx->playlists);
    atomic_exchange_explicit(&ctx->done, true, memory_order_relaxed);
    return 0;
}

typedef struct {
    Config cli_config;
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

    float magnitudes[FFT_SIZE / 2];
    float fft_max;

    Playlists playlists;
    char* playlist_dir;

    Overlay overlay;
    PlaylistPane playlist_pane;
    Color** fft_colors;
    StartupWorkCtx* ctx;
    bool startup_done;
} Wisp;

Wisp wisp_init(int argc, char** argv);
void wisp_tick(Wisp* wisp);

static const Album* wisp_get_selected_album(const Wisp* wisp);

static Color color_lerp(Color a, Color b, float t) {
    return (Color){
        .r = (unsigned char)(a.r + (b.r - a.r) * t),
        .g = (unsigned char)(a.g + (b.g - a.g) * t),
        .b = (unsigned char)(a.b + (b.b - a.b) * t),
        .a = (unsigned char)(a.a + (b.a - a.a) * t),
    };
}


static void draw_queue(const Wisp* w, Rectangle bound);
static void draw_fft(Wisp* w, Rectangle bound);
static void wisp_draw_visual_pane(Wisp* wisp);

static void wisp_next_pane(Wisp* wisp);
static void wisp_next_loop_mode(Wisp* wisp);
static void wisp_play_selected_track(Wisp* wisp);
static void wisp_queue_album_from_the_selected_track(Wisp* wisp);
static void prepare_fft_vis(Wisp* wisp);

int main(int argc, char** argv) {
    if (argc > 1 && strcmp("--help", argv[1]) == 0) {
        printf("Usage:\n");
        printf("%s [--help]: print this help message\n", argv[0]);
        printf("%s <PATH>: use this path as the base of the music on your system\n", argv[0]);
        return 1;
    }
    Wisp ws = wisp_init(argc, argv);

    while (!WindowShouldClose()) { wisp_tick(&ws); }

    playlists_save(ws.playlist_dir, &ws.playlists);

    audio_stop_playback(&ws.audio);
    UnloadFont(ws.font);
    unload_library(&ws.library);
    playlists_free(&ws.playlists);
    free(ws.playlist_dir);
    free(ws.overlay.filtered_indices);
    free(ws.covers);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

void wisp_tick(Wisp* wisp) {
    const float WW = (float)GetScreenWidth();
    const float WH = (float)GetScreenHeight();
    if (wisp->startup_done) goto over;
    if (atomic_load_explicit(&wisp->ctx->done, memory_order_relaxed) == true) {
        wisp->library = wisp->ctx->lib;
        wisp->covers = malloc(wisp->library.albums.count * sizeof(Texture2D));
        for (size_t i = 0; i < wisp->library.albums.count; i++) {
            wisp->covers[i] = LoadTextureFromImage(wisp->ctx->images[i]);
        }
        wisp->playlists = wisp->ctx->playlists;
        wisp->fft_colors = wisp->ctx->colors;
        for (uint32_t i = 0; i < wisp->library.albums.count; i++) {
            free(wisp->library.albums.items[i].tracks.items[0]->cover);
            free(wisp->ctx->images[i].data);
        }
        free(wisp->ctx->images);
        free(wisp->ctx);
        wisp->startup_done = true;
    } else {
        BeginDrawing();
        ClearBackground(BACKGROUND_COLOR);
        Vector2 size = MeasureTextEx(wisp->font, "loading", FONT_SIZE, 0.0);
        DrawTextEx(wisp->font, "loading", (Vector2){.x = WW / 2 - size.x / 2, .y = WH / 2 - size.y / 2}, FONT_SIZE, 0.0, FOCUSED_TEXT_COLOR);
        EndDrawing();
        return;
    }
over:

    if (wisp->overlay.mode != OVERLAY_NONE) {
        overlay_update(&wisp->overlay, &wisp->playlists, wisp->playlist_dir, &wisp->library.albums);
    } else {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        if (IsKeyPressed(KEY_TAB)) wisp_next_pane(wisp);
        if (IsKeyPressed(KEY_SPACE)) audio_toggle_playing_state(&wisp->audio);

        if (IsKeyPressed(KEY_PERIOD) && shift && !ctrl) audio_skip_track_forward(&wisp->audio);
        if (IsKeyPressed(KEY_COMMA) && shift && !ctrl) audio_skip_track_backward(&wisp->audio);
        if (IsKeyPressed(KEY_PERIOD) && !shift && !ctrl) audio_try_seeking_by(&wisp->audio, 5);
        if (IsKeyPressed(KEY_COMMA) && !shift && !ctrl) audio_try_seeking_by(&wisp->audio, -5);

        if (IsKeyPressed(KEY_S) && ctrl) wisp->audio.shuffle = !wisp->audio.shuffle;
        if (IsKeyPressed(KEY_R) && ctrl) wisp_next_loop_mode(wisp);

        if (IsKeyPressed(KEY_COMMA) && ctrl) audio_change_master_volume_by(&wisp->audio, -0.05);
        if (IsKeyPressed(KEY_PERIOD) && ctrl) audio_change_master_volume_by(&wisp->audio, 0.05);

        if (wisp->pane == PANE_MAIN) {
            if (wisp->main_pane == MP_ALBUM) {
                if (IsKeyPressed(KEY_J)) {
                    if (wisp->selected_album < wisp->library.albums.count - 1) { wisp->selected_album++; }
                }
                if (IsKeyPressed(KEY_K)) {
                    if (wisp->selected_album > 0) { wisp->selected_album--; }
                }
                if (IsKeyPressed(KEY_L)) {
                    wisp->main_pane = MP_TRACK;
                    wisp->selected_track = 0;
                }

                if (IsKeyPressed(KEY_A) && shift)
                    overlay_open(&wisp->overlay, true, wisp->selected_album, wisp->selected_track, &wisp->playlists);
                else if (IsKeyPressed(KEY_A))
                    overlay_open(&wisp->overlay, false, wisp->selected_album, wisp->selected_track, &wisp->playlists);
            }

            if (wisp->main_pane == MP_TRACK) {
                const float entry_height = TRACK_ENTRY_H;
                const float top_bound = wisp->track_scroll + entry_height;
                const float bottom_bound = wisp->track_scroll + WH - entry_height;
                const float cur_off = (float)wisp->selected_track * entry_height;
                const Album* album = wisp_get_selected_album(wisp);
                if (album) {
                    float max_scroll = (float)album->tracks.count * entry_height - WH;
                    if (max_scroll < 0) max_scroll = 0;
                    if (wisp->wanted_track_scroll < 0) wisp->wanted_track_scroll = 0;
                    if (wisp->wanted_track_scroll > max_scroll) wisp->wanted_track_scroll = max_scroll;
                    if (cur_off < top_bound) wisp->wanted_track_scroll = cur_off - entry_height;
                    if (cur_off > bottom_bound) wisp->wanted_track_scroll = cur_off - (WH - entry_height);

                    if (IsKeyPressed(KEY_J)) {
                        if (wisp->selected_track < album->tracks.count - 1) wisp->selected_track++;
                    }
                    if (IsKeyPressed(KEY_K)) {
                        if (wisp->selected_track > 0) wisp->selected_track--;
                    }
                    if (IsKeyPressed(KEY_H)) wisp->main_pane = MP_ALBUM;

                    if (IsKeyPressed(KEY_A) && shift)
                        overlay_open(&wisp->overlay, true, wisp->selected_album, wisp->selected_track, &wisp->playlists);
                    else if (IsKeyPressed(KEY_A))
                        overlay_open(&wisp->overlay, false, wisp->selected_album, wisp->selected_track, &wisp->playlists);
                }
            }

            if (IsKeyPressed(KEY_ENTER)) wisp_play_selected_track(wisp);

            if (shift && IsKeyPressed(KEY_Q))
                wisp_queue_album_from_the_selected_track(wisp);
            else if (IsKeyPressed(KEY_Q)) {
                Track* t = wisp->library.albums.items[wisp->selected_album].tracks.items[wisp->selected_track];
                audio_enqueue_single(&wisp->audio, t);
            }

            {
                const float top = wisp->actual_album_offset + ALBUM_ENTRY_H;
                const float bottom = wisp->actual_album_offset + WH - ALBUM_ENTRY_H;
                const float cur = PAD + (float)wisp->selected_album * ALBUM_ENTRY_H;
                float max_off = (float)wisp->library.albums.count * ALBUM_ENTRY_H - WH;
                if (max_off < 0) max_off = 0;
                if (wisp->wanted_album_offset < 0) wisp->wanted_album_offset = 0;
                if (wisp->wanted_album_offset > max_off) wisp->wanted_album_offset = max_off;
                if (cur < top) wisp->wanted_album_offset = cur - ALBUM_ENTRY_H;
                if (cur > bottom) wisp->wanted_album_offset = cur - (WH - ALBUM_ENTRY_H);
            }
        }

        if (wisp->pane == PANE_PLAYLIST) {
            playlist_pane_update(&wisp->playlist_pane, (Rectangle){.width = WW, .height = WH}, &wisp->playlists,
                                 &wisp->audio);
        }
        wisp->actual_album_offset += (wisp->wanted_album_offset - wisp->actual_album_offset) * SCROLL_SMOOTH;
        wisp->track_scroll += (wisp->wanted_track_scroll - wisp->track_scroll) * SCROLL_SMOOTH;
    }

    prepare_fft_vis(wisp);
    audio_update(&wisp->audio);
    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);

    switch (wisp->pane) {
        case PANE_MAIN: {
            {
                BeginScissorMode(0, 0, (int)(WW / 3 - PAD), (int)WH);
                Vector2 cursor = {PAD, PAD - wisp->actual_album_offset - (FONT_SIZE + 64)};
                for (size_t ai = 0; ai < wisp->library.albums.count; ai++) {
                    const bool focused = (ai == wisp->selected_album);
                    const Color text_color = focused ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;
                    const Rectangle bg_rect = {cursor.x - 2, cursor.y, WW / 3 - PAD * 2, FONT_SIZE + 4 + 64};
                    const Color bg_color = focused ? UNFOCUSED_PANEL_COLOR : FOCUSED_PANEL_COLOR;
                    const Rectangle cover_src = {0, 0, (float)wisp->covers[ai].width, (float)wisp->covers[ai].height};
                    const Rectangle cover_dst = {PAD + PAD / 2, cursor.y + 14, 64, 64};
                    draw_round_rect(bg_rect, bg_color, RECTANGLE_ROUNDNESS);
                    DrawTexturePro(wisp->covers[ai], cover_src, cover_dst, (Vector2){0}, 0, WHITE);
                    draw_text_with_shadow(wisp->library.albums.items[ai].name, wisp->font, text_color, SHADOW_COLOR,
                                          (Vector2){.x = cursor.x + 74, .y = cursor.y + 34});
                    cursor.y += ALBUM_ENTRY_H;
                }
                EndScissorMode();
            }
            {
                Vector2 cursor = {PAD + WW / 3, PAD - wisp->track_scroll};
                BeginScissorMode((int)(cursor.x - 3 * PAD), 0, (int)((WW * 2) / 3 + PAD), (int)WH);
                const Album* album = wisp_get_selected_album(wisp);
                for (size_t ti = 0; album && ti < album->tracks.count; ti++) {
                    const bool focused = (ti == wisp->selected_track);
                    const Color text_color = focused ? FOCUSED_TEXT_COLOR : UNFOCUSED_TEXT_COLOR;
                    const Rectangle bg_rect = {cursor.x - 2, cursor.y, 2 * (WW / 3) - PAD * 2, FONT_SIZE + 4};
                    draw_round_rect(bg_rect, FOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);

                    draw_text_with_shadow(album->tracks.items[ti]->title, wisp->font,
                                          text_color, SHADOW_COLOR, (Vector2){.x = cursor.x + 2, .y = cursor.y + 2});
                    cursor.y += TRACK_ENTRY_H;
                }
                EndScissorMode();
            }
            break;
        }
        case PANE_QUEUE: {
            draw_queue(wisp, (Rectangle){8, 8, WW, WH});
            break;
        }
        case PANE_VISUAL: {
            wisp_draw_visual_pane(wisp);
            break;
        }
        case PANE_PLAYLIST: {
            playlist_pane_draw(&wisp->playlist_pane, (Rectangle){.width = WW, .height = WH}, wisp->font,
                               &wisp->playlists);
            break;
        }
        case PANE_COUNT: assert(false);
    }

    if (wisp->overlay.mode != OVERLAY_NONE)
        overlay_draw(&wisp->overlay, (Rectangle){.width = GetScreenWidth(), .height = GetScreenHeight()}, wisp->font,
                     &wisp->playlists);

    EndDrawing();
}

Wisp wisp_init(int argc, char** argv) {
    Config cfg = {0};
    if (!config_parse_args(&argc, &argv, &cfg)) {
        printf("ERROR: Failed to parse cli args\n");
        help_and_exit(&cfg);
        exit(1);
    }

    char* home_path = getenv("HOME");

    Config pre_file = cfg;
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/.config/wisp.conf", home_path);
    if (!config_parse_file(config_path, &cfg)) {
        printf("WARN: Failed to parse config file, using defaults\n");
        cfg = pre_file;
    }

    if (cfg.custom_root_path == NULL) {
        char default_path[512] = {0};
        snprintf(default_path, sizeof(default_path), "%s/Music/", home_path);
        cfg.custom_root_path = strdup(default_path);
    }

    {
        DIR* d = opendir(cfg.custom_root_path);
        if (d == NULL) {
            printf("Failed to open root music library directory (%s): %s\n", cfg.custom_root_path, strerror(errno));
            exit(1);
        }
        closedir(d);
    }

    SetWindowState(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "wispy");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetExitKey(0);
    InitAudioDevice();
    SetTargetFPS(180);
    AttachAudioMixedProcessor(fill_fft_buffer_callback);

    Font font = LoadFontEx("res/Vollkorn-Medium.ttf", FONT_SIZE, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_ANISOTROPIC_16X);

    StartupWorkCtx* startup = calloc(1, sizeof(StartupWorkCtx));
    startup->done = false;
    startup->root_path = cfg.custom_root_path;
    startup->playlist_path = cfg.custom_playlist_dir;

    thrd_t th;
    thrd_create(&th, startup_worker, startup);

    return (Wisp){.font = font,
                  .main_pane = MP_ALBUM,
                  .pane = PANE_MAIN,
                  .cli_config = cfg,
                  .playlist_dir = cfg.custom_playlist_dir,
                  .audio = audio_init(),
                  .ctx = startup,
                  .fft_max = 1.0
    };
}

static void wisp_draw_visual_pane(Wisp* wisp) {
    const Rectangle fft_rect = {0, -(float)GetScreenHeight(), (float)GetScreenWidth(), (float)GetScreenHeight() * 2};
    if (wisp->audio.current_track == NULL) return;
    const char* title = wisp->audio.current_track->title;
    const char* album = wisp->audio.current_track->album;
    const char* artist = wisp->audio.current_track->artist;
    draw_fft(wisp, fft_rect);

    draw_text_with_shadow(title, wisp->font, FOCUSED_TEXT_COLOR, SHADOW_COLOR, (Vector2){.x = 8, .y = 8});
    draw_text_with_shadow(album, wisp->font, FOCUSED_TEXT_COLOR, SHADOW_COLOR, (Vector2){.x = 8, .y = 40});

    draw_text_with_shadow(artist, wisp->font, FOCUSED_TEXT_COLOR, SHADOW_COLOR, (Vector2){.x = 8, .y = 72});
}

static void wisp_next_pane(Wisp* wisp) { wisp->pane = (wisp->pane + 1) % PANE_COUNT; }

static const Album* wisp_get_selected_album(const Wisp* wisp) {
    if (wisp->selected_album >= wisp->library.albums.count) return NULL;
    return &wisp->library.albums.items[wisp->selected_album];
}

static void wisp_next_loop_mode(Wisp* wisp) {
    wisp->audio.loop_mode++;
    if (wisp->audio.loop_mode > LOOP_ALL) wisp->audio.loop_mode = LOOP_NONE;
}

static void wisp_play_selected_track(Wisp* wisp) {
    audio_start_playback(&wisp->audio, wisp_get_selected_album(wisp)->tracks.items[wisp->selected_track]);
}

static void wisp_queue_album_from_the_selected_track(Wisp* wisp) {
    const Album* alb = wisp_get_selected_album(wisp);
    for (size_t i = wisp->selected_track; i < alb->tracks.count; i++)
        audio_enqueue_single(&wisp->audio, alb->tracks.items[i]);
}

static void prepare_fft_vis(Wisp* wisp) {
    if (get_fft_ready() && wisp->pane == PANE_VISUAL) {
        fft_consumed();
        static float _Complex out[FFT_SIZE];
        float* shared_buf = get_fft_shared_buf();
        for (int i = 0; i < FFT_SIZE; i++) {
            float w = 0.5f * (1.0f - cosf(2.0f * PI * i / (FFT_SIZE - 1)));
            shared_buf[i] *= w;
        }
        compute_fft(out);
        for (int k = 0; k < FFT_SIZE / 2; k++) {
            float mag = sqrtf(creal(out[k]) * creal(out[k]) + cimag(out[k]) * cimag(out[k]));
            float db = 20.0f * log10f(mag + 1e-6f);
            float norm = (db - (-20.0f)) / (20.0f - (-20.0f));
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;
            wisp->magnitudes[k] = wisp->magnitudes[k] * 0.8f + norm * 0.2f;
        }
    }
}

static void draw_queue(const Wisp* w, Rectangle bound) {
    const float child_spacing = 8.0f;
    const float item_height = FONT_SIZE + child_spacing;
    const float center_y = bound.y + bound.height * 0.5f;

    if (w->audio.current_track) {
        Rectangle rect = {bound.x, center_y, (float)GetScreenWidth() - bound.x * 2, FONT_SIZE + 4};
        draw_round_rect(rect, FOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);

        draw_text_with_shadow(w->audio.current_track->title, w->font, FOCUSED_TEXT_COLOR, SHADOW_COLOR,
                              (Vector2){.x = bound.x + 5, .y = center_y + 3});
    }

    for (size_t i = 0; i < w->audio.queue.history.items.count; i++) {
        size_t idx = w->audio.queue.history.items.count - 1 - i;
        float y = center_y - item_height * (float)(i + 1);
        Rectangle rect = {bound.x, y, (float)GetScreenWidth() - bound.x * 2, FONT_SIZE + 4};
        draw_round_rect(rect, FOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);
        draw_text_with_shadow(w->audio.queue.history.items.items[idx]->title, w->font, UNFOCUSED_TEXT_COLOR,
                              SHADOW_COLOR, (Vector2){.x = bound.x + 5, .y = y + 3});
    }

    for (size_t i = 0; i < w->audio.queue.upcoming.items.count; i++) {
        float y = center_y + item_height * (float)(i + 1);
        Rectangle rect = {bound.x, y, (float)GetScreenWidth() - bound.x * 2, FONT_SIZE + 4};
        draw_round_rect(rect, FOCUSED_PANEL_COLOR, RECTANGLE_ROUNDNESS);

        draw_text_with_shadow(w->audio.queue.upcoming.items.items[i]->title, w->font, UNFOCUSED_TEXT_COLOR,
                              SHADOW_COLOR, (Vector2){.x = bound.x + 5, .y = y + 3});
    }
}

static void draw_fft(Wisp* w, Rectangle bound) {
    float max_h = 0.0f;

    float heights[BARS];
    for (int i = 0; i < BARS; i++) {
        float t0 = (float)i / BARS;
        float t1 = (float)(i + 1) / BARS;

        float log_min = logf(1.0f), log_max = logf(NYQUIST_LIMIT);
        int k0 = (int)expf(log_min + (log_max - log_min) * t0);
        int k1 = (int)expf(log_min + (log_max - log_min) * t1);
        if (k1 <= k0) k1 = k0 + 1;

        float sum = 0;
        int cnt = 0;
        for (int k = k0; k < k1; k++) {
            sum += w->magnitudes[k] * 1.25f;
            cnt++;
        }

        float mag = cnt > 0 ? sum / cnt : 0;
        mag = logf(1.0f + mag);

        float h = mag * bound.height / 3.0f;
        heights[i] = h;

        if (h > max_h) max_h = h;
    }
    float smoothed[BARS];

    for (int i = 0; i < BARS; i++) {
        float sum = heights[i] * 0.6f;

        if (i > 0)         sum += heights[i - 1] * 0.2f;
        if (i < BARS - 1)  sum += heights[i + 1] * 0.2f;

        smoothed[i] = sum;
    }
    memcpy(heights, smoothed, sizeof(float) * BARS);

    for (int i = 0; i < BARS; i++) {
        float h = heights[i];

        if (max_h > w->fft_max)
            w->fft_max = max_h;
        else
            w->fft_max *= 0.99f;
        float norm_h = (w->fft_max > 0.0f) ? (h / w->fft_max) : 0.0f;
        if (norm_h > 1.0f) norm_h = 1.0f;

        norm_h = powf(norm_h, 0.7f);

        float pos = norm_h * (N_CLUSTERS - 1);
        int idx0 = (int)floorf(pos);
        int idx1 = idx0 + 1;
        if (idx1 > N_CLUSTERS - 1) idx1 = N_CLUSTERS - 1;

        float t = pos - (float)idx0;

        Color c = color_lerp(
            w->fft_colors[w->selected_album][idx0],
            w->fft_colors[w->selected_album][idx1],
            t
        );

        Color prev_c = c;
        Color next_c = c;

        if (i > 0) {
            float prev_h = heights[i - 1];
            float prev_norm = (max_h > 0.0f) ? (prev_h / max_h) : 0.0f;
            float prev_pos = powf(prev_norm, 0.7f) * (N_CLUSTERS - 1);

            int p0 = (int)floorf(prev_pos);
            int p1 = p0 + 1;
            if (p1 > N_CLUSTERS - 1) p1 = N_CLUSTERS - 1;

            float pt = prev_pos - (float)p0;

            prev_c = color_lerp(
                w->fft_colors[w->selected_album][p0],
                w->fft_colors[w->selected_album][p1],
                pt
            );
        }

        if (i < BARS - 1) {
            float next_h = heights[i + 1];
            float next_norm = (max_h > 0.0f) ? (next_h / max_h) : 0.0f;
            float next_pos = powf(next_norm, 0.7f) * (N_CLUSTERS - 1);

            int n0 = (int)floorf(next_pos);
            int n1 = n0 + 1;
            if (n1 > N_CLUSTERS - 1) n1 = N_CLUSTERS - 1;

            float nt = next_pos - (float)n0;

            next_c = color_lerp(
                w->fft_colors[w->selected_album][n0],
                w->fft_colors[w->selected_album][n1],
                nt
            );
        }

        Color c_left  = color_lerp(prev_c, c, 0.5f);
        Color c_right = color_lerp(c, next_c, 0.5f);

        int x0 = (int)(bound.x + ((float)i / BARS) * bound.width);
        int x1 = (int)(bound.x + ((float)(i + 1) / BARS) * bound.width);
        if (x1 <= x0) x1 = x0 + 1;

        Rectangle r = {
            (float)x0,
            (float)(bound.y + bound.height - h),
            (float)(x1 - x0),
            h
        };

        DrawRectangleGradientEx(
            r,
            ColorAlpha(c_left,  1.0f),
            ColorAlpha(c_left,  1.0f),
            ColorAlpha(c_right, 1.0f),
            ColorAlpha(c_right, 1.0f)
        );
    }
}
