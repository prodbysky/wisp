#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "dft.h"
#include "library.h"
#include "playlist.h"

#define FONT_SIZE 24
#define BARS      512

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
    char* program_name;
    char* custom_root_path;
    char* custom_playlist_dir;  // default ($HOME/.wisp/playlists)
    bool help;
} Config;

bool config_parse_args(int* argc, char*** argv, Config* cc);
bool config_parse_file(const char* path, Config* cc);
void help_and_exit(const Config* cfg);

typedef enum {
    OVERLAY_NONE,
    OVERLAY_PLAYLIST_PICK,
    OVERLAY_PLAYLIST_NEW,
} OverlayMode;

#define OVERLAY_BUF_MAX 256

typedef struct {
    OverlayMode mode;

    char buf[OVERLAY_BUF_MAX];
    int buf_len;

    size_t* filtered_indices;
    size_t  filtered_count;

    size_t selected;

    bool add_whole_album;
    size_t pending_album_idx;
    size_t pending_track_idx;
} Overlay;

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

    Color* album_average_colors;
    Color last_color;
    float last_switch;

    float magnitudes[DFT_SIZE / 2];

    Playlists playlists;
    char* playlist_dir;

    size_t pl_selected_playlist;
    size_t pl_selected_track;

    float pl_list_offset;
    float pl_list_wanted_offset;

    float pl_track_scroll;
    float pl_track_wanted_scroll;

    MainPane pl_pane;
    Overlay overlay;
} Wisp;


typedef struct {
    Color focused_text;
    Color unfocused_text;
    Color shadow;
    Color rectangle;
} Theme;


Wisp wisp_init(int argc, char** argv);
void wisp_tick(Wisp* wisp);

static float color_luminance(Color c);
Theme wisp_derive_theme(const Wisp* w);
static Color wisp_get_lerped_base_color(const Wisp* wisp);
static const Album* wisp_get_selected_album(const Wisp* wisp);

static void draw_queue(const Wisp* w, Rectangle bound);
static void draw_dft(const Wisp* w, Rectangle bound);
static void wisp_draw_visual_pane(Wisp* wisp);
static void wisp_draw_playlist_pane(Wisp* wisp);
static void wisp_draw_overlay(Wisp* wisp);

static void wisp_next_pane(Wisp* wisp);
static void wisp_next_loop_mode(Wisp* wisp);
static void wisp_play_selected_track(Wisp* wisp);
static void wisp_queue_album_from_the_selected_track(Wisp* wisp);
static void prepare_dft_vis(Wisp* wisp);

static void overlay_open(Wisp* w, bool whole_album);
static void overlay_close(Wisp* w);
static void overlay_rebuild_filter(Wisp* w);
static void overlay_handle_char(Wisp* w, int ch);
static void overlay_confirm(Wisp* w);

int main(int argc, char** argv) {
    if (argc > 1 && strcmp("--help", argv[1]) == 0) {
        printf("Usage:\n");
        printf("%s [--help]: print this help message\n", argv[0]);
        printf("%s <PATH>: use this path as the base of the music on your system\n", argv[0]);
        return 1;
    }
    Wisp ws = wisp_init(argc, argv);

    while (!WindowShouldClose()) {
        wisp_tick(&ws);
    }

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


static const float ALBUM_COVER_SIDE_LENGTH = 128.0f;
static const float BORDER_PAD             = 8.0f;
static const float ALBUM_ENTRY_H          = FONT_SIZE + 8.0f + 64.0f;
static const float TRACK_ENTRY_H          = FONT_SIZE + 8.0f;

void wisp_tick(Wisp* wisp) {
    const float WW = (float)GetScreenWidth();
    const float WH = (float)GetScreenHeight();

    if (wisp->overlay.mode != OVERLAY_NONE) {
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (ch >= 32 && ch < 127) overlay_handle_char(wisp, ch);
        }
        if (IsKeyPressed(KEY_BACKSPACE) && wisp->overlay.buf_len > 0) {
            wisp->overlay.buf[--wisp->overlay.buf_len] = '\0';
            overlay_rebuild_filter(wisp);
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            overlay_close(wisp);
        }
        if (wisp->overlay.mode == OVERLAY_PLAYLIST_PICK) {
            if (IsKeyPressed(KEY_J) && wisp->overlay.selected + 1 < wisp->overlay.filtered_count)
                wisp->overlay.selected++;
            if (IsKeyPressed(KEY_K) && wisp->overlay.selected > 0) wisp->overlay.selected--;
            if (IsKeyPressed(KEY_N)) {
                wisp->overlay.mode = OVERLAY_PLAYLIST_NEW;
                wisp->overlay.buf_len = 0;
                wisp->overlay.buf[0] = '\0';
            }
            if (IsKeyPressed(KEY_ENTER)) overlay_confirm(wisp);
        } else if (wisp->overlay.mode == OVERLAY_PLAYLIST_NEW) {
            if (IsKeyPressed(KEY_ENTER) && wisp->overlay.buf_len > 0) overlay_confirm(wisp);
        }
        goto draw;
    }

    {
        const bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);

        if (IsKeyPressed(KEY_TAB)) wisp_next_pane(wisp);
        if (IsKeyPressed(KEY_SPACE)) audio_toggle_playing_state(&wisp->audio);

        if (IsKeyPressed(KEY_PERIOD) && shift)  audio_skip_track_forward(&wisp->audio);
        if (IsKeyPressed(KEY_COMMA)  && shift)  audio_skip_track_backward(&wisp->audio);
        if (IsKeyPressed(KEY_PERIOD) && !shift) audio_try_seeking_by(&wisp->audio,  5.0f);
        if (IsKeyPressed(KEY_COMMA)  && !shift) audio_try_seeking_by(&wisp->audio, -5.0f);

        if (IsKeyPressed(KEY_S) && ctrl) wisp->audio.shuffle = !wisp->audio.shuffle;
        if (IsKeyPressed(KEY_R) && ctrl) wisp_next_loop_mode(wisp);

        if (wisp->pane == PANE_MAIN) {
            if (wisp->main_pane == MP_ALBUM) {
                if (IsKeyPressed(KEY_J)) {
                    if (wisp->selected_album < wisp->library.albums.count - 1) {
                        wisp->last_color = wisp->album_average_colors[wisp->selected_album++];
                        wisp->last_switch = GetTime();
                    }
                }
                if (IsKeyPressed(KEY_K)) {
                    if (wisp->selected_album > 0) {
                        wisp->last_color = wisp->album_average_colors[wisp->selected_album--];
                        wisp->last_switch = GetTime();
                    }
                }
                if (IsKeyPressed(KEY_L)) {
                    wisp->main_pane = MP_TRACK;
                    wisp->selected_track = 0;
                }

                if (IsKeyPressed(KEY_A) && shift) overlay_open(wisp, true);
                else if (IsKeyPressed(KEY_A))      overlay_open(wisp, false);
            }

            if (wisp->main_pane == MP_TRACK) {
                const float entry_height = TRACK_ENTRY_H;
                const float top_bound    = wisp->track_scroll + entry_height;
                const float bottom_bound = wisp->track_scroll + WH - entry_height;
                const float cur_off      = (float)wisp->selected_track * entry_height;
                float max_scroll = (float)wisp_get_selected_album(wisp)->tracks.count * entry_height - WH;
                if (max_scroll < 0) max_scroll = 0;
                if (wisp->wanted_track_scroll < 0) wisp->wanted_track_scroll = 0;
                if (wisp->wanted_track_scroll > max_scroll) wisp->wanted_track_scroll = max_scroll;
                if (cur_off < top_bound)    wisp->wanted_track_scroll = cur_off - entry_height;
                if (cur_off > bottom_bound) wisp->wanted_track_scroll = cur_off - (WH - entry_height);

                if (IsKeyPressed(KEY_J)) {
                    if (wisp->selected_track < wisp_get_selected_album(wisp)->tracks.count - 1)
                        wisp->selected_track++;
                }
                if (IsKeyPressed(KEY_K)) {
                    if (wisp->selected_track > 0) wisp->selected_track--;
                }
                if (IsKeyPressed(KEY_H)) wisp->main_pane = MP_ALBUM;

                if (IsKeyPressed(KEY_A) && shift) overlay_open(wisp, true);
                else if (IsKeyPressed(KEY_A))      overlay_open(wisp, false);
            }

            if (IsKeyPressed(KEY_ENTER)) wisp_play_selected_track(wisp);

            if (shift && IsKeyPressed(KEY_Q)) wisp_queue_album_from_the_selected_track(wisp);
            else if (IsKeyPressed(KEY_Q)) {
                Track* t = wisp->library.albums.items[wisp->selected_album].tracks.items[wisp->selected_track];
                audio_enqueue_single(&wisp->audio, t);
            }

            {
                const float top    = wisp->actual_album_offset + ALBUM_ENTRY_H;
                const float bottom = wisp->actual_album_offset + WH - ALBUM_ENTRY_H;
                const float cur    = BORDER_PAD + (float)wisp->selected_album * ALBUM_ENTRY_H;
                float max_off = (float)wisp->library.albums.count * ALBUM_ENTRY_H - WH;
                if (max_off < 0) max_off = 0;
                if (wisp->wanted_album_offset < 0) wisp->wanted_album_offset = 0;
                if (wisp->wanted_album_offset > max_off) wisp->wanted_album_offset = max_off;
                if (cur < top)    wisp->wanted_album_offset = cur - ALBUM_ENTRY_H;
                if (cur > bottom) wisp->wanted_album_offset = cur - (WH - ALBUM_ENTRY_H);
            }
        }

        if (wisp->pane == PANE_PLAYLIST) {
            if (wisp->pl_pane == MP_ALBUM) {
                const Playlist* pl  = &wisp->playlists.items[wisp->pl_selected_playlist];

                if (IsKeyPressed(KEY_J)) {
                    if (wisp->playlists.count > 0 &&
                        wisp->pl_selected_playlist < wisp->playlists.count - 1)
                        wisp->pl_selected_playlist++;
                }
                if (IsKeyPressed(KEY_K)) {
                    if (wisp->pl_selected_playlist > 0) wisp->pl_selected_playlist--;
                }
                if (IsKeyPressed(KEY_L) && wisp->playlists.count > 0) {
                    wisp->pl_pane = MP_TRACK;
                    wisp->pl_selected_track = 0;
                }

                if (IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
                    for (size_t i = 0; i < pl->tracks.count; i++) {
                        audio_enqueue_single(&wisp->audio, pl->tracks.items[i]);
                    }
                }

                {
                    const float eh  = TRACK_ENTRY_H;
                    const float cur = (float)wisp->pl_selected_playlist * eh;
                    float max_off = (float)wisp->playlists.count * eh - WH;
                    if (max_off < 0) max_off = 0;
                    if (wisp->pl_list_wanted_offset < 0) wisp->pl_list_wanted_offset = 0;
                    if (wisp->pl_list_wanted_offset > max_off) wisp->pl_list_wanted_offset = max_off;
                    float top    = wisp->pl_list_offset + eh;
                    float bottom = wisp->pl_list_offset + WH - eh;
                    if (cur < top)    wisp->pl_list_wanted_offset = cur - eh;
                    if (cur > bottom) wisp->pl_list_wanted_offset = cur - (WH - eh);
                }
            }

            if (wisp->pl_pane == MP_TRACK && wisp->playlists.count > 0) {
                const Playlist* pl  = &wisp->playlists.items[wisp->pl_selected_playlist];
                const float eh      = TRACK_ENTRY_H;
                const float cur_off = (float)wisp->pl_selected_track * eh;
                float max_scroll    = (float)pl->tracks.count * eh - WH;
                if (max_scroll < 0) max_scroll = 0;
                if (wisp->pl_track_wanted_scroll < 0) wisp->pl_track_wanted_scroll = 0;
                if (wisp->pl_track_wanted_scroll > max_scroll) wisp->pl_track_wanted_scroll = max_scroll;
                float top    = wisp->pl_track_scroll + eh;
                float bottom = wisp->pl_track_scroll + WH - eh;
                if (cur_off < top)    wisp->pl_track_wanted_scroll = cur_off - eh;
                if (cur_off > bottom) wisp->pl_track_wanted_scroll = cur_off - (WH - eh);

                if (IsKeyPressed(KEY_J)) {
                    if (pl->tracks.count > 0 && wisp->pl_selected_track < pl->tracks.count - 1)
                        wisp->pl_selected_track++;
                }
                if (IsKeyPressed(KEY_K)) {
                    if (wisp->pl_selected_track > 0) wisp->pl_selected_track--;
                }
                if (IsKeyPressed(KEY_H)) wisp->pl_pane = MP_ALBUM;

                if (IsKeyPressed(KEY_ENTER) && pl->tracks.count > 0) {
                    audio_start_playback(&wisp->audio, pl->tracks.items[wisp->pl_selected_track]);
                }

                if (shift && IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
                    for (size_t i = 0; i < pl->tracks.count; i++) {
                        audio_enqueue_single(&wisp->audio, pl->tracks.items[i]);
                    }
                }
                if (!shift && IsKeyPressed(KEY_Q) && pl->tracks.count > 0) {
                    audio_enqueue_single(&wisp->audio, pl->tracks.items[wisp->pl_selected_track]);
                }
            }
        }
    }

    wisp->actual_album_offset += (wisp->wanted_album_offset - wisp->actual_album_offset) * 0.15f;
    wisp->track_scroll        += (wisp->wanted_track_scroll - wisp->track_scroll) * 0.15f;
    wisp->pl_list_offset      += (wisp->pl_list_wanted_offset - wisp->pl_list_offset) * 0.15f;
    wisp->pl_track_scroll     += (wisp->pl_track_wanted_scroll - wisp->pl_track_scroll) * 0.15f;

draw:
    prepare_dft_vis(wisp);
    audio_update(&wisp->audio);
    BeginDrawing();
    ClearBackground(wisp_get_lerped_base_color(wisp));
    const Theme theme = wisp_derive_theme(wisp);

    switch (wisp->pane) {
        case PANE_MAIN: {
            {
                BeginScissorMode(0, 0, (int)(WW / 3 - BORDER_PAD), (int)WH);
                Vector2 cursor = {BORDER_PAD, BORDER_PAD - wisp->actual_album_offset - (FONT_SIZE + 64)};
                for (size_t ai = 0; ai < wisp->library.albums.count; ai++) {
                    const bool focused = (ai == wisp->selected_album);
                    const Color text_color = focused ? theme.focused_text : ColorBrightness(theme.unfocused_text, -0.5f);
                    const Rectangle bg_rect = {cursor.x - 2, cursor.y, WW / 3 - BORDER_PAD * 2, FONT_SIZE + 4 + 64};
                    const Color bg_color = focused ? ColorBrightness(theme.rectangle, 0.5f) : theme.rectangle;
                    const Rectangle cover_src = {0, 0, (float)wisp->covers[ai].width, (float)wisp->covers[ai].height};
                    const Rectangle cover_dst = {BORDER_PAD + BORDER_PAD / 2, cursor.y + 14, 64, 64};
                    DrawRectangleRounded(bg_rect, 0.25f, 16, bg_color);
                    DrawTexturePro(wisp->covers[ai], cover_src, cover_dst, (Vector2){0}, 0, WHITE);
                    DrawTextPro(wisp->font, wisp->library.albums.items[ai].name, cursor, (Vector2){-3 - 64 - 4 - 4, -3 - 32}, 0, FONT_SIZE, 0, theme.shadow);
                    DrawTextPro(wisp->font, wisp->library.albums.items[ai].name, cursor, (Vector2){-2 - 64 - 4 - 4, -2 - 32}, 0, FONT_SIZE, 0, text_color);
                    cursor.y += ALBUM_ENTRY_H;
                }
                EndScissorMode();
            }
            {
                Vector2 cursor = {BORDER_PAD + WW / 3, BORDER_PAD - wisp->track_scroll};
                BeginScissorMode((int)(cursor.x - 3 * BORDER_PAD), 0, (int)((WW * 2) / 3 + BORDER_PAD), (int)WH);
                for (size_t ti = 0; ti < wisp_get_selected_album(wisp)->tracks.count; ti++) {
                    const bool focused = (ti == wisp->selected_track);
                    const Color text_color = focused ? theme.focused_text : ColorBrightness(theme.unfocused_text, -0.5f);
                    const Rectangle bg_rect = {cursor.x - 2, cursor.y, 2 * (WW / 3) - BORDER_PAD * 2, FONT_SIZE + 4};
                    DrawRectangleRounded(bg_rect, 0.25f, 16, theme.rectangle);
                    DrawTextPro(wisp->font, wisp_get_selected_album(wisp)->tracks.items[ti]->title, cursor, (Vector2){-3, -3}, 0, FONT_SIZE, 0, theme.shadow);
                    DrawTextPro(wisp->font, wisp_get_selected_album(wisp)->tracks.items[ti]->title, cursor, (Vector2){-2, -2}, 0, FONT_SIZE, 0, text_color);
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
            wisp_draw_playlist_pane(wisp);
            break;
        }
        case PANE_COUNT: assert(false);
    }

    if (wisp->overlay.mode != OVERLAY_NONE) wisp_draw_overlay(wisp);

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

    Library lib = prepare_library(cfg.custom_root_path);

    SetWindowState(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "wispy");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    InitAudioDevice();
    SetTargetFPS(180);
    AttachAudioMixedProcessor(fill_dft_buffer_callback);

    Font font = LoadFontEx("res/Iosevka.ttf", FONT_SIZE, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_ANISOTROPIC_16X);

    Texture2D* tex  = malloc(lib.albums.count * sizeof(Texture2D));
    Color*     tints = malloc(lib.albums.count * sizeof(Color));

    for (size_t i = 0; i < lib.albums.count; i++) {
        Track* t = lib.albums.items[i].tracks.items[0];
        Image img = {
            .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
            .width   = t->cover_w,
            .height  = t->cover_h,
            .data    = t->cover,
            .mipmaps = 1,
        };
        float r = 0, g = 0, b = 0;
        int count = 0;
        for (int y = 0; y < t->cover_h; y += 10) {
            for (int x = 0; x < t->cover_w; x += 10) {
                size_t idx = ((size_t)y * (size_t)t->cover_w + (size_t)x) * 3;
                r += (float)t->cover[idx]     / 255.0f;
                g += (float)t->cover[idx + 1] / 255.0f;
                b += (float)t->cover[idx + 2] / 255.0f;
                count++;
            }
        }
        if (count > 0) { r /= count; g /= count; b /= count; }
        tints[i] = (Color){(unsigned char)(r * 255), (unsigned char)(g * 255), (unsigned char)(b * 255), 255};
        tex[i] = LoadTextureFromImage(img);
    }

    char* pl_dir = playlist_dir_path(cfg.custom_playlist_dir);
    playlist_ensure_dir(pl_dir);

    Playlists playlists = {0};
    playlists_load(pl_dir, &lib, &playlists);

    return (Wisp){
        .font                = font,
        .library             = lib,
        .covers              = tex,
        .album_average_colors = tints,
        .main_pane           = MP_ALBUM,
        .pane                = PANE_MAIN,
        .cli_config          = cfg,
        .playlists           = playlists,
        .playlist_dir        = pl_dir,
        .pl_pane             = MP_ALBUM,
    };
}

bool config_parse_args(int* argc, char*** argv, Config* cc) {
    cc->program_name = **argv;
    if (*argc == 1) return true;
    (*argc)--;
    (*argv)++;
    while (*argc != 0) {
        if (strcmp(**argv, "--help") == 0) {
            cc->help = true;
            return true;
        } else if (strcmp(**argv, "--path") == 0) {
            if (cc->custom_root_path) { printf("ERROR: --path given twice\n"); return false; }
            if (*argc < 2) { printf("ERROR: --path requires an argument\n"); return false; }
            cc->custom_root_path = *((*argv) + 1);
            *argv += 2; (*argc) -= 2;
        } else if (strcmp(**argv, "--playlist-dir") == 0) {
            if (cc->custom_playlist_dir) { printf("ERROR: --playlist-dir given twice\n"); return false; }
            if (*argc < 2) { printf("ERROR: --playlist-dir requires an argument\n"); return false; }
            cc->custom_playlist_dir = *((*argv) + 1);
            *argv += 2; (*argc) -= 2;
        } else {
            printf("ERROR: Unknown flag `%s`\n", **argv);
            return false;
        }
    }
    return true;
}

bool config_parse_file(const char* path, Config* cc) {
    FILE* file = fopen(path, "r");
    if (!file) return false;

    static const struct { const char* key; size_t klen; ptrdiff_t off; } pairs[] = {
        {"library_path", 12, offsetof(Config, custom_root_path)},
        {"playlist_dir",  12, offsetof(Config, custom_playlist_dir)},
    };

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        bool matched = false;
        for (size_t i = 0; i < sizeof(pairs)/sizeof(pairs[0]); i++) {
            size_t klen = strlen(pairs[i].key);
            if (strncmp(line, pairs[i].key, klen) == 0) {
                char* val = line + klen;
                while (*val == ' ' || *val == '\t') val++;
                char* end = val + strlen(val) - 1;
                while (end > val && (*end == ' ' || *end == '\t')) *end-- = '\0';
                char** dst = (char**)((char*)cc + pairs[i].off);
                if (*dst == NULL) *dst = strdup(val);
                matched = true;
                break;
            }
        }
        if (!matched) {
            printf("WARN: Unknown config key: %s\n", line);
        }
    }
    fclose(file);
    return true;
}

void help_and_exit(const Config* cfg) {
    printf("USAGE:\n");
    printf("  %s [FLAGS]\n", cfg->program_name ? cfg->program_name : "wisp");
    printf("  --help                : show this help message\n");
    printf("  --path <DIR>          : set custom music library path\n");
    printf("  --playlist-dir <DIR>  : set custom playlist directory\n");
}

static void overlay_rebuild_filter(Wisp* w) {
    free(w->overlay.filtered_indices);
    w->overlay.filtered_indices = NULL;
    w->overlay.filtered_count   = 0;
    w->overlay.selected         = 0;

    size_t cap = w->playlists.count;
    if (cap == 0) return;

    w->overlay.filtered_indices = malloc(cap * sizeof(size_t));

    const char* needle = w->overlay.buf;
    for (size_t i = 0; i < w->playlists.count; i++) {
        if (needle[0] == '\0' || strstr(w->playlists.items[i].name, needle)) {
            w->overlay.filtered_indices[w->overlay.filtered_count++] = i;
        }
    }
}

static void overlay_open(Wisp* w, bool whole_album) {
    w->overlay.mode             = OVERLAY_PLAYLIST_PICK;
    w->overlay.buf_len          = 0;
    w->overlay.buf[0]           = '\0';
    w->overlay.add_whole_album  = whole_album;
    w->overlay.pending_album_idx = w->selected_album;
    w->overlay.pending_track_idx = w->selected_track;
    overlay_rebuild_filter(w);

    if (w->playlists.count == 0) w->overlay.mode = OVERLAY_PLAYLIST_NEW;
}

static void overlay_close(Wisp* w) {
    w->overlay.mode = OVERLAY_NONE;
    free(w->overlay.filtered_indices);
    w->overlay.filtered_indices = NULL;
    w->overlay.filtered_count   = 0;
}

static void overlay_handle_char(Wisp* w, int ch) {
    if (w->overlay.buf_len >= OVERLAY_BUF_MAX - 1) return;
    w->overlay.buf[w->overlay.buf_len++] = (char)ch;
    w->overlay.buf[w->overlay.buf_len]   = '\0';
    if (w->overlay.mode == OVERLAY_PLAYLIST_PICK) overlay_rebuild_filter(w);
}

static void overlay_add_pending_to_playlist(Wisp* w, Playlist* pl) {
    const Album* alb = &w->library.albums.items[w->overlay.pending_album_idx];
    if (w->overlay.add_whole_album) {
        for (size_t i = w->overlay.pending_track_idx; i < alb->tracks.count; i++)
            playlist_add_track(pl, alb->tracks.items[i]);
    } else {
        playlist_add_track(pl, alb->tracks.items[w->overlay.pending_track_idx]);
    }
}

static void overlay_confirm(Wisp* w) {
    if (w->overlay.mode == OVERLAY_PLAYLIST_PICK) {
        if (w->overlay.filtered_count == 0) return;
        size_t idx = w->overlay.filtered_indices[w->overlay.selected];
        overlay_add_pending_to_playlist(w, &w->playlists.items[idx]);
        overlay_close(w);
    } else if (w->overlay.mode == OVERLAY_PLAYLIST_NEW) {
        if (w->overlay.buf_len == 0) return;
        Playlist* pl = playlists_create(&w->playlists, w->overlay.buf);
        playlist_save_one(w->playlist_dir, pl);
        overlay_add_pending_to_playlist(w, pl);
        overlay_close(w);
    }
}

static void wisp_draw_overlay(Wisp* wisp) {
    const float WW = (float)GetScreenWidth();
    const float WH = (float)GetScreenHeight();
    const Theme t  = wisp_derive_theme(wisp);

    DrawRectangle(0, 0, (int)WW, (int)WH, ColorAlpha(BLACK, 0.55f));

    const float box_w = WW * 0.5f;
    const float box_x = (WW - box_w) * 0.5f;
    const float box_y = WH * 0.15f;
    const float line_h = FONT_SIZE + 8.0f;

    if (wisp->overlay.mode == OVERLAY_PLAYLIST_NEW) {
        const float box_h = line_h * 3.0f;
        DrawRectangleRounded((Rectangle){box_x, box_y, box_w, box_h}, 0.12f, 16, ColorBrightness(t.rectangle, 0.3f));

        DrawTextEx(wisp->font, "New playlist name:", (Vector2){box_x + 12, box_y + 8}, FONT_SIZE, 0, t.focused_text);

        DrawRectangleRounded((Rectangle){box_x + 8, box_y + line_h + 4, box_w - 16, line_h}, 0.2f, 16, ColorBrightness(t.rectangle, -0.3f));
        DrawTextEx(wisp->font, wisp->overlay.buf, (Vector2){box_x + 16, box_y + line_h + 8}, FONT_SIZE, 0, t.focused_text);

        if ((int)(GetTime() * 2) % 2 == 0) {
            float cx = box_x + 16 + MeasureTextEx(wisp->font, wisp->overlay.buf, FONT_SIZE, 0).x;
            DrawRectangle((int)cx, (int)(box_y + line_h + 8), 2, FONT_SIZE, t.focused_text);
        }

        DrawTextEx(wisp->font, "[Enter] confirm   [Esc] cancel", (Vector2){box_x + 12, box_y + 2 * line_h + 8}, FONT_SIZE - 6, 0, ColorBrightness(t.focused_text, -0.3f));
        return;
    }

    const float max_visible = 8.0f;
    float visible = (float)(wisp->overlay.filtered_count < (size_t)max_visible
                              ? wisp->overlay.filtered_count
                              : (size_t)max_visible);
    if (visible < 1) visible = 1;

    const float box_h = line_h * (visible + 2.5f);

    DrawRectangleRounded((Rectangle){box_x, box_y, box_w, box_h}, 0.08f, 16, ColorBrightness(t.rectangle, 0.3f));

    DrawRectangleRounded((Rectangle){box_x + 8, box_y + 6, box_w - 16, line_h}, 0.2f, 16, ColorBrightness(t.rectangle, -0.3f));
    const char* placeholder = "Search playlist…";
    const char* search_text = wisp->overlay.buf_len > 0 ? wisp->overlay.buf : placeholder;
    Color search_col = wisp->overlay.buf_len > 0 ? t.focused_text : ColorBrightness(t.unfocused_text, -0.3f);
    DrawTextEx(wisp->font, search_text, (Vector2){box_x + 16, box_y + 10}, FONT_SIZE, 0, search_col);
    if (wisp->overlay.buf_len > 0 && (int)(GetTime() * 2) % 2 == 0) {
        float cx = box_x + 16 + MeasureTextEx(wisp->font, wisp->overlay.buf, FONT_SIZE, 0).x;
        DrawRectangle((int)cx, (int)(box_y + 10), 2, FONT_SIZE, t.focused_text);
    }

    float ry = box_y + line_h + 8;
    if (wisp->overlay.filtered_count == 0) {
        DrawTextEx(wisp->font, "No playlists found.", (Vector2){box_x + 16, ry + 4}, FONT_SIZE, 0, ColorBrightness(t.focused_text, -0.3f));
    } else {
        for (size_t i = 0; i < (size_t)max_visible && i < wisp->overlay.filtered_count; i++) {
            size_t pi = wisp->overlay.filtered_indices[i];
            const bool sel = (i == wisp->overlay.selected);
            if (sel) DrawRectangleRounded((Rectangle){box_x + 4, ry, box_w - 8, line_h}, 0.2f, 16, ColorBrightness(t.rectangle, sel ? 0.5f : 0.0f));
            Color tc = sel ? t.focused_text : ColorBrightness(t.unfocused_text, -0.2f);
            DrawTextEx(wisp->font, wisp->playlists.items[pi].name, (Vector2){box_x + 16, ry + 4}, FONT_SIZE, 0, tc);
            ry += line_h;
        }
    }

    DrawTextEx(wisp->font, "[n] new   [Enter] add   [Esc] cancel", (Vector2){box_x + 12, box_y + box_h - line_h + 4}, FONT_SIZE, 0, ColorBrightness(t.focused_text, -0.3f));
}

static void wisp_draw_playlist_pane(Wisp* wisp) {
    const float WW = (float)GetScreenWidth();
    const float WH = (float)GetScreenHeight();
    const Theme theme = wisp_derive_theme(wisp);

    if (wisp->playlists.count == 0) {
        const char* msg = "No playlists yet.  Press 'a' in the main pane to add tracks.";
        Vector2 sz = MeasureTextEx(wisp->font, msg, FONT_SIZE, 0);
        DrawTextEx(wisp->font, msg, (Vector2){(WW - sz.x) * 0.5f, (WH - sz.y) * 0.5f}, FONT_SIZE, 0, theme.focused_text);
        return;
    }

    {
        BeginScissorMode(0, 0, (int)(WW / 3 - BORDER_PAD), (int)WH);
        Vector2 cursor = {BORDER_PAD, BORDER_PAD - wisp->pl_list_offset};
        for (size_t pi = 0; pi < wisp->playlists.count; pi++) {
            const bool focused = (pi == wisp->pl_selected_playlist);
            const Color text_color = focused ? theme.focused_text : ColorBrightness(theme.unfocused_text, -0.5f);
            const Color bg_color   = focused ? ColorBrightness(theme.rectangle, 0.5f) : theme.rectangle;
            const Rectangle bg     = {cursor.x - 2, cursor.y, WW / 3 - BORDER_PAD * 2, FONT_SIZE + 4};
            DrawRectangleRounded(bg, 0.25f, 16, bg_color);
            DrawTextPro(wisp->font, wisp->playlists.items[pi].name, cursor, (Vector2){-3, -3}, 0, FONT_SIZE, 0, theme.shadow);
            DrawTextPro(wisp->font, wisp->playlists.items[pi].name, cursor, (Vector2){-2, -2}, 0, FONT_SIZE, 0, text_color);
            cursor.y += TRACK_ENTRY_H;
        }
        EndScissorMode();
    }

    {
        const Playlist* pl = &wisp->playlists.items[wisp->pl_selected_playlist];
        Vector2 cursor     = {BORDER_PAD + WW / 3, BORDER_PAD - wisp->pl_track_scroll};
        BeginScissorMode((int)(cursor.x - 3 * BORDER_PAD), 0, (int)((WW * 2) / 3 + BORDER_PAD), (int)WH);

        if (pl->tracks.count == 0) {
            DrawTextEx(wisp->font, "Playlist is empty.", (Vector2){cursor.x, cursor.y}, FONT_SIZE, 0, ColorBrightness(theme.unfocused_text, -0.3f));
        }
        for (size_t ti = 0; ti < pl->tracks.count; ti++) {
            const bool focused  = (ti == wisp->pl_selected_track && wisp->pl_pane == MP_TRACK);
            const Color tc      = focused ? theme.focused_text : ColorBrightness(theme.unfocused_text, -0.5f);
            const Rectangle bg  = {cursor.x - 2, cursor.y, 2 * (WW / 3) - BORDER_PAD * 2, FONT_SIZE + 4};
            DrawRectangleRounded(bg, 0.25f, 16, theme.rectangle);
            DrawTextPro(wisp->font, pl->tracks.items[ti]->title, cursor, (Vector2){-3, -3}, 0, FONT_SIZE, 0, theme.shadow);
            DrawTextPro(wisp->font, pl->tracks.items[ti]->title, cursor, (Vector2){-2, -2}, 0, FONT_SIZE, 0, tc);
            cursor.y += TRACK_ENTRY_H;
        }
        EndScissorMode();
    }
}

static void wisp_draw_visual_pane(Wisp* wisp) {
    const Rectangle dft_rect = {0, -(float)GetScreenHeight(), (float)GetScreenWidth(), (float)GetScreenHeight() * 2};
    if (wisp->audio.current_track == NULL) return;
    const char* title  = wisp->audio.current_track->title;
    const char* album  = wisp->audio.current_track->album;
    const char* artist = wisp->audio.current_track->artist;
    draw_dft(wisp, dft_rect);
    const Theme t = wisp_derive_theme(wisp);
    DrawTextEx(wisp->font, title,  (Vector2){8,      8}, FONT_SIZE, 0, t.focused_text);
    DrawTextEx(wisp->font, album,  (Vector2){8,     40}, FONT_SIZE, 0, t.focused_text);
    DrawTextEx(wisp->font, artist, (Vector2){8,     72}, FONT_SIZE, 0, t.focused_text);
    DrawTextEx(wisp->font, title,  (Vector2){8 + 2, 10}, FONT_SIZE, 0, ColorAlpha(t.shadow, -0.1f));
    DrawTextEx(wisp->font, album,  (Vector2){8 + 2, 42}, FONT_SIZE, 0, ColorAlpha(t.shadow, -0.1f));
    DrawTextEx(wisp->font, artist, (Vector2){8 + 2, 74}, FONT_SIZE, 0, ColorAlpha(t.shadow, -0.1f));
}

static void wisp_next_pane(Wisp* wisp) {
    wisp->pane = (wisp->pane + 1) % PANE_COUNT;
}

static const Album* wisp_get_selected_album(const Wisp* wisp) {
    return &wisp->library.albums.items[wisp->selected_album];
}

static Color wisp_get_lerped_base_color(const Wisp* wisp) {
    float t = (float)(GetTime() - wisp->last_switch) / 0.2f;
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
    const Album* alb = wisp_get_selected_album(wisp);
    for (size_t i = wisp->selected_track; i < alb->tracks.count; i++)
        audio_enqueue_single(&wisp->audio, alb->tracks.items[i]);
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
            float mag  = sqrtf(out[k].real * out[k].real + out[k].imag * out[k].imag);
            float db   = 20.0f * log10f(mag + 1e-6f);
            float norm = (db - (-20.0f)) / (20.0f - (-20.0f));
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;
            wisp->magnitudes[k] = wisp->magnitudes[k] * 0.6f + norm * 0.4f;
        }
    }
}

static void draw_queue(const Wisp* w, Rectangle bound) {
    const float child_spacing = 8.0f;
    const float item_height   = FONT_SIZE + child_spacing;
    const Theme  theme        = wisp_derive_theme(w);
    const float  center_y     = bound.y + bound.height * 0.5f;

    if (w->audio.current_track) {
        Rectangle rect = {bound.x, center_y, (float)GetScreenWidth() - bound.x * 2, FONT_SIZE + 4};
        DrawRectangleRounded(rect, 0.25f, 17, theme.rectangle);
        DrawTextEx(w->font, w->audio.current_track->title, (Vector2){bound.x + 5, center_y + 3}, FONT_SIZE, 0, theme.focused_text);
    }

    for (size_t i = 0; i < w->audio.queue.history.items.count; i++) {
        size_t idx = w->audio.queue.history.items.count - 1 - i;
        float y = center_y - item_height * (float)(i + 1);
        Rectangle rect = {bound.x, y, (float)GetScreenWidth() - bound.x * 2, FONT_SIZE + 4};
        DrawRectangleRounded(rect, 0.25f, 17, ColorBrightness(theme.rectangle, -0.2f));
        DrawTextEx(w->font, w->audio.queue.history.items.items[idx]->title, (Vector2){bound.x + 5, y + 3}, FONT_SIZE, 0, ColorBrightness(theme.focused_text, -0.2f));
    }

    for (size_t i = 0; i < w->audio.queue.upcoming.items.count; i++) {
        float y = center_y + item_height * (float)(i + 1);
        Rectangle rect = {bound.x, y, (float)GetScreenWidth() - bound.x * 2, FONT_SIZE + 4};
        DrawRectangleRounded(rect, 0.25f, 17, ColorBrightness(theme.rectangle, -0.2f));
        DrawTextEx(w->font, w->audio.queue.upcoming.items.items[i]->title, (Vector2){bound.x + 5, y + 3}, FONT_SIZE, 0, ColorBrightness(theme.focused_text, -0.2f));
    }
}

static void draw_dft(const Wisp* w, Rectangle bound) {
    const Theme t = wisp_derive_theme(w);
    for (int i = 0; i < BARS; i++) {
        float t0 = (float)i / BARS;
        float t1 = (float)(i + 1) / BARS;
        float log_min = logf(1.0f), log_max = logf(NYQUIST_LIMIT);
        int k0 = (int)expf(log_min + (log_max - log_min) * t0);
        int k1 = (int)expf(log_min + (log_max - log_min) * t1);
        if (k1 <= k0) k1 = k0 + 1;
        float sum = 0; int cnt = 0;
        for (int k = k0; k < k1; k++) { sum += w->magnitudes[k]; cnt++; }
        float mag = cnt > 0 ? sum / cnt : 0;
        mag = logf(1.0f + mag);
        float h  = mag * bound.height / 3.0f;
        int   x0 = (int)(bound.x + ((float)i / BARS) * bound.width);
        int   x1 = (int)(bound.x + ((float)(i + 1) / BARS) * bound.width);
        if (x1 <= x0) x1 = x0 + 1;
        DrawRectangleGradientV(x0, (int)(bound.y + bound.height - h), x1 - x0, (int)h,
                               ColorAlpha(t.rectangle, -0.2f), ColorAlpha(t.rectangle, 1.0f));
    }
}

Theme wisp_derive_theme(const Wisp* w) {
    Color base = wisp_get_lerped_base_color(w);
    float lum  = color_luminance(base);
    Color styled     = ColorLerp(lum > 0.5f ? BLACK : WHITE, base, 0.15f);
    Color rect_color = lum > 0.5f ? ColorLerp(base, BLACK, 0.5f) : ColorLerp(base, WHITE, 0.5f);
    Color shadow_col = ColorAlpha(lum > 0.5f ? BLACK : WHITE, 0.8f);
    return (Theme){
        .focused_text   = styled,
        .unfocused_text = ColorBrightness(styled, 0.8f),
        .rectangle      = rect_color,
        .shadow         = shadow_col,
    };
}

static float color_luminance(Color c) {
    return 0.2126f * (c.r / 255.0f) + 0.7152f * (c.g / 255.0f) + 0.0722f * (c.b / 255.0f);
}
